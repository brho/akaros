/**********************************************************************
 * 
 *  An intial stab at an optomistic concurrency control queue.  This is 
 *  disabled for now, as there are some strange bugs w/ the clone test.
 *
 **********************************************************************/


#include "debug.h"
#include "atomic.h"
#include "occ_list.h"

#ifndef DEBUG_occ_list_c
#undef debug
#define debug(...)
#undef tdebug
#define tdebug(...)
#endif

#define FAKEOUT_WITH_SPINLOCKS 1

//////////////////////////////////////////////////////////////////////
//
//  linked list w/ optomistic concurrency control
//
//////////////////////////////////////////////////////////////////////


int occ_mutex = 0;

// assume no concurrency
occ_list_t* init_occ_list(occ_list_t *list)
{
  assert(list);
  list->head = NULL;
  list->tail = NULL;
  list->num = 0;
  list->writer_id = 0;  // an invalid stack location
  return list;
}

// assume no concurrency
occ_list_t* new_occ_list()
{
  occ_list_t *list = malloc( sizeof(occ_list_t) );
  assert( list );
  return init_occ_list( list );
}

void occ_print_list(occ_list_t *list) __attribute__((__unused__));
void occ_print_list(occ_list_t *list)
{
  occ_list_entry_t *e = list->head;
  int num = 0;

  output("list %p  {head=%p, tail=%p, num=%d}\n", list, list->head, list->tail, list->num);
  while( e ) {
    num++;
    output("   %p -> %p\n", e, e->next);
    e = e->next;
  }
}


#if defined(DEBUG_occ_list_c)
#define CHECK(expr) if( !(expr) ) bad=__STRING(expr)
int sanity_check_mutex=0;
int num_active=0;
int num_active_mutex=0;
int copy_lock = 0;
static inline void occ_list_sanity_check(occ_list_t **listp)
{
  occ_list_t *list=NULL;
  char *bad=0; 
  int num=0; 
  int listnum=0;

  spinlock_lock( &sanity_check_mutex );
  { 
    int done=0;
    while( !done ) {
      spinlock_lock( &num_active_mutex );
      if(num_active == 0) done=1;
      spinlock_unlock( &num_active_mutex );
    }
  }

  while( list != *listp ) {
    list = *listp;
    CHECK( list->num >= 0 ); 
    CHECK( list->num == 0 ? list->head == NULL : 1 ); 
    CHECK( list->num >  0 ? list->head != NULL : 1 ); 
    CHECK( list->num >  0 ? list->tail != NULL : 1 ); 
    { 
      occ_list_entry_t *e = list->head; 
      while( e ) { num++; if(e==list->tail) break; e=e->next; } 
      CHECK( num == list->num );
      listnum = list->num;
    } 
  }

  if( bad ) {
    occ_print_list(list);
    output("LIST SANITY CHECK FAILED: %s\n",bad);
    output("num=%d listnum=%d\n",num,listnum);
    assert(0);
  }

  spinlock_unlock( &sanity_check_mutex );
}

static inline void inc_num_active() 
{
  spinlock_lock( &sanity_check_mutex );
  spinlock_lock( &num_active_mutex );
  num_active++;
  spinlock_unlock( &num_active_mutex );
  spinlock_unlock( &sanity_check_mutex );
}

static inline void dec_num_active() 
{
  spinlock_lock( &num_active_mutex );
  num_active--;
  spinlock_unlock( &num_active_mutex );
}

#else
#define occ_list_sanity_check(list)
#define inc_num_active() 
#define dec_num_active() 
#endif


/**
 * add a data item to the end of the list
 **/
#ifndef FAKEOUT_WITH_SPINLOCKS
void occ_enqueue(occ_list_t **list, occ_list_t **spare, occ_list_entry_t *entry)
{
  occ_list_t *orig, *new=0;
  int id = (int) &orig;  // the stack addr is unique for all concurrent callers!


  //spinlock_lock( &occ_mutex );
  debug("starting: id=%ld\n", id);
  
  // sanity checks
  assert( list );   assert( *list );  assert( spare );  assert( *spare );  assert( entry );

  occ_list_sanity_check(list);
  inc_num_active();
  new = *spare;

  entry->next = NULL;

  // loop
  do {
    // save the original pointer
    orig = *list;

    // note that we are updating 
    orig->writer_id = id;
    SERIALIZATION_BARRIER();  // prevent out-of-order execution 

    // update the tail 
    if( orig->tail ) 
      orig->tail->next = entry;

    // copy the original
    *new = *orig;
    if( orig != *list ) // the copy was not atomic, so try again
      continue;

    // update pointers
    if( new->head == NULL ) {
      new->head = entry; 
      new->tail = entry;
    } else {
      new->tail = entry;
    }
    new->num++;
    
    // try again if the someone else is updating
    if( orig->writer_id != id )
      continue;
    // FIXME: a context switch between the check and the cmpxchg can still cause bugs, since another thread can mess up the tail pointer!!

  } while( cmpxchg(list, orig, new) != orig );

  new->writer_id = 0; // an invalid stack location

  dec_num_active();
  debug("checking: id=%ld\n", id);
  occ_list_sanity_check(list);
  debug("done:     id=%ld\n", id);

  // save orig as the new spare
  *spare = orig;
  //spinlock_unlock( &occ_mutex );
}
#else 
void occ_enqueue(occ_list_t **list, occ_list_t **spare, occ_list_entry_t *entry)
{
  occ_list_t* l = *list;
  (void) spare;

  entry->next = NULL;
  spinlock_lock( &l->writer_id );
  
  // update pointers
  if( l->head == NULL ) {
    l->head = entry; 
    l->tail = entry;
  } else {
    l->tail->next = entry;
    l->tail = entry;
  }
  l->num++;

  occ_list_sanity_check(list);
  spinlock_unlock( &l->writer_id );
}
#endif


/**
 * Dequeue the head
 **/
#ifndef FAKEOUT_WITH_SPINLOCKS
occ_list_entry_t* occ_dequeue(occ_list_t **list, occ_list_t **spare)
{
  occ_list_entry_t *entry;
  occ_list_t *orig, *new=0;

  //spinlock_lock( &occ_mutex );
  debug("starting: id=%ld\n", (long)&orig);

  // sanity checks
  assert( list );   assert( *list );  assert( spare );  assert( *spare );

  occ_list_sanity_check(list);
  inc_num_active();

  new = *spare;

  // loop
  do {
    // save the original pointer
    orig = *list;
    SERIALIZATION_BARRIER();

    // copy the data structure
    *new = *orig;
    if( orig != *list ) // the copy was not atomic, so try again
      continue;

    // save head
    entry = new->head;
    if( entry )
      new->num--;
    
    // update pointers
    if( new->head == new->tail )
      new->head = new->tail = NULL;
    else 
      new->head = new->head->next;

  } while( cmpxchg(list, orig, new) != orig );


  dec_num_active();
  debug("checking: id=%ld\n", (long)&orig);
  occ_list_sanity_check(list);
  debug("done:     id=%ld\n", (long)&orig);

  // save orig as the new spare
  *spare = orig;

  if( entry )
    entry->next = NULL;
  //spinlock_unlock( &occ_mutex );
  return entry;
}
#else // fake out w/ spinlocks
occ_list_entry_t* occ_dequeue(occ_list_t **list, occ_list_t **spare)
{
  occ_list_entry_t *entry;
  occ_list_t* l = *list;
  (void) spare;

  spinlock_lock( &l->writer_id );

  entry = l->head; 

  // update pointers
  if( l->head == l->tail )
    l->head = l->tail = NULL;
  else 
    l->head = l->head->next;
  if( entry )
    l->num--;
  
  occ_list_sanity_check(list);
  spinlock_unlock( &l->writer_id );

  if( entry )
    entry->next = NULL;
  return entry;
}
#endif


//////////////////////////////////////////////////////////////////////
// testing code - move to utiltest.c
//////////////////////////////////////////////////////////////////////
#if 0


static int mutex = 0;

#define lock() \
do { \
  if( 0 ) write(2, "try lock\n", 9); \
  while( tas(&mutex) == 1 ) {\
    struct timespec ts; \
    ts.tv_sec = 0;  ts.tv_nsec = 1000000; \
    nanosleep(&ts, NULL); \
  } \
  if( 0 ) write(2, "got lock\n", 9); \
} while( 0 )

//#define unlock() {  write(2, "unlock\n", 7); mutex = 0;}
#define unlock() { mutex = 0;}

#undef lock
#define lock()
#undef unlock
#define unlock()

//#define msg(args...) fprintf(stderr,args)
//#define msg(s) write(2, s, strlen(s))
//#define msg(s...) output(s)

/*
#define msg(args...) \
do {\
  while( tas(&mutex) == 1 ) \
    sched_yield(); \
  fprintf(stderr,"%d: ",getpid()); \
  fprintf(stderr,args); \
  mutex = 0; \
} while( 0 )
*/   

#define msg(args...) \
do {\
  while( tas(&mutex) == 1 ) \
    sched_yield(); \
  output("%d: ",getpid()); \
  output(args); \
  mutex = 0; \
} while( 0 )


static int malloc_mutex = 0;

static inline void* r_malloc(size_t size)
{
  void *p; 
  while( tas(&malloc_mutex) == 1) 
    ;
    //sched_yield();
  p = malloc(size);
  malloc_mutex = 0;
  return p;
}

static inline void r_free(void *ptr)
{
  while( tas(&malloc_mutex) == 1)
    //sched_yield();
    ;
  free(ptr);
  malloc_mutex = 0;
}





//////////////////////////////////////////////////////////////////////
//
//////////////////////////////////////////////////////////////////////

static occ_list_t *g_list;

static int entrynum = 0;

int cloned_child(void *arg)
{
  int i;
  occ_list_entry_t *entry;
  occ_list_t *spare = NULL;
  int me;
  (void) arg;

  msg("child starting\n");

  /*
  sleep(1);
  
  while( 1 ) {
    //int me = getpid();
    msg("spinning\n");
    sleep(1);
  }
  */

  while( 1 ) {
    
    // add 5
    for(i=0; i<5; i++) {
      entry = (occ_list_entry_t*) r_malloc( sizeof(occ_list_entry_t) );
      entry->data = (void*) entrynum++;
      occ_enqueue(&g_list, &spare, entry);
      //msg("enqueued entry %d\n", (int)entry->data);
      msg("enqueued entry\n");
    }

    // release 5 
    for(i=0; i<5; i++) {
      entry = occ_dequeue(&g_list, &spare);
      if( entry ) {
        //msg("dequeued entry %d\n", (int)entry->data);
        msg("dequeued entry\n");
        r_free(entry);
      }
    }
    sleep(1);
  }

  return 0;
}

#define CLONE_THREAD_OPTS (\
   CLONE_FS | \
   CLONE_FILES | \
   CLONE_SIGHAND | \
   CLONE_PTRACE | \
   CLONE_VM | \
   0)

//   CLONE_THREAD |

#define STACKSIZE 2048



int main(void)
{
  int i;
  int ret;
  char *stack;

  g_list = new_occ_list();

  msg("main starting up\n");
  
  // clone a bunch of new threads
  for(i=0; i<1; i++) {
    lock();
    msg("main creating clone\n");
    unlock();
    stack = r_malloc(STACKSIZE);  assert(stack);
    ret = clone(cloned_child, stack+STACKSIZE-4, CLONE_THREAD_OPTS, NULL);

    if(ret == -1) {
      lock();
      perror("clone"); 
      unlock();
      exit(1);
    }
  }

  cloned_child(NULL);
  
  return 0;
}


#endif 

