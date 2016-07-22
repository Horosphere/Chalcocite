#include "decoding.h"

#include <assert.h>

inline void PacketQueue_init(PacketQueue* pq)
{
	memset(pq, 0, sizeof(PacketQueue));
	pq->mutex = SDL_CreateMutex();
	pq->cond = SDL_CreateCond();
}
bool PacketQueue_put(PacketQueue* pq, AVPacket* packet)
{
	AVPacketList* pl;
	pl = av_malloc(sizeof(AVPacketList));
	if (!pl)
		return false;
	if (av_packet_ref(&pl->pkt, packet) < 0)
		return false;
	pl->pkt = *packet;
	pl->next = NULL;

	SDL_LockMutex(pq->mutex);
	if (!pq->last)
		pq->first = pl;
	else
		pq->last->next = pl;
	pq->last = pl;
	++pq->nPackets;
	pq->size += pl->pkt.size;

	SDL_CondSignal(pq->cond);

	SDL_UnlockMutex(pq->mutex);
	return true;
}
int PacketQueue_get(PacketQueue* pq, AVPacket* packet, int block)
{
	SDL_LockMutex(pq->mutex);
	AVPacketList* pl;
	int result;
	while (true)
	{
		if (state == STATE_QUIT)
		{
			result = -1;
			break;
		}
		pl = pq->first;
		if (pl)
		{
			pq->first = pl->next;
			if (!pq->first) pq->last = NULL;
			--pq->nPackets;
			pq->size -= pl->pkt.size;
			*packet = pl->pkt;
			av_free(pl);
			result = 1;
			break;
		}
		else if (!block)
		{
			result = 0;
			break;
		}
		else SDL_CondWait(pq->cond, pq->mutex);
	}
	SDL_UnlockMutex(pq->mutex);
	return result;
}

int audio_decode_frame(struct PacketQueue_CodecContext* userdata, uint8_t* buffer,
                       size_t bufferSize)
{
	(void) bufferSize;

	static AVPacket packet;
	static uint8_t* audioPacketData = NULL;
	static int audioPacketSize = 0;
	static AVFrame frame;

	AVCodecContext* cc = userdata->codecContext;
	while (true)
	{
		while (audioPacketData > 0)
		{
			int gotFrame = 0;
			int len = avcodec_decode_audio4(cc, &frame, &gotFrame, &packet);
			if (len < 0)
			{
				audioPacketSize = 0;
				break;
			}
			audioPacketData += len;
			audioPacketSize -= len;
			int dataSize = 0;
			if (gotFrame)
			{
				dataSize = av_samples_get_buffer_size(NULL, cc->channels, 
						frame.nb_samples,
						cc->sample_fmt, 1);
				assert(dataSize <= bufferSize);
				memcpy(buffer, frame.data[0], dataSize);
			}
			if (dataSize <= 0) continue; // No data yet
			return dataSize;
		}
		if (packet.data) av_packet_unref(&packet);
		if (state == STATE_QUIT) return -1;
		if (PacketQueue_get(&userdata->pq, &packet, 1) < 0) return -1;
		audioPacketData = packet.data;
		audioPacketSize = packet.size;
	}
	return 0;
}
void audio_callback(void* userdata, uint8_t* stream, int len)
{
	static uint8_t audioBuffer[192000 * 3 / 2]; // max frame size * 1.5
	static uint32_t audioBufferSize = 0;
	static uint32_t audioBufferIndex = 0;

	while (len > 0)
	{
		if (audioBufferIndex >= audioBufferSize)
		{
			// All data sent
			int audioSize = audio_decode_frame(userdata, audioBuffer,
			                                   sizeof(audioBuffer));
			if (audioSize < 0) // Error
			{
				// Silence
				audioBufferSize = 1024;
				memset(audioBuffer, 0, audioBufferSize);
			}
			else
				audioBufferSize = audioSize;
			audioBufferIndex = 0;
		}
		int l = audioBufferSize - audioBufferIndex;
		if (l > len) l = len;
		memcpy(stream, (uint8_t*)audioBuffer + audioBufferIndex, l);
		len -= l;
		stream += l;
		audioBufferIndex += l;
	}
}
