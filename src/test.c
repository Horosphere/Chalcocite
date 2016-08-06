#include "test.h"

#include <stdbool.h>
#include <math.h>

#include <SDL2/SDL.h>
//#include <SDL2/SDL_thread.h>

#include "chalcocite.h"

// Audio playing test code

struct TestData
{
	int count;
};

void test_audio_callback(struct TestData* data,
                         uint8_t* stream, int len)
{
	(void) data;
	(void) stream;
	(void) len;

	if (data->count > 44100 * 5)
	{
		SDL_Event event;
		event.type = CHAL_EVENT_QUIT;
		event.user.data1 = data;
		SDL_PushEvent(&event);
	}

	float* floatStream = (float*) stream;
	for (size_t i = 0; i < len / sizeof(float); ++i)
	{
		floatStream[i] = sin(i / 2.2f);
	}

	data->count += len;
}
void test()
{
	fprintf(stdout, "Executing Chalcocite test routine\n");

	SDL_Window* screen = SDL_CreateWindow("Test",
	                                      SDL_WINDOWPOS_UNDEFINED,
	                                      SDL_WINDOWPOS_UNDEFINED,
	                                      640, 480, 0);
	SDL_Renderer* renderer = SDL_CreateRenderer(screen, -1, 0);

	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
	SDL_RenderClear(renderer);
	SDL_RenderPresent(renderer);

	struct TestData data;
	memset(&data, 0, sizeof(struct TestData));

	SDL_AudioSpec specTarget;
	// Test data
	specTarget.freq = 44100;
	specTarget.format = AUDIO_F32;
	specTarget.channels = 1;
	specTarget.silence = 0;
	specTarget.samples = 1024;
	specTarget.callback = NULL; // Do not use callback, push audio instead
	//specTarget.userdata = &data;

	SDL_AudioSpec spec;
	SDL_AudioDeviceID audioDevice =
	  SDL_OpenAudioDevice(NULL, 0,
	                      &specTarget, &spec,
	                      SDL_AUDIO_ALLOW_FORMAT_CHANGE);
	if (!audioDevice)
	{
		fprintf(stderr, "[SDL] %s\n", SDL_GetError());
		return;
	}
	SDL_PauseAudioDevice(audioDevice, 0);

	int index = 0;
	float samples[1024];
	while (true)
	{
		if (index > 44100 * 5) break;

		for (int i = 0; i < 1024; ++i)
		{
			samples[i] = sin(2 * M_PI * (i * 0.01f +
			                             (index + i) * (index + i) * 0.00001f));
		}
		SDL_QueueAudio(audioDevice, samples, sizeof(samples));
		index += 1024;

	}

	SDL_CloseAudioDevice(audioDevice);
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(screen);
}
