#include "media.h"

#include <assert.h>

#include <SDL2/SDL_thread.h>
#include <libavutil/time.h>

struct AVFormatContext* av_open_file(char const* fileName)
{
	struct AVFormatContext* fc = NULL;
	if (avformat_open_input(&fc, fileName, NULL, NULL))
	{
		fprintf(stderr, "Unable to open file\n");
		return NULL;
	}
	if (avformat_find_stream_info(fc, NULL) < 0)
	{
		fprintf(stderr, "Unable to find streams within media\n");
		avformat_close_input(&fc);
		return NULL;
	}
	return fc;
}
bool av_stream_context(struct AVFormatContext* const fc, unsigned streamIndex,
                       struct AVCodecContext** const cc)
{
	assert(streamIndex < fc->nb_streams);

	AVCodec* codec = avcodec_find_decoder(fc->streams[streamIndex]->codec->codec_id);
	if (!codec)
	{
		fprintf(stderr, "Unsupported codec\n");
		return false;
	}
	*cc = avcodec_alloc_context3(codec);
	if (avcodec_copy_context(*cc, fc->streams[streamIndex]->codec))
	{
		fprintf(stderr, "Could not copy codec context\n");
		avcodec_free_context(cc);
		return false;
	}
	if (avcodec_open2(*cc, codec, NULL) < 0)
	{
		fprintf(stderr, "Unsupported codec\n");
		avcodec_free_context(cc);
		return false;
	}
	return true;
}

void Media_init(struct Media* const media)
{
	assert(media);
	memset(media, 0, sizeof(struct Media));
	media->streamIndexA = media->streamIndexV = CHAL_UNSIGNED_INVALID;
	media->pictQueueMutex = SDL_CreateMutex();
	media->pictQueueCond = SDL_CreateCond();
	PacketQueue_init(&media->queueA);
	PacketQueue_init(&media->queueV);
	media->frameVideo = av_frame_alloc();
	media->frameAudio = av_frame_alloc();
}
void Media_destroy(struct Media* const media)
{
	if (!media) return;
	SDL_DestroyMutex(media->pictQueueMutex);
	SDL_DestroyCond(media->pictQueueCond);
	PacketQueue_destroy(&media->queueA);
	PacketQueue_destroy(&media->queueV);
	swr_free(&media->swrContext);
	sws_freeContext(media->swsContext);
	av_frame_free(&media->frameVideo);
	av_frame_free(&media->frameAudio);
}
bool Media_pictQueue_init(struct Media* const media)
{
	assert(media);
	assert(media->renderer);
	assert(media->outWidth != 0 && media->outHeight != 0);

	size_t planeSizeY = media->outWidth * media->outHeight;
	size_t planeSizeUV = planeSizeY / 4;
	for (size_t i = 0; i < PICTQUEUE_SIZE; ++i)
	{
		struct VideoPicture* const vp = &media->pictQueue[i];
		vp->width = media->outWidth;
		vp->height = media->outHeight;
		vp->texture = SDL_CreateTexture(media->renderer,
		                                SDL_PIXELFORMAT_YV12,
		                                SDL_TEXTUREACCESS_STREAMING,
		                                vp->width, vp->height);
		if (!vp->texture) goto fail;
		vp->planeY = malloc(sizeof(*vp->planeY) * planeSizeY);
		vp->planeU = malloc(sizeof(*vp->planeU) * planeSizeUV);
		vp->planeV = malloc(sizeof(*vp->planeV) * planeSizeUV);
		if (!vp->planeY || !vp->planeU || !vp->planeV) goto fail;
	}
	return true;
fail:
	Media_pictQueue_destroy(media);
	return false;
}
void Media_pictQueue_destroy(struct Media* const media)
{
	if (!media) return;
	for (size_t i = 0; i < PICTQUEUE_SIZE; ++i)
	{
		struct VideoPicture* const vp = &media->pictQueue[i];
		SDL_DestroyTexture(vp->texture);
		free(vp->planeY);
		free(vp->planeU);
		free(vp->planeV);
	}
}
bool Media_pictQueue_wait_write(struct Media* const media)
{
	assert(media);
	SDL_LockMutex(media->pictQueueMutex);
	while (media->pictQueueSize >= PICTQUEUE_SIZE && media->state != STATE_QUIT)
		SDL_CondWait(media->pictQueueCond, media->pictQueueMutex);
	SDL_UnlockMutex(media->pictQueueMutex);

	if (media->state == STATE_QUIT) return false;
	return true;
}

bool Media_open_best_streams(struct Media* const media)
{
	// Audio stream
	for (unsigned i = 0; i < media->formatContext->nb_streams; ++i)
		if (media->formatContext->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO)
		{
			if (!av_stream_context(media->formatContext, i, &media->ccA)) break;
			media->streamIndexA = i;
			media->streamA = media->formatContext->streams[i];
			break;
		}
	for (unsigned i = 0; i < media->formatContext->nb_streams; ++i)
		if (media->formatContext->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
		{
			if (!av_stream_context(media->formatContext, i, &media->ccV)) break;

			// TODO: Allow the window to be resized
			media->streamIndexV = i;
			media->streamV = media->formatContext->streams[i];
			break;
		}
	return media->ccA || media->ccV;
}
void Media_close(struct Media* const media)
{
	avcodec_close(media->ccA);
	avcodec_close(media->ccV);
}
// Synchronisation
double Media_synchronise_video(struct Media* const media,
                               struct AVFrame* const frame,
                               double pts)
{
	double frameDelay;
	if (pts != 0.0)
		media->clockVideo = pts;
	else
		pts = media->clockVideo;
	frameDelay = av_q2d(media->streamV->codec->time_base);
	frameDelay += frame->repeat_pict * (frameDelay * 0.5);
	media->clockVideo += frameDelay;

	return pts;
}

double Media_get_audio_clock(struct Media const* const media)
{
	double pts = media->clockAudio;

	return pts;
}
