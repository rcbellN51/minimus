#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "state.h"

#define STATE_DIRECTORY "minimus"
#define STATE_FILENAME "minimus.conf"
#define PATH_SIZE 1024
#define LINE_SIZE 2048

struct state_config {
	unsigned int volume;
	char default_playlist[PATH_SIZE];
};

static int
state_path(char *path, size_t size)
{
	const char *config_home;
	const char *home;

	config_home = getenv("XDG_CONFIG_HOME");

	if (config_home != NULL && *config_home != '\0') {
		if (snprintf(path, size, "%s/%s/%s", config_home,
		    STATE_DIRECTORY, STATE_FILENAME) >= (int)size)
			return -1;

		return 0;
	}

	home = getenv("HOME");
	if (home == NULL || *home == '\0')
		return -1;

	if (snprintf(path, size, "%s/.config/%s/%s", home,
	    STATE_DIRECTORY, STATE_FILENAME) >= (int)size)
		return -1;

	return 0;
}

static int
state_directory(char *path, size_t size)
{
	const char *config_home;
	const char *home;

	config_home = getenv("XDG_CONFIG_HOME");

	if (config_home != NULL && *config_home != '\0') {
		if (snprintf(path, size, "%s/%s", config_home,
		    STATE_DIRECTORY) >= (int)size)
			return -1;

		return 0;
	}

	home = getenv("HOME");
	if (home == NULL || *home == '\0')
		return -1;

	if (snprintf(path, size, "%s/.config/%s", home,
	    STATE_DIRECTORY) >= (int)size)
		return -1;

	return 0;
}

static int
state_read(struct state_config *config)
{
	FILE *fp;
	char path[PATH_SIZE];
	char line[LINE_SIZE];
	char *end;
	unsigned long number;
	size_t length;

	config->volume = STATE_DEFAULT_VOLUME;
	config->default_playlist[0] = '\0';

	if (state_path(path, sizeof(path)) == -1)
		return -1;

	fp = fopen(path, "r");
	if (fp == NULL) {
		if (errno == ENOENT)
			return 0;

		return -1;
	}

	while (fgets(line, sizeof(line), fp) != NULL) {
		if (strncmp(line, "volume=", 7) == 0) {
			number = strtoul(line + 7, &end, 10);
			if (end == line + 7 || number > 100)
				continue;

			while (*end == ' ' || *end == '\t')
				end++;

			if (*end == '\r')
				end++;

			if (*end != '\0' && *end != '\n')
				continue;

			config->volume = (unsigned int)number;
		} else if (strncmp(line, "default_playlist=", 17) == 0) {
			end = line + 17;
			length = strcspn(end, "\r\n");

			if (length == 0 ||
			    length >= sizeof(config->default_playlist))
				continue;

			memcpy(config->default_playlist, end, length);
			config->default_playlist[length] = '\0';
		}
	}

	if (ferror(fp)) {
		fclose(fp);
		return -1;
	}

	if (fclose(fp) == EOF)
		return -1;

	return 0;
}

static int
state_write(const struct state_config *config)
{
	FILE *fp;
	char directory[PATH_SIZE];
	char path[PATH_SIZE];

	if (config->volume > 100)
		return -1;

	if (state_directory(directory, sizeof(directory)) == -1)
		return -1;

	if (mkdir(directory, 0700) == -1 && errno != EEXIST)
		return -1;

	if (state_path(path, sizeof(path)) == -1)
		return -1;

	fp = fopen(path, "w");
	if (fp == NULL)
		return -1;

	if (fprintf(fp, "volume=%u\n", config->volume) < 0) {
		fclose(fp);
		return -1;
	}

	if (config->default_playlist[0] != '\0') {
		if (fprintf(fp, "default_playlist=%s\n",
		    config->default_playlist) < 0) {
			fclose(fp);
			return -1;
		}
	}

	if (fclose(fp) == EOF)
		return -1;

	return 0;
}

int
state_load_volume(unsigned int *volume)
{
	struct state_config config;

	if (volume == NULL)
		return -1;

	if (state_read(&config) == -1)
		return -1;

	*volume = config.volume;
	return 0;
}

int
state_save_volume(unsigned int volume)
{
	struct state_config config;

	if (volume > 100)
		return -1;

	if (state_read(&config) == -1)
		return -1;

	config.volume = volume;

	return state_write(&config);
}

int
state_load_playlist(char *path, size_t size)
{
	struct state_config config;
	size_t length;

	if (path == NULL || size == 0)
		return -1;

	path[0] = '\0';

	if (state_read(&config) == -1)
		return -1;

	length = strlen(config.default_playlist);
	if (length >= size)
		return -1;

	memcpy(path, config.default_playlist, length + 1);

	return 0;
}

int
state_save_playlist(const char *path)
{
	struct state_config config;
	size_t length;

	if (path == NULL)
		return -1;

	length = strlen(path);
	if (length >= PATH_SIZE)
		return -1;

	if (state_read(&config) == -1)
		return -1;

	memcpy(config.default_playlist, path, length + 1);

	return state_write(&config);
}
