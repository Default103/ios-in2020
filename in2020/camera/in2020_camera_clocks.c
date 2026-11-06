/* SM8250 kona-v2 camera clocks/power, bounded one-frame use. APSL2.0.
 * Numeric configuration from pinned camcc-kona.c, gcc-kona.c, refgen.c.
 * Never configures PLL2; MCLK comes directly from TCXO. Shared state retained.
 */
#include "in2020_camera_raw.h"
static vm_offset_t cc,gcc,refgen;
struct clock_branch {bool gcc;unsigned offset;uint32_t old;bool owned;};
static struct clock_branch branches[]={
    {true,0xb008,0,false},{true,0xb040,0,false},{true,0xb02c,0,false},{true,0xb030,0,false},
    {false,0xc0f4,0,false},{false,0xc114,0,false},{false,0x607c,0,false},{false,0x6078,0,false},
    {false,0xc040,0,false},{false,0xc044,0,false},{false,0xc018,0,false},
    {false,0xc03c,0,false},{false,0xc034,0,false}};
struct clock_root {unsigned cmd,value;uint32_t old;bool owned;};
static struct clock_root roots[]={
    {0x7058,0x20e,0,false},{0xc0f8,0x203,0,false},{0xa068,0x202,0,false},
    {0x6060,0x203,0,false},{0xc01c,0x202,0,false},{0xc000,0x301,0,false}};
static const unsigned pll_regs[]={0,4,8,0xc,0x10,0x14,0x18,0x1c,0x20,0x38,0x40};
static uint32_t pll_old[11],gdsc_old,ref_old,mclk_cfg,mclk_branch;
static bool arc_owned,pll_owned,gdsc_owned,ref_owned,mipi_owned,mclk_owned;
static bool branch_on(unsigned i) {
    struct clock_branch *b=&branches[i];vm_offset_t p=b->gcc?gcc:cc;
    b->old=cr_read(p,b->offset);if(b->old==0xffffffff)return false;
    if(b->old&1)return true;
    b->owned=true;
    return cr_put(p,b->offset,b->old|1u,1) && cr_wait(p,b->offset,1u<<31,0,2000);
}
static bool update(unsigned cmd) {
    cr_write(cc,cmd,cr_read(cc,cmd)|1u);
    if(cr_wait(cc,cmd,1,0,2000))return true;
    printf("IN2020 RAW: RCG update timeout offset=%#x\n",cmd);return false;
}
static bool mipi_state(bool on) {
    uint8_t lo=0,hi=0,en=0,status=0,type=0;
    return in2020_camera_pmic_read(0x148,4,&type) && type==4 &&
        in2020_camera_pmic_read(0x148,0x40,&lo) && in2020_camera_pmic_read(0x148,0x41,&hi) &&
        (lo|(hi<<8))==1200 && in2020_camera_pmic_read(0x148,0x46,&en) && !!(en&0x80)==on &&
        in2020_camera_pmic_read(0x148,8,&status) && (!on || (status&0xa0)==0x80);
}
bool cr_mclk(bool on) {
    if(!on) {
        if(!mclk_owned)return true;
        bool ok=cr_put(cc,cr_config.mclk_branch,cr_read(cc,cr_config.mclk_branch)&~1u,1) && cr_wait(cc,cr_config.mclk_branch,1u<<31,1u<<31,2000);
        ok=cr_put(cc,(cr_config.mclk_cmd+4),mclk_cfg,0x371f)&&ok;ok=update(cr_config.mclk_cmd)&&ok;
        if(ok)mclk_owned=false;return ok;
    }
    if(!cc)return false;
    mclk_cfg=cr_read(cc,(cr_config.mclk_cmd+4));mclk_branch=cr_read(cc,cr_config.mclk_branch);
    if(mclk_cfg==0xffffffff || mclk_branch==0xffffffff || (mclk_branch&1) || (cr_read(cc,cr_config.mclk_cmd)&1))return false;
    mclk_owned=true;
    return cr_put(cc,(cr_config.mclk_cmd+4),(mclk_cfg&~0x371fu)|1u,0x371f) && update(cr_config.mclk_cmd) &&
        cr_put(cc,cr_config.mclk_branch,mclk_branch|1u,1) && cr_wait(cc,cr_config.mclk_branch,1u<<31,0,2000);
}
bool cr_clocks_up(void) {
    if(cr_config.rear) {roots[3].cmd=0x6040;branches[6].offset=0x605c;branches[7].offset=0x6058;}
    arc_owned=true;if(in2020_rpmh_camera_arc(true))return false;
    gcc=ml_io_map(0x100000,0x10000);if(!gcc || !branch_on(0) || !branch_on(1))return false;
    cc=ml_io_map(IN2020_CAMERA_CAMCC,IN2020_CAMERA_CAMCC_SIZE);refgen=ml_io_map(0x088e7000,0x1000);
    if(!cc || !refgen)return false;
    gdsc_old=cr_read(cc,0xc144);ref_old=cr_read(refgen,0x80);
    printf("IN2020 RAW: GDSC=%#x REFGEN=%#x PLL0=%#x\n",gdsc_old,ref_old,cr_read(cc,0));
    if(gdsc_old==0xffffffff || ref_old==0xffffffff)return false;
    if(!(gdsc_old&(1u<<31))) {
        if(!(gdsc_old&1))return false;gdsc_owned=true;
        if(!cr_put(cc,0xc144,(gdsc_old|0x800u)&~1u,0x801) || !cr_wait(cc,0xc144,1u<<31,1u<<31,100000))return false;
    }
    if(!(ref_old&1)) {ref_owned=true;if(!cr_put(refgen,0x80,ref_old|1u,1))return false;delay(5);}
    if(!mipi_state(true)) {
        if(!mipi_state(false))return false;mipi_owned=true;
        if(in2020_rpmh_camera_vrm("ldoa9",true))return false;
        bool ready=false;for(unsigned n=0;n<50;++n) {if(mipi_state(true)) {ready=true;break;}delay_for_interval(1,NSEC_PER_MSEC);}
        if(!ready)return false;
    }
    for(unsigned i=0;i<11;++i) {pll_old[i]=cr_read(cc,pll_regs[i]);if(pll_old[i]==0xffffffff)return false;}
    if(pll_old[0]&1) {
        if(cr_read(cc,4)!=0x3e || (cr_read(cc,0x40)&0xffff)!=0x8000 || !(cr_read(cc,0)&(1u<<31)))return false;
    } else {
        static const uint32_t setup[][2]={{4,0x3e},{8,0x44},{0x40,0x8000},{0x18,0x20485699},
            {0x1c,0x2261},{0x20,0x329a699c},{0xc,0x3100},{0x10,0x805},{0x14,0}};
        pll_owned=true;
        for(unsigned i=0;i<9;++i)if(!cr_put(cc,setup[i][0],setup[i][1],~0u))return false;
        if(!cr_put(cc,0,cr_read(cc,0)|(1u<<23),1u<<23) || !cr_put(cc,0x38,0,1) ||
           !cr_put(cc,0,cr_read(cc,0)|4u,4) || !cr_put(cc,0x38,1,1) ||
           !cr_wait(cc,0,1u<<31,1u<<31,2000) || !cr_put(cc,0xc,cr_read(cc,0xc)|7u,7) ||
           !cr_put(cc,0,cr_read(cc,0)|1u,1))return false;
    }
    for(unsigned i=0;i<6;++i) {
        struct clock_root *r=&roots[i];r->old=cr_read(cc,r->cmd+4);
        if(r->old==0xffffffff || (cr_read(cc,r->cmd)&1))return false;
        if((r->old&0x371f)==r->value)continue;
        r->owned=true;if(!cr_put(cc,r->cmd+4,(r->old&~0x371fu)|r->value,0x371f) || !update(r->cmd))return false;
    }
    for(unsigned i=2;i<sizeof(branches)/sizeof(branches[0]);++i)if(!branch_on(i))return false;
    printf("IN2020 RAW: kona camera clocks ready, PLL0=1200MHz IFE/CSID=400MHz PHYtimer=300MHz\n");return true;
}
bool cr_clocks_down(void) {
    bool ok=cr_mclk(false);
    if(cc && gdsc_owned) {
        ok=cr_put(cc,0xc144,cr_read(cc,0xc144)|1u,1)&&ok;
        ok=cr_wait(cc,0xc144,1u<<31,0,100000)&&ok;
        if(ok)ok=cr_put(cc,0xc144,gdsc_old,0x801)&&ok;
    }
    if(ref_owned)ok=cr_put(refgen,0x80,ref_old,1)&&ok;
    if(mipi_owned) {
        if(in2020_rpmh_camera_vrm("ldoa9",false))ok=false;
        delay_for_interval(2,NSEC_PER_MSEC);ok=mipi_state(false)&&ok;
    }
    if(cc) {
        for(int i=(int)(sizeof(branches)/sizeof(branches[0]))-1;i>=2;--i)if(branches[i].owned) {
            vm_offset_t p=branches[i].gcc?gcc:cc;
            ok=cr_put(p,branches[i].offset,branches[i].old,1)&&ok;
        }
        for(int i=5;i>=0;--i)if(roots[i].owned) {
            ok=cr_put(cc,roots[i].cmd+4,roots[i].old,0x371f)&&ok;ok=update(roots[i].cmd)&&ok;
        }
        if(pll_owned) {
            cr_write(cc,0,cr_read(cc,0)&~1u);cr_write(cc,0xc,cr_read(cc,0xc)&~7u);cr_write(cc,0x38,0);
            cr_write(cc,0,cr_read(cc,0)&~4u);
            for(unsigned i=1;i<11;++i)cr_write(cc,pll_regs[i],pll_old[i]);
            cr_write(cc,0,pll_old[0]&~(1u<<31));
        }
    }
    if(arc_owned && in2020_rpmh_camera_arc(false))ok=false;
    for(int i=1;i>=0;--i)if(branches[i].owned)ok=cr_put(gcc,branches[i].offset,branches[i].old,1)&&ok;
    printf("IN2020 RAW: clock/core/refgen cleanup=%d\n",ok);return ok;
}
