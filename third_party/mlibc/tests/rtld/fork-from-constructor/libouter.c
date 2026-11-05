#include <assert.h>
#include <dlfcn.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef USE_HOST_LIBC
#define LIBINNER "libnative-fork-constructor-inner.so"
#else
#define LIBINNER "libfork-constructor-inner.so"
#endif

static int initialized;

[[gnu::constructor]] static void init(void) {
	pid_t child = fork();
	assert(child >= 0);
	if(!child) {
		void *handle = dlopen(LIBINNER, RTLD_NOW | RTLD_LOCAL);
		assert(handle);
		int (*child_value)(void) = dlsym(handle, "child_value");
		assert(child_value);
		assert(child_value() == 3);
		_exit(0);
	}

	int status;
	assert(waitpid(child, &status, 0) == child);
	assert(WIFEXITED(status));
	assert(WEXITSTATUS(status) == 0);
	initialized = 1;
}

int outer_initialized(void) {
	return initialized;
}
