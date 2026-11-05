#include <assert.h>
#include <dlfcn.h>
#include <link.h>
#include <stddef.h>

#ifdef USE_HOST_LIBC
#define LIBFOO "libnative-iterate-reentrant-foo.so"
#else
#define LIBFOO "libiterate-reentrant-foo.so"
#endif

static int count;
static void *handle;

static int count_callback(struct dl_phdr_info *info, size_t size, void *data) {
	(void)info;
	(void)size;
	(void)data;
	count++;
	return 0;
}

static int reentrant_callback(struct dl_phdr_info *info, size_t size, void *data) {
	(void)info;
	(void)size;
	(void)data;
	count++;
	if(!handle) {
		handle = dlopen(LIBFOO, RTLD_NOW | RTLD_LOCAL);
		assert(handle);
	}
	return 0;
}

int main(void) {
	assert(!dl_iterate_phdr(count_callback, NULL));
	int initial_count = count;

	count = 0;
	assert(!dl_iterate_phdr(reentrant_callback, NULL));
#ifdef USE_HOST_LIBC
	assert(count == initial_count || count == initial_count + 1);
#else
	assert(count == initial_count);
#endif

	count = 0;
	assert(!dl_iterate_phdr(count_callback, NULL));
	assert(count == initial_count + 1);
	return 0;
}
