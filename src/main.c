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
	SDL_AddTimer(delay, refresh_timer_cb, media);
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
		if (!vp->texture) return;
		float aspectRatio = media->ccV->sample_aspect_ratio.num ?
			av_q2d(media->ccV->sample_aspect_ratio) * media->ccV->width /
			media->ccV->height : 0;
		if (aspectRatio <= 0.f)
			aspectRatio = (float) media->ccV->width / (float) media->ccV->height;
		SDL_Rect rect;
		rect.h = media->ccV->height;
		rect.w = media->ccV->width;
		rect.x = 0;
		rect.y = 0;
		SDL_LockMutex(media->screenMutex);
		SDL_RenderClear(media->renderer);
		SDL_RenderCopy(media->renderer, vp->texture, NULL, NULL);
		SDL_RenderPresent(media->renderer);
		SDL_UnlockMutex(media->screenMutex);
		SDL_DestroyTexture(vp->texture);

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
int decode_thread(struct Media* const media)
{
	int flag = -1;
	AVFormatContext* formatContext = NULL;
	if (avformat_open_input(&formatContext, media->fileName, NULL, NULL) != 0)
	{
		fprintf(stderr, "Unable to open file\n");
		goto fail0;
	}
	media->formatContext = formatContext;
	if (avformat_find_stream_info(formatContext, NULL) < 0)
	{
		fprintf(stderr, "Unable to find streams\n");
		goto fail1;
	}
	av_dump_format(formatContext, 0, media->fileName, 0);

	// Find video stream
	unsigned streamIndexVideo = CHAL_UNSIGNED_INVALID;
	unsigned streamIndexAudio = CHAL_UNSIGNED_INVALID;
	for (unsigned i = 0; i < formatContext->nb_streams; ++i)
		if (formatContext->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
		{
			streamIndexVideo = i;
			break;
		}
	for (unsigned i = 0; i < formatContext->nb_streams; ++i)
		if (formatContext->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO)
		{
			streamIndexAudio = i;
			break;
		}
	if (streamIndexVideo != CHAL_UNSIGNED_INVALID)
	{
		if (Media_open_stream(media, streamIndexVideo) == AVMEDIA_TYPE_NB)
		{
			fprintf(stderr, "Could not open codecs\n");
			goto fail1;
		}
	}
	if (streamIndexAudio != CHAL_UNSIGNED_INVALID)
	{
		if (Media_open_stream(media, streamIndexAudio) == AVMEDIA_TYPE_NB)
		{
			fprintf(stderr, "Could not open codecs\n");
			goto fail2;
		}
	}
	load_SDL(media);

	SDL_CondSignal(media->stageCond);
	media->screen = SDL_CreateWindow(media->fileName, SDL_WINDOWPOS_UNDEFINED,
			SDL_WINDOWPOS_UNDEFINED, media->ccV->width, media->ccV->height, 0);
	if (!media->screen)
	{
		fprintf(stderr, "[SDL] %s\n", SDL_GetError());
	}
	media->renderer = SDL_CreateRenderer(media->screen, -1, 0);
	if (!media->renderer)
	{
		fprintf(stderr, "[SDL] %s\n", SDL_GetError());
		SDL_DestroyWindow(media->screen);
	}
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
			PacketQueue_put(&media->queueV, &packet);
		else if (packet.stream_index == (int) media->streamIndexA)
			PacketQueue_put(&media->queueA, &packet);
		else
			av_packet_unref(&packet);
	}
	fprintf(stdout, "Decoding complete. Waiting for program to exit\n");
	while (media->state == STATE_QUIT)
		SDL_Delay(100);

	flag = 0;

	avcodec_close(media->ccA);
fail2:
	avcodec_close(media->ccV);
fail1:
	avformat_close_input(&formatContext);
fail0:;
	SDL_Event event;
	event.type = CHAL_EVENT_QUIT;
	event.user.data1 = media;
	SDL_PushEvent(&event);

	return flag;
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

	schedule_refresh(media, 40);
	media->threadParse = SDL_CreateThread((int (*)(void*)) decode_thread,
			"decode", media);
	if (!media->threadParse)
	{
		fprintf(stderr, "Failed to launch thread\n");
		goto fail1;
	}
	SDL_LockMutex(media->stageMutex);
	SDL_CondWait(media->stageCond, media->stageMutex);
	SDL_UnlockMutex(media->stageMutex);

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
			break;	
		case CHAL_EVENT_REFRESH:
			video_refresh_timer(event.user.data1);
			break;
		default:
			break;
		}
	}

	SDL_DestroyRenderer(media->renderer);
	SDL_DestroyWindow(media->screen);
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
