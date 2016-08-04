#ifndef CHALCOCITE__AUDIO_H_
#define CHALCOCITE__AUDIO_H_

#include "media.h"

/**
 * Load the given media into SDL
 */
bool audio_load_SDL(struct Media* const media);
void audio_unload_SDL(struct Media* const media);



#endif // !CHALCOCITE__AUDIO_H_
