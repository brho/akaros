/* Copyright (c) 2014 The Regents of the University of California
 * Kevin Klues <klueska@cs.berkeley.edu>
 * Andrew Waterman <waterman@cs.berkeley.edu>
 * See LICENSE for details.
 */

#include <parlib/assert.h>
#include <stdlib.h>
#include <parlib/arch/atomic.h>
#include <parlib/waitfreelist.h>

void wfl_init(struct wfl *list)
{
	list->first.next = NULL;
	list->first.data = NULL;
	list->head = &list->first;
}

void wfl_destroy(struct wfl *list)
{
	assert(list->first.data == NULL);
	struct wfl_entry *p = list->first.next; // don't free the first element

	while (p != NULL) {
		assert(p->data == NULL);
		struct wfl_entry *tmp = p;
		p = p->next;
		free(tmp);
	}
}

size_t wfl_capacity(struct wfl *list)
{
	size_t res = 0;

	for (struct wfl_entry *p = list->head; p != NULL; p = p->next)
		res++;
	return res;
}

size_t wfl_size(struct wfl *list)
{
	size_t res = 0;

	for (struct wfl_entry *p = list->head; p != NULL; p = p->next)
		res += p->data != NULL;
	return res;
}

#if 0
void wfl_insert(struct wfl *list, void *data)
{
retry:
  struct wfl_entry *p = list->head; // list head is never null
  struct wfl_entry *new_entry = NULL;
  while (1) {
    if (p->data == NULL) {
      if (__sync_bool_compare_and_swap(&p->data, NULL, data)) {
        free(new_entry);
        return;
      }
    }

    if (p->next != NULL) {
      p = p->next;
      continue;
    }

    if (new_entry == NULL) {
      new_entry = malloc(sizeof(struct wfl_entry));
      if (new_entry == NULL)
        abort();
      new_entry->data = data;
      new_entry->next = NULL;
      wmb();
    }
 
    if (__sync_bool_compare_and_swap(&p->next, NULL, new_entry))
      return;
    goto retry;
    p = list->head;
  }
}
#endif

void wfl_insert(struct wfl *list, void *data)
{
	struct wfl_entry *p = list->head; // list head is never null
	
	while (1) {
		if (p->data == NULL) {
			if (__sync_bool_compare_and_swap(&p->data, NULL, data))
				return;
		}
		
		if (p->next == NULL)
			break;
		
		p = p->next;
	}
	
	struct wfl_entry *new_entry = malloc(sizeof(struct wfl_entry));

	if (new_entry == NULL)
		abort();
	new_entry->data = data;
	new_entry->next = NULL;
	
	wmb();
	
	struct wfl_entry *next;

	while ((next = __sync_val_compare_and_swap(&p->next, NULL, new_entry)))
		p = next;
}

void *wfl_remove(struct wfl *list)
{
	for (struct wfl_entry *p = list->head; p != NULL; p = p->next) {
		if (p->data != NULL) {
			void *data = atomic_swap_ptr(&p->data, 0);

			if (data != NULL)
				return data;
		}
	}
	return NULL;
}

size_t wfl_remove_all(struct wfl *list, void *data)
{
	size_t n = 0;

	for (struct wfl_entry *p = list->head; p != NULL; p = p->next) {
		if (p->data == data)
			n += __sync_bool_compare_and_swap(&p->data, data, NULL);
	}
	return n;
}
