#ifndef CHALCOCITE__VIDEOPICTURE_H_
#define CHALCOCITE__VIDEOPICTURE_H_

#include <stdbool.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_thread.h>

struct VideoPicture
{
	/*
	 * The SDL Render API can only be called from one thread. That includes any
	 * allocation/destruction of textures.
	 */
	SDL_Texture* texture;
	int width, height; // Source
	// Must be allocated with texture
	uint8_t* planeY;
	uint8_t* planeU;
	uint8_t* planeV;
	double timestamp;
};

SDL_mutex* screenMutex;

#endif // !CHALCOCITE__VIDEOPICTURE_H_
