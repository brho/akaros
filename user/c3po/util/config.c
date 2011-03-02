/**
 * 
 * Configure capriccio.  This is done via environment variables which are read at startup.
 *
 **/
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "threadlib.h"
#include "debug.h"
#include "config.h"

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif


// define variables.  These should match config.h
int conf_no_init_messages = FALSE;
int conf_dump_blocking_graph = FALSE;
int conf_dump_timing_info = FALSE;
int conf_show_thread_stacks = FALSE;
int conf_show_thread_details = FALSE;
int conf_no_debug = FALSE;
int conf_no_stacktrace = FALSE;
int conf_no_statcollect = FALSE;

long conf_new_stack_size = 128*1024;
int conf_new_stack_kb_log2 = 7;

static inline int bool_value(char *str)
{
  if(str == NULL) return 0;
  if( atoi(str) ) return 1;
  if( !strcasecmp(str,"true") ) return 1;
  if( !strcasecmp(str,"yes") ) return 1;
  if( !strcasecmp(str,"y") ) return 1;
  return 0;
}

#define get_bool(env,var) {\
  var = bool_value( getenv(__STRING(env)) ); \
  if( !conf_no_init_messages ) \
    output("%s=%s\n",__STRING(env), (var ? "yes" : "no")); \
}  


// for now, just read from env vars.  Add config file option later.
void read_config(void) {
  static int read_config_done = 0;
  
  if( read_config_done ) return;
  read_config_done = 1;

  // BOOLEAN FLAGS
  get_bool(CAPRICCIO_NO_INIT_MESSAGES, conf_no_init_messages);
  get_bool(CAPRICCIO_NO_DEBUG, conf_no_debug);
  get_bool(CAPRICCIO_NO_STACKTRACE, conf_no_stacktrace);
  get_bool(CAPRICCIO_NO_STATCOLLECT, conf_no_statcollect);

  get_bool(CAPRICCIO_DUMP_BLOCKING_GRAPH, conf_dump_blocking_graph);
  get_bool(CAPRICCIO_DUMP_TIMING_INFO, conf_dump_timing_info);
  get_bool(CAPRICCIO_SHOW_THREAD_DETAILS, conf_show_thread_details);

  // NOTE: this is subbordinate to CAPRICCIO_SHOW_THREAD_DETAILS
  get_bool(CAPRICCIO_SHOW_THREAD_STACKS, conf_show_thread_stacks);


  // STACK SIZE
  {
    char *str;
    str = getenv("CAPRICCIO_DEFAULT_STACK_SIZE");
    if( str != NULL ) {
      char *p;
      int mult=0;
      long val;
    
      // read the value
      val = strtol(str,&p,0);
      if( p == str )
        fatal("Bad number format for CAPRICCIO_DEFAULT_STACK_SIZE: '%s'\n", str); 
    
      // read the units
      while( isspace(*p) ) p++;
      if( *p == '\0' )
        mult = 1024; // default to KB
      else if( *p == 'b' || *p == 'B' )
        mult = 1;        
      else if( *p == 'k' || *p == 'K' )
        mult = 1024;
      else if( *p == 'm' || *p == 'M' )
        mult = 1024*1024;
      else 
        fatal("Bad units for CAPRICCIO_DEFAULT_STACK_SIZE: '%s'\n",str);
    
      // pick the next bigger power of 2
      // FIXME: allow smaller than 1K?
      // FIXME: allow not power of 2?
      conf_new_stack_size = 1024;
      conf_new_stack_kb_log2 = 0;
      while( conf_new_stack_size < mult * val ) {
        conf_new_stack_kb_log2++;
        conf_new_stack_size = conf_new_stack_size << 1;
      }
    }

    if( !conf_no_init_messages ) {
      // show MB, if big enough, and evenly divisible
      if( conf_new_stack_size > 1024*1024  &&  !(conf_new_stack_size & 0xFFFFF) )
        output("CAPRICCIO_DEFAULT_STACKSIZE=%ldM\n",conf_new_stack_size/1024/1024);

      // show KB, if big enough, and evenly divisible
      else if( conf_new_stack_size > 1024  &&  !(conf_new_stack_size & 0x3FF) )
        output("CAPRICCIO_DEFAULT_STACKSIZE=%ldK\n",conf_new_stack_size/1024);

      // otherwise, show in bytes
      else
        output("CAPRICCIO_DEFAULT_STACKSIZE=%ldb\n",conf_new_stack_size);
    }
  }


  // FIXME: scan environment for unrecognized CAPRICCIO_* env vars,
  // and warn about likely misspelling.

}



