
static inline int activate_shadow_pt_64(struct guest_info * info) {
  struct cr3_64 * shadow_cr3 = (struct cr3_64 *)&(info->ctrl_regs.cr3);
  struct cr3_64 * guest_cr3 = (struct cr3_64 *)&(info->shdw_pg_state.guest_cr3);
  struct shadow_page_data * shadow_pt = create_new_shadow_pt(info);
  addr_t shadow_pt_addr = shadow_pt->page_pa;

  // Because this is a new CR3 load the allocated page is the new CR3 value
  shadow_pt->cr3 = shadow_pt->page_pa;

  PrintDebug("Top level Shadow page pa=%p\n", (void *)shadow_pt_addr);

  shadow_cr3->pml4t_base_addr = PAGE_BASE_ADDR_4KB(shadow_pt_addr);
  PrintDebug("Creating new 64 bit shadow page table %p\n", (void *)BASE_TO_PAGE_ADDR(shadow_cr3->pml4t_base_addr));

  
  shadow_cr3->pwt = guest_cr3->pwt;
  shadow_cr3->pcd = guest_cr3->pcd;

  return 0;
}






/* 
 * *
 * * 
 * * 64 bit Page table fault handlers
 * *
 * *
 */

static int handle_2MB_shadow_pagefault_64(struct guest_info * info, addr_t fault_addr, pf_error_t error_code,
					  pte64_t * shadow_pt, pde64_2MB_t * large_guest_pde);

static int handle_pte_shadow_pagefault_64(struct guest_info * info, addr_t fault_addr, pf_error_t error_code,
					  pte64_t * shadow_pt, pte64_t * guest_pt);

static int handle_pde_shadow_pagefault_64(struct guest_info * info, addr_t fault_addr, pf_error_t error_code,
					  pde64_t * shadow_pd, pde64_t * guest_pd);

static int handle_pdpe_shadow_pagefault_64(struct guest_info * info, addr_t fault_addr, pf_error_t error_code,
					   pdpe64_t * shadow_pdp, pdpe64_t * guest_pdp);


static inline int handle_shadow_pagefault_64(struct guest_info * info, addr_t fault_addr, pf_error_t error_code) {
  pml4e64_t * guest_pml = NULL;
  pml4e64_t * shadow_pml = CR3_TO_PML4E64_VA(info->ctrl_regs.cr3);
  addr_t guest_cr3 = CR3_TO_PML4E64_PA(info->shdw_pg_state.guest_cr3);
  pt_access_status_t guest_pml4e_access;
  pt_access_status_t shadow_pml4e_access;
  pml4e64_t * guest_pml4e = NULL;
  pml4e64_t * shadow_pml4e = (pml4e64_t *)&(shadow_pml[PML4E64_INDEX(fault_addr)]);

  PrintDebug("64 bit Shadow page fault handler: %p\n", (void *)fault_addr);
  PrintDebug("Handling PML fault\n");

  if (guest_pa_to_host_va(info, guest_cr3, (addr_t*)&guest_pml) == -1) {
    PrintError("Invalid Guest PML4E Address: 0x%p\n",  (void *)guest_cr3);
    return -1;
  } 

  guest_pml4e = (pml4e64_t *)&(guest_pml[PML4E64_INDEX(fault_addr)]);

  PrintDebug("Checking Guest %p\n", (void *)guest_pml);
  // Check the guest page permissions
  guest_pml4e_access = v3_can_access_pml4e64(guest_pml, fault_addr, error_code);

  PrintDebug("Checking shadow %p\n", (void *)shadow_pml);
  // Check the shadow page permissions
  shadow_pml4e_access = v3_can_access_pml4e64(shadow_pml, fault_addr, error_code);
  
  /* Was the page fault caused by the Guest's page tables? */
  if (is_guest_pf(guest_pml4e_access, shadow_pml4e_access) == 1) {
    PrintDebug("Injecting PML4E pf to guest: (guest access error=%d) (pf error code=%d)\n", 
	       *(uint_t *)&guest_pml4e_access, *(uint_t *)&error_code);
    inject_guest_pf(info, fault_addr, error_code);
    return 0;
  }

  if (shadow_pml4e_access == PT_ACCESS_USER_ERROR) {
    //
    // PML4 Entry marked non-user
    //      
    PrintDebug("Shadow Paging User access error (shadow_pml4e_access=0x%x, guest_pml4e_access=0x%x)\n", 
	       shadow_pml4e_access, guest_pml4e_access);
    inject_guest_pf(info, fault_addr, error_code);
    return 0;
  } else if ((shadow_pml4e_access != PT_ACCESS_NOT_PRESENT) &&
	     (shadow_pml4e_access != PT_ACCESS_OK)) {
    // inject page fault in guest
    inject_guest_pf(info, fault_addr, error_code);
    PrintDebug("Unknown Error occurred (shadow_pde_access=%d)\n", shadow_pml4e_access);
    PrintDebug("Manual Says to inject page fault into guest\n");
    return 0;
  }


  pdpe64_t * shadow_pdp = NULL;
  pdpe64_t * guest_pdp = NULL;

  // Get the next shadow page level, allocate if not present

  if (shadow_pml4e_access == PT_ACCESS_NOT_PRESENT) {
    struct shadow_page_data * shdw_page =  create_new_shadow_pt(info);
    shadow_pdp = (pdpe64_t *)V3_VAddr((void *)shdw_page->page_pa);


    shadow_pml4e->present = 1;
    shadow_pml4e->user_page = guest_pml4e->user_page;
    shadow_pml4e->writable = guest_pml4e->writable;
    
    // VMM Specific options
    shadow_pml4e->write_through = 0;
    shadow_pml4e->cache_disable = 0;
    //
    
    guest_pml4e->accessed = 1;
    
    shadow_pml4e->pdp_base_addr = PAGE_BASE_ADDR(shdw_page->page_pa);
  } else {
    shadow_pdp = (pdpe64_t *)V3_VAddr((void *)(addr_t)BASE_TO_PAGE_ADDR(shadow_pml4e->pdp_base_addr));
  }

  // Continue processing at the next level

  if (guest_pa_to_host_va(info, BASE_TO_PAGE_ADDR(guest_pml4e->pdp_base_addr), (addr_t *)&guest_pdp) == -1) {
    // Machine check the guest
    PrintDebug("Invalid Guest PDP Address: 0x%p\n", (void *)BASE_TO_PAGE_ADDR(guest_pml4e->pdp_base_addr));
    v3_raise_exception(info, MC_EXCEPTION);
    return 0;
  }
  
  if (handle_pdpe_shadow_pagefault_64(info, fault_addr, error_code, shadow_pdp, guest_pdp) == -1) {
    PrintError("Error handling Page fault caused by PDPE\n");
    return -1;
  }

  return 0;
}



// For now we are not going to handle 1 Gigabyte pages
static int handle_pdpe_shadow_pagefault_64(struct guest_info * info, addr_t fault_addr, pf_error_t error_code,
					   pdpe64_t * shadow_pdp, pdpe64_t * guest_pdp) {
  pt_access_status_t guest_pdpe_access;
  pt_access_status_t shadow_pdpe_access;
  pdpe64_t * guest_pdpe = (pdpe64_t *)&(guest_pdp[PDPE64_INDEX(fault_addr)]);
  pdpe64_t * shadow_pdpe = (pdpe64_t *)&(shadow_pdp[PDPE64_INDEX(fault_addr)]);
 
  PrintDebug("Handling PDP fault\n");

  // Check the guest page permissions
  guest_pdpe_access = v3_can_access_pdpe64(guest_pdp, fault_addr, error_code);

  // Check the shadow page permissions
  shadow_pdpe_access = v3_can_access_pdpe64(shadow_pdp, fault_addr, error_code);
  
  /* Was the page fault caused by the Guest's page tables? */
  if (is_guest_pf(guest_pdpe_access, shadow_pdpe_access) == 1) {
    PrintDebug("Injecting PDPE pf to guest: (guest access error=%d) (pf error code=%d)\n", 
	       *(uint_t *)&guest_pdpe_access, *(uint_t *)&error_code);
    inject_guest_pf(info, fault_addr, error_code);
    return 0;
  }

  if (shadow_pdpe_access == PT_ACCESS_USER_ERROR) {
    //
    // PML4 Entry marked non-user
    //      
    PrintDebug("Shadow Paging User access error (shadow_pdpe_access=0x%x, guest_pdpe_access=0x%x)\n", 
	       shadow_pdpe_access, guest_pdpe_access);
    inject_guest_pf(info, fault_addr, error_code);
    return 0;
  } else if ((shadow_pdpe_access != PT_ACCESS_NOT_PRESENT) &&
	     (shadow_pdpe_access != PT_ACCESS_OK)) {
    // inject page fault in guest
    inject_guest_pf(info, fault_addr, error_code);
    PrintDebug("Unknown Error occurred (shadow_pde_access=%d)\n", shadow_pdpe_access);
    PrintDebug("Manual Says to inject page fault into guest\n");
    return 0;
  }


  pde64_t * shadow_pd = NULL;
  pde64_t * guest_pd = NULL;

  // Get the next shadow page level, allocate if not present

  if (shadow_pdpe_access == PT_ACCESS_NOT_PRESENT) {
    struct shadow_page_data * shdw_page = create_new_shadow_pt(info);
    shadow_pd = (pde64_t *)V3_VAddr((void *)shdw_page->page_pa);


    shadow_pdpe->present = 1;
    shadow_pdpe->user_page = guest_pdpe->user_page;
    shadow_pdpe->writable = guest_pdpe->writable;
    
    // VMM Specific options
    shadow_pdpe->write_through = 0;
    shadow_pdpe->cache_disable = 0;
    //
    
    guest_pdpe->accessed = 1;
    
    shadow_pdpe->pd_base_addr = PAGE_BASE_ADDR(shdw_page->page_pa);
  } else {
    shadow_pd = (pde64_t *)V3_VAddr((void *)(addr_t)BASE_TO_PAGE_ADDR(shadow_pdpe->pd_base_addr));
  }

  // Continue processing at the next level

  if (guest_pa_to_host_va(info, BASE_TO_PAGE_ADDR(guest_pdpe->pd_base_addr), (addr_t *)&guest_pd) == -1) {
    // Machine check the guest
    PrintDebug("Invalid Guest PTE Address: 0x%p\n", (void *)BASE_TO_PAGE_ADDR(guest_pdpe->pd_base_addr));
    v3_raise_exception(info, MC_EXCEPTION);
    return 0;
  }
  
  if (handle_pde_shadow_pagefault_64(info, fault_addr, error_code, shadow_pd, guest_pd) == -1) {
    PrintError("Error handling Page fault caused by PDE\n");
    return -1;
  }

  return 0;
}


static int handle_pde_shadow_pagefault_64(struct guest_info * info, addr_t fault_addr, pf_error_t error_code,
					  pde64_t * shadow_pd, pde64_t * guest_pd) {
  pt_access_status_t guest_pde_access;
  pt_access_status_t shadow_pde_access;
  pde64_t * guest_pde = (pde64_t *)&(guest_pd[PDE64_INDEX(fault_addr)]);
  pde64_t * shadow_pde = (pde64_t *)&(shadow_pd[PDE64_INDEX(fault_addr)]);

  PrintDebug("Handling PDE fault\n");
 
  // Check the guest page permissions
  guest_pde_access = v3_can_access_pde64(guest_pd, fault_addr, error_code);

  // Check the shadow page permissions
  shadow_pde_access = v3_can_access_pde64(shadow_pd, fault_addr, error_code);
  
  /* Was the page fault caused by the Guest's page tables? */
  if (is_guest_pf(guest_pde_access, shadow_pde_access) == 1) {
    PrintDebug("Injecting PDE pf to guest: (guest access error=%d) (pf error code=%d)\n", 
	       *(uint_t *)&guest_pde_access, *(uint_t *)&error_code);
    inject_guest_pf(info, fault_addr, error_code);
    return 0;
  }

  if (shadow_pde_access == PT_ACCESS_USER_ERROR) {
    //
    // PDE Entry marked non-user
    //      
    PrintDebug("Shadow Paging User access error (shadow_pdpe_access=0x%x, guest_pdpe_access=0x%x)\n", 
	       shadow_pde_access, guest_pde_access);
    inject_guest_pf(info, fault_addr, error_code);
    return 0;

  } else if ((shadow_pde_access == PT_ACCESS_WRITE_ERROR) && 
	     (guest_pde->large_page == 1)) {

    ((pde64_2MB_t *)guest_pde)->dirty = 1;
    shadow_pde->writable = guest_pde->writable;

    PrintDebug("Returning due to large page Write Error\n");
    PrintHostPageTree(info->cpu_mode, fault_addr, info->ctrl_regs.cr3);

    return 0;
  } else if ((shadow_pde_access != PT_ACCESS_NOT_PRESENT) &&
	     (shadow_pde_access != PT_ACCESS_OK)) {
    // inject page fault in guest
    inject_guest_pf(info, fault_addr, error_code);
    PrintDebug("Unknown Error occurred (shadow_pde_access=%d)\n", shadow_pde_access);
    PrintDebug("Manual Says to inject page fault into guest\n");
    return 0;
  }


  pte64_t * shadow_pt = NULL;
  pte64_t * guest_pt = NULL;

  // Get the next shadow page level, allocate if not present

  if (shadow_pde_access == PT_ACCESS_NOT_PRESENT) {
    struct shadow_page_data * shdw_page = create_new_shadow_pt(info);
    shadow_pt = (pte64_t *)V3_VAddr((void *)shdw_page->page_pa);

    PrintDebug("Creating new shadow PT: %p\n", shadow_pt);

    shadow_pde->present = 1;
    shadow_pde->user_page = guest_pde->user_page;


    if (guest_pde->large_page == 0) {
      shadow_pde->writable = guest_pde->writable;
    } else {
      if (error_code.write) {
	shadow_pde->writable = guest_pde->writable;
	((pde64_2MB_t *)guest_pde)->dirty = 1;	
      } else {
	shadow_pde->writable = 0;
	((pde64_2MB_t *)guest_pde)->dirty = 0;
      }
    }
    
    // VMM Specific options
    shadow_pde->write_through = 0;
    shadow_pde->cache_disable = 0;
    shadow_pde->global_page = 0;
    //
    
    guest_pde->accessed = 1;
    
    shadow_pde->pt_base_addr = PAGE_BASE_ADDR(shdw_page->page_pa);
  } else {
    shadow_pt = (pte64_t *)V3_VAddr((void *)BASE_TO_PAGE_ADDR(shadow_pde->pt_base_addr));
  }

  // Continue processing at the next level
  if (guest_pde->large_page == 0) {
    if (guest_pa_to_host_va(info, BASE_TO_PAGE_ADDR(guest_pde->pt_base_addr), (addr_t *)&guest_pt) == -1) {
      // Machine check the guest
      PrintDebug("Invalid Guest PTE Address: 0x%p\n", (void *)BASE_TO_PAGE_ADDR(guest_pde->pt_base_addr));
      v3_raise_exception(info, MC_EXCEPTION);
      return 0;
    }
    
    if (handle_pte_shadow_pagefault_64(info, fault_addr, error_code, shadow_pt, guest_pt) == -1) {
      PrintError("Error handling Page fault caused by PDE\n");
      return -1;
    }
  } else {
    if (handle_2MB_shadow_pagefault_64(info, fault_addr, error_code, shadow_pt, (pde64_2MB_t *)guest_pde) == -1) {
      PrintError("Error handling large pagefault\n");
      return -1;
    } 
  }

  return 0;
}


static int handle_pte_shadow_pagefault_64(struct guest_info * info, addr_t fault_addr, pf_error_t error_code,
					  pte64_t * shadow_pt, pte64_t * guest_pt) {
  pt_access_status_t guest_pte_access;
  pt_access_status_t shadow_pte_access;
  pte64_t * guest_pte = (pte64_t *)&(guest_pt[PTE64_INDEX(fault_addr)]);;
  pte64_t * shadow_pte = (pte64_t *)&(shadow_pt[PTE64_INDEX(fault_addr)]);
  addr_t guest_pa = BASE_TO_PAGE_ADDR((addr_t)(guest_pte->page_base_addr)) +  PAGE_OFFSET(fault_addr);
  //  struct shadow_page_state * state = &(info->shdw_pg_state);

  PrintDebug("Handling PTE fault\n");

  struct v3_shadow_region * shdw_reg =  v3_get_shadow_region(info, guest_pa);



  if ((shdw_reg == NULL) || 
      (shdw_reg->host_type == SHDW_REGION_INVALID)) {
    // Inject a machine check in the guest
    PrintDebug("Invalid Guest Address in page table (0x%p)\n", (void *)guest_pa);
    v3_raise_exception(info, MC_EXCEPTION);
    return 0;
  }

  // Check the guest page permissions
  guest_pte_access = v3_can_access_pte64(guest_pt, fault_addr, error_code);

  // Check the shadow page permissions
  shadow_pte_access = v3_can_access_pte64(shadow_pt, fault_addr, error_code);

  /* Was the page fault caused by the Guest's page tables? */
  if (is_guest_pf(guest_pte_access, shadow_pte_access) == 1) {
    PrintDebug("Access error injecting pf to guest (guest access error=%d) (pf error code=%d)\n", 
	       guest_pte_access, *(uint_t*)&error_code);    
    inject_guest_pf(info, fault_addr, error_code);
    return 0; 
  }

 
  if (shadow_pte_access == PT_ACCESS_OK) {
    // Inconsistent state...
    // Guest Re-Entry will flush page tables and everything should now work
    PrintDebug("Inconsistent state... Guest re-entry should flush tlb\n");
    return 0;
  }


  if (shadow_pte_access == PT_ACCESS_NOT_PRESENT) {
    // Page Table Entry Not Present
    PrintDebug("guest_pa =%p\n", (void *)guest_pa);

    if ((shdw_reg->host_type == SHDW_REGION_ALLOCATED) ||
	(shdw_reg->host_type == SHDW_REGION_WRITE_HOOK)) {
      addr_t shadow_pa = v3_get_shadow_addr(shdw_reg, guest_pa);
      
      shadow_pte->page_base_addr = PAGE_BASE_ADDR(shadow_pa);
      
      shadow_pte->present = guest_pte->present;
      shadow_pte->user_page = guest_pte->user_page;
      
      //set according to VMM policy
      shadow_pte->write_through = 0;
      shadow_pte->cache_disable = 0;
      shadow_pte->global_page = 0;
      //
      
      guest_pte->accessed = 1;
      
      if (guest_pte->dirty == 1) {
	shadow_pte->writable = guest_pte->writable;
      } else if ((guest_pte->dirty == 0) && (error_code.write == 1)) {
	shadow_pte->writable = guest_pte->writable;
	guest_pte->dirty = 1;
      } else if ((guest_pte->dirty == 0) && (error_code.write == 0)) {
	shadow_pte->writable = 0;
      }

      // dirty flag has been set, check if its in the cache
/*       if (find_pte_map(state->cached_ptes, PAGE_ADDR(guest_pa)) != NULL) { */
/* 	if (error_code.write == 1) { */
/* 	  state->cached_cr3 = 0; */
/* 	  shadow_pte->writable = guest_pte->writable; */
/* 	} else { */
/* 	  shadow_pte->writable = 0; */
/* 	} */
/*       } */

      // Write hooks trump all, and are set Read Only
      if (shdw_reg->host_type == SHDW_REGION_WRITE_HOOK) {
	shadow_pte->writable = 0;
      }

    } else {
      // Page fault handled by hook functions

      if (v3_handle_mem_full_hook(info, fault_addr, guest_pa, shdw_reg, error_code) == -1) {
	PrintError("Special Page fault handler returned error for address: %p\n",  (void *)fault_addr);
	return -1;
      }
    }
  } else if (shadow_pte_access == PT_ACCESS_WRITE_ERROR) {
    guest_pte->dirty = 1;

    if (shdw_reg->host_type == SHDW_REGION_WRITE_HOOK) {
      if (v3_handle_mem_wr_hook(info, fault_addr, guest_pa, shdw_reg, error_code) == -1) {
	PrintError("Special Page fault handler returned error for address: %p\n",  (void *)fault_addr);
	return -1;
      }
    } else {
      PrintDebug("Shadow PTE Write Error\n");
      shadow_pte->writable = guest_pte->writable;
    }

/*     if (find_pte_map(state->cached_ptes, PAGE_ADDR(guest_pa)) != NULL) { */
/*       struct shadow_page_state * state = &(info->shdw_pg_state); */
/*       PrintDebug("Write operation on Guest PAge Table Page\n"); */
/*       state->cached_cr3 = 0; */
/*     } */

    return 0;

  } else {
    // Inject page fault into the guest	
    inject_guest_pf(info, fault_addr, error_code);
    PrintError("PTE Page fault fell through... Not sure if this should ever happen\n");
    PrintError("Manual Says to inject page fault into guest\n");
    return -1;
  }

  return 0;
}



static int handle_2MB_shadow_pagefault_64(struct guest_info * info, 
					  addr_t fault_addr, pf_error_t error_code, 
					  pte64_t * shadow_pt, pde64_2MB_t * large_guest_pde) 
{
  pt_access_status_t shadow_pte_access = v3_can_access_pte64(shadow_pt, fault_addr, error_code);
  pte64_t * shadow_pte = (pte64_t *)&(shadow_pt[PTE64_INDEX(fault_addr)]);
  addr_t guest_fault_pa = BASE_TO_PAGE_ADDR_2MB(large_guest_pde->page_base_addr) + PAGE_OFFSET_2MB(fault_addr);
  //  struct shadow_page_state * state = &(info->shdw_pg_state);

  PrintDebug("Handling 2MB fault (guest_fault_pa=%p) (error_code=%x)\n", (void *)guest_fault_pa, *(uint_t*)&error_code);
  PrintDebug("ShadowPT=%p, LargeGuestPDE=%p\n", shadow_pt, large_guest_pde);

  struct v3_shadow_region * shdw_reg = v3_get_shadow_region(info, guest_fault_pa);

 
  if ((shdw_reg == NULL) || 
      (shdw_reg->host_type == SHDW_REGION_INVALID)) {
    // Inject a machine check in the guest
    PrintDebug("Invalid Guest Address in page table (0x%p)\n", (void *)guest_fault_pa);
    v3_raise_exception(info, MC_EXCEPTION);
    return -1;
  }

  if (shadow_pte_access == PT_ACCESS_OK) {
    // Inconsistent state...
    // Guest Re-Entry will flush tables and everything should now workd
    PrintDebug("Inconsistent state... Guest re-entry should flush tlb\n");
    PrintHostPageTree(info->cpu_mode, fault_addr, info->ctrl_regs.cr3);
    return 0;
  }

  
  if (shadow_pte_access == PT_ACCESS_NOT_PRESENT) {
    // Get the guest physical address of the fault

    if ((shdw_reg->host_type == SHDW_REGION_ALLOCATED) || 
	(shdw_reg->host_type == SHDW_REGION_WRITE_HOOK)) {
      addr_t shadow_pa = v3_get_shadow_addr(shdw_reg, guest_fault_pa);

      PrintDebug("Shadow PA=%p, ShadowPTE=%p\n", (void *)shadow_pa, (void *)shadow_pte);

      shadow_pte->page_base_addr = PAGE_BASE_ADDR(shadow_pa);
      PrintDebug("Test1\n");

      shadow_pte->present = 1;

      /* We are assuming that the PDE entry has precedence
       * so the Shadow PDE will mirror the guest PDE settings, 
       * and we don't have to worry about them here
       * Allow everything
       */
      shadow_pte->user_page = 1;



/*       if (find_pte_map(state->cached_ptes, PAGE_ADDR(guest_fault_pa)) != NULL) { */
/* 	// Check if the entry is a page table... */
/* 	PrintDebug("Marking page as Guest Page Table (large page)\n"); */
/* 	shadow_pte->writable = 0; */
/*       } else */ if (shdw_reg->host_type == SHDW_REGION_WRITE_HOOK) {
	shadow_pte->writable = 0;
      } else {
	shadow_pte->writable = 1;
      }

      //set according to VMM policy
      shadow_pte->write_through = 0;
      shadow_pte->cache_disable = 0;
      shadow_pte->global_page = 0;
      //
      
    } else {
      // Handle hooked pages as well as other special pages
      //      if (handle_special_page_fault(info, fault_addr, guest_fault_pa, error_code) == -1) {

      if (v3_handle_mem_full_hook(info, fault_addr, guest_fault_pa, shdw_reg, error_code) == -1) {
	PrintError("Special Page Fault handler returned error for address: %p\n", (void *)fault_addr);
	return -1;
      }
    }
  } else if (shadow_pte_access == PT_ACCESS_WRITE_ERROR) {

    if (shdw_reg->host_type == SHDW_REGION_WRITE_HOOK) {

      if (v3_handle_mem_wr_hook(info, fault_addr, guest_fault_pa, shdw_reg, error_code) == -1) {
	PrintError("Special Page Fault handler returned error for address: %p\n", (void *)fault_addr);
	return -1;
      }
    }


/*     if (find_pte_map(state->cached_ptes, PAGE_ADDR(guest_fault_pa)) != NULL) { */
/*       struct shadow_page_state * state = &(info->shdw_pg_state); */
/*       PrintDebug("Write operation on Guest PAge Table Page (large page)\n"); */
/*       state->cached_cr3 = 0; */
/*       shadow_pte->writable = 1; */
/*     } */

  } else {
    PrintError("Error in large page fault handler...\n");
    PrintError("This case should have been handled at the top level handler\n");
    return -1;
  }

  PrintHostPageTree(info->cpu_mode, fault_addr, info->ctrl_regs.cr3);
  PrintDebug("Returning from large page fault handler\n");
  return 0;
}





static inline int handle_shadow_invlpg_64(struct guest_info * info, addr_t vaddr) {
  PrintError("64 bit shadow paging not implemented\n");
  return -1;
}
