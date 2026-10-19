#ifndef _NVX_SIGNAL_H
#define _NVX_SIGNAL_H
typedef int sig_atomic_t;
#define SIGINT 2
void (*signal(int,void(*)(int)))(int);
int raise(int);
#endif
