// glibc_patch.h
//
// This file contains the libc patch for Deputy.  Annotations on the
// functions below will be patched into the libc headers when Deputy runs.
// In this file, void types are treated as wildcards, so any types that
// are irrelevant to the patch can be changed to void.

// Typedefs for types that appear below but are irrelevant to the patch.

#define hs_norc __attribute__((hs_norc))
//#define BASE(p) __attribute__((base(p)))

typedef void __gid_t;
typedef void __uid_t;
typedef unsigned int size_t;
typedef signed int ssize_t;
typedef void socklen_t;
typedef void __socklen_t;
typedef void mode_t;
typedef void dev_t;
typedef void time_t;
typedef void FILE;
typedef void DIR;
typedef void __gnuc_va_list;

#define __SSIZE_T       ssize_t
#define __SIZE_T        size_t
#define __SOCKLEN_T     socklen_t
#define __MODE_T        mode_t
#define __DEV_T         dev_t

#define __SOCKADDR_ARG		struct sockaddr *
#define __CONST_SOCKADDR_ARG	struct sockaddr *

// Some handy macros for the types below.

#define OPTSTRING    char * NTS OPT
#define STRING       char * NTS NONNULL
#define STRINGBUF(n) char * NT COUNT(n) NONNULL

// assert.h

void __assert_fail(const STRING __assertion, const STRING __file,
                   unsigned int __line, const STRING __function);

#if __GNUC__ < 4 
void __assert(const STRING what, int exp, const STRING extra);
#endif

// Darwin
void __assert_rtn(const char * NTS, const char * NTS, int, const char * NTS);
void __eprintf(const char * NTS, const char * NTS, unsigned, const char * NTS);

// crypt.h

STRING crypt(const STRING key, const char * COUNT(2) NONNULL salt);
void setkey(STRING key);

// ctype.h

// gcc 3.4.4
extern const char (COUNT(256) _ctype_)[256];

// gcc 4
const unsigned short * COUNT(256) * __ctype_b_loc();
const signed int * COUNT(256) * __ctype_tolower_loc();
const signed int * COUNT(256) * __ctype_toupper_loc();

// fcntl.h

int open(const STRING file, int flags, int mode);
int create(const STRING file, mode_t mode);

// glob.h

typedef struct {
    char * NTS * COUNT(gl_pathc) NT gl_pathv; 
} glob_t;

int glob(const char * NTS pattern, int flags,
         int errfunc(const char * NTS epath, int eerrno),
         glob_t * SAFE pglob);

// grp.h

struct group {
    char * NTS NONNULL gr_name; 
    char * NTS NONNULL gr_passwd;
    char * NTS * NTS gr_mem;
};

struct group * SAFE getgrnam(const STRING name);
int initgroups(char * NTS __user, __gid_t __group);
int getgrouplist(char * NTS __user, __gid_t __group, __gid_t *__groups,
                 int *__ngroups);

// malloc.h

void * OPT (DALLOC(size) malloc)(int size);
void * OPT (DALLOC(size) alloca)(int size);
void * OPT (DALLOC(size) __builtin_alloca)(unsigned int size);
void * OPT (DALLOC(nmemb * size) calloc)(size_t nmemb, size_t size);
void * OPT (DREALLOC(p, size) realloc)(void *p, size_t size);
void (DFREE(p) free)(void *p);

// netdb.h

struct hostent {
    const char * NTS h_name;
    char * NTS * NTS h_aliases;
    // We ought to say h_length instead of 4 there
    char * COUNT(4) * NTS h_addr_list;
};

struct netent {
    char * NTS n_name;
    char * NTS * NTS n_aliases;
};

struct servent {
    char * NTS s_name;
    char * NTS * NTS s_aliases;
    char * NTS s_proto;
};

struct protoent {
    char * NTS p_name;
    char * NTS * NTS p_aliases;
};

struct rpcent {
    char * NTS r_name;
    char * NTS * NTS r_aliases;
};

struct sockaddr_un {
    char (NT sun_path)[108];
};

struct hostent *gethostbyaddr(const void * COUNT(__len),
                              __socklen_t __len, int);

int gethostbyaddr_r(void * COUNT(__len) __addr, __socklen_t __len,
                    int __type, struct hostent *__result_buf,
                    char * COUNT(__buflen) __buf, size_t __buflen,
                    struct hostent * SAFE * SAFE __result, int *__h_errnop);

struct hostent *gethostbyname(const STRING);
int gethostbyname_r(char * NTS __name, struct hostent *__result_buf,
                    char * COUNT(__buflen) __buf, size_t __buflen,
                    struct hostent * SAFE * SAFE __restrict, int *__h_errnop);

struct netent *getnetbyname(const STRING);
struct protoent *getprotobyname(const STRING);

struct servent *getservbyname (const STRING, const STRING);
struct servent *getservbyport (int, const STRING);

int getservent_r(struct servent *__result_buf,
                 char * COUNT(__buflen) __buf, size_t __buflen,
                 struct servent **__result);

int getservbyname_r(const STRING __name, const STRING __proto,
                    struct servent *__result_buf,
                    char * COUNT(__buflen) __buf, size_t __buflen,
                    struct servent **__result);

int getservbyport_r(int __port, const STRING __proto,
                    struct servent *__result_buf,
                    char * COUNT(__buflne) __buf, size_t __buflen,
                    struct servent **__result);

struct rpcent *getrpcbyname(const STRING);

void herror(const STRING);

int getaddrinfo(char * NTS __name, char * NTS __service,
                struct addrinfo *__req, struct addrinfo **__pai);

int getnameinfo(struct sockaddr *__sa, socklen_t __salen,
                char * NT COUNT(__hostlen - 1) __host, socklen_t __hostlen,
                char * NT COUNT(__servlen-1) __serv,
                socklen_t __servlen, unsigned int __flags);

// pwd.h

struct passwd {
    char * NTS NONNULL pw_name;
    char * NTS NONNULL pw_passwd;
    char * NTS NONNULL pw_comment;
    char * NTS NONNULL pw_class;
    char * NTS NONNULL pw_gecos;
    char * NTS NONNULL pw_dir;
    char * NTS NONNULL pw_shell;
};

struct passwd *getpwnam(const STRING);

// socket.h

int setsockopt(int __s, int __level, int __optname,
               void const * COUNT(__optlen) optval, __SOCKLEN_T __optlen);

// TODO: Indicate that optval is as big as *__optlen.
// TODO: Say something about __addrlen.
int getsockopt(int s, int level, int optname,
               void* optval, __SOCKLEN_T * __optlen);
int bind(int sockfd, __CONST_SOCKADDR_ARG __my_addr, __SOCKLEN_T __addrlen);
int connect(int sockfd, __CONST_SOCKADDR_ARG __my_addr, __SOCKLEN_T __addrlen);
int accept(int s, __SOCKADDR_ARG __peer, __SOCKLEN_T *addrlen);
int getpeername(int s, __SOCKADDR_ARG __peer, __SOCKLEN_T *namelen);
int getsockname(int s, __SOCKADDR_ARG name, __SOCKLEN_T *namelen);

__SSIZE_T sendto(int s, const void * NONNULL COUNT(len) msg,
                 __SIZE_T len, int flags,
                 __CONST_SOCKADDR_ARG __addr, __SOCKLEN_T __addr_len);    

__SSIZE_T recvfrom(int s, void * NONNULL COUNT(__n) buf, 
                   __SIZE_T __n, int flags,
                   __SOCKADDR_ARG __addr, __SOCKLEN_T * __addr_len);

__SSIZE_T recv(int s, void * NONNULL COUNT(__n) buf, 
               __SIZE_T __n, int flags);

__SSIZE_T send(int s, const void * NONNULL COUNT(__n) msg, 
               __SIZE_T __n, int flags);

// stat.h

int chmod(const STRING __path, __MODE_T __mode);
int mkdir(const STRING __path, __MODE_T __mode);
int mkfifo(const STRING __path, __MODE_T __mode);
int stat(const STRING __path, struct stat * SAFE NONNULL __sbuf);

int lstat(const STRING __path, struct stat * SAFE NONNULL __sbuf);
int _stat(const STRING __path, struct stat * SAFE NONNULL __sbuf);

int __xstat(int __ver, const STRING __path, struct stat * SAFE NONNULL __sbuff);
int __lxstat(int __ver, const STRING __path, struct stat * SAFE NONNULL __sbuf);

int mknod(const STRING __path, __MODE_T __mode, __DEV_T __dev);

// stdio.h

int (DPRINTF(2) fprintf) (FILE * SAFE __stream, char * NTS __format, ...);
int (DPRINTF(1) printf)  (char * NTS __format, ...);
int (DPRINTF(2) sprintf) (char * TRUSTED __s, char * NTS __format, ...);
int (DPRINTF(3) snprintf)(char * NT COUNT(__maxlen-1) __s, size_t __maxlen,
                          char * NTS __restrict __format, ...);
int (DPRINTF(2) vfprintf)(FILE * SAFE __s, char * NTS __format,
                          __gnuc_va_list __arg);
int (DPRINTF(1) vprintf) (char * NTS __format, __gnuc_va_list __arg);
int (DPRINTF(2) vsprintf)(char * TRUSTED __s, __const char * NTS __format,
                          __gnuc_va_list __arg);

int fscanf(FILE * SAFE NONNULL, const char * NTS, ...);
int scanf(const char * NTS, ...);
int sscanf(const char * TRUSTED, const char * NTS, ...);

char * NTS fgets(char * NT COUNT(__n-1) NONNULL __s, int __n,
                 FILE * NONNULL __stream);

FILE * fdopen(int filedes, const char * NTS NONNULL mode);

int fputs(const char * NTS NONNULL s, FILE * SAFE NONNULL fl);
int puts(const char* NTS NONNULL s);

size_t fread(void * COUNT(_size * _n) NONNULL, size_t _size,
             size_t _n, FILE * SAFE NONNULL);
size_t fwrite(const void * COUNT(_size  *_n) NONNULL,
              size_t _size, size_t _n, FILE * SAFE NONNULL);

extern void perror(const char * NTS NONNULL); 

FILE * SAFE NULLABLE fopen(const char * NTS NONNULL _name,
                           const char * NTS NONNULL _type);

int fseek(FILE * SAFE NONNULL, long, int);

int remove(const char * NTS NONNULL);
int rename(const char * NTS NONNULL, const char * NTS NONNULL);

// Darwin
extern FILE (COUNT(3) __sF)[];

// stdlib.h

double atof(char * NTS);
int atoi(char * NTS);
long int atol(char * NTS);
long long atoll(char * NTS);

double      __strtod_internal (char * NTS, char * NTS *, int);
float       __strtof_internal (char * NTS, char * NTS *, int);
long double __strtold_internal(char * NTS, char * NTS *, int);
long int    __strtol_internal (char * NTS, char * NTS *, int, int);
long long int    __strtoll_internal (char * NTS, char * NTS *, int, int);

double strtod(const STRING str, OPTSTRING * SAFE endptr);
long strtol(const STRING str, OPTSTRING * SAFE endptr, int base);
long long strtoll(const STRING str, OPTSTRING * SAFE endptr, int base);

OPTSTRING getenv(const STRING str);
int putenv(char *NTS);
int unsetenv(char *NTS);

int system(const OPTSTRING str);

// zra: This is wrong. base is probably COUNT(something)
// whereas the arguments to compar are probably SAFE.
//void qsort(TV(t) base, size_t nmemb, size_t size,
//           int (*compar)(const TV(t),const TV(t)));

// string.h

unsigned int  strlen(const STRING s);

void * (DMEMSET(1, 2, 3) memset)(void* p, int what, void sz);
int    (DMEMCMP(1, 2, 3) memcmp)(void* s1, void* s2, void sz);
void * (DMEMCPY(1, 2, 3) memcpy)(void* dst, void* src, void sz);
void * (DMEMCPY(1, 2, 3) memmove)(void *dst, void* src, void sz);

void bzero(void * COUNT(size) buff, unsigned int size);

STRING strncpy(STRINGBUF(n) dest, const STRING src, void n);
STRING __builtin_strncpy(STRINGBUF(n) dest, const STRING src, size_t n);

int  strcmp(const STRING s1, const STRING s2);
int  __builtin_strcmp(const STRING s1, const STRING s2);

int  strncmp(const STRING s1, const STRING s2, size_t n);
int  __builtin_strncmp(const STRING s1, const STRING s2, size_t n);

size_t strlcpy(STRINGBUF(siz-1) dst, const STRING src, size_t siz);

STRING strncat(STRINGBUF(n) dest, const STRING src, size_t n);
STRING __builtin_strncat(STRINGBUF(n) dest, const STRING src, size_t n);

size_t strlcat(STRINGBUF(n-1) dest, const STRING src, size_t n);

OPTSTRING strchr(const STRING s, int chr);
OPTSTRING __builtin_strchr(const STRING s, int chr);

OPTSTRING strrchr(const STRING s, int chr);
OPTSTRING strdup(const STRING s);
OPTSTRING __strdup(const STRING s);
OPTSTRING strpbrk(const STRING str, const STRING accept_arg);
OPTSTRING __builtin_strpbrk(const STRING str, const STRING accept_arg);
OPTSTRING __strpbrk_c2 (__const STRING str, int __accept1, int __accept2);
OPTSTRING strsep(char * NTS * NT stringp, const STRING delim);

size_t strspn(const STRING str, const STRING charset);
size_t __builtin_strspn(const STRING str, const STRING charset);

size_t __strspn_c1(const STRING str, int accept1);
size_t __strspn_c2(const STRING str, int accept1, int accept2);
size_t __strspn_c3(const STRING str, int accept1, int accept2, int accept3);

size_t strcspn(const STRING str, const STRING charset);
size_t __builtin_strcspn(const STRING str, const STRING charset);

size_t __strcspn_c1(const STRING str, int reject1);
size_t __strcspn_c2(const STRING str, int reject1, int reject2);
size_t __strcspn_c3(const STRING str, int reject1, int reject2, int reject3);

int strcasecmp(const STRING s1, const STRING s2);
int strncasecmp(const STRING s1, const STRING s2, size_t n);

OPTSTRING strtok(OPTSTRING str, const STRING delim);

OPTSTRING strerror(int errnum);
OPTSTRING strstr(const STRING __haystack, const STRING __needle);

// time.h

extern STRING ctime(const time_t *timer);
extern STRING asctime(const struct tm *timep);

//similar to gethostname - null term is not guarenteed to exist

size_t strftime(char * NONNULL COUNT(max) s, size_t max, const STRING format,
                           const struct tm * SAFE tm);


// sys/uio.h

struct iovec {
    void * COUNT(iov_len) iov_base;
    size_t iov_len;
};

int readv(int fd, const struct iovec * COUNT(__count), int __count);
int writev(int fd, const struct iovec * COUNT(__count), int __count);

// unistd.h

void read (int __fd, void * COUNT(__nbytes) __buf, void __nbytes);
void write (int __fd, const void * COUNT(__n) __buf, void __n);

void pwrite(int fd, void *COUNT(count) buf, void count, void offset);

int access(const char * NTS path, int amode);
int execv(const char * NTS NONNULL path, char * NTS * NTS argv);
int execvp(const char * NTS NONNULL path, char * NTS * NTS argv);
int execve(const char * NTS NONNULL path, char * NTS * NTS argv,
           char * NTS * NTS envp);

char * NTS getlogin(void);
char * NTS ttyname(int filedes);

int getopt(int argc, char * NTS * NT COUNT(argc) argv,
           const char * NTS optstring);

extern char *NTS optarg;

char * NTS getusershell(void);

int chdir(const char * NTS NONNULL);
int unlink(const char * NTS NONNULL __path);
int rmdir(char * NTS __path);

//modified as not guarenteed to be NT
//this function is not type-safe per se
//We need to wrap around it
int gethostname (char * NONNULL COUNT(__len) __name, size_t __len);

int chown(char * NTS __file, __uid_t __owner, __gid_t __group);
int link(char * NTS __from, char * NTS __to);
int chroot(char * NTS __path);

int readlink (char *NTS, char *NTS, size_t);

OPTSTRING getcwd (STRINGBUF(__size) __buf, size_t __size);

// reent.h

//#ifdef __CYGWIN__
// Take care of the union in reent.h (on cygwin)
// This union is not actually used, so we can use WHEN 
// clauses to enable only the used field.
struct _reent {
    union {
        void _reent WHEN(1);
        void _unused WHEN(0);
    } _new;
};
//#endif

// siginfo.h

// Trust the sigval union--there is no way to know what it is (in bits).
typedef union TRUSTED sigval { 
    int sival_int;
    void *sival_ptr;
} sigval_t;

#define	SIGILL		4
#define	SIGBUS		7
#define	SIGFPE		8
#define	SIGKILL		9
#define	SIGSEGV		11
#define	SIGALRM		14
#define	SIGCHLD		17
#define	SIGPOLL		SIGIO
#define	SIGIO		29

struct siginfo {
    union {
        void _kill WHEN(si_signo == SIGKILL);
        void _timer WHEN(si_signo == SIGALRM);
        void _rt WHEN(0); // TODO: When is this used?
        void _sigchld WHEN(si_signo == SIGCHLD);
        void _sigfault WHEN(si_signo == SIGILL || si_signo == SIGFPE ||
                            si_signo == SIGSEGV || si_signo == SIGBUS);
        void _sigpoll WHEN(si_signo == SIGPOLL);
    } _sifields;
};

// sigaction.h

#define SA_SIGINFO 4

typedef void __sighandler_t;
typedef void siginfo_t;

struct sigaction {
    union {
        /* Used if SA_SIGINFO is not set.  */
        __sighandler_t sa_handler
            WHEN (!(sa_flags & SA_SIGINFO));
        /* Used if SA_SIGINFO is set.  */
        void (*sa_sigaction)(int, siginfo_t *, void *)
            WHEN(sa_flags & SA_SIGINFO);
    } __sigaction_handler;
};

// syslog.h

void openlog(char *NTS, int, int);
void syslog(int, char * NTS, ...);

// resolv.h

struct __res_state {
    char *NTS (NT dnsrch)[0];
    char (NT defdname)[0];
    union {
        void pad WHEN(0);
        void _ext WHEN(1);
    } _u;
};

// sys/utsname.h

struct utsname {
    char (NT sysname)[0];
    char (NT nodename)[0];
    char (NT release)[0];
    char (NT version)[0];
    char (NT machine)[0];
    char (NT domainname)[0];
    char (NT __domainname)[0];
};

// net/if.h

struct ifreq {
    union {
        char (NT ifrn_name)[0];
    } ifr_ifrn;
    union TRUSTED {
    } ifr_ifru;
};

struct ifconf {
    union TRUSTED {
        char * NTS ifcu_buf;
    } ifc_ifcu;
};

// sys/statfs.h

int statvfs(char * NTS, struct statvfs *);
int statvfs64(char * NTS, struct statvfs64 *);
int statfs(char * NTS, struct statfs *);
int statfs64(char * NTS, struct statfs64 *);

// sys/shm.h

void *TRUSTED shmat(int shmid, const void *smaddr, int shmflg);

// db.h

typedef void DB_ENV;
typedef void DB_INFO;
typedef void DBTYPE;
typedef void DB;
typedef unsigned int u_int32_t;

int   db_appinit(const char * NTS, char * NTS const *, DB_ENV *, u_int32_t);
int   db_appexit(DB_ENV *);
int   db_jump_set(void *, int);
int   db_open(const char * NTS, DBTYPE, u_int32_t, int,
              DB_ENV *, DB_INFO *, DB **);
int   db_value_set(int, int);
char *db_version(int *, int *, int *);
int   db_xa_open(const char * NTS, DBTYPE, u_int32_t, int, DB_INFO *, DB **);

// dirent.h

struct dirent {
    char (NT d_name)[256];
};

struct dirent64 {
    char (NT d_name)[256];
};

DIR *opendir(char * NTS);

// bits/pthreadtypes.h

#define __SIZEOF_PTHREAD_MUTEX_T 24
#define __SIZEOF_PTHREAD_COND_T 48

typedef struct __pthread_internal_slist
{
  struct __pthread_internal_slist *hs_norc __next;
} __pthread_slist_t;

#ifndef __APPLE__
typedef union
{
  struct __pthread_mutex_s
  {
    int __lock;
    unsigned int __count;
    int __owner;
    /* KIND must stay at this position in the structure to maintain
       binary compatibility.  */
    int __kind;
    unsigned int __nusers;
    __extension__ union
    {
      int __spins;
      __pthread_slist_t __list;
    } TRUSTED;
  } __data;
  char __size[__SIZEOF_PTHREAD_MUTEX_T];
  long int __align;
} TRUSTED pthread_mutex_t;

typedef union
{
  struct
  {
    int __lock;
    unsigned int __futex;
    __extension__ unsigned long long int __total_seq;
    __extension__ unsigned long long int __wakeup_seq;
    __extension__ unsigned long long int __woken_seq;
    void *hs_norc __mutex;
    unsigned int __nwaiters;
    unsigned int __broadcast_seq;
  } __data;
  char __size[__SIZEOF_PTHREAD_COND_T];
  __extension__ long long int __align;
} TRUSTED pthread_cond_t;
#endif

// inet calls

#ifdef __APPLE__
typedef unsigned int __uint32_t;
typedef __uint32_t in_addr_t;
#else
typedef unsigned int uint32_t;
typedef uint32_t in_addr_t;
#endif
struct in_addr {
  in_addr_t s_addr;
};

in_addr_t inet_addr(char *NTS cp);
char *NTS inet_ntoa(struct in_addr in);

// fftw.h

//int ftw(char * __dir, int (*)(char *NTS __filename, void __status, int __flag), int __descriptors);

// locale.h

char *setlocale(int category, char *NTS locale);

// pthread.h

int pthread_create(void *tid, void *attr, void (*fn)(TV(t) data), TV(t) arg);
