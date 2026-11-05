#include <assert.h>
#include <dlfcn.h>
#include <pthread.h>

#ifdef USE_HOST_LIBC
#define LIBFOO "libnative-concurrent-foo.so"
#else
#define LIBFOO "libconcurrent-foo.so"
#endif

enum { num_threads = 8 };

static pthread_barrier_t barrier;
static void *handles[num_threads];

static void *worker(void *arg) {
	int index = (int)(long)arg;
	int barrier_result = pthread_barrier_wait(&barrier);
	assert(!barrier_result || barrier_result == PTHREAD_BARRIER_SERIAL_THREAD);

	handles[index] = dlopen(LIBFOO, RTLD_NOW | RTLD_LOCAL);
	assert(handles[index]);
	int (*value)(void) = dlsym(handles[index], "value");
	assert(value);
	assert(value() == 42);
	return NULL;
}

int main(void) {
	pthread_t threads[num_threads];
	assert(!pthread_barrier_init(&barrier, NULL, num_threads));
	for(int i = 0; i < num_threads; i++)
		assert(!pthread_create(&threads[i], NULL, worker, (void *)(long)i));
	for(int i = 0; i < num_threads; i++)
		assert(!pthread_join(threads[i], NULL));

	for(int i = 1; i < num_threads; i++)
		assert(handles[i] == handles[0]);
	int (*get_constructor_count)(void) = dlsym(handles[0], "get_constructor_count");
	assert(get_constructor_count);
	assert(get_constructor_count() == 1);
	assert(!pthread_barrier_destroy(&barrier));
	return 0;
}
