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
	media->screenMutex = SDL_CreateMutex();
	PacketQueue_init(&media->queueA);
	PacketQueue_init(&media->queueV);
}
void Media_destroy(struct Media* const media)
{
	if (!media) return;
	for (size_t i = 0; i < PICTQUEUE_SIZE; ++i)
		if (media->pictQueue[i].allocated)
			SDL_DestroyTexture(media->pictQueue[i].texture);
	SDL_DestroyMutex(media->pictQueueMutex);
	SDL_DestroyCond(media->pictQueueCond);
	SDL_DestroyMutex(media->screenMutex);
	PacketQueue_destroy(&media->queueA);
	PacketQueue_destroy(&media->queueV);
}

bool Media_queue_picture(struct Media* const media)
{
	// Wait for space in pictQueue
	SDL_LockMutex(media->pictQueueMutex);
	while (media->pictQueueSize >= PICTQUEUE_SIZE && media->state != STATE_QUIT)
		SDL_CondWait(media->pictQueueCond, media->pictQueueMutex);
	SDL_UnlockMutex(media->pictQueueMutex);

	if (media->state == STATE_QUIT) return false;
	struct VideoPicture* vp = &media->pictQueue[media->pictQueueIndexW];
	if (!vp->texture ||
			vp->width != media->ccV->width ||
			vp->height != media->ccV->height)
	{

		// Allocate video picture
		if (vp->texture)
			SDL_DestroyTexture(vp->texture);
		SDL_LockMutex(media->screenMutex);
		// Update
		vp->texture = SDL_CreateTexture(media->renderer,
				SDL_PIXELFORMAT_YV12, SDL_TEXTUREACCESS_STREAMING,
				media->outWidth, media->outHeight);
		SDL_UnlockMutex(media->screenMutex);
		vp->width = media->outWidth;
		vp->height = media->outHeight;
		vp->allocated = true;
		if (media->state == STATE_QUIT)
		{
			return false;
		}
	}
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
