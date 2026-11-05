static int constructor_count;

[[gnu::constructor]] static void init(void) {
	constructor_count++;
}

int get_constructor_count(void) {
	return constructor_count;
}

int value(void) {
	return 42;
}
