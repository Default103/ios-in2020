/* Bounded os_log evidence for the selected UI processes. Jump technique adapted
 * from pinned ios-apollo sweet_fix.c (OOSEX GANG, MIT; ca-bridge/LICENSE.HackiOS).
 * Arguments are not treated as Objective-C objects; no unsafe descriptions. */
#include <dlfcn.h>
#include <mach/mach.h>
#include <libkern/OSCacheControl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
static unsigned lines;
static _Thread_local int entered;
static void log_entry(void *dso, void *log, uint8_t type, const char *format, uint8_t *buf, uint32_t size)
{
    (void)dso; (void)log;
    if (entered || !format || __atomic_load_n(&lines, __ATOMIC_RELAXED) >= 500) return;
    entered = 1;
    if (type == 0 || type >= 0x10 || strstr(format,"oster") || strstr(format,"allpaper") ||
        strstr(format,"aunch") || strstr(format,"ailed") || strstr(format,"ontainer")) {
        char out[1024]; int n = snprintf(out, sizeof(out), "sweet-oslog: %u %.650s", type, format);
        size_t at = 2;
        for (unsigned i=0; buf && size>=2 && i<buf[1] && i<16 && at+2<=size && n<850; ++i) {
            uint8_t kind=buf[at++], len=buf[at++]; if (len>size-at) break;
            if ((kind&0xf0)==0 && len>0 && len<=8) {
                uint64_t value=0; memcpy(&value,buf+at,len);
                n+=snprintf(out+n,sizeof(out)-(size_t)n," |%llu",(unsigned long long)value);
            }
            at+=len;
        }
        if (n>0 && n<(int)sizeof(out)-1) { out[n++]='\n'; write(2,out,(size_t)n); }
        __atomic_fetch_add(&lines,1,__ATOMIC_RELAXED);
    }
    entered = 0;
}
static kern_return_t protect_raw(mach_port_t task, uintptr_t address, uintptr_t size, int prot)
{
    register long x0 __asm__("x0")=task, x1 __asm__("x1")=address, x2 __asm__("x2")=size;
    register long x3 __asm__("x3")=0, x4 __asm__("x4")=prot, x16 __asm__("x16")=-14;
    __asm__ volatile("svc #0x80" : "+r"(x0) : "r"(x1),"r"(x2),"r"(x3),"r"(x4),"r"(x16) : "memory","cc");
    return (kern_return_t)x0;
}
void sweet_install_log(void)
{
    const char *symbols[]={"_os_log_impl","_os_log_error_impl","_os_log_fault_impl"};
    unsigned installed=0;
    for (unsigned i=0;i<3;++i) {
        uint32_t *p=dlsym(RTLD_DEFAULT,symbols[i]); if (!p) continue;
        uintptr_t ps=getpagesize(), base=(uintptr_t)p&~(ps-1), span=((uintptr_t)(p+4)-base+ps-1)&~(ps-1);
        uint64_t dest=(uintptr_t)log_entry; mach_port_t task=mach_task_self();
        if (protect_raw(task,base,span,VM_PROT_READ|VM_PROT_WRITE|VM_PROT_COPY)!=KERN_SUCCESS) continue;
        ((volatile uint32_t*)p)[0]=0x58000050; ((volatile uint32_t*)p)[1]=0xd61f0200;
        ((volatile uint32_t*)p)[2]=(uint32_t)dest; ((volatile uint32_t*)p)[3]=(uint32_t)(dest>>32);
        if (protect_raw(task,base,span,VM_PROT_READ|VM_PROT_EXECUTE)!=KERN_SUCCESS) _exit(126);
        sys_icache_invalidate(p,16); installed++;
    }
    fprintf(stderr,"sweet-oslog: bounded mirror enabled (%u entries, max500 lines)\n",installed);
}
