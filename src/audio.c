#include "audio.h"

#include <assert.h>

#define MAX(a, b) (a) < (b) ? (b) : (a)

bool audio_load_SDL(struct Media* const media)
{
	SDL_AudioSpec specTarget;
	specTarget.freq = media->ccA->sample_rate;
	specTarget.format = AUDIO_S16SYS;
	specTarget.channels = media->ccA->channels;
	specTarget.silence = 0;
	specTarget.samples = 1024;
	specTarget.callback = NULL;

	media->audioDevice = SDL_OpenAudioDevice(NULL, 0,
			&specTarget, &media->audioSpec, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
	if (!media->audioDevice)
	{
		fprintf(stderr, "[SDL] %s\n", SDL_GetError());
		return false;
	}
	media->swrContext = swr_alloc_set_opts(NULL,
			av_get_default_channel_layout(media->audioSpec.channels), AV_SAMPLE_FMT_S16, media->audioSpec.freq,
			media->ccA->channel_layout, media->ccA->sample_fmt, media->ccA->sample_rate,
			0, NULL);
	if (!media->swrContext)
	{
		fprintf(stderr, "Unable to allocate Swr_Context\n");
		SDL_CloseAudioDevice(media->audioDevice);
		return false;
	}
	if (swr_init(media->swrContext) < 0)
	{
		fprintf(stderr, "Unable to initialise Swr_Context\n");
		swr_free(&media->swrContext);
		SDL_CloseAudioDevice(media->audioDevice);
		return false;
	}

	SDL_PauseAudioDevice(media->audioDevice, 0);
	return true;
}
void audio_unload_SDL(struct Media* const media)
{
	swr_free(&media->swrContext);
	SDL_CloseAudioDevice(media->audioDevice);
}
