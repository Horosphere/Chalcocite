#ifndef CHALCOCITE__PACKETQUEUE_H_
#define CHALCOCITE__PACKETQUEUE_H_

#include <stdbool.h>
#include <SDL2/SDL.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

#include "../chalcocite.h"

/**
 * Linked list implementation of a queue.
 */
typedef struct
{
	AVPacketList* first; // First element of the list
	AVPacketList* last; // Last element of the list
	int nPackets;
	size_t size; // Total size in bytes of the packets
	SDL_mutex* mutex;
	SDL_cond* cond;
} PacketQueue;

/**
 * @brief Initialise the given PacketQueue.
 */
void PacketQueue_init(PacketQueue* const);
void PacketQueue_destroy(PacketQueue* const);

/**
 * @brief Enqueue a AVPacket into a PacketQueue. Thread safe.
 * @return true if successful.
 */
bool PacketQueue_put(PacketQueue* pq, AVPacket* packet);

/**
 * @brief Dequeue an element from the end of the PacketQueue. Thread safe.
 * @param pq A packet queue.
 * @param[out] packet Output
 * @param[in] block If set to true, waits until the PacketQueue receives a
 *	packet.
 * @param[in] Atomic pointer to a State.
 * @return -1 if state is set to quit. 0 if block is not enabled and no packet
 *	is retrieved. 1 if successful.
 */
int PacketQueue_get(PacketQueue* pq, AVPacket* packet, bool block,
		_Atomic enum State const* const state);

static inline size_t PacketQueue_size(PacketQueue* const pq)
{
	return pq->size;
}

#endif // !CHALCOCITE__PACKETQUEUE_H_
