#ifndef CHALCOCITE__DECODING_H_
#define CHALCOCITE__DECODING_H_

#include <stdbool.h>
#include <SDL/SDL.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

#include "chalcocite.h"
#include "packetqueue.h"

struct PacketQueue_CodecContext
{
	PacketQueue pq;
	AVCodecContext* codecContext;
};
int audio_decode_frame(struct PacketQueue_CodecContext* userdata,
		uint8_t* ab, size_t size);
/**
 * @brief Callback function supplied to SDL_AudioSpec to decode audio
 */
void audio_callback(void* userdata, uint8_t* stream, int len);

// Implementations


#endif // !CHALCOCITE__DECODING_H_
