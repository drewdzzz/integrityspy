#pragma once

#include <stdarg.h>
#include <stdio.h>
#include <syslog.h>

/**
 * Calculates length of a static array.
 */
#define lengthof(arr) sizeof(arr) / sizeof((arr)[0])

/**
 * Writes an error to stderr and syslog.
 * Mimics printf interface.
 */
#define say_error(...) 							\
	do {								\
		fprintf(stderr, "%s:%d - ", __FILE__, __LINE__);	\
		fprintf(stderr, __VA_ARGS__);				\
		fprintf(stderr, "\n");					\
		syslog(LOG_ERR, __VA_ARGS__);				\
	} while (0);
	