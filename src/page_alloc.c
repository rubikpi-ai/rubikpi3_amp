#include <mm.h>

extern char _end[];
extern char _stack_bottom[];
extern char _heap_start[];
extern char _heap_end[];

static unsigned long low_memory;      /* heap start (page-aligned) */
static unsigned long high_memory;     /* heap end (page-aligned, exclusive) */
static unsigned long nr_pages;

static unsigned char *mem_map;
static unsigned char mem_map_static[ (128 * 1024 * 1024) / 4096 ]; /* 32768 bytes worst-case */

void mem_init(unsigned long start_mem, unsigned long end_mem)
{
	if (!start_mem)
		start_mem = (unsigned long)_heap_start;
	if (!end_mem)
		end_mem = (unsigned long)_heap_end;

	start_mem = PAGE_ALIGN(start_mem);
	end_mem &= PAGE_MASK;

	low_memory = start_mem;
	high_memory = end_mem;

	if (high_memory <= low_memory) {
		nr_pages = 0;
		return;
	}

	nr_pages = (high_memory - low_memory) / PAGE_SIZE;

	mem_map = mem_map_static;

	for (unsigned long i = 0; i < nr_pages; i++)
		mem_map[i] = 0;
}

unsigned long get_free_page(void)
{
	for (unsigned long i = 0; i < nr_pages; i++) {
		if (mem_map[i] == 0) {
			mem_map[i] = 1;
			return low_memory + i * PAGE_SIZE;
		}
	}
	return 0;
}

void free_page(unsigned long p)
{
	if (p < low_memory || p >= high_memory)
		return;

	mem_map[(p - low_memory) / PAGE_SIZE] = 0;
}
