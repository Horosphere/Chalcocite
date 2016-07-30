#ifndef CHALCOCITE__AUDIO_H_
#define CHALCOCITE__AUDIO_H_

#include "media.h"

int audio_decode_frame(struct Media* media, uint8_t* buffer,
                              size_t bufferSize);
void audio_callback(struct Media* media, uint8_t* stream, int len);
bool audio_load_SDL(struct Media* const media);

#endif // !CHALCOCITE__AUDIO_H_
