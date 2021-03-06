#ifndef _X86_64_MMU_H
#define _X86_64_MMU_H

#include <lwk/spinlock.h>
// #include <asm/semaphore.h>

/*
 * The x86_64 doesn't have a mmu context, but
 * we put the segment information here.
 */
typedef struct { 
	void *ldt;
	rwlock_t ldtlock; 
	int size;
//	struct semaphore sem; 
} mm_context_t;

#endif
