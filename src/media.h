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

	unsigned streamIndexA; // = CHAL_UNSIGNED_INVALID if no audio
	AVStream* streamA; // = NULL if no audio
	AVCodecContext* ccA; ///< Audio codec context
	PacketQueue queueA;
	uint8_t audioBuffer[AUDIO_BUFFER_MAX_SIZE];
	size_t audioBufferSize;
	size_t audioBufferIndex;
	AVPacket audioPacket;
	uint8_t* audioPacketData;
	size_t audioPacketSize;
	AVFrame audioFrame;

	unsigned streamIndexV; // = CHAL_UNSIGNED_INVALID if no video
	AVStream* streamV; // = NULL if no video
	AVCodecContext* ccV; ///< Video codec context
	PacketQueue queueV;

	struct SwsContext* swsContext;
	int outWidth, outHeight;
	struct VideoPicture pictQueue[PICTQUEUE_SIZE];
	// Number of pictures in use, reading index, writing index
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
 * @warning Uses SDl Render API (Not thread safe). User responsible for locking
 *	mutexes.
 * @brief Allocate components of the picture queue with dimensions outWidth *
 *	outHeight. The format for textures is YV12.
 */
bool Media_pictQueue_init(struct Media* const);
/**
 * @warning Uses SDl Render API (Not thread safe). User responsible for locking
 *	mutexes.
 */
void Media_pictQueue_destroy(struct Media* const);

bool Media_pictQueue_wait_write(struct Media* const);
/**
 * @brief Extracts and copies codec context of given stream. Guarenteed to
 *	clean up upon failure.
 * @param[in] fc An opened AVFormatContext that has stream info. That is,
 *	avformat_find_stream_info(fc, NULL) >= 0
 * @param[in] streamIndex Index of the stream in the format context. Must be
 *	less than fc->nb_streams
 * @param[out] cc Output to store the copied AVCodecContext. Must be
 *	dereferencible.
 * @return true if successful.
 */
bool av_stream_context(AVFormatContext* const fc, unsigned streamIndex,
		AVCodecContext** const cc);

#endif // !CHALCOCITE__MEDIA_H_
