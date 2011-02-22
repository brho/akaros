#ifndef STACKBOUNDS_H
#define STACKBOUNDS_H

#pragma stacksize("strlen", 128)
#pragma stacksize("strcpy", 128)
#pragma stacksize("strcmp", 128)
#pragma stacksize("strcasecmp", 128)
#pragma stacksize("strchr", 128)
#pragma stacksize("strrchr", 128)

#pragma stacksize("tolower", 128)

#pragma stacksize("memset", 128)
#pragma stacksize("memcpy", 128)
#pragma stacksize("memcmp", 128)
#pragma stacksize("memchr", 128)

#pragma stacksize("__errno_location", 128)

#pragma stacksize("__ctype_b_loc", 128)
#pragma stacksize("__xstat", 128)

#pragma stacksize("read", 256)
#pragma stacksize("write", 256)
#pragma stacksize("readv", 256)
#pragma stacksize("writev", 256)
#pragma stacksize("open", 256)
#pragma stacksize("close", 256)
#pragma stacksize("accept", 256)
#pragma stacksize("poll", 256)
#pragma stacksize("fcntl", 256)

#pragma stacksize("pthread_mutex_lock", 128)
#pragma stacksize("pthread_mutex_unlock", 128)
#pragma stacksize("pthread_cond_wait", 128)
#pragma stacksize("pthread_cond_signal", 128)

#pragma stacksize("pthread_yield", 128)

#endif // STACKBOUNDS_H
