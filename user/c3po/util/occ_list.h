
#ifndef _OCC_LIST_H
#define _OCC_LIST_H

typedef struct occ_list_entry_st {
  struct occ_list_entry_st *next;
  void *data;
} occ_list_entry_t;

typedef struct occ_list_st {
  occ_list_entry_t *head;
  occ_list_entry_t *tail;
  int writer_id;
  int num;
} occ_list_t;


occ_list_t* init_occ_list(occ_list_t *list);
occ_list_t* new_occ_list();
void occ_enqueue(occ_list_t **list, occ_list_t **spare, occ_list_entry_t *entry);
occ_list_entry_t* occ_dequeue(occ_list_t **list, occ_list_t **spare);


#endif // _OCC_LIST_H
