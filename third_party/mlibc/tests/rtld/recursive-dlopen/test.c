#include <assert.h>
#include <dlfcn.h>

#ifdef USE_HOST_LIBC
#define LIBOUTER "libnative-recursive-outer.so"
#else
#define LIBOUTER "librecursive-outer.so"
#endif

int main(void) {
	void *handle = dlopen(LIBOUTER, RTLD_NOW | RTLD_LOCAL);
	assert(handle);
	int (*outer_initialized)(void) = dlsym(handle, "outer_initialized");
	assert(outer_initialized);
	assert(outer_initialized() == 1);
	return 0;
}
