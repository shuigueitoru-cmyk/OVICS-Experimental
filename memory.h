#ifndef OVICS_MEMORY_H
#define OVICS_MEMORY_H
typedef unsigned int pm_u32;
typedef unsigned long long pm_u64;
#define PM_PAGES 1048576u
#define PM_PAGE_SIZE 4096u
struct pm_stats { pm_u32 ram_kib, available, used, free, errors, ready; };
extern struct pm_stats pm_stats;
int pm_init(pm_u32 magic, pm_u32 info, pm_u32 kernel_begin, pm_u32 kernel_finish);
pm_u32 pm_alloc(void);
int pm_free(pm_u32 address);
int pm_selftest(void);
extern pm_u32 pm_vm_structures;
int pm_direct_allowed(pm_u32 page);
int pm_owned(pm_u32 address);
int pm_vm_pin_structure(pm_u32 address);
int pm_vm_release_structure(pm_u32 address);
int pm_vm_hold(pm_u32 address);
int pm_vm_drop(pm_u32 address);
#endif
