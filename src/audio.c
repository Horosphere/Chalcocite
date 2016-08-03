#include "audio.h"

#include <assert.h>

int audio_decode_frame(struct Media* media, uint8_t* buffer,
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
				swr_convert(media->swrContext, &buffer, bufferSize,
						(uint8_t const**) media->audioFrame.extended_data,
						media->audioFrame.nb_samples);
				//memcpy(buffer, media->audioFrame.data[0], dataSize);
			}
			if (dataSize > 0) return dataSize; // Got data
		}
		if (media->audioPacket.data) av_packet_unref(&media->audioPacket);
		if (media->state == STATE_QUIT) return -1;
		if (PacketQueue_get(&media->queueA, &media->audioPacket, true,
					&media->state) < 0)
			return -1;

		// Update relevant members of media
		media->audioPacketData = media->audioPacket.data;
		media->audioPacketSize = media->audioPacket.size;
	}
}
void audio_callback(struct Media* media, uint8_t* stream, int len)
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

bool audio_load_SDL(struct Media* const media)
{
	SDL_AudioSpec specTarget;
	specTarget.freq = media->ccA->sample_rate;
	specTarget.format = AUDIO_S16SYS;
	specTarget.channels = media->ccA->channels;
	specTarget.silence = 0;
	specTarget.samples = 1024;
	specTarget.callback = (void (*)(void*, uint8_t*, int)) audio_callback;
	specTarget.userdata = media;

	SDL_AudioSpec spec;
	if (SDL_OpenAudio(&specTarget, &spec) < 0)
	{
		fprintf(stderr, "[SDL] %s\n", SDL_GetError());
		return false;
	}
	media->swrContext = swr_alloc_set_opts(NULL,
			av_get_default_channel_layout(spec.channels), AV_SAMPLE_FMT_S16, spec.freq,
			media->ccA->channel_layout, media->ccA->sample_fmt, media->ccA->sample_rate,
			0, NULL);
	if (!media->swrContext || swr_init(media->swrContext) < 0)
	{
		fprintf(stderr, "Unable to allocate/Initialise Swr_Context\n");
		return false;
	}

	SDL_PauseAudio(0);
	return true;
}
void audio_unload_SDL(struct Media* const media)
{
	(void) media;
	SDL_CloseAudio();
}
