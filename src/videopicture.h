#ifndef CHALCOCITE__VIDEOPICTURE_H_
#define CHALCOCITE__VIDEOPICTURE_H_

#include <stdbool.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_thread.h>

struct VideoPicture
{
	SDL_Texture* texture;
	int width, height; // Source

	bool allocated;
};

SDL_mutex* screenMutex;

#endif // !CHALCOCITE__VIDEOPICTURE_H_
