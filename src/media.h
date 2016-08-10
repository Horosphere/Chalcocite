#ifndef CHALCOCITE__MEDIA_H_
#define CHALCOCITE__MEDIA_H_

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libswresample/swresample.h>

#include "chalcocite.h"
#include "videopicture.h"
#include "container/packetqueue.h"

#define AUDIO_QUEUE_MAX_SIZE (5 * 16 * 1024)
#define VIDEO_QUEUE_MAX_SIZE (5 * 256 * 1024)
#define PICTQUEUE_SIZE 1

/**
 * @brief Opens a AVFormatContext from the given fileName.
 * @return NULL if failed.
 */
struct AVFormatContext* av_open_file(char const* fileName);
/**
 * @brief Extracts and copies codec context of given stream. Guarenteed to
 *  clean up upon failure.
 * @param[in] fc An opened AVFormatContext that has stream info. That is,
 *  avformat_find_stream_info(fc, NULL) >= 0
 * @param[in] streamIndex Index of the stream in the format context. Must be
 *  less than fc->nb_streams
 * @param[out] cc Output to store the copied AVCodecContext. Must be
 *  dereferencible.
 * @return true if successful.
 */
bool av_stream_context(struct AVFormatContext* const fc, unsigned streamIndex,
                       struct AVCodecContext** const cc);

/**
 * Must be initialised with \ref Media_init and destroyed by \red Media_destroy
 * @brief Media represents a collection of playable audio/video streams.
 */
struct Media
{
	char fileName[1024]; ///< Path to the media file
	_Atomic enum State state; ///< Playback state
	struct AVFormatContext* formatContext;

	unsigned streamIndexA;
	struct AVStream* streamA; ///< streamA is NULL if no audio
	struct AVCodecContext* ccA; ///< Audio codec context
	PacketQueue queueA; ///< Packet queue to store audio packets.

	struct SDL_AudioSpec audioSpec;
	struct SwrContext* swrContext; ///< Converts audio to SDL playable format
	SDL_AudioDeviceID audioDevice;

	unsigned streamIndexV;
	struct AVStream* streamV; // = NULL if no video
	struct AVCodecContext* ccV; ///< Video codec context
	PacketQueue queueV;

	struct SwsContext* swsContext; ///< Converts video to SDL playable format
	int outWidth, outHeight; ///< Dimension of the screen
	/**
	 * This queue is filled by Media_pictQueue_init. The indices pictQueueIndexR,
	 *  pictQueueIndexW are for reading and writing from the queue, respectively.
	 *  pictQueueSize represents the number of elements in use.
	 * Lock pictQueueMutex when operating in the queue.
	 */
	struct VideoPicture pictQueue[PICTQUEUE_SIZE];
	// Number of pictures in use, reading index, writing index
	int pictQueueSize, pictQueueIndexR, pictQueueIndexW;
	SDL_mutex* pictQueueMutex;
	SDL_cond* pictQueueCond;

	/*
	 * All synchronisation variables are in second
	 */
	double timer;
	double clockAudio;
	double clockVideo;
	double lastFrameDelay;
	double lastFrameTimestamp;

	SDL_Window* screen;
	SDL_Renderer* renderer;
	SDL_Thread* threadParse;
	SDL_Thread* threadAudio;
	SDL_Thread* threadVideo;

	// Cache
	struct AVFrame* frameVideo;
	struct AVFrame* frameAudio;

};


void Media_init(struct Media* const);
void Media_destroy(struct Media* const);

/**
 * @warning Uses SDl Render API (Not thread safe). User responsible for locking
 *  mutexes.
 * @brief Allocate components of the picture queue with dimensions outWidth *
 *  outHeight. The format for textures is YV12.
 */
bool Media_pictQueue_init(struct Media* const);
/**
 * @warning Uses SDl Render API (Not thread safe). User responsible for locking
 *  mutexes.
 */
void Media_pictQueue_destroy(struct Media* const);

/**
 * @brief Wait for the writing position in media->pictQueue to be available.
 * @return false if media->state is set to quit
 */
bool Media_pictQueue_wait_write(struct Media* const);

/**
 * @brief Fills ccA/V, streamIndexA/V, streamA/V with appropriate values.
 * @return false if no audio and no video streams are found.
 */
bool Media_open_best_streams(struct Media* const);
/**
 * @brief Closes all codec contextes but not format context.
 */
void Media_close(struct Media* const);

double Media_synchronise_video(struct Media* const, struct AVFrame* const,
                               double pts);
double Media_get_audio_clock(struct Media const* const);
#endif // !CHALCOCITE__MEDIA_H_
