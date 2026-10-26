#include "memory.h"
/* 0 reserved/unmapped; 1 free; 2 owned by allocator. Static BSS is reserved. */
static unsigned char pm_state[PM_PAGES];
struct pm_stats pm_stats;
static pm_u32 pm_hint;
/* Permanent identity-map plan and dynamic alias references. */
static unsigned char pm_direct[PM_PAGES/8];
static unsigned char pm_vm_refs[PM_PAGES];
pm_u32 pm_vm_structures;
pm_u32 pm_boot_info,pm_boot_map,pm_boot_map_length;
static void pm_direct_region(pm_u64 a, pm_u64 e) {
    const pm_u64 limit=(pm_u64)1<<32;
    if(a>=limit || e<=a)return;
    if(e>limit)e=limit;
    for(pm_u32 p=(pm_u32)(a>>12);p<(pm_u32)((e+4095)>>12);p++)
        pm_direct[p>>3]|=(unsigned char)(1u<<(p&7));
}
int pm_direct_allowed(pm_u32 page) {
    return page<PM_PAGES && (pm_direct[page>>3] & (1u<<(page&7)))!=0;
}
int pm_owned(pm_u32 address) {
    return pm_stats.ready && !(address&4095) && pm_state[address>>12]==2;
}
int pm_vm_pin_structure(pm_u32 a) {
    if(!pm_owned(a) || pm_vm_refs[a>>12])return 0;
    pm_state[a>>12]=3;pm_vm_structures++;return 1;
}
int pm_vm_release_structure(pm_u32 a) {
    if((a&4095) || pm_state[a>>12]!=3 || !pm_vm_structures)return 0;
    pm_state[a>>12]=2;pm_vm_structures--;return pm_free(a);
}
int pm_vm_hold(pm_u32 a) {
    if(!pm_owned(a) || pm_vm_refs[a>>12]==255)return 0;
    pm_vm_refs[a>>12]++;return 1;
}
int pm_vm_drop(pm_u32 a) {
    if(!pm_owned(a) || !pm_vm_refs[a>>12])return 0;
    pm_vm_refs[a>>12]--;return 1;
}
static pm_u32 rd32(pm_u32 a) { return *(const pm_u32 *)a; }
static pm_u64 end64(pm_u64 a, pm_u64 n) { return n > ~a ? ~(pm_u64)0 : a+n; }
static void region(pm_u64 a, pm_u64 e, int available) {
    pm_u64 limit = (pm_u64)1 << 32;
    if (a >= limit || e <= a) return;
    if (e > limit) e = limit;
    pm_u32 first = (pm_u32)((a + (available ? 4095 : 0)) >> 12);
    pm_u32 last = (pm_u32)((e + (available ? 0 : 4095)) >> 12);
    for (pm_u32 p=first; p<last; p++) pm_state[p] = available ? 1 : 0;
}
static int reserve(pm_u32 a, pm_u64 n) {
    if (!a || n > ((pm_u64)1<<32)-a) return 0;
    region(a,(pm_u64)a+n,0); pm_direct_region(a,(pm_u64)a+n); return 1;
}
static int string_reserve(pm_u32 a) {
    if (!a) return 0;
    for (pm_u32 n=0;n<4096 && a<=0xffffffffu-n;n++)
        if (!*(const char *)(a+n)) return reserve(a,n+1);
    return 0;
}
int pm_init(pm_u32 magic, pm_u32 info, pm_u32 kb, pm_u32 ke) {
    for (pm_u32 p=0;p<PM_PAGES;p++) { pm_state[p]=0;pm_vm_refs[p]=0; }
    for (pm_u32 i=0;i<PM_PAGES/8;i++) pm_direct[i]=0;
    pm_vm_structures=0;pm_boot_info=info;pm_boot_map=pm_boot_map_length=0;
    pm_stats.ram_kib=pm_stats.available=pm_stats.used=pm_stats.free=pm_stats.errors=pm_stats.ready=0; pm_hint=0;
    if (magic!=0x2badb002 || !info || info>0xffffffffu-116 || ke<=kb) return 0;
    pm_u32 f=rd32(info);
    /* Unknown ancillary data must never silently become allocatable. */
    if (!(f&64) || (f & ~0x3ffu) || ((f&48)==48)) return 0;
    pm_u32 length=rd32(info+44), map=rd32(info+48);
    if (!map || !length || length>1048576 || map>0xffffffffu-length) return 0;
    pm_boot_map=map;pm_boot_map_length=length;
    /* Validate all variable-sized entries before admitting any pages. */
    for (pm_u32 o=0;o<length;) {
        if (length-o<24) return 0;
        pm_u32 n=rd32(map+o);
        if (n<20 || n>length-o-4) return 0;
        o+=n+4;
    }
    for (int pass=0;pass<2;pass++) for (pm_u32 o=0;o<length;) {
        pm_u32 a=map+o, n=rd32(a), type=rd32(a+20);
        pm_u64 start=(pm_u64)rd32(a+4)|((pm_u64)rd32(a+8)<<32);
        pm_u64 size=(pm_u64)rd32(a+12)|((pm_u64)rd32(a+16)<<32);
        if ((type==1)==(pass==0)) region(start,end64(start,size),pass==0);
        o+=n+4;
    }
    /* RAM capacity: union of type-1 full pages, before kernel reservations. */
    for (pm_u32 p=0;p<PM_PAGES;p++) if (pm_state[p]) pm_stats.ram_kib+=4;
    /* Physical pointers used by PM require all usable high RAM to be mapped.
       Low RAM is mapped only for explicitly retained boot metadata. */
    for(pm_u32 p=256;p<PM_PAGES;p++)if(pm_state[p])pm_direct[p>>3]|=(unsigned char)(1u<<(p&7));
    region(0,0x100000,0); region(kb,ke,0); pm_direct_region(kb,ke);
    if (!reserve(info,116) || !reserve(map,length)) return 0;
    if ((f&4) && !string_reserve(rd32(info+16))) return 0;
    if ((f&512) && !string_reserve(rd32(info+64))) return 0;
    if (f&8) {
        pm_u32 count=rd32(info+20), mods=rd32(info+24);
        if (count>4096 || (count && !reserve(mods,(pm_u64)count*16))) return 0;
        for (pm_u32 i=0;i<count;i++) {
            pm_u32 a=mods+i*16, b=rd32(a), e=rd32(a+4), s=rd32(a+8);
            if (e<b || (e>b && !reserve(b,e-b)) || (s && !string_reserve(s))) return 0;
        }
    }
    if ((f&16) && !reserve(rd32(info+36),(pm_u64)rd32(info+28)+rd32(info+32))) return 0;
    if (f&32) {
        pm_u32 count=rd32(info+28), stride=rd32(info+32), table=rd32(info+36);
        if (count>4096 || stride<40 || !reserve(table,(pm_u64)count*stride)) return 0;
        for (pm_u32 i=0;i<count;i++) {
            pm_u32 sh=table+i*stride, address=rd32(sh+12), size=rd32(sh+20);
            if (address && size && !reserve(address,size)) return 0;
        }
    }
    if ((f&128) && rd32(info+52) && !reserve(rd32(info+56),rd32(info+52))) return 0;
    /* BIOS configuration table has no defined size: refuse allocation. */
    if (f&256) return 0;
    for (pm_u32 p=0;p<PM_PAGES;p++) if (pm_state[p]) pm_stats.free++;
    pm_stats.available=pm_stats.free; pm_stats.ready=1; return 1;
}
pm_u32 pm_alloc(void) {
    if (!pm_stats.ready || !pm_stats.free) return 0;
    for (pm_u32 i=0;i<PM_PAGES;i++) {
        pm_u32 p=(pm_hint+i)&(PM_PAGES-1);
        if (pm_state[p]==1) {
            pm_state[p]=2; pm_hint=p+1; pm_stats.free--; pm_stats.used++;
            return p<<12;
        }
    }
    return 0;
}
int pm_free(pm_u32 address) {
    pm_u32 p=address>>12;
    if (!pm_stats.ready || (address&4095) || pm_state[p]!=2 || pm_vm_refs[p]) { pm_stats.errors++; return 0; }
    pm_state[p]=1; pm_stats.free++; pm_stats.used--; if(p<pm_hint) pm_hint=p; return 1;
}
/* Exhaust every allocatable page, touch real RAM, verify, then restore counts. */
int pm_selftest(void) {
    if (!pm_stats.ready || pm_stats.used!=pm_vm_structures || pm_stats.free<2) return 0;
    pm_u32 base_used=pm_stats.used, original=pm_stats.free, a=pm_alloc(), b=pm_alloc();
    if (!a || !b || a==b) return 0;
    if (!pm_free(a) || pm_free(a) || pm_free(b+1) || pm_free(0x100000) || pm_free(0) || !pm_free(b)) return 0;
    pm_u32 count=0;
    while ((a=pm_alloc())!=0) {
        volatile pm_u32 *v=(volatile pm_u32 *)a;
        v[0]=a^0xabcddcba; v[1023]=~a; count++;
    }
    if (count!=original || pm_alloc()!=0 || pm_stats.free || pm_stats.used!=original+base_used) return 0;
    int ok=1;
    for (pm_u32 p=0;p<PM_PAGES;p++) if (pm_state[p]==2) {
        a=p<<12; volatile pm_u32 *v=(volatile pm_u32 *)a;
        if(v[0]!=(a^0xabcddcba) || v[1023]!=~a) ok=0;
        if(!pm_free(a)) ok=0;
    }
    return ok && pm_stats.free==original && pm_stats.used==base_used;
}
