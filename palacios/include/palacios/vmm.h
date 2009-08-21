/*
 * This file is part of the Palacios Virtual Machine Monitor developed
 * by the V3VEE Project with funding from the United States National 
 * Science Foundation and the Department of Energy.  
 *
 * The V3VEE Project is a joint project between Northwestern University
 * and the University of New Mexico.  You can find out more at 
 * http://www.v3vee.org
 *
 * Copyright (c) 2008, Jack Lange <jarusl@cs.northwestern.edu> 
 * Copyright (c) 2008, The V3VEE Project <http://www.v3vee.org> 
 * All rights reserved.
 *
 * Author: Jack Lange <jarusl@cs.northwestern.edu>
 *
 * This is free software.  You are permitted to use,
 * redistribute, and modify it as specified in the file "V3VEE_LICENSE".
 */

#ifndef __VMM_H__
#define __VMM_H__


#include <palacios/vm_guest.h>
#include <palacios/vmm_mem.h>
#include <palacios/vmm_types.h>



#ifdef __V3VEE__

//#include <palacios/vmm_types.h>
#include <palacios/vmm_string.h>


//#include <palacios/vmm_paging.h>

/* utility definitions */


#define V3_Print(fmt, args...)					\
    do {							\
	extern struct v3_os_hooks * os_hooks;			\
	if ((os_hooks) && (os_hooks)->print) {			\
	    (os_hooks)->print((fmt), ##args);			\
	}							\
    } while (0)	


#define PrintDebug(fmt, args...)			\
    do {						\
	extern struct v3_os_hooks * os_hooks;		\
	if ((os_hooks) && (os_hooks)->print) {	\
	    (os_hooks)->print((fmt), ##args);	\
	}						\
    } while (0)						


#define PrintError(fmt, args...)					\
    do {								\
	extern struct v3_os_hooks * os_hooks;				\
	if ((os_hooks) && (os_hooks)->print) {			\
	    (os_hooks)->print("%s(%d): " fmt, __FILE__, __LINE__, ##args); \
	}								\
    } while (0)						



#define V3_AllocPages(num_pages)				\
    ({								\
	extern struct v3_os_hooks * os_hooks;			\
	void * ptr = 0;						\
	if ((os_hooks) && (os_hooks)->allocate_pages) {		\
	    ptr = (os_hooks)->allocate_pages(num_pages);	\
	}							\
	ptr;							\
    })


#define V3_FreePage(page)				\
    do {						\
	extern struct v3_os_hooks * os_hooks;		\
	if ((os_hooks) && (os_hooks)->free_page) {	\
	    (os_hooks)->free_page(page);		\
	}						\
    } while(0)


#define V3_VAddr(addr) ({					\
	    extern struct v3_os_hooks * os_hooks;		\
	    void * var = 0;					\
	    if ((os_hooks) && (os_hooks)->paddr_to_vaddr) {	\
		var = (os_hooks)->paddr_to_vaddr(addr);		\
	    }							\
	    var;						\
	})


#define V3_PAddr(addr) ({					\
	    extern struct v3_os_hooks * os_hooks;		\
	    void * var = 0;					\
	    if ((os_hooks) && (os_hooks)->vaddr_to_paddr) {	\
		var = (os_hooks)->vaddr_to_paddr(addr);		\
	    }							\
	    var;						\
	})



#define V3_Malloc(size) ({				\
	    extern struct v3_os_hooks * os_hooks;	\
	    void * var = 0;				\
	    if ((os_hooks) && (os_hooks)->malloc) {	\
		var = (os_hooks)->malloc(size);		\
	    }						\
	    var;					\
	})

// We need to check the hook structure at runtime to ensure its SAFE
#define V3_Free(addr)				\
    do {					\
	extern struct v3_os_hooks * os_hooks;	\
	if ((os_hooks) && (os_hooks)->free) {	\
	    (os_hooks)->free(addr);		\
	}					\
    } while (0)

// uint_t V3_CPU_KHZ();
#define V3_CPU_KHZ() ({							\
	    unsigned int khz = 0;					\
	    extern struct v3_os_hooks * os_hooks;			\
	    if ((os_hooks) && (os_hooks)->get_cpu_khz) {		\
		khz = (os_hooks)->get_cpu_khz();			\
	    }								\
	    khz;							\
	})								\
	


#define V3_CREATE_THREAD(fn, arg, name)				\
    do {							\
	extern struct v3_os_hooks * os_hooks;			\
	if ((os_hooks) && (os_hooks)->start_kernel_thread) {	\
	    (os_hooks)->start_kernel_thread(fn, arg, name);	\
	}							\
    } while (0)


#define V3_Hook_Interrupt(irq, opaque) ({				\
	    int ret = 0;						\
	    extern struct v3_os_hooks * os_hooks;			\
	    if ((os_hooks) && (os_hooks)->hook_interrupt) {		\
		ret = (os_hooks)->hook_interrupt(irq, opaque);		\
	    }								\
	    ret;							\
	})								\
	

#define V3_ACK_IRQ(irq)						\
    do {							\
	extern struct v3_os_hooks * os_hooks;			\
	if ((os_hooks) && (os_hooks)->ack_irq) {		\
	    (os_hooks)->ack_irq(irq);				\
	}							\
    } while (0)





/* ** */

#define V3_ASSERT(x)							\
    do {								\
	if (!(x)) {							\
	    PrintDebug("Failed assertion in %s: %s at %s, line %d, RA=%lx\n", \
		       __func__, #x, __FILE__, __LINE__,		\
		       (ulong_t) __builtin_return_address(0));		\
	    while(1);							\
	}								\
    } while(0)								\
	



#define VMM_INVALID_CPU 0
#define VMM_VMX_CPU 1
#define VMM_SVM_CPU 2


// Maybe make this a define....
typedef enum v3_cpu_arch {V3_INVALID_CPU, V3_SVM_CPU, V3_SVM_REV3_CPU, V3_VMX_CPU, V3_VMX_EPT_CPU} v3_cpu_arch_t;


v3_cpu_mode_t v3_get_host_cpu_mode();

void v3_yield(struct guest_info * info);
void v3_yield_cond(struct guest_info * info);


#endif //!__V3VEE__



struct guest_info;

/* This will contain function pointers that provide OS services */
struct v3_os_hooks {
    void (*print)(const char * format, ...)
  	__attribute__ ((format (printf, 1, 2)));
  
    void *(*allocate_pages)(int numPages);
    void (*free_page)(void * page);

    void *(*malloc)(unsigned int size);
    void (*free)(void * addr);

    void *(*paddr_to_vaddr)(void *addr);
    void *(*vaddr_to_paddr)(void *addr);

    int (*hook_interrupt)(struct guest_info * vm, unsigned int irq);

    int (*ack_irq)(int irq);

    unsigned int (*get_cpu_khz)(void);

    void (*start_kernel_thread)(int (*fn)(void * arg), void * arg, char * thread_name); 

    void (*yield_cpu)(void); 

    void *(*mutex_alloc)(void);
    void (*mutex_free)(void * mutex);
    void (*mutex_lock)(void * mutex, int must_spin);
    void (*mutex_unlock)(void * mutex);
};



typedef enum {NONE, HARDDRIVE, CDROM, VIRTIO} v3_disk_type_t;
typedef enum {RAM, NETWORK} v3_disk_connection_t;

union v3_disk_info {
    struct {
	void * data_ptr;
	int size;
    } ram;

    struct {
	char * ip_str;
	int port;
	char * disk_name;
    } net;
};

struct v3_vm_config {

    unsigned long mem_size; // in bytes, var should be natural size of cpu
    // so we can specify maximum physical address size
    // (We're screwed if we want to do 32 bit host/64 bit guest)

    int enable_telemetry;
    int enable_nested_paging;

    int enable_pci;

    int enable_swap;

    unsigned long schedule_freq; // in HZ

    v3_disk_type_t pri_disk_type;
    v3_disk_connection_t pri_disk_con;
    union v3_disk_info pri_disk_info;
   
    v3_disk_type_t sec_disk_type;
    v3_disk_connection_t sec_disk_con;
    union v3_disk_info sec_disk_info;
};



/* This will contain Function pointers that control the VMs */
struct v3_ctrl_ops {
    struct guest_info *(*allocate_guest)(void);

    int (*init_guest)(struct guest_info * info, struct v3_vm_config * config_ptr);
    int (*start_guest)(struct guest_info * info);
    //  int (*stop_vm)(uint_t vm_id);

    int (*has_nested_paging)(void);

    //  v3_cpu_arch_t (*get_cpu_arch)();
};




//
//
// This is the interrupt state that the VMM's interrupt handlers need to see
//
struct v3_interrupt {
    unsigned int irq;
    unsigned int error;

    unsigned int should_ack;  // Should the vmm ack this interrupt, or will
    // the host OS do it?
};




void Init_V3(struct v3_os_hooks * hooks, struct v3_ctrl_ops * vmm_ops);

int v3_deliver_irq(struct guest_info * vm, struct v3_interrupt * intr);



#endif
