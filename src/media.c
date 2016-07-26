#include "media.h"

#include <assert.h>
#include <SDL2/SDL_thread.h>

void Media_init(struct Media* const media)
{
	memset(media, 0, sizeof(struct Media));
	media->streamIndexA = media->streamIndexV = CHAL_UNSIGNED_INVALID;
	media->stageCond = SDL_CreateCond();
	media->stageMutex = SDL_CreateMutex();
	media->pictQueueMutex = SDL_CreateMutex();
	media->pictQueueCond = SDL_CreateCond();
	media->screenMutex = SDL_CreateMutex();
}
void Media_destroy(struct Media* const media)
{
	SDL_DestroyCond(media->stageCond);
	SDL_DestroyMutex(media->stageMutex);
	SDL_DestroyMutex(media->pictQueueMutex);
	SDL_DestroyCond(media->pictQueueCond);
	SDL_DestroyMutex(media->screenMutex);
}

bool Media_queue_picture(struct Media* const media)
{
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
		vp->allocated = true;

		// Allocate video picture
		if (vp->texture)
			SDL_DestroyTexture(vp->texture);
		SDL_LockMutex(media->screenMutex);
		// Update
		vp->texture = SDL_CreateTexture(media->renderer,
				SDL_PIXELFORMAT_YV12, SDL_TEXTUREACCESS_STREAMING,
				media->ccV->width, media->ccV->height);
		SDL_UnlockMutex(media->screenMutex);
		vp->width = media->ccV->width;
		vp->height = media->ccV->height;
		vp->allocated = true;
		if (media->state == STATE_QUIT)
		{
			return false;
		}
	}
	return true;
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
	unsigned planeSizeY = media->ccV->width * media->ccV->height;
	unsigned planeSizeUV = planeSizeY / 4;
	uint8_t planeY[planeSizeY];
	uint8_t planeU[planeSizeUV];
	uint8_t planeV[planeSizeUV];

	unsigned pitchUV = media->ccV->width / 2;
	
	while (true)
	{
		AVPacket packet;
		if (PacketQueue_get(&media->queueV, &packet, true) < 0)
		{
			break;
		}
		int finished;
		avcodec_decode_video2(media->streamV->codec, frame, &finished, &packet);

		if (finished)
		{
			if (!Media_queue_picture(media)) break;
			frameRGB->data[0] = planeY;
			frameRGB->data[1] = planeU;
			frameRGB->data[2] = planeV;
			frameRGB->linesize[0] = media->ccV->width;
			frameRGB->linesize[1] = pitchUV;
			frameRGB->linesize[2] = pitchUV;

			sws_scale(media->swsContext, (uint8_t const* const*) frame->data,
					frame->linesize, 0, media->ccV->height,
					frameRGB->data, frameRGB->linesize);
			struct VideoPicture* vp = &media->pictQueue[media->pictQueueIndexW];
			SDL_UpdateYUVTexture(vp->texture, NULL, planeY, media->ccV->width,
					planeU, pitchUV, planeV, pitchUV);
			if (++media->pictQueueIndexW == PICTQUEUE_SIZE)
				media->pictQueueIndexW = 0;
			SDL_LockMutex(media->pictQueueMutex);
			++media->pictQueueSize;
			SDL_UnlockMutex(media->pictQueueMutex);
			
		}
		av_packet_unref(&packet);
	}
	av_frame_free(&frameRGB);
	av_frame_free(&frame);
	return 0;
}
enum AVMediaType Media_open_stream(struct Media* const media,
                                   unsigned streamIndex)
{
	AVFormatContext* fc = media->formatContext;
	if (streamIndex >= fc->nb_streams)
		return false;
	AVCodec* codec = avcodec_find_decoder(fc->streams[streamIndex]->codec->codec_id);
	if (!codec)
	{
		fprintf(stderr, "Unsupported codec\n");
		return AVMEDIA_TYPE_NB;
	}
	AVCodecContext* codecContext = avcodec_alloc_context3(codec);
	if (avcodec_copy_context(codecContext, fc->streams[streamIndex]->codec))
	{
		fprintf(stderr, "Could not copy codec context\n");
		avcodec_free_context(&codecContext);
		return AVMEDIA_TYPE_NB;
	}
	if (avcodec_open2(codecContext, codec, NULL) < 0)
	{
		fprintf(stderr, "Unsupported codec\n");
		avcodec_free_context(&codecContext);
		return AVMEDIA_TYPE_UNKNOWN;
	}
	switch (codecContext->codec_type)
	{
	case AVMEDIA_TYPE_AUDIO:
		media->streamIndexA = streamIndex;
		media->streamA = fc->streams[streamIndex];
		media->ccA = codecContext;
		media->audioBufferSize = 0;
		media->audioBufferIndex = 0;
		memset(&media->queueA, 0, sizeof(media->queueA));
		PacketQueue_init(&media->queueA);
		return AVMEDIA_TYPE_AUDIO;
	case AVMEDIA_TYPE_VIDEO:
		media->streamIndexV = streamIndex;
		media->streamV = fc->streams[streamIndex];
		media->ccV = codecContext;

		PacketQueue_init(&media->queueV);
		media->threadVideo = SDL_CreateThread((int (*)(void*)) video_thread,
				"video", media);
		// TODO: Allow the window to be resized
		media->swsContext = sws_getContext(media->ccV->width, media->ccV->height,
		                                   media->ccV->pix_fmt, media->ccV->width,
		                                   media->ccV->height, AV_PIX_FMT_YUV420P,
		                                   SWS_BILINEAR, NULL, NULL, NULL);
		return AVMEDIA_TYPE_VIDEO;
	default:
		avcodec_free_context(&codecContext);
		return AVMEDIA_TYPE_NB;
	}
}


static int audio_decode_frame(struct Media* media, uint8_t* buffer,
                              size_t bufferSize)
{
	(void) bufferSize;

	while (true) // This loop never ends
	{
		// When there are packets, decode
		while (media->audioPacketSize > 0)
		{
			int gotFrame = 0;
			int len = avcodec_decode_audio4(media->ccA, &media->audioFrame, &gotFrame,
					&media->audioPacket);
			if (len < 0)
			{
				media->audioPacketSize = 0;
				break;
			}
			media->audioPacketData += len;
			media->audioPacketSize -= len;
			int dataSize = 0;
			if (gotFrame)
			{
				dataSize = av_samples_get_buffer_size(NULL, media->ccA->channels,
				                                      media->audioFrame.nb_samples,
				                                      media->ccA->sample_fmt, 1);
				assert(dataSize <= bufferSize);
				memcpy(buffer, media->audioFrame.data[0], dataSize);
			}
			if (dataSize > 0) return dataSize; // Got data
		}
		if (media->audioPacket.data) av_packet_unref(&media->audioPacket);
		if (media->state == STATE_QUIT) return -1;
		if (PacketQueue_get(&media->queueA, &media->audioPacket, true) < 0)
			return -1;

		// Update relevant members of media
		media->audioPacketData = media->audioPacket.data;
		media->audioPacketSize = media->audioPacket.size;
	}
}
static void audio_callback(struct Media* media, uint8_t* stream, int len)
{
	
	while (len > 0) // Send samples until len is depleted.
	{
		if (media->audioBufferIndex >= media->audioBufferSize)
		{
			// All data has been sent. Obtain more
			int audioSize = audio_decode_frame(media, media->audioBuffer,
			                                   sizeof(media->audioBuffer));
			if (audioSize < 0) // Error
			{
				// Silence
				media->audioBufferSize = 1024;
				memset(media->audioBuffer, 0, media->audioBufferSize);
			}
			else
				media->audioBufferSize = audioSize;
			media->audioBufferIndex = 0;
		}
		int l = media->audioBufferSize - media->audioBufferIndex; // Avaliable samples
		if (l > len) l = len; // If this condition holds, the loop will exit.
		memcpy(stream, (uint8_t*)media->audioBuffer + media->audioBufferIndex, l);
		len -= l;
		stream += l;
		media->audioBufferIndex += l;
	}
}
#define MAX(a, b) (a) < (b) ? (b) : (a)

bool load_SDL(struct Media* const media)
{
	SDL_AudioSpec specTarget;
	specTarget.freq = media->ccA->sample_rate;
	specTarget.format = AUDIO_S16SYS;
	specTarget.channels = media->ccA->channels;
	specTarget.silence = 0;
	specTarget.samples = MAX(512, 2 << av_log2(specTarget.freq / 30));
	specTarget.callback = (void (*)(void*, uint8_t*, int)) audio_callback;
	specTarget.userdata = media;

	SDL_AudioSpec spec;
	if (SDL_OpenAudio(&specTarget, &spec) < 0)
	{
		fprintf(stderr, "[SDL] %s\n", SDL_GetError());
		return false;
	}

	SDL_PauseAudio(0);
	return true;
}
