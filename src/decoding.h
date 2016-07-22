#ifndef CHALCOCITE__DECODING_H_
#define CHALCOCITE__DECODING_H_

#include <stdbool.h>
#include <SDL/SDL.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

#include "chalcocite.h"

typedef struct
{
	AVPacketList* first;
	AVPacketList* last;
	int nPackets;
	int size;
	SDL_mutex* mutex;
	SDL_cond* cond;
} PacketQueue;
struct PacketQueue_CodecContext
{
	PacketQueue pq;
	AVCodecContext* codecContext;
};
void PacketQueue_init(PacketQueue* pq);
bool PacketQueue_put(PacketQueue* pq, AVPacket* packet);
static int PacketQueue_get(PacketQueue* pq, AVPacket* packet, int block);
int audio_decode_frame(struct PacketQueue_CodecContext* codecContext,
		uint8_t* ab, size_t size);
void audio_callback(void* userdata, uint8_t* stream, int len);

// Implementations


#endif // !CHALCOCITE__DECODING_H_
