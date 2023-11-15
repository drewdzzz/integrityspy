#pragma once

#include <stdbool.h>
#include <stdint.h>

/**
 * The file contains methods of assoc - a simple associative container.
 * It has linear complexity, so it's better to use mhash or another hash-table
 * for large amount of data.
 * 
 * The keys has const char * type (must be zero-terminated strings) and values
 * are uint32_t.
 */

struct assoc_node;

struct assoc {
	struct assoc_node *head;
};

struct assoc_iterator {
	struct assoc_node *curr;
};

void
assoc_create(struct assoc *assoc);

/**
 * If there is a value with the same key in assoc, it is replaced.
 */
int
assoc_put(struct assoc *assoc, const char *k, uint32_t v);

/**
 * Key must be a zero-terminated string.
 * Returns true if key is found, false otherwise.
 * Occupied resources are freed.
 */
bool
assoc_pop(struct assoc *assoc, const char *k, uint32_t *out);

/**
 * Frees all resources.
 */
void
assoc_destroy(struct assoc *assoc);

/**
 * Starts an iterator over assoc.
 * Returns false if it is empty, true otherwise.
 */
bool
assoc_iterator_start(struct assoc *assoc, struct assoc_iterator *it,
		     const char **k, uint32_t *v);

/**
 * Advance the iterator.
 * Returns false if there are no more elements, true otherwise.
 */
bool
assoc_iterator_next(struct assoc_iterator *it, const char **k, uint32_t *v);
