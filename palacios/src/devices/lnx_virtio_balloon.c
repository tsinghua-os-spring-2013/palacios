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

#include <palacios/vmm.h>
#include <palacios/vmm_dev_mgr.h>
#include <devices/lnx_virtio_pci.h>
#include <palacios/vm_guest_mem.h>

#include <devices/pci.h>


#define BLN_REQUESTED_PORT     20
#define BLN_ALLOCATED_PORT     28


#define PAGE_SIZE 4096

#define BALLOON_HCALL 0xba00

struct balloon_config {
    uint32_t requested_pages;
    uint32_t allocated_pages;
} __attribute__((packed));



/* How this works:
 * A ballooning request is made by specifying the new memory size of the guest. The guest 
 * will then shrink the amount of of memory it uses to target. The target size is stored in the 
 * Virtio PCI configuration space in the requested pages field. The device raises its irq, to notify the guest
 * 
 * The guest might not be able to shrink to target, so it stores the size it was able to shrink to 
 * into the allocate_pages field of the pci configuration space.
 * 
 * When the guest frees pages it writes the addresses to the deflation queue (the 2nd one), and does a kick.
 * When pages are given back to the host they are fed in via the inflation queue (the 1st one), and raises an irq.
 */


#define QUEUE_SIZE 128

/* Host Feature flags */
#define VIRTIO_NOTIFY_HOST       0x01     





struct virtio_balloon_state {
    struct balloon_config balloon_cfg;
    struct virtio_config virtio_cfg;

    struct vm_device * pci_bus;
    struct pci_device * pci_dev;
    
    struct virtio_queue queue[2];


    struct virtio_queue * cur_queue;

    int io_range_size;
};


static int virtio_free(struct vm_device * dev) {
    return -1;
}

static int virtio_reset(struct vm_device * dev) {
    struct virtio_balloon_state * virtio = (struct virtio_balloon_state *)dev->private_data;

    memset(virtio->queue, 0, sizeof(struct virtio_queue) * 2);

    virtio->cur_queue = &(virtio->queue[0]);


    virtio->virtio_cfg.status = 0;
    virtio->virtio_cfg.pci_isr = 0;


    /* Balloon configuration */
    virtio->virtio_cfg.host_features = VIRTIO_NOTIFY_HOST;

    // Virtio Balloon uses two queues
    virtio->queue[0].queue_size = QUEUE_SIZE;
    virtio->queue[1].queue_size = QUEUE_SIZE;


    memset(&(virtio->balloon_cfg), 0, sizeof(struct balloon_config));

    return 0;
}




static int handle_kick(struct vm_device * dev) {
    struct virtio_balloon_state * virtio = (struct virtio_balloon_state *)dev->private_data;    
    struct virtio_queue * q = virtio->cur_queue;

    PrintDebug("VIRTIO KICK: cur_index=%d, avail_index=%d\n", q->cur_avail_idx, q->avail->index);

    while (q->cur_avail_idx < q->avail->index) {
	struct vring_desc * hdr_desc = NULL;
	struct vring_desc * buf_desc = NULL;
	struct vring_desc * status_desc = NULL;
	uint16_t chain_idx = q->avail->ring[q->cur_avail_idx];
	uint32_t req_len = 0;
	int chained = 1;

	PrintDebug("chained=%d, Chain Index=%d\n", chained, chain_idx);

	while (chained) {
	    hdr_desc = &(q->desc[chain_idx]);
	    
	    PrintDebug("Header Descriptor (ptr=%p) gpa=%p, len=%d, flags=%x, next=%d\n", hdr_desc, 
		       (void *)(hdr_desc->addr_gpa), hdr_desc->length, hdr_desc->flags, hdr_desc->next);
	
	    if (!(hdr_desc->flags & VIRTIO_NEXT_FLAG)) {
		PrintError("Balloon operations must chain a buffer descriptor\n");
		return -1;
	    }

	    buf_desc = &(q->desc[hdr_desc->next]);
	    
	    PrintDebug("Buffer Descriptor (ptr=%p) gpa=%p, len=%d, flags=%x, next=%d\n", buf_desc, 
		       (void *)(buf_desc->addr_gpa), buf_desc->length, buf_desc->flags, buf_desc->next);
	    
	    if (!(buf_desc->flags & VIRTIO_NEXT_FLAG)) {
		PrintError("Balloon operatoins must chain a status descriptor\n");
		return -1;
	    }
	    
	    status_desc = &(q->desc[buf_desc->next]);

	    // We detect whether we are chained here...
	    if (status_desc->flags & VIRTIO_NEXT_FLAG) {
		chained = 1;
		chain_idx = status_desc->next; 
	    } else {
		chained = 0;
	    }

	    PrintDebug("Status Descriptor (ptr=%p) gpa=%p, len=%d, flags=%x, next=%d\n", status_desc, 
		       (void *)(status_desc->addr_gpa), status_desc->length, status_desc->flags, status_desc->next);
	    

	    /* 	    
	       if (handle_balloon_op(dev, hdr_desc, buf_desc, status_desc) == -1) {
	       PrintError("Error handling balloon operation\n");
	       return -1;
	       }
	    */

	    PrintDebug("Guest Balloon Currently Ignored\n");
	    PrintDebug("\t Requested=%d, Allocated=%d\n", virtio->balloon_cfg.requested_pages, virtio->balloon_cfg.allocated_pages);


	    req_len += (buf_desc->length + status_desc->length);
	}

	q->used->ring[q->used->index].id = q->avail->ring[q->cur_avail_idx];
	q->used->ring[q->used->index].length = req_len; // What do we set this to????

	q->used->index = (q->used->index + 1) % (QUEUE_SIZE * sizeof(struct vring_desc));;


	q->cur_avail_idx = (q->cur_avail_idx + 1) % (QUEUE_SIZE * sizeof(struct vring_desc));
    }

    if (!(q->avail->flags & VIRTIO_NO_IRQ_FLAG)) {
	PrintDebug("Raising IRQ %d\n",  virtio->pci_dev->config_header.intr_line);
	v3_pci_raise_irq(virtio->pci_bus, 0, virtio->pci_dev);
	virtio->virtio_cfg.pci_isr = 1;
    }

    return 0;
}

static int virtio_io_write(uint16_t port, void * src, uint_t length, struct vm_device * dev) {
    struct virtio_balloon_state * virtio = (struct virtio_balloon_state *)dev->private_data;
    int port_idx = port % virtio->io_range_size;


    PrintDebug("VIRTIO BALLOON Write for port %d (index=%d) len=%d, value=%x\n", 
	       port, port_idx,  length, *(uint32_t *)src);



    switch (port_idx) {
	case GUEST_FEATURES_PORT:
	    if (length != 4) {
		PrintError("Illegal write length for guest features\n");
		return -1;
	    }
	    
	    virtio->virtio_cfg.guest_features = *(uint32_t *)src;

	    break;
	case VRING_PG_NUM_PORT:
	    if (length == 4) {
		addr_t pfn = *(uint32_t *)src;
		addr_t page_addr = (pfn << VIRTIO_PAGE_SHIFT);


		virtio->cur_queue->pfn = pfn;
		
		virtio->cur_queue->ring_desc_addr = page_addr ;
		virtio->cur_queue->ring_avail_addr = page_addr + (QUEUE_SIZE * sizeof(struct vring_desc));
		virtio->cur_queue->ring_used_addr = ( virtio->cur_queue->ring_avail_addr + \
						 sizeof(struct vring_avail)    + \
						 (QUEUE_SIZE * sizeof(uint16_t)));
		
		// round up to next page boundary.
		virtio->cur_queue->ring_used_addr = (virtio->cur_queue->ring_used_addr + 0xfff) & ~0xfff;

		if (guest_pa_to_host_va(dev->vm, virtio->cur_queue->ring_desc_addr, (addr_t *)&(virtio->cur_queue->desc)) == -1) {
		    PrintError("Could not translate ring descriptor address\n");
		    return -1;
		}


		if (guest_pa_to_host_va(dev->vm, virtio->cur_queue->ring_avail_addr, (addr_t *)&(virtio->cur_queue->avail)) == -1) {
		    PrintError("Could not translate ring available address\n");
		    return -1;
		}


		if (guest_pa_to_host_va(dev->vm, virtio->cur_queue->ring_used_addr, (addr_t *)&(virtio->cur_queue->used)) == -1) {
		    PrintError("Could not translate ring used address\n");
		    return -1;
		}

		PrintDebug("RingDesc_addr=%p, Avail_addr=%p, Used_addr=%p\n",
			   (void *)(virtio->cur_queue->ring_desc_addr),
			   (void *)(virtio->cur_queue->ring_avail_addr),
			   (void *)(virtio->cur_queue->ring_used_addr));

		PrintDebug("RingDesc=%p, Avail=%p, Used=%p\n", 
			   virtio->cur_queue->desc, virtio->cur_queue->avail, virtio->cur_queue->used);

	    } else {
		PrintError("Illegal write length for page frame number\n");
		return -1;
	    }
	    break;
	case VRING_Q_SEL_PORT:
	    virtio->virtio_cfg.vring_queue_selector = *(uint16_t *)src;

	    if (virtio->virtio_cfg.vring_queue_selector > 1) {
		PrintError("Virtio Balloon device only uses 2 queue, selected %d\n", 
			   virtio->virtio_cfg.vring_queue_selector);
		return -1;
	    }
	    
	    virtio->cur_queue = &(virtio->queue[virtio->virtio_cfg.vring_queue_selector]);

	    break;
	case VRING_Q_NOTIFY_PORT:
	    PrintDebug("Handling Kick\n");
	    if (handle_kick(dev) == -1) {
		PrintError("Could not handle Balloon Notification\n");
		return -1;
	    }
	    break;
	case VIRTIO_STATUS_PORT:
	    virtio->virtio_cfg.status = *(uint8_t *)src;

	    if (virtio->virtio_cfg.status == 0) {
		PrintDebug("Resetting device\n");
		virtio_reset(dev);
	    }

	    break;

	case VIRTIO_ISR_PORT:
	    virtio->virtio_cfg.pci_isr = *(uint8_t *)src;
	    break;
	default:
	    return -1;
	    break;
    }

    return length;
}


static int virtio_io_read(uint16_t port, void * dst, uint_t length, struct vm_device * dev) {
    struct virtio_balloon_state * virtio = (struct virtio_balloon_state *)dev->private_data;
    int port_idx = port % virtio->io_range_size;


    PrintDebug("VIRTIO BALLOON Read  for port %d (index =%d), length=%d\n", 
	       port, port_idx, length);

    switch (port_idx) {
	case HOST_FEATURES_PORT:
	    if (length != 4) {
		PrintError("Illegal read length for host features\n");
		return -1;
	    }

	    *(uint32_t *)dst = virtio->virtio_cfg.host_features;
	
	    break;
	case VRING_PG_NUM_PORT:
	    if (length != 4) {
		PrintError("Illegal read length for page frame number\n");
		return -1;
	    }

	    *(uint32_t *)dst = virtio->cur_queue->pfn;

	    break;
	case VRING_SIZE_PORT:
	    if (length != 2) {
		PrintError("Illegal read length for vring size\n");
		return -1;
	    }
		
	    *(uint16_t *)dst = virtio->cur_queue->queue_size;

	    break;

	case VIRTIO_STATUS_PORT:
	    if (length != 1) {
		PrintError("Illegal read length for status\n");
		return -1;
	    }

	    *(uint8_t *)dst = virtio->virtio_cfg.status;
	    break;

	case VIRTIO_ISR_PORT:
	    *(uint8_t *)dst = virtio->virtio_cfg.pci_isr;
	    virtio->virtio_cfg.pci_isr = 0;
	    v3_pci_lower_irq(virtio->pci_bus, 0, virtio->pci_dev);
	    break;

	default:
	    if ( (port_idx >= sizeof(struct virtio_config)) && 
		 (port_idx < (sizeof(struct virtio_config) + sizeof(struct balloon_config))) ) {

		uint8_t * cfg_ptr = (uint8_t *)&(virtio->balloon_cfg);
		memcpy(dst, cfg_ptr, length);
		
	    } else {
		PrintError("Read of Unhandled Virtio Read\n");
		return -1;
	    }
	  
	    break;
    }

    return length;
}




static struct v3_device_ops dev_ops = {
    .free = virtio_free,
    .reset = NULL,
    .start = NULL,
    .stop = NULL,
};


static int set_size(struct vm_device * dev, addr_t size) {
    struct virtio_balloon_state * virtio = (struct virtio_balloon_state *)dev->private_data;

    virtio->balloon_cfg.requested_pages = size / PAGE_SIZE; // number of pages
    v3_pci_raise_irq(virtio->pci_bus, 0, virtio->pci_dev);
    
    return 0;
}


static int handle_hcall(struct guest_info * info, uint_t hcall_id, void * priv_data) {
    struct vm_device * dev = (struct vm_device *)priv_data;
    int tgt_size = info->vm_regs.rcx;

    
    return set_size(dev, tgt_size);
}





static int virtio_init(struct guest_info * vm, void * cfg_data) {
    struct vm_device * pci_bus = v3_find_dev(vm, (char *)cfg_data);
    struct virtio_balloon_state * virtio_state = NULL;
    struct pci_device * pci_dev = NULL;

    PrintDebug("Initializing VIRTIO Balloon device\n");

    if (pci_bus == NULL) {
	PrintError("VirtIO devices require a PCI Bus");
	return -1;
    }

    
    virtio_state  = (struct virtio_balloon_state *)V3_Malloc(sizeof(struct virtio_balloon_state));
    memset(virtio_state, 0, sizeof(struct virtio_balloon_state));


    struct vm_device * dev = v3_allocate_device("LNX_VIRTIO_BALLOON", &dev_ops, virtio_state);
    if (v3_attach_device(vm, dev) == -1) {
	PrintError("Could not attach device %s\n", "LNX_VIRTIO_BALLOON");
	return -1;
    }




    // PCI initialization
    {
	struct v3_pci_bar bars[6];
	int num_ports = sizeof(struct virtio_config) + sizeof(struct balloon_config);
	int tmp_ports = num_ports;
	int i;



	// This gets the number of ports, rounded up to a power of 2
	virtio_state->io_range_size = 1; // must be a power of 2

	while (tmp_ports > 0) {
	    tmp_ports >>= 1;
	    virtio_state->io_range_size <<= 1;
	}
	
	// this is to account for any low order bits being set in num_ports
	// if there are none, then num_ports was already a power of 2 so we shift right to reset it
	if ((num_ports & ((virtio_state->io_range_size >> 1) - 1)) == 0) {
	    virtio_state->io_range_size >>= 1;
	}


	for (i = 0; i < 6; i++) {
	    bars[i].type = PCI_BAR_NONE;
	}

	bars[0].type = PCI_BAR_IO;
	bars[0].default_base_port = -1;
	bars[0].num_ports = virtio_state->io_range_size;

	bars[0].io_read = virtio_io_read;
	bars[0].io_write = virtio_io_write;

	pci_dev = v3_pci_register_device(pci_bus, PCI_STD_DEVICE, 
					 0, PCI_AUTO_DEV_NUM, 0,
					 "LNX_VIRTIO_BALLOON", bars,
					 NULL, NULL, NULL, dev);

	if (!pci_dev) {
	    PrintError("Could not register PCI Device\n");
	    return -1;
	}
	
	pci_dev->config_header.vendor_id = VIRTIO_VENDOR_ID;
	pci_dev->config_header.subsystem_vendor_id = VIRTIO_SUBVENDOR_ID;
	

	pci_dev->config_header.device_id = VIRTIO_BALLOON_DEV_ID;
	pci_dev->config_header.class = PCI_CLASS_MEMORY;
	pci_dev->config_header.subclass = PCI_MEM_SUBCLASS_RAM;
    
	pci_dev->config_header.subsystem_id = VIRTIO_BALLOON_SUBDEVICE_ID;


	pci_dev->config_header.intr_pin = 1;

	pci_dev->config_header.max_latency = 1; // ?? (qemu does it...)


	virtio_state->pci_dev = pci_dev;
	virtio_state->pci_bus = pci_bus;
    }

    virtio_reset(dev);

    v3_register_hypercall(vm, BALLOON_HCALL, handle_hcall, dev);

    return 0;
}


device_register("LNX_VIRTIO_BALLOON", virtio_init)