#include "media.h"

#include <assert.h>
#include <SDL2/SDL_thread.h>

void Media_init(struct Media* const media)
{
	if (!media) return;
	memset(media, 0, sizeof(struct Media));
	media->streamIndexA = media->streamIndexV = CHAL_UNSIGNED_INVALID;
	media->pictQueueMutex = SDL_CreateMutex();
	media->pictQueueCond = SDL_CreateCond();
	PacketQueue_init(&media->queueA);
	PacketQueue_init(&media->queueV);
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
}
bool Media_pictQueue_init(struct Media* const media)
{
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
		size_t planeSizeY = vp->width * vp->height;
		size_t planeSizeUV = planeSizeY / 4;
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
	SDL_LockMutex(media->pictQueueMutex);
	while (media->pictQueueSize >= PICTQUEUE_SIZE && media->state != STATE_QUIT)
		SDL_CondWait(media->pictQueueCond, media->pictQueueMutex);
	SDL_UnlockMutex(media->pictQueueMutex);

	if (media->state == STATE_QUIT) return false;
	return true;
}

bool av_stream_context(AVFormatContext* const fc, unsigned streamIndex,
                       AVCodecContext** const cc)
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
