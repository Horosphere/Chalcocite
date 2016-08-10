/*
 * Main entry to Chalcocite
 */
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_thread.h>
#include <SDL2/SDL_video.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/time.h>
#include <libswscale/swscale.h>

#include "media.h"
#include "video.h"
#include "audio.h"
#include "test.h"
#include "interactive.h"
#include "playback.h"

int main(int argc, char* argv[])
{
	char const usage[] =
	  "Usage:\n"
	  "Execute with no argument to enter the interactive console\n"
	  "--test/-t: Execute a test routine to check functions\n"
	  "--file/-f: Play a media file. The file name must be supplied after the"
	  " argument.\n";

	// Initialisation
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER))
	{
		fprintf(stderr, "[SDL] %s\n", SDL_GetError());
		return -1;
	}
	av_register_all();

	// Parsing
	if (argc > 1)
	{
		if (strcmp(argv[1], "--help") == 0)
		{
			fprintf(stdout, usage);
		}
		else if (strcmp(argv[1], "--test") == 0 ||
		         strcmp(argv[1], "-t") == 0)
		{
			test();
		}
		else if (strcmp(argv[1], "--file") == 0 ||
		         strcmp(argv[1], "-f") == 0)
		{
			if (argc > 2)
				play_file(argv[2]);
			else
				fprintf(stderr, "Argument error: Please supply one or more file names\n");
		}
		else
		{
			fprintf(stderr, "Argument error: Unknown argument\n");
			fprintf(stdout, usage);
		}
		return 0;
	}

	int result = interactive_exec();
	SDL_Quit();
	return result;
}
