/* Keep this in sync with the kernel */
struct dirent
{
  __ino64_t          d_ino;
  __off64_t          d_off;
  unsigned short     d_reclen;
  unsigned char      d_type;
  char               d_name[256];
} __attribute__((aligned(8)));

#define d_fileno d_ino
#define dirent64 dirent
