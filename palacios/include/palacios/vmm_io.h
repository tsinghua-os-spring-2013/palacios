#ifndef __VMM_IO_H
#define __VMM_IO_H



#include <palacios/vmm_types.h>
#include <palacios/vmm_util.h>


struct guest_info;
struct vmm_io_hook;

struct vmm_io_map {
  uint_t num_ports;
  struct vmm_io_hook * head;

};


int v3_unhook_io_port(struct guest_info * info, uint_t port);


/* External API */
int v3_hook_io_port(struct guest_info * info, uint_t port, 
		    int (*read)(ushort_t port, void * dst, uint_t length, void * priv_data),
		    int (*write)(ushort_t port, void * src, uint_t length, void * priv_data), 
		    void * priv_data);

void init_vmm_io_map(struct guest_info * info);


#ifdef __V3VEE__



// FOREACH_IO_HOOK(vmm_io_map_t * io_map, vmm_io_hook_t * io_hook)
#define FOREACH_IO_HOOK(io_map, io_hook) for (io_hook = (io_map).head; io_hook != NULL; io_hook = (io_hook)->next)


struct vmm_io_hook {
  ushort_t port;

  // Reads data into the IO port (IN, INS)
  int (*read)(ushort_t port, void * dst, uint_t length, void * priv_data);

  // Writes data from the IO port (OUT, OUTS)
  int (*write)(ushort_t port, void * src, uint_t length, void * priv_data);

  void * priv_data;

  struct vmm_io_hook * next;
  struct vmm_io_hook * prev;

};


struct vmm_io_hook * v3_get_io_hook(struct vmm_io_map * io_map, uint_t port);


void PrintDebugIOMap(struct vmm_io_map * io_map);


#endif // !__V3VEE__





#endif
