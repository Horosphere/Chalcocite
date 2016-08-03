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

	struct TestData data;
	memset(&data, 0, sizeof(struct TestData));

	SDL_AudioSpec specTarget;
	// Test data
	specTarget.freq = 44100;
	specTarget.format = AUDIO_F32;
	specTarget.channels = 1;
	specTarget.silence = 0;
	specTarget.samples = 1024;
	specTarget.callback = (SDL_AudioCallback) test_audio_callback;
	specTarget.userdata = &data;

	SDL_AudioSpec spec;
	if (SDL_OpenAudio(&specTarget, &spec) < 0)
	{
		fprintf(stderr, "[SDL] %s\n", SDL_GetError());
		return;
	}
	SDL_PauseAudio(0);
	while (true)
	{
		SDL_Event event;
		SDL_WaitEvent(&event);
		switch (event.type)
		{
		case CHAL_EVENT_QUIT:
		case SDL_QUIT:
			SDL_Quit();
			goto finish;
			break;
		default:
			break;
		}
	}
finish:
	SDL_CloseAudio();
	
}
