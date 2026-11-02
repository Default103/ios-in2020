/* Recovery-only IN2020 fixed-sensor identity probes, not frame capture.
 * ABI: OnePlusOSS SM8250 98719830 cam_defs/cam_req_mgr/cam_sensor headers.
 * Profile: this phone's semco_imx689 module; the MP alternative has identical
 * power/identity settings except the expected vendor nibble (0 or 1).
 * The original kernel controls power-up/down and reads IDs. No configure,
 * stream, OIS, EEPROM, OTP programming or calibration command is emitted.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#ifndef IN2020_PROBE_HOST_TEST
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#endif

struct control { uint32_t op, size, type, reserved; uint64_t handle; };
struct alloc_cmd { uint64_t len, align; int32_t mmu[16]; uint32_t count, flags;
    struct { uint32_t handle; int32_t fd; uint64_t address; } out; };
struct packet_header { uint32_t op, size; uint64_t request; uint32_t flags, padding; };
struct descriptor { int32_t handle; uint32_t offset, size, length, type, metadata; };
struct packet { struct packet_header header; uint32_t cmd_offset, commands,
    io_offset, ios, patch_offset, patches, kmd_index, kmd_offset;
    struct descriptor desc[2]; };
struct cap { uint32_t slot, secure, pitch, roll, yaw, actuator, eeprom, ois, flash, csiphy; };
struct __attribute__((packed)) i2c_info { uint32_t address; uint8_t frequency, type; uint16_t reserved; };
struct __attribute__((packed)) probe { uint8_t data_type, addr_type, op, type;
    uint32_t reg, expected, mask; uint16_t camera, reserved; };
struct __attribute__((packed)) power_cmd { uint32_t count; uint8_t reserved, type;
    uint16_t more_reserved, sequence, unused; uint32_t value, high; };
struct __attribute__((packed)) wait_cmd { int16_t delay, reserved; uint8_t op, type; uint16_t more_reserved; };
struct step { uint16_t type; uint32_t value; uint16_t ms; };
#if defined(IN2020_COLOR_PROBE)
/* Stock slot 4 / CSI4; GC5035 ID/power metadata only, no register tables. */
#define SENSOR_NODE "/dev/v4l-subdev13"
#define SENSOR_SLOT 4
#define SENSOR_PHY 4
#define SENSOR_ADDRESS 0x6e
#define SENSOR_ID 0x5035
#define SENSOR_ID_REG 0xf0
#define SENSOR_ADDR_TYPE 1
#define PROBE_OPTION "--probe-color"
#define POWER_BYTES 256
#define POWER_STEPS 10
#define POWER_WAITS 7
#define VARIANTS 1
static const struct step up[]={{3,1,1},{2,1,1},{1,1,1},{0,24000000,1},{8,1,1}};
static const struct step down[]={{8,0,1},{0,0,1},{1,0,0},{2,0,0},{3,0,0}};
#elif defined(IN2020_ULTRA_PROBE)
/* Stock moduleConfiguration slot 1 / measured CSI1; IMX586, no vendor subtype claim. */
#define SENSOR_NODE "/dev/v4l-subdev12"
#define SENSOR_SLOT 1
#define SENSOR_PHY 1
#define SENSOR_ADDRESS 0x20
#define SENSOR_ID 0x586
#define PROBE_OPTION "--probe-ultra"
#define POWER_BYTES 408
#define POWER_STEPS 16
#define POWER_WAITS 11
#define VARIANTS 1
static const struct step up[]={{1,1,1},{6,1,1},{7,1,1},{10,1,0},{2,1,1},{3,1,1},{0,19200000,1},{8,1,1}};
static const struct step down[]={{8,0,1},{0,0,1},{3,0,0},{2,0,0},{7,0,1},{6,0,1},{10,0,0},{1,0,0}};
#elif defined(IN2020_TELE_PROBE)
/* Stock moduleConfiguration slot 3 / measured CSI2; semco_s5k3m5. */
#define SENSOR_NODE "/dev/v4l-subdev14"
#define SENSOR_SLOT 3
#define SENSOR_PHY 2
#define SENSOR_ADDRESS 0x5a
#define SENSOR_ID 0x30d5
#define SENSOR_ID_REG 0
#define PROBE_OPTION "--probe-tele"
#define POWER_BYTES 280
#define POWER_STEPS 10
#define POWER_WAITS 10
#define VARIANTS 1
static const struct step up[]={{1,1,1},{2,1,1},{3,1,1},{0,19200000,1},{8,1,1}};
static const struct step down[]={{8,0,1},{0,0,1},{3,0,1},{1,0,1},{2,0,1}};
#elif defined(IN2020_FRONT_PROBE)
/* Measured slot 2 / CSI3; stock truly_imx471 profile. */
#define SENSOR_NODE "/dev/v4l-subdev15"
#define SENSOR_SLOT 2
#define SENSOR_PHY 3
#define SENSOR_ADDRESS 0x20
#define SENSOR_ID 0x471
#define PROBE_OPTION "--probe-front"
#define POWER_BYTES 280
#define POWER_STEPS 10
#define POWER_WAITS 10
#define VARIANTS 1
static const struct step up[]={{1,1,1},{2,1,1},{3,1,1},{0,19200000,1},{8,1,1}};
static const struct step down[]={{8,0,1},{0,0,1},{3,0,1},{2,0,1},{1,0,1}};
#else
#define SENSOR_NODE "/dev/v4l-subdev11"
#define SENSOR_SLOT 0
#define SENSOR_PHY 0
#define SENSOR_ADDRESS 0x34
#define SENSOR_ID 0x689
#define PROBE_OPTION "--probe-main"
#define POWER_BYTES 296
#define POWER_STEPS 12
#define POWER_WAITS 7
#define VARIANTS 2
static const struct step up[]={{1,1,1},{6,1,0},{2,1,1},{3,1,1},{0,19200000,1},{8,1,1}};
static const struct step down[]={{8,0,1},{0,0,1},{3,0,0},{2,0,0},{1,0,0},{6,0,0}};
#endif
#ifndef SENSOR_ID_REG
#define SENSOR_ID_REG 0x16
#endif
#ifndef SENSOR_ADDR_TYPE
#define SENSOR_ADDR_TYPE 2
#endif
_Static_assert(sizeof(struct control)==24, "control ABI");
_Static_assert(sizeof(struct alloc_cmd)==104, "allocation ABI");
_Static_assert(sizeof(struct cap)==40, "query ABI");
_Static_assert(sizeof(struct probe)==20 && sizeof(struct i2c_info)==8, "probe ABI");
_Static_assert(offsetof(struct packet,desc)==56 && sizeof(struct packet)==104, "packet ABI");
_Static_assert(sizeof(struct power_cmd)==20 && sizeof(struct wait_cmd)==8, "power ABI");

static unsigned emit_power(uint8_t *buffer, unsigned at, const struct step *steps, unsigned count, unsigned type)
{
    for (unsigned i=0;i<count;++i) {
        struct power_cmd p={0}; p.count=1;p.type=(uint8_t)type;
        p.sequence=steps[i].type;p.value=steps[i].value;
        memcpy(buffer+at,&p,sizeof(p));at+=sizeof(p);
        if(steps[i].ms) {
            struct wait_cmd w={0};w.delay=(int16_t)steps[i].ms;w.op=3;w.type=9;
            memcpy(buffer+at,&w,sizeof(w));at+=sizeof(w);
        }
    }
    return at;
}

static void make_packet(uint8_t buffer[4096], uint32_t handle, unsigned vendor)
{
    memset(buffer,0,4096);
    struct packet *p=(struct packet *)buffer;
    p->header.op=3;p->header.size=sizeof(*p);p->commands=2;
    p->desc[0]=(struct descriptor){(int32_t)handle,256,48,48,7,0};
    struct i2c_info info={SENSOR_ADDRESS,0,4,0}; /* standard I2C frequency */
    struct probe sensor={2,SENSOR_ADDR_TYPE,0,1,SENSOR_ID_REG,SENSOR_ID,0xffff,SENSOR_SLOT,0};
    struct probe variant={1,2,0,1,SENSOR_ID==0x689?0x18:0,vendor,0xff,SENSOR_SLOT,0};
    memcpy(buffer+256,&info,sizeof(info));
    memcpy(buffer+264,&sensor,sizeof(sensor));
    memcpy(buffer+284,&variant,sizeof(variant));
    unsigned end=emit_power(buffer,512,up,sizeof(up)/sizeof(up[0]),2);
    end=emit_power(buffer,end,down,sizeof(down)/sizeof(down[0]),3);
    p->desc[1]=(struct descriptor){(int32_t)handle,512,end-512,end-512,7,0};
}

#ifdef IN2020_PROBE_HOST_TEST
int main(void)
{
    union { uint64_t align; uint8_t b[4096]; } data;
    for(unsigned vendor=0;vendor<VARIANTS;++vendor) {
        make_packet(data.b,123,vendor);struct packet *p=(struct packet *)data.b;
        if(p->desc[0].length!=48 || p->desc[1].length!=POWER_BYTES || p->commands!=2 ||
           ((struct probe *)(data.b+284))->expected!=vendor) return 1;
        unsigned at=512, steps=0, waits=0;
        while(at<512+POWER_BYTES) {
            unsigned type=data.b[at+5];
            if(type==2 || type==3) { ++steps;at+=20; }
            else if(type==9) { ++waits;at+=8; }
            else return 2;
        }
        if(at!=512+POWER_BYTES || steps!=POWER_STEPS || waits!=POWER_WAITS) return 3;
    }
    puts("sensor probe ABI / bounded power packet fixtures PASS");return 0;
}
#else
static int command(int fd, unsigned op, unsigned size, unsigned type, uint64_t handle)
{
    struct control c={op,size,type,0,handle};return ioctl(fd,_IOWR('V',192,struct control),&c);
}

int main(int argc,char **argv)
{
    if(argc!=2 || (strcmp(argv[1],PROBE_OPTION) &&
        !(VARIANTS==2 && !strcmp(argv[1],"--probe-main-mp")))) {
        puts("Explicit " PROBE_OPTION " required; powers sensor only for ID match, then kernel powers down.");return 2;
    }
    unsigned vendor=!strcmp(argv[1],"--probe-main-mp");
    int sensor=open(SENSOR_NODE,O_RDWR|O_CLOEXEC);
    if(sensor<0) { perror("sensor open");return 1; }
    struct cap cap={0};
    if(command(sensor,0x101,sizeof(cap),1,(uint64_t)(uintptr_t)&cap) || cap.slot!=SENSOR_SLOT || cap.csiphy!=SENSOR_PHY || cap.secure) {
        puts("Unexpected sensor topology; no probe issued");close(sensor);return 1;
    }
    int manager=open("/dev/video0",O_RDWR|O_CLOEXEC);
    if(manager<0) { perror("manager open");close(sensor);return 1; }
    struct alloc_cmd alloc={0};alloc.len=4096;alloc.align=4096;
    alloc.flags=(1u<<3)|(1u<<4)|(1u<<6);alloc.out.fd=-1;
    if(command(manager,0x11d,sizeof(alloc),1,(uint64_t)(uintptr_t)&alloc)) {
        perror("camera memory allocation");close(manager);close(sensor);return 1;
    }
    int result=1;
    void *mapped=mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_SHARED,alloc.out.fd,0);
    if(mapped==MAP_FAILED) perror("camera buffer mmap");
    else {
        make_packet(mapped,alloc.out.handle,vendor);
        printf("SENSOR_PROBE slot=%u id=%#x reg=%#x vendor=%u; original-kernel power up/match/down\n",SENSOR_SLOT,SENSOR_ID,SENSOR_ID_REG,vendor);fflush(stdout);
        int rc=command(sensor,0x115,0,2,alloc.out.handle);
        int error=errno;printf("SENSOR_PROBE rc=%d errno=%d\n",rc,rc?error:0);
        result=rc?1:0;
        munmap(mapped,4096);
    }
    close(sensor); /* original close callback also performs sensor shutdown */
    struct { int32_t handle; uint32_t reserved; } release={(int32_t)alloc.out.handle,0};
    if(command(manager,0x11f,sizeof(release),1,(uint64_t)(uintptr_t)&release)) perror("release camera memory");
    if(alloc.out.fd>=0) close(alloc.out.fd);
    close(manager);return result;
}
#endif
