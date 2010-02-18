
struct dirent
{
  unsigned long long d_ino;
  unsigned long long d_off;
  unsigned short     d_reclen;
  unsigned char      d_type;
  char               d_name[256];
};

#define d_fileno d_ino

#define dirent64 dirent
