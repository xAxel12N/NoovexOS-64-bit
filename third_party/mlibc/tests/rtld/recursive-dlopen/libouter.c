#include <assert.h>
#include <dlfcn.h>

#ifdef USE_HOST_LIBC
#define LIBINNER "libnative-recursive-inner.so"
#else
#define LIBINNER "librecursive-inner.so"
#endif

static int initialized;

[[gnu::constructor]] static void init(void) {
	void *handle = dlopen(LIBINNER, RTLD_NOW | RTLD_LOCAL);
	assert(handle);
	int (*inner_initialized)(void) = dlsym(handle, "inner_initialized");
	assert(inner_initialized);
	initialized = inner_initialized();
}

int outer_initialized(void) {
	return initialized;
}
