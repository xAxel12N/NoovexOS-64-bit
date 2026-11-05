#ifndef MLIBC_DLAPI_HPP
#define MLIBC_DLAPI_HPP

#include <mlibc-config.h>

extern "C" {

struct __dlapi_symbol {
	const char *file;
	void *base;
	const char *symbol;
	void *address;
	const void *elf_symbol;
	void *link_map;
};

const char *__dlapi_error();
void *__dlapi_open(const char *, int, void *);
void *__dlapi_resolve(void *, const char *, void *, const char *);
void *__dlapi_vdsosym(const char *, const char *);
int __dlapi_reverse(const void *, __dlapi_symbol *);
int __dlapi_close(void *);

// fork() support for ld.so. fork(), forkpty() and pdfork() bracket the fork as:
//
//     __dlapi_prefork();
//     <fork>
//     if(!child_pid)
//         __dlapi_postfork_rebind(parent_tid);
//     __dlapi_postfork_finish();
//
// _Fork() and vfork() do not synchronize with ld.so; they only call
// __dlapi_postfork_rebind() in the child.

// Lock the ld.so mutex so that the child inherits consistent loader state.
void __dlapi_prefork();
// Unlock the ld.so mutex again.
void __dlapi_postfork_finish();
// Rebind the ld.so lock from parent to child.
void __dlapi_postfork_rebind(unsigned int parent_tid);

#if __MLIBC_GLIBC_OPTION
#include <dlfcn.h>
int __dlapi_find_object(void *__address, dl_find_object *__result);
#endif

}

#endif // MLIBC_DLAPI_HPP
