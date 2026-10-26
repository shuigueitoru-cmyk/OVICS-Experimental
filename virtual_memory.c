#include "virtual_memory.h"
struct vm_status vm_stats;
const char *vm_last_error="Not initialized";
extern unsigned char kernel_start[],kernel_end[],kernel_text_start[],kernel_ro_end[];
static pm_u32 *vm_directory;
static pm_u32 vm_dynamic_pages;
static int vm_error(const char *s) { vm_last_error=s;vm_stats.errors++;return 0; }
static void vm_flush(void) {
    if(vm_stats.enabled) {
        __asm__ volatile("mov %0, %%cr3"::"r"(vm_stats.directory):"memory");
        vm_stats.tlb_flushes++;
    }
}
static pm_u32 vm_structure_alloc(void) {
    pm_u32 a=pm_alloc();
    if(!a)return 0;
    if(!pm_vm_pin_structure(a)){pm_free(a);return 0;}
    pm_u32 *p=(pm_u32 *)a;
    for(pm_u32 i=0;i<1024;i++)p[i]=0;
    return a;
}
static pm_u32 *vm_table(pm_u32 va) {
    pm_u32 entry=vm_directory[va>>22];
    return (entry&VM_PRESENT) && !(entry&128)?(pm_u32 *)(entry&0xfffff000u):0;
}
int vm_translate(pm_u32 va,pm_u32 *physical,pm_u32 *flags) {
    if(!vm_directory)return 0;
    pm_u32 pde=vm_directory[va>>22];pm_u32 *table=vm_table(va);
    if(!table)return 0;
    pm_u32 pte=table[(va>>12)&1023];
    if(!(pte&VM_PRESENT))return 0;
    if(physical)*physical=(pte&0xfffff000u)|(va&4095);
    if(flags)*flags=VM_PRESENT|(pte&pde&(VM_WRITE|VM_USER))|(pte&(VM_PWT|VM_PCD));
    return 1;
}
int vm_is_mapped(pm_u32 va) { return vm_translate(va,0,0); }
static int vm_valid_api_va(pm_u32 va) {
    return !(va&4095) && va>=VM_API_BEGIN && va<VM_API_END;
}
int vm_map_page(pm_u32 va,pm_u32 physical,pm_u32 flags) {
    if(!vm_stats.enabled)return vm_error("Paging unavailable");
    if(!vm_valid_api_va(va) || (physical&4095) || (flags&~(VM_WRITE|VM_USER)))
        return vm_error("Invalid address or flags");
    pm_u32 *table=vm_table(va),new_table=0;
    if(table && (table[(va>>12)&1023]&VM_PRESENT))return vm_error("Already mapped (or protected)");
    if(!pm_owned(physical) || !pm_vm_hold(physical))return vm_error("Physical page not caller-owned or reference limit");
    if(!table) {
        new_table=vm_structure_alloc();
        if(!new_table){pm_vm_drop(physical);return vm_error("Out of physical pages");}
        table=(pm_u32 *)new_table;
    }
    table[(va>>12)&1023]=physical|VM_PRESENT|flags|VM_DYNAMIC;
    if(new_table) {
        vm_directory[va>>22]=new_table|VM_PRESENT|VM_WRITE|(flags&VM_USER);
        vm_stats.table_count++;
    } else if(flags&VM_USER)vm_directory[va>>22]|=VM_USER;
    vm_stats.mapped_pages++;vm_dynamic_pages++;vm_flush();vm_last_error="None";return 1;
}
int vm_unmap_page(pm_u32 va) {
    if(!vm_stats.enabled || !vm_valid_api_va(va))return vm_error("Invalid or protected virtual address");
    pm_u32 *table=vm_table(va);
    if(!table || !(table[(va>>12)&1023]&VM_PRESENT))return vm_error("Not mapped");
    pm_u32 entry=table[(va>>12)&1023];
    if((entry&VM_LOCKED) || !(entry&VM_DYNAMIC))return vm_error("Protected mapping");
    if(!pm_vm_drop(entry&0xfffff000u))return vm_error("Invalid frame ownership");
    table[(va>>12)&1023]=0;vm_stats.mapped_pages--;vm_dynamic_pages--;
    pm_u32 nonempty=0;
    for(pm_u32 i=0;i<1024;i++)if(table[i]&VM_PRESENT){nonempty=1;break;}
    pm_u32 release=0;
    if(!nonempty) {
        release=(pm_u32)table;vm_directory[va>>22]=0;vm_stats.table_count--;
    }
    /* Invalidate both cached translations and cached PDE permissions before reuse. */
    vm_flush();
    if(release && !pm_vm_release_structure(release))return vm_error("Page-table ownership corruption");
    vm_last_error="None";return 1;
}
static int vm_boot_map(pm_u32 physical,pm_u32 flags) {
    pm_u32 *table=vm_table(physical);
    if(!table) {
        pm_u32 a=vm_structure_alloc();if(!a)return 0;
        table=(pm_u32 *)a;vm_directory[physical>>22]=a|VM_PRESENT|VM_WRITE;
        vm_stats.table_count++;
    }
    pm_u32 *entry=&table[(physical>>12)&1023];
    if(!(*entry&VM_PRESENT))vm_stats.mapped_pages++;
    *entry=physical|VM_PRESENT|flags|VM_LOCKED;return 1;
}
/* Privileged device path: one permanent, supervisor-only, uncached framebuffer.
   Never admit RAM, boot metadata, or an existing identity/cache alias. The
   public page API still accepts only allocator-owned RAM. No BAR probing writes. */
static int vm_framebuffer_map(pm_u32 physical,pm_u32 bytes) {
    const pm_u32 va=0xE1000000u;
    if(!vm_stats.enabled || !bytes || (physical&4095) || physical<0x100000u ||
       bytes>0x400000u || (pm_u64)physical+bytes>((pm_u64)1<<32) || vm_directory[va>>22])
        return vm_error("Invalid or occupied framebuffer window");
    pm_u32 pages=(bytes+4095)>>12;
    for(pm_u32 i=0;i<pages;i++) {
        pm_u32 p=(physical>>12)+i;
        if(pm_state[p] || pm_direct_allowed(p))return vm_error("Framebuffer overlaps RAM or boot metadata");
    }
    pm_u32 a=vm_structure_alloc();if(!a)return vm_error("Framebuffer page table allocation failed");
    pm_u32 *table=(pm_u32 *)a;
    for(pm_u32 i=0;i<pages;i++)table[i]=(physical+i*4096)|VM_PRESENT|VM_WRITE|VM_PCD|VM_PWT|VM_LOCKED;
    vm_directory[va>>22]=a|VM_PRESENT|VM_WRITE;
    vm_stats.table_count++;vm_stats.mapped_pages+=pages;vm_flush();return 1;
}
static void vm_boot_rollback(void) {
    if(vm_directory) {
        for(pm_u32 i=0;i<1024;i++)if(vm_directory[i]&VM_PRESENT)pm_vm_release_structure(vm_directory[i]&0xfffff000u);
        pm_vm_release_structure((pm_u32)vm_directory);
    }
    vm_directory=0;vm_stats.directory=vm_stats.mapped_pages=vm_stats.table_count=0;
}
static void vm_legacy_mode(void) {
    /* CPUs with any of these CPUID features have CR4. Avoid CR4 on 386/486s
       without them; unsupported PAE cannot be enabled on such CPUs. */
    pm_u32 original,toggled,readback;
    __asm__ volatile("pushfl; popl %0":"=r"(original));
    toggled=original^(1u<<21);
    __asm__ volatile("pushl %0; popfl"::"r"(toggled):"cc","memory");
    __asm__ volatile("pushfl; popl %0":"=r"(readback));
    __asm__ volatile("pushl %0; popfl"::"r"(original):"cc","memory");
    if((readback^original)&(1u<<21)) {
        pm_u32 a,b,c,d;
        __asm__ volatile("cpuid":"=a"(a),"=b"(b),"=c"(c),"=d"(d):"a"(1),"c"(0));
        if(d&((1u<<3)|(1u<<6)|(1u<<13))) {
            pm_u32 cr4;__asm__ volatile("mov %%cr4, %0":"=r"(cr4));
            cr4&=~((1u<<4)|(1u<<5)|(1u<<7));
            __asm__ volatile("mov %0, %%cr4"::"r"(cr4):"memory");
        }
    }
}
int vm_initialize(void) {
    if(!pm_stats.ready || vm_directory)return vm_error("Missing PM or repeated initialization");
    pm_u32 cr0;__asm__ volatile("mov %%cr0, %0":"=r"(cr0));
    if(cr0&0x80000000u)return vm_error("Unexpected preexisting paging");
    vm_stats.directory=vm_structure_alloc();if(!vm_stats.directory)return vm_error("Directory allocation failed");
    vm_directory=(pm_u32 *)vm_stats.directory;
    for(pm_u32 p=0;p<PM_PAGES;p++)if(pm_direct_allowed(p)) {
        pm_u32 a=p<<12,flags=VM_WRITE;
        if(a>=(pm_u32)kernel_start && a<(pm_u32)kernel_ro_end)flags=0;
        if(!vm_boot_map(a,flags))goto failed;
    }
    if(!vm_boot_map(0xb8000,VM_WRITE|VM_PCD|VM_PWT))goto failed;
    vm_legacy_mode();
    __asm__ volatile("mov %0, %%cr3"::"r"(vm_stats.directory):"memory");
    cr0|=0x80010000u; /* PG and WP: supervisor writes must obey read-only PTEs. */
    __asm__ volatile("mov %0, %%cr0"::"r"(cr0):"memory");
    __asm__ volatile("mov %%cr0, %0":"=r"(cr0));
    if((cr0&0x80010000u)!=0x80010000u) {
        cr0&=~0x80000000u;__asm__ volatile("mov %0, %%cr0"::"r"(cr0):"memory");
        goto failed;
    }
    vm_stats.enabled=1;vm_stats.boot_status=1;vm_last_error="None";return 1;
failed:
    vm_boot_rollback();return vm_error("Bootstrap allocation or PG/WP failed");
}
void vm_print_info(void) {
    pm_u32 cr0;__asm__ volatile("mov %%cr0, %0":"=r"(cr0));
    print("Paging Enabled: ");print(cr0&0x80000000u?"Yes\n":"No\n");
    print("Write Protect: ");print(cr0&0x10000u?"Yes\n":"No\n");
    print("Page Directory Address: 0x");print_hex(vm_stats.directory,8);print("\n");
    print("Mapped Pages: ");print_number(vm_stats.mapped_pages);print("\n");
    print("Page Table Count: ");print_number(vm_stats.table_count);print("\n");
    print("Page Fault Count: ");print_number(vm_stats.fault_count);print("\n");
    print("TLB Flushes: ");print_number(vm_stats.tlb_flushes);print("\n");
    print("Memory Mapping Status: ");print(vm_stats.boot_status?"Ready\n":"Unavailable\n");
}
/* Diagnostic fields are read by the external QMP test; pause defaults to zero.
   The debug-only pause is not a command and cannot cause a fault. */
struct vm_test_record {
    pm_u32 before_used,before_mapped,before_tables,physical_a,physical_b,alias;
    pm_u32 initial_read,remap_read,offset_translation,user_flags,checks,passed,failure;
    pm_u32 after_used,after_mapped,after_tables;
};
volatile struct vm_test_record vm_test_record;
volatile pm_u32 vm_test_pause,vm_test_stage;
#define VM_CHECK(c) do { if(!(c)){vm_test_record.failure=__LINE__;goto cleanup;} vm_test_record.checks++; } while(0)
int vm_selftest(void) {
    if(!vm_stats.enabled || vm_dynamic_pages || pm_stats.used!=pm_vm_structures || pm_stats.free<3)return 0;
    volatile pm_u32 *record=(volatile pm_u32 *)&vm_test_record;
    for(pm_u32 i=0;i<sizeof(vm_test_record)/4;i++)record[i]=0;
    vm_test_stage=0;
    pm_u32 used=pm_stats.used,mapped=vm_stats.mapped_pages,tables=vm_stats.table_count;
    pm_u32 a=pm_alloc(),b=pm_alloc(),v=0xd0000000u,pa=0,flags=0;
    vm_test_record.before_used=used;vm_test_record.before_mapped=mapped;vm_test_record.before_tables=tables;
    vm_test_record.physical_a=a;vm_test_record.physical_b=b;vm_test_record.alias=v;
    VM_CHECK(a && b && a!=b);
    *(volatile pm_u32 *)a=0x11223344;*(volatile pm_u32 *)b=0xa1b2c3d4;
    VM_CHECK(vm_map_page(v,a,VM_WRITE));
    *(volatile pm_u32 *)v=0x12345678;
    vm_test_record.initial_read=*(volatile pm_u32 *)v;
    VM_CHECK(*(volatile pm_u32 *)a==0x12345678);
    VM_CHECK(vm_translate(v+123,&pa,&flags) && pa==a+123 && (flags&VM_WRITE));
    vm_test_record.offset_translation=pa;vm_test_stage=1;
    while(vm_test_pause)__asm__ volatile("nop":::"memory");
    VM_CHECK(!vm_map_page(v,b,VM_WRITE) && *(volatile pm_u32 *)v==0x12345678);
    VM_CHECK(!pm_free(a));
    VM_CHECK(vm_map_page(v+4096,a,0));
    VM_CHECK(vm_translate(v+4096,&pa,&flags) && !(flags&(VM_WRITE|VM_USER)));
    VM_CHECK(*(volatile pm_u32 *)(v+4096)==0x12345678);
    VM_CHECK(!vm_map_page(v+8192,vm_stats.directory,VM_WRITE));
    VM_CHECK(!pm_free(vm_stats.directory));
    VM_CHECK(!vm_unmap_page((pm_u32)kernel_start) && !vm_unmap_page(vm_stats.directory));
    VM_CHECK(!vm_map_page(v+1,b,VM_WRITE) && !vm_map_page(v+8192,b+1,VM_WRITE));
    VM_CHECK(!vm_map_page(0,b,VM_WRITE) && !vm_map_page(v+8192,0x100000,VM_WRITE));
    VM_CHECK(!vm_map_page(v+8192,b,8) && !vm_unmap_page(v+12288));
    VM_CHECK(vm_unmap_page(v) && !vm_is_mapped(v) && !pm_free(a));
    VM_CHECK(vm_map_page(v,b,VM_WRITE));
    vm_test_record.remap_read=*(volatile pm_u32 *)v;
    VM_CHECK(vm_test_record.remap_read==0xa1b2c3d4);
    *(volatile pm_u32 *)v=0x55667788;
    VM_CHECK(*(volatile pm_u32 *)b==0x55667788 && *(volatile pm_u32 *)a==0x12345678);
    VM_CHECK(vm_map_page(v+8192,b,VM_WRITE|VM_USER));
    VM_CHECK(vm_translate(v+8192,&pa,&flags) && (flags&(VM_USER|VM_WRITE))==(VM_USER|VM_WRITE));
    vm_test_record.user_flags=flags;
cleanup:
    for(pm_u32 i=0;i<3;i++)if(vm_is_mapped(v+i*4096))vm_unmap_page(v+i*4096);
    if(a && pm_owned(a))pm_free(a);if(b && pm_owned(b))pm_free(b);
    vm_test_record.after_used=pm_stats.used;vm_test_record.after_mapped=vm_stats.mapped_pages;vm_test_record.after_tables=vm_stats.table_count;
    int ok=!vm_test_record.failure && pm_stats.used==used && vm_stats.mapped_pages==mapped && vm_stats.table_count==tables;
    /* Force new-PT allocation failure with real physical exhaustion, then restore. */
    if(ok) {
        pm_u32 original=pm_stats.free,first=0,count=0,x;
        while((x=pm_alloc())!=0){if(!first)first=x;count++;}
        ok=count==original && first && !vm_map_page(v,first,VM_WRITE) && !pm_vm_refs[first>>12] && vm_stats.table_count==tables;
        for(pm_u32 p=0;p<PM_PAGES;p++)if(pm_state[p]==2)if(!pm_free(p<<12))ok=0;
        if(pm_stats.used!=used || pm_stats.free!=original)ok=0;
    }
    vm_test_record.passed=ok;vm_test_stage=2;return ok;
}
#undef VM_CHECK
#ifndef OVICS_VM_FAULT_TEST
#define OVICS_VM_FAULT_TEST 0
#endif
#if OVICS_VM_FAULT_TEST
static void vm_fault_probe(void) {
    const pm_u32 absent=0xe0000000u,alias=0xd0000000u;
    (void)absent;(void)alias;
#if OVICS_VM_FAULT_TEST == 1
    vm_fault_probe_readback=*(volatile pm_u32 *)absent;
#elif OVICS_VM_FAULT_TEST == 2
    pm_u32 p=pm_alloc();vm_fault_probe_physical=p;
    *(volatile pm_u32 *)p=0x11223344;
    if(!vm_map_page(alias,p,0)){print("Fault fixture failed\n");exception_stop();}
    vm_fault_probe_readback=*(volatile pm_u32 *)alias;
    *(volatile pm_u32 *)alias=0xdeadbeef;
#elif OVICS_VM_FAULT_TEST == 3
    *(volatile pm_u32 *)absent=0xdeadbeef;
#elif OVICS_VM_FAULT_TEST == 4
    __asm__ volatile("movl 0, %%eax":"=a"(vm_fault_probe_readback)::"memory");
#elif OVICS_VM_FAULT_TEST == 5
    vm_fault_probe_physical=(pm_u32)kernel_text_start;
    vm_fault_probe_readback=*(volatile unsigned char *)kernel_text_start;
    *(volatile unsigned char *)kernel_text_start=(unsigned char)(vm_fault_probe_readback^0xff);
#endif
    print("Fault fixture unexpectedly returned\n");exception_stop();
}
#endif
