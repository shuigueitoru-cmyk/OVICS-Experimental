#ifndef OVICS_VM_H
#define OVICS_VM_H
#include "memory.h"
#define VM_PRESENT 1u
#define VM_WRITE 2u
#define VM_USER 4u
#define VM_PWT 8u
#define VM_PCD 16u
#define VM_LOCKED 512u
#define VM_DYNAMIC 1024u
#define VM_API_BEGIN 0xC0000000u
#define VM_API_END 0xFFC00000u
struct vm_status {
 pm_u32 enabled, directory, mapped_pages, table_count;
 pm_u32 fault_count, errors, tlb_flushes, boot_status;
};
extern struct vm_status vm_stats;
extern const char *vm_last_error;
int vm_initialize(void);
int vm_map_page(pm_u32 virtual_address, pm_u32 physical_address, pm_u32 flags);
int vm_unmap_page(pm_u32 virtual_address);
int vm_translate(pm_u32 virtual_address, pm_u32 *physical_address, pm_u32 *flags);
int vm_is_mapped(pm_u32 virtual_address);
int vm_selftest(void);
void vm_print_info(void);
void exceptions_initialize(void);
#endif
