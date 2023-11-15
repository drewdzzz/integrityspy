#pragma once
/**
 * Jstream - a very simple JSON stream over fstream. The whole stream is a big
 * JSON array object, and every element is written as a separate element if this
 * array.
 * Isn't thread-safe.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

struct jstream {
	FILE *f;
	uint32_t lvl;
	bool is_first;
};

/**
 * Opens the stream and starts an array.
 * Returns 0 on success, -1 otherwise.
 */
int
jstream_open(struct jstream *jstream, const char *file_name);

/**
 * Writes a string->string map as the next element of the jstream's array.
 * Argument num is the size of map, arguments keys and values must contain at
 * least num zero terminated strings.
 */
int
jstream_write_map(struct jstream *jstream, const char **keys,
		  const char **values, size_t num);

/**
 * End an array and closes the stream.
 * Returns 0 on success, -1 otherwise.
 */
int
jstream_close(struct jstream *jstream);
