#include "packetqueue.h"

inline void PacketQueue_init(PacketQueue* const pq)
{
	memset(pq, 0, sizeof(PacketQueue));
	pq->mutex = SDL_CreateMutex();
	pq->cond = SDL_CreateCond();
}
inline void PacketQueue_destroy(PacketQueue* const pq)
{
	SDL_DestroyMutex(pq->mutex);
	SDL_DestroyCond(pq->cond);
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
int PacketQueue_get(PacketQueue* pq, AVPacket* packet, bool block,
		_Atomic enum State const* const state)
{
	SDL_LockMutex(pq->mutex);
	AVPacketList* pl;
	int result;
	while (true)
	{
		if (*state == STATE_QUIT)
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
