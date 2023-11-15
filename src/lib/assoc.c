#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "assoc.h"

struct assoc_node {
	struct assoc_node *next;
	char *key;
	uint32_t value;
};

void
assoc_create(struct assoc *assoc)
{
	assoc->head = NULL;
}

int
assoc_put(struct assoc *assoc, const char *k, uint32_t v)
{
	struct assoc_node *new_head = malloc(sizeof(*new_head));
	if (new_head == NULL)
		return -1;
	new_head->next = assoc->head;
	new_head->value = v;
	new_head->key = strdup(k);
	if (new_head->key == NULL) {
		free(new_head);
		return -1;
	}
	assoc->head = new_head;
	return 0;
}

bool
assoc_pop(struct assoc *assoc, const char *k, uint32_t *out)
{
	struct assoc_node *prev = assoc->head;
	struct assoc_node *curr = assoc->head;
	for (; curr != NULL; curr = curr->next) {
		if (strcmp(k, curr->key) == 0) {
			*out = curr->value;
			if (curr == assoc->head)
				assoc->head = curr->next;
			else
				prev->next = curr->next;
			free(curr->key);
			free(curr);
			return true;
		}
		prev = curr;
	}
	return false;
}

void
assoc_destroy(struct assoc *assoc)
{
	struct assoc_node *curr = assoc->head;
	while (curr != NULL) {
		struct assoc_node *next = curr->next;
		free(curr->key);
		free(curr);
		curr = next;
	}
}

bool
assoc_iterator_start(struct assoc *assoc, struct assoc_iterator *it,
		     const char **k, uint32_t *v)
{
	if (assoc->head == NULL)
		return false;
	it->curr = assoc->head;
	*k = it->curr->key;
	*v = it->curr->value;
	return true;
}

bool
assoc_iterator_next(struct assoc_iterator *it, const char **k, uint32_t *v)
{
	it->curr = it->curr->next;
	if (it->curr == NULL)
		return false;
	*k = it->curr->key;
	*v = it->curr->value;
	return true;
}
