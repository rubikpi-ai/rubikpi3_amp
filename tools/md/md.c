// mdq.c - "md.q" like memory dump tool for Linux using /dev/mem
// Build: aarch64-linux-gnu-gcc -O2 -Wall -o mdq mdq.c
// Usage: ./mdq <phys_addr> [words]    (words = number of 64-bit words, default 64)
// Example: ./mdq 0xd7c00000 0x40

#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static uint64_t parse_u64(const char *s)
{
	char *end = NULL;
	errno = 0;
	uint64_t v = strtoull(s, &end, 0);
	if (errno || !end || *end != '\0') {
		fprintf(stderr, "Invalid number: %s\n", s);
		exit(1);
	}
	return v;
}

static void dump_line(uint64_t addr, const uint8_t *p, size_t n)
{
	/* print address */
	printf("%08" PRIx64 ":", addr);

	/* print up to 16 bytes as two qwords */
	for (int i = 0; i < 16; i += 8) {
		if ((size_t)i + 8 <= n) {
			uint64_t q;
			memcpy(&q, p + i, sizeof(q));
			printf(" %016" PRIx64, q);
		} else {
			printf(" %16s", "");
		}
	}

	printf("  ");

	/* ascii */
	for (size_t i = 0; i < 16; i++) {
		if (i < n) {
			unsigned char c = p[i];
			printf("%c", isprint(c) ? c : '.');
		} else {
			printf(" ");
		}
	}
	printf("\n");
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "Usage: %s <phys_addr> [qwords]\n", argv[0]);
		fprintf(stderr, "  phys_addr: physical address (hex like 0xd7c00000)\n");
		fprintf(stderr, "  qwords   : number of 64-bit words to dump (default 64)\n");
		return 2;
	}

	uint64_t phys = parse_u64(argv[1]);
	uint64_t qwords = (argc >= 3) ? parse_u64(argv[2]) : 64;

	size_t bytes = (size_t)qwords * 8;
	long page_sz = sysconf(_SC_PAGESIZE);
	if (page_sz <= 0) page_sz = 4096;

	uint64_t page_base = phys & ~((uint64_t)page_sz - 1);
	uint64_t page_off  = phys - page_base;

	size_t map_len = (size_t)page_off + bytes;
	/* round up to page */
	map_len = (map_len + (page_sz - 1)) & ~(size_t)(page_sz - 1);

	int fd = open("/dev/mem", O_RDONLY | O_SYNC);
	if (fd < 0) {
		perror("open(/dev/mem)");
		return 1;
	}

	void *map = mmap(NULL, map_len, PROT_READ, MAP_SHARED, fd, (off_t)page_base);
	if (map == MAP_FAILED) {
		perror("mmap");
		close(fd);
		return 1;
	}

	const uint8_t *p = (const uint8_t *)map + page_off;

	/* dump 16 bytes per line */
	uint64_t addr = phys;
	size_t remaining = bytes;
	while (remaining > 0) {
		size_t n = remaining >= 16 ? 16 : remaining;
		dump_line(addr, p, n);
		p += n;
		addr += n;
		remaining -= n;
	}

	munmap(map, map_len);
	close(fd);
	return 0;
}
