#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <readline/readline.h>
#include <SDL/SDL.h>
#include <SDL/SDL_thread.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>

#include "decoding.h"

void saveFrame(AVFrame* pFrame, int width, int height, int iFrame)
{
	FILE* pFile;
	char szFilename[32];
	int  y;

	// Open file
	sprintf(szFilename, "frame%d.ppm", iFrame);
	pFile=fopen(szFilename, "wb");
	if (pFile==NULL)
		return;

	// Write header
	fprintf(pFile, "P6\n%d %d\n255\n", width, height);

	// Write pixel data
	for (y=0; y<height; y++)
		fwrite(pFrame->data[0]+y*pFrame->linesize[0], 1, width*3, pFile);

	// Close file
	fclose(pFile);
}

bool test(char const* fileName)
{
	bool flag = false;
	AVFormatContext* formatContext = NULL;
	if (avformat_open_input(&formatContext, fileName, NULL, NULL))
	{
		fprintf(stderr, "Unable to open file");
		goto fail0;
	}
	if (avformat_find_stream_info(formatContext, NULL) < 0)
	{
		fprintf(stderr, "Could not find stream");
		goto fail1;
	}
	// Writes the format information to the console
	av_dump_format(formatContext, 0, fileName, 0);


	// Select audio and video stream
	size_t streamIndexVideo = -1;
	size_t streamIndexAudio = -1;
	for (size_t i = 0; i < formatContext->nb_streams; ++i)
	{
		// Select *first* video and audio stream
		enum AVMediaType codecType = formatContext->streams[i]->codec->codec_type;
		if (streamIndexVideo == (size_t) -1 && codecType == AVMEDIA_TYPE_VIDEO)
			streamIndexVideo = i;
		if (streamIndexAudio == (size_t) -1 && codecType == AVMEDIA_TYPE_AUDIO)
			streamIndexAudio = i;
	}

	if (streamIndexVideo == (size_t) -1 ||
	    streamIndexAudio == (size_t) -1)
	{
		// TODO: Allow playing of pure audio
		fprintf(stderr, "Unable to find video stream");
		goto fail1;
	}

	// Find audio and video decoders
	AVCodecContext* codecContextVideo =
	  formatContext->streams[streamIndexVideo]->codec;
	AVCodec* codecVideo = avcodec_find_decoder(codecContextVideo->codec_id);
	AVCodecContext* codecContextAudio =
	  formatContext->streams[streamIndexAudio]->codec;
	AVCodec* codecAudio = avcodec_find_decoder(codecContextAudio->codec_id);
	if (codecVideo == NULL || codecAudio == NULL)
	{
		fprintf(stderr, "Unsupported codec");
		goto fail2;
	}
	{
		AVCodecContext* codecContextTemp = codecContextVideo;
		codecContextVideo = avcodec_alloc_context3(codecVideo);
		if (avcodec_copy_context(codecContextVideo, codecContextTemp) != 0)
		{
			fprintf(stderr, "Unable to copy video codec context");
			goto fail2;
		}
		codecContextTemp = codecContextAudio;
		codecContextAudio = avcodec_alloc_context3(codecAudio);
		if (avcodec_copy_context(codecContextAudio, codecContextTemp) != 0)
		{
			fprintf(stderr, "Unable to copy audio codec context");
			goto fail3;
		}
	}
	if (avcodec_open2(codecContextVideo, codecVideo, NULL) < 0 ||
	    avcodec_open2(codecContextAudio, codecAudio, NULL) < 0)
	{
		fprintf(stderr, "Could not open codec");
		goto fail3;
	}
	/*
	 * Allocate frames for decoding.
	 * frameRGB is only used to store the video output.
	 */
	int const ALIGN = 32;
	int nBytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24,
	                                      codecContextVideo->width,
	                                      codecContextVideo->height, ALIGN);
	uint8_t* buffer = (uint8_t*) av_malloc(nBytes);

	AVFrame* frame = av_frame_alloc();
	if (frame == NULL)
	{
		fprintf(stderr, "Unable to allocate frame");
		goto fail4;
	}
	AVFrame* frameRGB = av_frame_alloc(); // Decoded image
	if (frame == NULL)
	{
		fprintf(stderr, "Unable to allocate frame");
		goto fail5;
	}
	av_image_fill_arrays(frameRGB->data, frameRGB->linesize, buffer,
	                     AV_PIX_FMT_RGB24, codecContextVideo->width,
	                     codecContextVideo->height, ALIGN);

	struct SwsContext* swsContext =
	  sws_getContext(codecContextVideo->width, codecContextVideo->height,
	                 codecContextVideo->pix_fmt, codecContextVideo->width,
	                 codecContextVideo->height, AV_PIX_FMT_YUV420P, SWS_BILINEAR,
	                 NULL, NULL, NULL);

	// SDL Video handling
	SDL_Surface* screen = SDL_SetVideoMode(codecContextVideo->width,
	                                       codecContextVideo->height, 0, 0);
	if (!screen)
	{
		fprintf(stderr, "[SDL] Could not set video mode");
		goto fail6;
	}
	SDL_Overlay* overlayYUV = SDL_CreateYUVOverlay(codecContextVideo->width,
	                          codecContextVideo->height, SDL_YV12_OVERLAY, screen);

	// SDL Audio handling
	SDL_AudioSpec audioSpec;
	audioSpec.freq = codecContextAudio->sample_rate;
	audioSpec.format = AUDIO_S16SYS;
	audioSpec.channels = codecContextAudio->channels;
	audioSpec.silence = 0;
	audioSpec.samples = 512; // = SDL_AUDIO_MIN_BUFFER_SIZE in ffplay.c
	audioSpec.callback = (void (*)(void*, uint8_t*, int)) audio_callback;
	struct PacketQueue_CodecContext userdata =
		{ .codecContext = codecContextAudio };
	audioSpec.userdata = &userdata;

	state = STATE_NORMAL;
	PacketQueue_init(&userdata.pq);
	AVPacket packet;
	SDL_AudioSpec specTemp;
	if (SDL_OpenAudio(&audioSpec, &specTemp) < 0)
	{
		fprintf(stderr, "[SDL] %s\n", SDL_GetError());
		goto fail6;
	}

	SDL_PauseAudio(0);
	while (av_read_frame(formatContext, &packet) >= 0)
	{
		if (packet.stream_index == (int) streamIndexVideo)
		{
			int frameFinished;
			avcodec_decode_video2(codecContextVideo, frame, &frameFinished, &packet);
			if (frameFinished)
			{
				SDL_LockYUVOverlay(overlayYUV);
				/*
				 * The indices here must be altered
				 */
				frameRGB->data[0] = overlayYUV->pixels[0];
				frameRGB->data[1] = overlayYUV->pixels[2];
				frameRGB->data[2] = overlayYUV->pixels[1];
				frameRGB->linesize[0] = overlayYUV->pitches[0];
				frameRGB->linesize[1] = overlayYUV->pitches[2]; 
				frameRGB->linesize[2] = overlayYUV->pitches[1];
				sws_scale(swsContext, (uint8_t const* const*) frame->data, frame->linesize, 0,
				          codecContextVideo->height, frameRGB->data, frameRGB->linesize);
				SDL_UnlockYUVOverlay(overlayYUV);
				SDL_Rect rect;
				rect.x = rect.y = 0;
				rect.w = codecContextVideo->width;
				rect.h = codecContextVideo->height;
				SDL_DisplayYUVOverlay(overlayYUV, &rect);
			}
			av_packet_unref(&packet);
		}
		else if (packet.stream_index == (int) streamIndexAudio)
		{
			PacketQueue_put(&userdata.pq, &packet);
		}
		else
			av_packet_unref(&packet);

		SDL_Event event;
		SDL_PollEvent(&event);
		switch (event.type)
		{
		case SDL_QUIT:
			state = STATE_QUIT;
			SDL_Quit();
			break;
		default:
			break;
		}
	}

	flag = true;
fail6:
	av_frame_free(&frameRGB);
fail5:
	av_frame_free(&frame);
fail4:
	av_free(buffer);
fail3:
	avcodec_free_context(&codecContextAudio);
fail2:
	avcodec_free_context(&codecContextVideo);
fail1:
	avformat_close_input(&formatContext);
fail0:
	return flag;
}

int main(int argc, char* argv[])
{
	(void) argc;
	(void) argv;

	// Initialisation
	av_register_all();
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER))
	{
		fprintf(stderr, "[SDL] %s\n", SDL_GetError());
		return -1;
	}

	if (argc > 1)
	{
		bool flag = test(argv[1]);
		return flag ? 0 : -1;
	}

	while (true)
	{
		char* line = readline("(chal) ");
		if (!line || line[0] == '\0')
		{
			printf("Error: Empty command\n");
			continue;
		}
		// Use strtok to split the line into tokens
		char const* token = strtok(line, " ");
		if (strcmp(token, "quit") == 0)
		{
			if (strtok(NULL, " "))
				printf("Type 'quit' to quit\n");
			else
				break;
		}
		else if (strcmp(token, "test") == 0)
		{
			token = strtok(NULL, " ");
			if (!token)
				printf("Please supply an argument\n");
			test(token);
		}
		else
		{
			printf("Error: Unknown command\n");
			continue;
		}
		free(line);
	}

	return 0;
}
