/* Copyright (c) 2014 The Regents of the University of California
 * Kevin Klues <klueska@cs.berkeley.edu>
 * Andrew Waterman <waterman@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * A wait-free unordered list data structure.
 * 
 */

#ifndef _PARLIB_WAITFREELIST_H
#define _PARLIB_WAITFREELIST_H

#include <string.h>

struct wfl_entry {
  struct wfl_entry *next;
  void *data;
};

struct wfl {
  struct wfl_entry *head;
  struct wfl_entry first;
};

#define WFL_INITIALIZER(list) {&(list).first, {0, 0}}

#ifdef __cplusplus
extern "C" {
#endif

void wfl_init(struct wfl *list);
void wfl_destroy(struct wfl *list);
size_t wfl_capacity(struct wfl *list);
size_t wfl_size(struct wfl *list);
void wfl_insert(struct wfl *list, void *data);
void *wfl_remove(struct wfl *list);
size_t wfl_remove_all(struct wfl *list, void *data);

/* Iterate over list.  Safe w.r.t. inserts, but not w.r.t. removals. */
#define wfl_foreach_unsafe(elm, list) \
  for (struct wfl_entry *_p = (list)->head; \
       elm = _p == NULL ? NULL : _p->data, _p != NULL; \
       _p = _p->next) \
    if (elm)

#ifdef __cplusplus
}
#endif

#endif
