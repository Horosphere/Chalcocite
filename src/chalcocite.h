#ifndef CHALCOCITE__CHALCOCITE_H_
#define CHALCOCITE__CHALCOCITE_H_

#include <SDL2/SDL.h>

enum State
{
	STATE_NORMAL,
	STATE_QUIT	
};

_Atomic enum State state;

#define CHAL_EVENT_QUIT (SDL_USEREVENT + 1)
#define CHAL_EVENT_REFRESH (SDL_USEREVENT + 2)
#define CHAL_UNSIGNED_INVALID (unsigned) (-1)

#endif // !CHALCOCITE__CHALCOCITE_H_
