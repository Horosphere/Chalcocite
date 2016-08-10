#include "playback.h"

#include <assert.h>
#include <libavutil/time.h>

#include "media.h"
#include "video.h"
#include "audio.h"

#define SYNC_LOWER_THRESHOULD 0.01
#define SYNC_UPPER_THRESHOULD 10.0

static void video_refresh_timer(struct Media* const media)
{
	if (media->state == STATE_QUIT)
	{
		SDL_SetRenderDrawColor(media->renderer, 255, 127, 255, 255);
		SDL_RenderClear(media->renderer);
		SDL_RenderPresent(media->renderer);
		return;
	}
	if (!media->streamV)
	{
		schedule_refresh(media, 100);
		return;
	}

	if (media->pictQueueSize == 0)
		schedule_refresh(media, 1);
	else
	{
		// Show picture
		struct VideoPicture* vp = &media->pictQueue[media->pictQueueIndexR];
		assert(vp->texture &&
		       vp->planeY && vp->planeU && vp->planeV);

		double delay = vp->timestamp - media->lastFrameTimestamp;
		if (delay <= 0.0 || delay >= 1.0)
			delay = media->lastFrameDelay;
		media->lastFrameDelay = delay;
		media->lastFrameTimestamp = vp->timestamp;

		// Synchronise with audio
		double audioReference = Media_get_audio_clock(media);
		double diff = vp->timestamp - audioReference;
		double syncThreshould = (delay > SYNC_LOWER_THRESHOULD) ? delay :
		                        SYNC_LOWER_THRESHOULD;
		if (fabs(diff) < syncThreshould)
		{
			if (diff <= syncThreshould) // Video behind
				delay = 0.0;
			else if (diff >= syncThreshould) // Audio behind
				delay *= 2.0;
		}
		media->timer += delay;
		// 1,000,000 converts microsecond to second
		double delayReal = media->timer - (av_gettime() / 1000000.0);
		if (delayReal < 0.01) delayReal = 0.01;

		fprintf(stdout, "\b[Video] F:%f, D:%f, A:%f, T:%f, R:%f\r",
		        media->timer, delay, audioReference, vp->timestamp, delayReal);
		fflush(stdout);
		schedule_refresh(media, (int)(delayReal * 1000 + 0.5));


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
static int video_thread(struct Media* const media)
{
	AVFrame* frame = media->frameVideo;

	size_t pitchUV = media->outWidth / 2;

	double pts;
	while (true)
	{
		struct AVPacket packet;
		if (PacketQueue_get(&media->queueV, &packet, true, &media->state) < 0)
		{
			break;
		}
		int finished;
		avcodec_decode_video2(media->ccV, frame, &finished, &packet);

		pts = packet.dts == AV_NOPTS_VALUE ? 0.0 :
		      av_frame_get_best_effort_timestamp(frame);
		pts *= av_q2d(media->streamV->time_base);

		if (finished)
		{
			pts = Media_synchronise_video(media, frame, pts);
			if (!Media_pictQueue_wait_write(media)) break;
			struct VideoPicture* vp = &media->pictQueue[media->pictQueueIndexW];

			uint8_t* imageData[3];
			int imageLinesize[3];
			imageData[0] = vp->planeY;
			imageData[1] = vp->planeU;
			imageData[2] = vp->planeV;
			imageLinesize[0] = media->outWidth;
			imageLinesize[1] = pitchUV;
			imageLinesize[2] = pitchUV;

			sws_scale(media->swsContext,
			          (uint8_t const* const*) frame->data,
			          frame->linesize, 0, media->ccV->height,
			          imageData, imageLinesize);
			vp->timestamp = pts;

			// Move picture queue writing index
			if (++media->pictQueueIndexW == PICTQUEUE_SIZE)
				media->pictQueueIndexW = 0;
			SDL_LockMutex(media->pictQueueMutex);
			++media->pictQueueSize;
			SDL_UnlockMutex(media->pictQueueMutex);

			av_packet_unref(&packet);
		}
	}
	fprintf(stdout, "Video thread complete\n");
	return 0;
}
static int audio_thread(struct Media* const media)
{
	AVFrame* frame = media->frameAudio;
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
			int dataSize = avcodec_decode_audio4(media->ccA, frame,
			                                     &gotFrame, &packet);
			if (dataSize >= 0 && gotFrame)
			{
				packet.size -= dataSize;
				packet.data += dataSize;
				int bufferSize = av_samples_get_buffer_size(NULL, media->ccA->channels,
				                 frame->nb_samples, AV_SAMPLE_FMT_S16, true);
				swr_convert(media->swrContext, &buffer, bufferSize,
				            (uint8_t const**) frame->extended_data,
				            frame->nb_samples);
				SDL_QueueAudio(media->audioDevice, buffer, bufferSize);

				media->clockAudio += bufferSize / (media->ccA->channels *
				                                   media->ccA->sample_rate);
			}
			else
			{
				packet.size = 0;
				packet.data = NULL;
			}

			if (packet.pts != AV_NOPTS_VALUE)
				media->clockAudio = av_q2d(media->streamA->time_base) * packet.pts;
		}
		av_packet_unref(&packet);
	}
	free(buffer);
	fprintf(stdout, "Audio thread complete\n");
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
			if (media->screen)
				PacketQueue_put(&media->queueV, &packet);
			else
				av_packet_unref(&packet);
		}
		else if (packet.stream_index == (int) media->streamIndexA)
		{
			if (media->audioDevice)
				PacketQueue_put(&media->queueA, &packet);
			else
				av_packet_unref(&packet);
		}
		else
			av_packet_unref(&packet);
	}
	while (media->state != STATE_QUIT)
		SDL_Delay(100);


	SDL_Event event;
	event.type = CHAL_EVENT_QUIT;
	event.user.data1 = media;
	SDL_PushEvent(&event);
	fprintf(stdout, "Decoding complete\n");

	return 0;
}
void play_file(char const* const fileName)
{
	struct Media media;
	Media_init(&media);
	strncpy(media.fileName, fileName, sizeof(media.fileName));
	media.formatContext = av_open_file(fileName);
	if (!media.formatContext)
	{
		return;
	}
	av_dump_format(media.formatContext, 0, media.fileName, 0);

	// Find Audio and Video streams

	// Converts av_gettime()'s microsecond to second
	media.state = STATE_NORMAL;
	media.timer = (double)av_gettime() / 1000000.0;
	media.lastFrameDelay = 40e-3;

	if (!Media_open_best_streams(&media))
	{
		goto complete;
	}
	if (media.streamA)
	{
		audio_load_SDL(&media);
		media.threadAudio = SDL_CreateThread((SDL_ThreadFunction) audio_thread,
		                                      "audio", &media);
	}
	if (media.streamV)
	{
		media.outWidth = media.ccV->width;
		media.outHeight = media.ccV->height;
		media.swsContext = sws_getContext(media.ccV->width, media.ccV->height,
		                                   media.ccV->pix_fmt,
		                                   media.outWidth, media.outHeight,
		                                   AV_PIX_FMT_YUV420P, SWS_BILINEAR,
		                                   NULL, NULL, NULL);
		media.screen = SDL_CreateWindow(media.fileName, SDL_WINDOWPOS_UNDEFINED,
		                                 SDL_WINDOWPOS_UNDEFINED,
		                                 media.outWidth, media.outHeight,
		                                 SDL_WINDOW_RESIZABLE);
		if (!media.screen)
		{
			fprintf(stderr, "[SDL] %s\n", SDL_GetError());
			goto start;
		}
		media.renderer = SDL_CreateRenderer(media.screen, -1, 0);
		if (!media.renderer)
		{
			fprintf(stderr, "[SDL] %s\n", SDL_GetError());
			media.screen = NULL;
			goto start;
		}
		Media_pictQueue_init(&media);
		media.threadVideo = SDL_CreateThread((SDL_ThreadFunction) video_thread,
		                                      "video", &media);
	}
start:

	media.threadParse = SDL_CreateThread((SDL_ThreadFunction) decode_thread,
	                                      "decode", &media);

	schedule_refresh(&media, 40);

	printf("\n");
	fflush(stdout);
	while (true)
	{
		SDL_Event event;
		SDL_WaitEvent(&event);
		switch (event.type)
		{
		case CHAL_EVENT_QUIT:
		case SDL_QUIT:
			media.state = STATE_QUIT;
			goto complete;
			break;
		case CHAL_EVENT_REFRESH:
			video_refresh_timer(event.user.data1);
			break;
		default:
			break;
		}
	}

complete:
	if (media.streamV) Media_pictQueue_destroy(&media);
	SDL_DestroyRenderer(media.renderer);
	SDL_DestroyWindow(media.screen);
	audio_unload_SDL(&media);

	Media_close(&media);
	avformat_close_input(&media.formatContext);
	Media_destroy(&media);
	return;
}
