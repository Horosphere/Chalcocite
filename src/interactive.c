#include "interactive.h"

#include <stdbool.h>

#include <SDL2/SDL.h>
#include <readline/readline.h>
#include <libavformat/avformat.h>

#include "playback.h"
#include "test.h"
#include "media.h"
#include "container/vectorptr.h"

#define COMMAND(str) else if (strcmp(token, str) == 0)
#define COMMAND2(str0, str1) else if (strcmp(token, str0) == 0 ||\
                                      strcmp(token, str1) == 0)

int interactive_exec()
{
	// Interactive console
	int nAudioDevices = SDL_GetNumAudioDevices(0);

	if (!nAudioDevices)
	{
		fprintf(stdout, "Warning: No audio device found\n");
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
		COMMAND("test")
		{
			test();
		}
		COMMAND2("refresh", "re")
		{
			nAudioDevices = SDL_GetNumAudioDevices(0);
		}
		COMMAND2("playfile", "pf")
		{
			token = strtok(NULL, " ");
			if (!token)
			{
				printf("Please supply an argument\n");
				continue;
			}
			play_file(token);
		}
		COMMAND2("info", "i")
		{
			token = strtok(NULL, " ");
			if (token)
			{
				if (strcmp(token, "devices") == 0)
				{
					nAudioDevices = SDL_GetNumAudioDevices(0);
					printf("Total number of devices: %d\n", nAudioDevices);
					for (int i = 0; i < nAudioDevices; ++i)
					{
						printf("%d: %s\n", i, SDL_GetAudioDeviceName(i, 0));
					}
				}
			}
			else
			{
				printf("Usage:\n"
				       "info/i devices: Print available audio devices\n");
			}
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
