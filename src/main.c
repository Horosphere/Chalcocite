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
#include "test.h"

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

			SDL_UpdateYUVTexture(vp->texture, NULL,
			                     vp->planeY, vp->width,
			                     vp->planeU, vp->width / 2,
			                     vp->planeV, vp->width / 2);
			//SDL_SetRenderDrawColor(media->renderer, 255, 127, 255, 255);
			SDL_RenderClear(media->renderer);
			SDL_RenderCopy(media->renderer, vp->texture, NULL, 0);
			SDL_RenderPresent(media->renderer);

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
	uint8_t* imageData[3];
	int imageLinesize[3];

	size_t pitchUV = media->outWidth / 2;

	while (true)
	{
		struct AVPacket packet;
		if (PacketQueue_get(&media->queueV, &packet, true, &media->state) < 0)
		{
			break;
		}
		int finished;
		avcodec_decode_video2(media->ccV, media->frameVideo, &finished, &packet);

		if (finished)
		{
			if (!Media_pictQueue_wait_write(media)) break;
			struct VideoPicture* vp = &media->pictQueue[media->pictQueueIndexW];
			imageData[0] = vp->planeY;
			imageData[1] = vp->planeU;
			imageData[2] = vp->planeV;
			imageLinesize[0] = media->outWidth;
			imageLinesize[1] = pitchUV;
			imageLinesize[2] = pitchUV;

			sws_scale(media->swsContext,
					(uint8_t const* const*) media->frameVideo->data,
			          media->frameVideo->linesize, 0, media->ccV->height,
			          imageData, imageLinesize);

			// Move picture queue writing index
			if (++media->pictQueueIndexW == PICTQUEUE_SIZE)
				media->pictQueueIndexW = 0;
			SDL_LockMutex(media->pictQueueMutex);
			++media->pictQueueSize;
			SDL_UnlockMutex(media->pictQueueMutex);

			av_packet_unref(&packet);
		}
	}
	return 0;
}
static int audio_thread(struct Media* const media)
{
	uint8_t* buffer = malloc(192000 * 3 / 2);
	while (true)
	{
		struct AVPacket packet;
		if (PacketQueue_get(&media->queueA, &packet, true, &media->state) < 0)
		{
			break;
		}
		while (packet.size > 0)
		{
		int gotFrame = 0;
		int dataSize = avcodec_decode_audio4(media->ccA, media->frameAudio,
				&gotFrame, &packet);
		if (dataSize >= 0 && gotFrame)
		{
			packet.size -= dataSize;
			packet.data += dataSize;
			int bufferSize = av_samples_get_buffer_size(NULL, media->ccA->channels,
					media->frameAudio->nb_samples, AV_SAMPLE_FMT_S16, true);
			swr_convert(media->swrContext, &buffer, bufferSize,
					(uint8_t const**) media->frameAudio->extended_data,
					media->frameAudio->nb_samples);
			SDL_QueueAudio(media->audioDevice, buffer, bufferSize);
		}
		else
		{
			packet.size = 0;
			packet.data = NULL;
		}
		}
		av_packet_unref(&packet);
	}
	free(buffer);
	return 0;
}
static int decode_thread(struct Media* const media)
{
	struct AVPacket packet;
	while (true)
	{
		if (media->state == STATE_QUIT)
			break;
		// TODO: Seek
		if (PacketQueue_size(&media->queueA) > AUDIO_QUEUE_MAX_SIZE ||
		    PacketQueue_size(&media->queueV) > VIDEO_QUEUE_MAX_SIZE)
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
			else
				av_packet_unref(&packet);
		}
		else if (packet.stream_index == (int) media->streamIndexA)
		{
			if (media->streamA)
				PacketQueue_put(&media->queueA, &packet);
			else
				av_packet_unref(&packet);
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
void play_file(char const* fileName)
{
	struct Media media;
	Media_init(&media);
	strncpy(media.fileName, fileName, sizeof(media.fileName));
	media.state = STATE_NORMAL;

	if (avformat_open_input(&media.formatContext, media.fileName, NULL, NULL) != 0)
	{
		fprintf(stderr, "Unable to open file\n");
		goto fail;
	}
	if (avformat_find_stream_info(media.formatContext, NULL) < 0)
	{
		fprintf(stderr, "Unable to find streams\n");
		goto fail;
	}
	av_dump_format(media.formatContext, 0, media.fileName, 0);

	// Find Audio and Video streams

	// Audio stream
	for (unsigned i = 0; i < media.formatContext->nb_streams; ++i)
		if (media.formatContext->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO)
		{
			if (!av_stream_context(media.formatContext, i, &media.ccA)) break;
			media.streamIndexA = i;
			media.streamA = media.formatContext->streams[i];
			audio_load_SDL(&media);
			media.threadAudio = SDL_CreateThread((SDL_ThreadFunction) audio_thread,
			                                      "audio", &media);
			break;
		}
	for (unsigned i = 0; i < media.formatContext->nb_streams; ++i)
		if (media.formatContext->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
		{
			if (!av_stream_context(media.formatContext, i, &media.ccV)) break;

			// TODO: Allow the window to be resized
			media.outWidth = media.ccV->width;
			media.outHeight = media.ccV->height;
			media.swsContext = sws_getContext(media.ccV->width, media.ccV->height,
			                                   media.ccV->pix_fmt,
			                                   media.outWidth, media.outHeight,
			                                   AV_PIX_FMT_YUV420P, SWS_BILINEAR,
			                                   NULL, NULL, NULL);
			media.streamIndexV = i;
			media.screen = SDL_CreateWindow(media.fileName, SDL_WINDOWPOS_UNDEFINED,
			                                 SDL_WINDOWPOS_UNDEFINED,
			                                 media.outWidth, media.outHeight,
			                                 SDL_WINDOW_RESIZABLE);
			if (!media.screen)
			{
				fprintf(stderr, "[SDL] %s\n", SDL_GetError());
				break;
			}
			media.renderer = SDL_CreateRenderer(media.screen, -1, 0);
			if (!media.renderer)
			{
				fprintf(stderr, "[SDL] %s\n", SDL_GetError());
				media.screen = NULL;
				break;
			}

			media.streamV = media.formatContext->streams[i];
			Media_pictQueue_init(&media);
			media.threadVideo = SDL_CreateThread((SDL_ThreadFunction) video_thread,
			                                      "video", &media);
			break;
		}
	// Empty media is not worth playing
	if (!media.streamV && !media.streamA) goto fail;
	media.threadParse = SDL_CreateThread((SDL_ThreadFunction) decode_thread,
	                                      "decode", &media);
	if (!media.threadParse)
	{
		fprintf(stderr, "Failed to launch thread\n");
		goto fail;
	}

	schedule_refresh(&media, 40);
	while (true)
	{
		SDL_Event event;
		SDL_WaitEvent(&event);
		switch (event.type)
		{
		case CHAL_EVENT_QUIT:
		case SDL_QUIT:
			media.state = STATE_QUIT;
			SDL_Quit();
			goto fail;
			break;
		case CHAL_EVENT_REFRESH:
			video_refresh_timer(event.user.data1);
			break;
		default:
			break;
		}
	}

fail:
	if (media.streamV) Media_pictQueue_destroy(&media);
	SDL_DestroyRenderer(media.renderer);
	SDL_DestroyWindow(media.screen);
	if (media.ccA) audio_unload_SDL(&media);
	avcodec_close(media.ccA);
	avcodec_close(media.ccV);
	avformat_close_input(&media.formatContext);
	Media_destroy(&media);
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
	if (argc > 1)
	{
		if (strcmp(argv[1], "--help") == 0)
		{
			fprintf(stdout,
			        "Usage:\n"
			        "Execute with no argument to enter the interactive console\n"
			        "--test/-t: Execute a test routine to check functions\n"
			        "--file/-f: Play a media file. The file name must be supplied after"
			        " the argument.\n");
		}
		else if (strcmp(argv[1], "--test") == 0 ||
		         strcmp(argv[1], "-t") == 0)
		{
			test();
		}
		else if (strcmp(argv[1], "--file") == 0 ||
		         strcmp(argv[1], "-f") == 0)
		{
			if (argc > 2)
				play_file(argv[2]);
			else
				fprintf(stderr, "Argument error: Please supply one or more file names\n");
		}
		else
			fprintf(stderr, "Argument error: Unknown argument\n");
		return 0;
	}

	int nAudioDevices = SDL_GetNumAudioDevices(0);

	if (!nAudioDevices)
	{
		fprintf(stdout, "Warning: No audio device found\n");
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
			test();
		}
		else if (strcmp(token, "info") == 0)
		{
			token = strtok(NULL, " ");
			if (!token)
			{
				printf("Usage:\n"
						"info devices: Print available audio devices\n");
				continue;
			}
			nAudioDevices = SDL_GetNumAudioDevices(0);
			printf("Total number of devices: %d\n", nAudioDevices);
			for (int i = 0; i < nAudioDevices; ++i)
			{
				printf("%d: %s\n", i, SDL_GetAudioDeviceName(i, 0));
			}
		}
		else if (strcmp(token, "refresh") == 0 ||
				strcmp(token, "re") == 0)
		{
			nAudioDevices = SDL_GetNumAudioDevices(0);
		}
		else if (strcmp(token, "play") == 0)
		{
			token = strtok(NULL, " ");
			if (!token)
				printf("Please supply an argument\n");
			else
				play_file(token);
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
