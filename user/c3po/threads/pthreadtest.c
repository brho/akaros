/*
**  test_pthread.c: Pth test program (pthread API)
*/

#ifdef GLOBAL
#include <pthread.h>
#define pthread_yield_np sched_yield
#else
#include "pthread.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#define die(str) \
    do { \
        fprintf(stdout, "**die: %s: errno=%d\n", str, errno); \
        exit(1); \
    } while (0)

static void *child(void *_arg)
{
    char *name = (char *)_arg;
    int i;

    fprintf(stdout, "child: startup %s\n", name);
    for (i = 0; i < 100; i++) {
        if (i++ % 10 == 0)
            pthread_yield_np();
        fprintf(stdout, "child: %s counts i=%d\n", name, i);
    }
    fprintf(stdout, "child: shutdown %s\n", name);
    return _arg;
}

int main(int argc, char *argv[])
{
    pthread_attr_t thread_attr;
    pthread_t thread[4];
    char *rc;
    (void) argc;
    (void) argv;

    fprintf(stdout, "main: init\n");

    fprintf(stdout, "main: initializing attribute object\n");
    if (pthread_attr_init(&thread_attr) != 0)
        die("pthread_attr_init");
    if (pthread_attr_setdetachstate(&thread_attr, PTHREAD_CREATE_JOINABLE) != 0)
        die("pthread_attr_setdetachstate");

    fprintf(stdout, "main: create thread 1\n");
    if (pthread_create(&thread[0], &thread_attr, child, (void *)"foo") != 0)
        die("pthread_create");
    fprintf(stdout, "main: create thread 2\n");
    if (pthread_create(&thread[1], &thread_attr, child, (void *)"bar") != 0)
        die("pthread_create");
    fprintf(stdout, "main: create thread 3\n");
    if (pthread_create(&thread[2], &thread_attr, child, (void *)"baz") != 0)
        die("pthread_create");
    fprintf(stdout, "main: create thread 4\n");
    if (pthread_create(&thread[3], &thread_attr, child, (void *)"quux") != 0)
        die("pthread_create");

    fprintf(stdout, "main: destroying attribute object\n");
    if (pthread_attr_destroy(&thread_attr) != 0)
        die("pthread_attr_destroy");

    pthread_yield_np();

    fprintf(stdout, "main: joining...\n");
    if (pthread_join(thread[0], (void **)&rc) != 0)
        die("pthread_join");
    fprintf(stdout, "main: joined thread: %s\n", rc);
    if (pthread_join(thread[1], (void **)&rc) != 0)
        die("pthread_join");
    fprintf(stdout, "main: joined thread: %s\n", rc);
    if (pthread_join(thread[2], (void **)&rc) != 0)
        die("pthread_join");
    fprintf(stdout, "main: joined thread: %s\n", rc);
    if (pthread_join(thread[3], (void **)&rc) != 0)
        die("pthread_join");
    fprintf(stdout, "main: joined thread: %s\n", rc);

    fprintf(stdout, "main: exit\n");
    return 0;
}

