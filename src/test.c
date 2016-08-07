#include "test.h"

#include <stdbool.h>
#include <assert.h>
#include <math.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_thread.h>

#include "chalcocite.h"
#include "media.h"

// Audio playing test code

#define TEST_WIDTH_IN 640
#define TEST_HEIGHT_IN 480
#define TEST_PIXFMT_IN AV_PIX_FMT_RGB24

static uint32_t test_refresh_timer_cb(uint32_t interval, void* data)
{
	(void) interval;

	SDL_Event event;
	event.type = CHAL_EVENT_REFRESH;
	event.user.data1 = data;
	SDL_PushEvent(&event);
	return 0; // Stops the timer
}
static void test_schedule_refresh(struct Media* const media, int delay)
{
	if (!SDL_AddTimer(delay, test_refresh_timer_cb, media))
	{
		fprintf(stderr, "[SDL] %s\n", SDL_GetError());
	}
}
static void test_video_refresh_timer(struct Media* const media)
{
	if (media->streamV)
	{
		if (media->pictQueueSize == 0)
			test_schedule_refresh(media, 1);
		else
		{
			test_schedule_refresh(media, 40);

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
	else test_schedule_refresh(media, 100);
}
static void test_fill_rgb24(uint8_t* const data, size_t width, size_t height)
{
	size_t width2 = width / 2;
	size_t height2 = height / 2;
	for (size_t i = 0; i < width2; ++i)
	{
		for (size_t j = 0; j < height2; ++j)
		{
			size_t base = i + j * width;
			data[base * 3 + 0] = 0;
			data[base * 3 + 1] = 127;
			data[base * 3 + 2] = 255;
		}
		for (size_t j = height2; j < height; ++j)
		{
			size_t base = i + j * width;
			data[base * 3 + 0] = 255;
			data[base * 3 + 1] = 255;
			data[base * 3 + 2] = 0;
		}
	}
	for (size_t i = width2; i < width; ++i)
	{
		for (size_t j = 0; j < height2; ++j)
		{
			size_t base = i + j * width;
			data[base * 3 + 0] = 0;
			data[base * 3 + 1] = 0;
			data[base * 3 + 2] = 255;
		}
		for (size_t j = height2; j < height; ++j)
		{
			size_t base = i + j * width;
			data[base * 3 + 0] = 255;
			data[base * 3 + 1] = 127;
			data[base * 3 + 2] = 0;
		}
	}
}
static int test_video_thread(struct Media* const media)
{
	uint8_t* dataIn[3];
	uint8_t dataInRGB[TEST_WIDTH_IN * TEST_HEIGHT_IN * 3];
	dataIn[0] = dataInRGB;
	dataIn[1] = dataInRGB;
	dataIn[2] = dataInRGB;

	int linesizeIn[3];
	linesizeIn[0] = TEST_WIDTH_IN * 3;
	linesizeIn[1] = TEST_WIDTH_IN * 3;
	linesizeIn[2] = TEST_WIDTH_IN * 3;
	uint8_t* dataOut[3];
	int linesizeOut[3];
	
	test_fill_rgb24(dataInRGB, TEST_WIDTH_IN, TEST_HEIGHT_IN);
	size_t pitchUV = media->outWidth / 2;

	while (true)
	{

		if (!Media_pictQueue_wait_write(media)) break;
		struct VideoPicture* vp = &media->pictQueue[media->pictQueueIndexW];
		dataOut[0] = vp->planeY;
		dataOut[1] = vp->planeU;
		dataOut[2] = vp->planeV;
		linesizeOut[0] = media->outWidth;
		linesizeOut[1] = pitchUV;
		linesizeOut[2] = pitchUV;


		sws_scale(media->swsContext,
		          (uint8_t const* const*) dataIn, linesizeIn, 0, TEST_HEIGHT_IN,
		          dataOut, linesizeOut);

		// Move picture queue writing index
		if (++media->pictQueueIndexW == PICTQUEUE_SIZE)
			media->pictQueueIndexW = 0;
		SDL_LockMutex(media->pictQueueMutex);
		++media->pictQueueSize;
		SDL_UnlockMutex(media->pictQueueMutex);
	}
	return 0;
}
void test()
{
	fprintf(stdout, "Executing Chalcocite test routine\n");

	struct Media media;
	Media_init(&media);
	media.state = STATE_NORMAL;

	media.streamV = (void*) 1;
	media.outWidth = TEST_WIDTH_IN;
	media.outHeight = TEST_HEIGHT_IN;
	media.screen = SDL_CreateWindow("Test",
	                                SDL_WINDOWPOS_UNDEFINED,
	                                SDL_WINDOWPOS_UNDEFINED,
	                                media.outWidth, media.outHeight,
	                                SDL_WINDOW_RESIZABLE);
	media.renderer = SDL_CreateRenderer(media.screen, -1, 0);

	media.swsContext = sws_getContext(TEST_WIDTH_IN, TEST_HEIGHT_IN,
	                                  TEST_PIXFMT_IN,
	                                  media.outWidth, media.outHeight,
	                                  AV_PIX_FMT_YUV420P, SWS_BILINEAR,
	                                  NULL, NULL, NULL);
	Media_pictQueue_init(&media);

	media.threadVideo = SDL_CreateThread((SDL_ThreadFunction) test_video_thread,
	                                     "video", &media);
	test_schedule_refresh(&media, 40);
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
			goto finish;
			break;
		case CHAL_EVENT_REFRESH:
			test_video_refresh_timer(event.user.data1);
			break;
		default:
			break;
		}
	}

finish:
	return;
}
