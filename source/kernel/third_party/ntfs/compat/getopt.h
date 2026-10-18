#ifndef _NVX_GETOPT_H
#define _NVX_GETOPT_H
struct option { const char* name; int has_arg; int* flag; int val; };
#define no_argument 0
#define required_argument 1
#define optional_argument 2
extern char* optarg; extern int optind, opterr, optopt;
int getopt(int,char* const*,const char*);
int getopt_long(int,char* const*,const char*,const struct option*,int*);
#endif
