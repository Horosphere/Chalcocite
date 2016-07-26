#ifndef CHALCOCITE__MEDIA_H_
#define CHALCOCITE__MEDIA_H_

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>

#include "packetqueue.h"
#include "chalcocite.h"
#include "videopicture.h"

#define AUDIO_BUFFER_MAX_SIZE (192000 * 3 / 2)
#define AUDIO_QUEUE_MAX_SIZE (5 * 16 * 1024)
#define VIDEO_QUEUE_MAX_SIZE (5 * 256 * 1024)
#define PICTQUEUE_SIZE 1

struct Media
{
	AVFormatContext* formatContext;

	unsigned streamIndexA;
	AVStream* streamA;
	AVCodecContext* ccA; ///< Audio codec context
	PacketQueue queueA;
	uint8_t audioBuffer[AUDIO_BUFFER_MAX_SIZE];
	size_t audioBufferSize;
	size_t audioBufferIndex;
	AVPacket audioPacket;
	uint8_t* audioPacketData;
	size_t audioPacketSize;
	AVFrame audioFrame;

	unsigned streamIndexV;
	AVStream* streamV;
	AVCodecContext* ccV; ///< Video codec context
	PacketQueue queueV;

	struct SwsContext* swsContext;

	// Emits signal when loading is complete
	SDL_cond* stageCond;
	SDL_mutex* stageMutex;

	struct VideoPicture pictQueue[PICTQUEUE_SIZE];
	int pictQueueSize, pictQueueIndexR, pictQueueIndexW;
	SDL_mutex* pictQueueMutex;
	SDL_cond* pictQueueCond;
	
	SDL_Thread* threadParse;
	SDL_Thread* threadVideo;

	SDL_Window* screen;
	SDL_mutex* screenMutex;
	SDL_Renderer* renderer;

	char fileName[1024];
	_Atomic enum State state;
};


void Media_init(struct Media* const);
void Media_destroy(struct Media* const);

/**
 * Media must be pre-allocated and formatContext must be filled.
 * @return The stream type if successful. Otherwise AVMEDIA_TYPE_NB
 */
enum AVMediaType Media_open_stream(struct Media* const, unsigned streamIndex);
bool Media_queue_picture(struct Media* const);
bool load_SDL(struct Media* const);

#endif // !CHALCOCITE__MEDIA_H_
