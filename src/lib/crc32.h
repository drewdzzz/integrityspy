/*-
 *  COPYRIGHT (C) 1986 Gary S. Brown.  You may use this program, or
 *  code or tables extracted from it, as desired without restriction.
 */
#pragma once

#include <stdint.h>

uint32_t
calculate_crc32c(uint32_t crc32c, const unsigned char *buffer,
		 unsigned int length);
