#ifndef _NDB_FCALLFMT_H
#define _NDB_FCALLFMT_H

#include <printf-ext.h>
#include <fcall.h>

int printf_fcall(FILE *stream, const struct printf_info *info,
                 const void *const *args);
int printf_fcall_info(const struct printf_info* info, size_t n, int *argtypes,
                      int *size);
int printf_dir(FILE *stream, const struct printf_info *info,
               const void *const *args);
int printf_dir_info(const struct printf_info* info, size_t n, int *argtypes,
                    int *size);

int read9pmsg(int, void *, unsigned int);

#endif /* _NDB_FCALLFMT_H */
