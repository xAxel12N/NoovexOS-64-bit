#include <assert.h>
#include <dlfcn.h>
#include <pthread.h>
#include <string.h>

static pthread_barrier_t barrier;
static char errors[2][128];

static void *worker(void *arg) {
	int index = (int)(long)arg;
	const char *path = index ? "libmissing-second.so" : "libmissing-first.so";
	assert(!dlopen(path, RTLD_NOW));
	int barrier_result = pthread_barrier_wait(&barrier);
	assert(!barrier_result || barrier_result == PTHREAD_BARRIER_SERIAL_THREAD);
	const char *error = dlerror();
	assert(error);
	strncpy(errors[index], error, sizeof(errors[index]) - 1);
	errors[index][sizeof(errors[index]) - 1] = '\0';
	assert(!dlerror());
	return NULL;
}

int main(void) {
	pthread_t threads[2];
	assert(!pthread_barrier_init(&barrier, NULL, 2));
	for(int i = 0; i < 2; i++)
		assert(!pthread_create(&threads[i], NULL, worker, (void *)(long)i));
	for(int i = 0; i < 2; i++)
		assert(!pthread_join(threads[i], NULL));
	assert(strstr(errors[0], "libmissing-first.so"));
	assert(strstr(errors[1], "libmissing-second.so"));
	assert(!pthread_barrier_destroy(&barrier));
	return 0;
}
