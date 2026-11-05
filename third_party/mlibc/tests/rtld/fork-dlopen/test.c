#include <assert.h>
#include <dlfcn.h>
#include <pthread.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef USE_HOST_LIBC
#define LIBANCHOR "libnative-fork-anchor.so"
#define LIBCHILD "libnative-fork-child.so"
#else
#define LIBANCHOR "libfork-anchor.so"
#define LIBCHILD "libfork-child.so"
#endif

static void *worker(void *arg) {
	(void)arg;
	void *handle = dlopen(LIBANCHOR, RTLD_NOW | RTLD_LOCAL);
	assert(handle);
	int (*anchor)(void) = dlsym(handle, "anchor");
	assert(anchor);
	assert(anchor() == 1);
	return NULL;
}

static void open_child(void) {
	void *handle = dlopen(LIBCHILD, RTLD_NOW | RTLD_LOCAL);
	assert(handle);
	int (*child_value)(void) = dlsym(handle, "child_value");
	assert(child_value);
	assert(child_value() == 2);
}

int main(void) {
	pthread_t thread;
	assert(!pthread_create(&thread, NULL, worker, NULL));
	assert(!pthread_join(thread, NULL));

	pid_t child = fork();
	assert(child >= 0);
	if(!child) {
		open_child();
		_exit(0);
	}

	int status;
	assert(waitpid(child, &status, 0) == child);
	assert(WIFEXITED(status));
	assert(WEXITSTATUS(status) == 0);
	open_child();
	return 0;
}
