/* IN2020 bounded, one-frame native RAW10 capture. APSL 2.0. */
#ifndef IN2020_CAMERA_RAW_H
#define IN2020_CAMERA_RAW_H
#include <sys/param.h>
#include <sys/systm.h>
#include <kern/clock.h>
#include <machine/machine_routines.h>
#include "in2020_camera_board.h"
struct cr_configuration {
    unsigned width,height,stride,init_count,mode_count,id,id_reg,slave,phy,phy_base,phy_irq;
    unsigned mclk_cmd,mclk_branch,settle;
    bool rear;
};
extern struct cr_configuration cr_config;
#define CR_WIDTH (cr_config.width)
#define CR_HEIGHT (cr_config.height)
#define CR_STRIDE (cr_config.stride)
#define CR_BYTES (CR_STRIDE*CR_HEIGHT)
#define CR_IOVA 0x40000000u
#define CR_FSR_FAULT 0xc00001feu
extern vm_offset_t ml_io_map(vm_offset_t,vm_size_t);
static inline uint32_t cr_read(vm_offset_t p,unsigned o) {
    uint32_t v=*(volatile uint32_t *)(p+o);__asm__ volatile("dmb oshld":::"memory");return v;
}
static inline void cr_write(vm_offset_t p,unsigned o,uint32_t v) {
    __asm__ volatile("dmb oshst":::"memory");*(volatile uint32_t *)(p+o)=v;__asm__ volatile("dsb osh":::"memory");
}
static inline bool cr_put(vm_offset_t p,unsigned o,uint32_t v,uint32_t mask) {
    cr_write(p,o,v);uint32_t got=cr_read(p,o);
    if((got&mask)!=(v&mask)) {printf("IN2020 RAW: write offset=%#x wanted=%#x got=%#x mask=%#x\n",o,v,got,mask);return false;}return true;
}
static inline bool cr_wait(vm_offset_t p,unsigned o,uint32_t mask,uint32_t want,unsigned us) {
    for(unsigned n=0;n<us;n+=10) {if((cr_read(p,o)&mask)==want)return true;delay(10);}
    printf("IN2020 RAW: wait timeout offset=%#x mask=%#x wanted=%#x got=%#x us=%u\n",o,mask,want,cr_read(p,o),us);return false;
}
bool in2020_camera_pmic_read(unsigned,unsigned,uint8_t *);
int in2020_rpmh_camera_vrm(const char *,bool);
int in2020_rpmh_camera_arc(bool);
bool cr_sensor_plan_valid(void);
bool cr_sensor_up(void);
bool cr_sensor_program(void);
bool cr_sensor_stream(bool);
bool cr_sensor_down(void);
bool cr_clocks_up(void);
bool cr_clocks_down(void);
bool cr_mclk(bool);
bool cr_dma_up(void);
bool cr_dma_ok(void);
void cr_dma_down(bool);
uint8_t *cr_dma_data(void);
void cr_dma_invalidate(void);
void in2020_camera_capture(void);
#endif
