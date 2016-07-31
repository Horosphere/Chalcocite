#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <readline/readline.h>
#include <SDL2/SDL_video.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_thread.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>

#include "media.h"
#include "audio.h"

static uint32_t refresh_timer_cb(uint32_t interval, void* data)
{
	(void) interval;

	SDL_Event event;
	event.type = CHAL_EVENT_REFRESH;
	event.user.data1 = data;
	SDL_PushEvent(&event);
	return 0; // Stops the timer
}
static void schedule_refresh(struct Media* const media, int delay)
{
	if (!SDL_AddTimer(delay, refresh_timer_cb, media))
	{
		fprintf(stderr, "[SDL] %s\n", SDL_GetError());
	}
}
static void video_refresh_timer(struct Media* const media)
{
	(void) media;
	if (media->streamV)
	{
		if (media->pictQueueSize == 0)
			schedule_refresh(media, 1);
		else
		{
			schedule_refresh(media, 40);

			// Show picture
			struct VideoPicture* vp = &media->pictQueue[media->pictQueueIndexR];
			assert(vp->texture &&
					vp->planeY && vp->planeU && vp->planeV);

			SDL_Rect rect;
			rect.x = rect.y = 0;
			rect.w = vp->width;
			rect.h = vp->height;
			SDL_LockMutex(media->screenMutex);
			SDL_UpdateYUVTexture(vp->texture, NULL,
			                     vp->planeY, vp->width,
			                     vp->planeU, vp->width / 2,
			                     vp->planeV, vp->width / 2);
			//SDL_SetRenderDrawColor(media->renderer, 255, 127, 255, 255);
			SDL_RenderClear(media->renderer);
			SDL_RenderCopy(media->renderer, vp->texture, NULL, &rect);
			SDL_RenderPresent(media->renderer);
			SDL_UnlockMutex(media->screenMutex);

			++media->pictQueueIndexR;
			if (media->pictQueueIndexR == PICTQUEUE_SIZE)
				media->pictQueueIndexR = 0;
			SDL_LockMutex(media->pictQueueMutex);
			--media->pictQueueSize;
			SDL_CondSignal(media->pictQueueCond);
			SDL_UnlockMutex(media->pictQueueMutex);
		}
	}
	else schedule_refresh(media, 100);
}
static int video_thread(struct Media* const media)
{
	AVFrame* frame = av_frame_alloc();
	if (!frame) return -1;
	AVFrame* frameRGB = av_frame_alloc();
	if (!frameRGB)
	{
		av_frame_free(&frame);
		return -1;
	}

	// Allocate YV12 pixel array
	size_t pitchUV = media->outWidth / 2;

	while (true)
	{
		AVPacket packet;
		if (PacketQueue_get(&media->queueV, &packet, true, &media->state) < 0)
		{
			break;
		}
		int finished;
		avcodec_decode_video2(media->ccV, frame, &finished, &packet);

		if (finished)
		{
			if (!Media_pictQueue_wait_write(media)) break;
			struct VideoPicture* vp = &media->pictQueue[media->pictQueueIndexW];
			frameRGB->data[0] = vp->planeY;
			frameRGB->data[1] = vp->planeU;
			frameRGB->data[2] = vp->planeV;
			frameRGB->linesize[0] = media->outWidth;
			frameRGB->linesize[1] = pitchUV;
			frameRGB->linesize[2] = pitchUV;

			sws_scale(media->swsContext, (uint8_t const* const*) frame->data,
			          frame->linesize, 0, media->ccV->height,
			          frameRGB->data, frameRGB->linesize);

			// Move picture queue writing index
			if (++media->pictQueueIndexW == PICTQUEUE_SIZE)
				media->pictQueueIndexW = 0;
			SDL_LockMutex(media->pictQueueMutex);
			++media->pictQueueSize;
			SDL_UnlockMutex(media->pictQueueMutex);

			av_packet_unref(&packet);
		}
	}
	av_frame_free(&frameRGB);
	av_frame_free(&frame);
	return 0;
}
static int decode_thread(struct Media* const media)
{

	// TODO: If streams not found, silence either video or audio
	AVPacket packet;
	while (true)
	{
		if (media->state == STATE_QUIT)
			break;
		// TODO: Seek
		if (media->queueA.size > AUDIO_QUEUE_MAX_SIZE ||
		    media->queueV.size > VIDEO_QUEUE_MAX_SIZE)
		{
			SDL_Delay(10);
			continue;
		}
		if (av_read_frame(media->formatContext, &packet) < 0)
		{
			break;
			if (media->formatContext->pb->error == 0)
			{
				SDL_Delay(100);
				continue;
			}
			else
				break; // Error

		}
		// Stream switch
		if (packet.stream_index == (int) media->streamIndexV)
		{
			if (media->streamV)
				PacketQueue_put(&media->queueV, &packet);
		}
		else if (packet.stream_index == (int) media->streamIndexA)
		{
			if (media->streamA)
				PacketQueue_put(&media->queueA, &packet);
		}
		else
			av_packet_unref(&packet);
	}
	fprintf(stdout, "Decoding complete. Waiting for program to exit\n");
	while (media->state != STATE_QUIT)
		SDL_Delay(100);

	fprintf(stdout, "Decoding thread successfully exits\n");


	SDL_Event event;
	event.type = CHAL_EVENT_QUIT;
	event.user.data1 = media;
	SDL_PushEvent(&event);

	return 0;
}
void test(char const* fileName)
{
	struct Media* media = av_malloc(sizeof(struct Media));
	if (!media)
	{
		fprintf(stderr, "Could not allocate media\n");
		goto fail0;
	}
	Media_init(media);
	strncpy(media->fileName, fileName, sizeof(media->fileName));
	media->state = STATE_NORMAL;

	if (avformat_open_input(&media->formatContext, media->fileName, NULL, NULL) != 0)
	{
		fprintf(stderr, "Unable to open file\n");
		goto fail2;
	}
	if (avformat_find_stream_info(media->formatContext, NULL) < 0)
	{
		fprintf(stderr, "Unable to find streams\n");
		goto fail2;
	}
	av_dump_format(media->formatContext, 0, media->fileName, 0);

	// Find Audio and Video streams

	// Audio stream
	for (unsigned i = 0; i < media->formatContext->nb_streams; ++i)
		if (media->formatContext->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO)
		{
			if (!av_stream_context(media->formatContext, i, &media->ccA)) break;
			media->streamIndexA = i;
			media->streamA = media->formatContext->streams[i];
			media->audioBufferSize = media->audioBufferIndex = 0;
			audio_load_SDL(media);
			break;
		}
	for (unsigned i = 0; i < media->formatContext->nb_streams; ++i)
		if (media->formatContext->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
		{
			if (!av_stream_context(media->formatContext, i, &media->ccV)) break;

			// TODO: Allow the window to be resized
			media->outWidth = media->ccV->width;
			media->outHeight = media->ccV->height;
			media->swsContext = sws_getContext(media->ccV->width, media->ccV->height,
			                                   media->ccV->pix_fmt,
			                                   media->outWidth, media->outHeight,
			                                   AV_PIX_FMT_YUV420P, SWS_BILINEAR,
			                                   NULL, NULL, NULL);
			media->streamIndexV = i;
			media->screen = SDL_CreateWindow(media->fileName, SDL_WINDOWPOS_UNDEFINED,
			                                 SDL_WINDOWPOS_UNDEFINED,
			                                 media->outWidth, media->outHeight, 0);
			if (!media->screen)
			{
				fprintf(stderr, "[SDL] %s\n", SDL_GetError());
				break;
			}
			media->renderer = SDL_CreateRenderer(media->screen, -1, 0);
			if (!media->renderer)
			{
				fprintf(stderr, "[SDL] %s\n", SDL_GetError());
				media->screen = NULL;
				break;
			}

			media->streamV = media->formatContext->streams[i];
			Media_pictQueue_init(media);
			media->threadVideo = SDL_CreateThread((int (*)(void*)) video_thread,
			                                      "video", media);
			break;
		}
	// Empty media is not worth playing
	if (!media->streamV && !media->streamA) goto fail2;
	media->threadParse = SDL_CreateThread((int (*)(void*)) decode_thread,
	                                      "decode", media);
	if (!media->threadParse)
	{
		fprintf(stderr, "Failed to launch thread\n");
		goto fail2;
	}

	schedule_refresh(media, 40);
	while (true)
	{
		SDL_Event event;
		SDL_WaitEvent(&event);
		switch (event.type)
		{
		case CHAL_EVENT_QUIT:
		case SDL_QUIT:
			media->state = STATE_QUIT;
			SDL_Quit();
			goto fail1;
			break;
		case CHAL_EVENT_REFRESH:
			video_refresh_timer(event.user.data1);
			break;
		default:
			break;
		}
	}

fail2:
	if (media->streamV) Media_pictQueue_destroy(media);
	SDL_DestroyRenderer(media->renderer);
	SDL_DestroyWindow(media->screen);
	avcodec_close(media->ccA);
	avcodec_close(media->ccV);
	avformat_close_input(&media->formatContext);

fail1:
	Media_destroy(media);
	av_free(media);
fail0:
	return;
}

int main(int argc, char* argv[])
{
	(void) argc;
	(void) argv;

	// Initialisation
	av_register_all();
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER))
	{
		fprintf(stderr, "[SDL] %s\n", SDL_GetError());
		return -1;
	}
	if (argc == 2)
	{
		test(argv[1]);
		return 0;
	}

	while (true)
	{
		char* line = readline("(chal) ");
		if (!line || line[0] == '\0')
		{
			printf("Error: Empty command\n");
			continue;
		}
		// Use strtok to split the line into tokens
		char const* token = strtok(line, " ");
		if (strcmp(token, "quit") == 0)
		{
			if (strtok(NULL, " "))
				printf("Type 'quit' to quit\n");
			else
				break;
		}
		else if (strcmp(token, "test") == 0)
		{
			token = strtok(NULL, " ");
			if (!token)
				printf("Please supply an argument\n");
			test(token);
		}
		else if (strcmp(token, "blank") == 0)
		{
			SDL_Window* screen = SDL_CreateWindow("Test",
			                                      SDL_WINDOWPOS_UNDEFINED,
			                                      SDL_WINDOWPOS_UNDEFINED,
			                                      640, 480, 0);
			SDL_Renderer* renderer = SDL_CreateRenderer(screen, -1, 0);

			SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
			SDL_RenderClear(renderer);
			SDL_RenderPresent(renderer);
		}
		else
		{
			printf("Error: Unknown command\n");
			continue;
		}
		free(line);
	}

	return 0;
}
