#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include "pico/pico.h"

void lprintf(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stdout, fmt, ap);
	va_end(ap);
}

void *plat_mmap(unsigned long addr, size_t size, int need_exec, int is_fixed) {
	return malloc(size);
}

void *plat_mremap(void *ptr, size_t oldsize, size_t newsize) {
	return realloc(ptr, newsize);
}

void plat_munmap(void *ptr, size_t size) {
	free(ptr);
}

int mp3_get_bitrate(void *f, int size) {
	// stub
	return -1;
}

void mp3_start_play(void *f, int pos) {
	// stub
}

void mp3_update(int *buffer, int length, int stereo) {
	// stub
}