#ifndef _NVX_UNISTD_H
#define _NVX_UNISTD_H
#include <sys/types.h>
#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
ssize_t read(int,void*,size_t); ssize_t write(int,const void*,size_t); int close(int); off_t lseek(int,off_t,int); int fsync(int);
uid_t getuid(void); gid_t getgid(void); uid_t geteuid(void); gid_t getegid(void); pid_t getpid(void); unsigned int sleep(unsigned int); int usleep(unsigned int); long sysconf(int); int isatty(int);
#define _SC_PAGESIZE 30
#endif
