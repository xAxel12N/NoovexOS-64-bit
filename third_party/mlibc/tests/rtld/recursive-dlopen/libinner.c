static int initialized;

[[gnu::constructor]] static void init(void) {
	initialized++;
}

int inner_initialized(void) {
	return initialized;
}
