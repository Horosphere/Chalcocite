#include "video.h"

static uint32_t push_refresh_event(uint32_t interval, void* data)
{
	(void) interval;

	SDL_Event event;
	event.type = CHAL_EVENT_REFRESH;
	event.user.data1 = data;
	SDL_PushEvent(&event);
	return 0; // Stops the timer
}
bool schedule_refresh(struct Media* const media, int delay)
{
	bool flag = SDL_AddTimer(delay, push_refresh_event, media);
	if (!flag) {
		fprintf(stderr, "[SDL] %s\n", SDL_GetError());
	}
	return flag;
}
