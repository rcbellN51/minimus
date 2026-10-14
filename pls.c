#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pls.h"

#define LINE_SIZE 2048

struct pls_entry {
	unsigned int index;
	char *title;
	char *url;
};

static char *
copy_string(const char *s)
{
	char *p;

	p = malloc(strlen(s) + 1);
	if (p != NULL)
		strcpy(p, s);

	return p;
}

static void
trim_newline(char *s)
{
	size_t len;

	len = strlen(s);
	while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r'))
		s[--len] = '\0';
}

static struct pls_entry *
find_entry(struct pls_entry **entries, size_t *count, unsigned int index)
{
	struct pls_entry *new_entries;
	size_t i;

	for (i = 0; i < *count; i++) {
		if ((*entries)[i].index == index)
			return &(*entries)[i];
	}

	new_entries = realloc(*entries, (*count + 1) * sizeof(**entries));
	if (new_entries == NULL)
		return NULL;

	*entries = new_entries;

	(*entries)[*count].index = index;
	(*entries)[*count].title = NULL;
	(*entries)[*count].url = NULL;

	(*count)++;

	return &(*entries)[*count - 1];
}

static int
entry_compare(const void *a, const void *b)
{
	const struct pls_entry *ea = a;
	const struct pls_entry *eb = b;

	if (ea->index < eb->index)
		return -1;
	if (ea->index > eb->index)
		return 1;

	return 0;
}

static void
free_entries(struct pls_entry *entries, size_t count)
{
	size_t i;

	for (i = 0; i < count; i++) {
		free(entries[i].title);
		free(entries[i].url);
	}

	free(entries);
}

int
pls_load(const char *path, struct pls_playlist *playlist)
{
	struct pls_entry *entries = NULL;
	struct pls_entry *entry;
	size_t entry_count = 0;
	size_t station_count = 0;
	FILE *fp;
	char line[LINE_SIZE];
	char *value;
	char *copy;
	unsigned int index;
	size_t i;
	size_t j;

	if (path == NULL || playlist == NULL)
		return -1;

	playlist->stations = NULL;
	playlist->count = 0;

	fp = fopen(path, "r");
	if (fp == NULL)
		return -1;

	while (fgets(line, sizeof(line), fp) != NULL) {
		trim_newline(line);

		if (sscanf(line, "File%u=", &index) == 1) {
			if (index == 0)
				continue;

			value = strchr(line, '=');
			if (value == NULL)
				continue;

			entry = find_entry(&entries, &entry_count, index);
			if (entry == NULL)
				goto error;

			copy = copy_string(value + 1);
			if (copy == NULL)
				goto error;

			free(entry->url);
			entry->url = copy;
		} else if (sscanf(line, "Title%u=", &index) == 1) {
			if (index == 0)
				continue;

			value = strchr(line, '=');
			if (value == NULL)
				continue;

			entry = find_entry(&entries, &entry_count, index);
			if (entry == NULL)
				goto error;

			copy = copy_string(value + 1);
			if (copy == NULL)
				goto error;

			free(entry->title);
			entry->title = copy;
		}
	}

	if (ferror(fp))
		goto error;

	fclose(fp);
	fp = NULL;

	qsort(entries, entry_count, sizeof(*entries), entry_compare);

	for (i = 0; i < entry_count; i++) {
		if (entries[i].url != NULL)
			station_count++;
	}

	if (station_count != 0) {
		playlist->stations =
		    calloc(station_count, sizeof(*playlist->stations));
		if (playlist->stations == NULL)
			goto error;
	}

	for (i = 0, j = 0; i < entry_count; i++) {
		if (entries[i].url == NULL)
			continue;

		playlist->stations[j].title = entries[i].title;
		playlist->stations[j].url = entries[i].url;

		entries[i].title = NULL;
		entries[i].url = NULL;
		j++;
	}

	playlist->count = station_count;
	free_entries(entries, entry_count);

	return 0;

error:
	if (fp != NULL)
		fclose(fp);

	free_entries(entries, entry_count);
	pls_free(playlist);

	return -1;
}

void
pls_free(struct pls_playlist *playlist)
{
	size_t i;

	if (playlist == NULL)
		return;

	for (i = 0; i < playlist->count; i++) {
		free(playlist->stations[i].title);
		free(playlist->stations[i].url);
	}

	free(playlist->stations);
	playlist->stations = NULL;
	playlist->count = 0;
}
