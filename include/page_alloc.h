#ifndef _PAGE_ALLOC_H
#define _PAGE_ALLOC_H

void mem_init(unsigned long start_mem, unsigned long end_mem);
unsigned long get_free_page(void);

#endif
