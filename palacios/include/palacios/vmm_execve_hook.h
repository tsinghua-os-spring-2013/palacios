/* 
 * This file is part of the Palacios Virtual Machine Monitor developed
 * by the V3VEE Project with funding from the United States National 
 * Science Foundation and the Department of Energy.  
 *
 * The V3VEE Project is a joint project between Northwestern University
 * and the University of New Mexico.  You can find out more at 
 * http://www.v3vee.org
 *
 * Copyright (c) 2011, Kyle C. Hale <kh@u.norhtwestern.edu>
 * Copyright (c) 2011, The V3VEE Project <http://www.v3vee.org> 
 * All rights reserved.
 *
 * Author: Kyle C. Hale <kh@u.northwestern.edu>
 *
 * This is free software.  You are permitted to use,
 * redistribute, and modify it as specified in the file "V3VEE_LICENSE".
 */


#ifndef __VMM_EXECVE_HOOK_H__
#define __VMM_EXECVE_HOOK_H__


struct v3_exec_hooks {
    struct list_head hook_list;
    struct hashtable * bin_table;
};

struct exec_hook {
    int (*handler)(struct guest_info * core, void * priv_data);
    struct list_head hook_node;
    void * priv_data;
};


int v3_init_exec_hooks (struct guest_info * core);
int v3_deinit_exec_hooks (struct guest_info * core);

int v3_hook_executable (struct guest_info * core, 
    const uchar_t * binfile,
    int (*handler)(struct guest_info * core, void * priv_data),
    void * priv_data);


#endif
