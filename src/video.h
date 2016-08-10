#ifndef CHALCOCITE__VIDEO_H_
#define CHALCOCITE__VIDEO_H_

#include <SDL2/SDL.h>

#include "media.h"

/**
 * @brief push a SDL_EVENT of type CHAL_EVENT_REFRESH after a delay
 * @param[in] media equals event.user.data1
 * @param[in] delay delay in miliseconds.
 * @return True if successful. Prints error to stderr if fails
 */
bool schedule_refresh(struct Media* const media, int delay);


#endif // !CHALCOCITE__VIDEO_H_
