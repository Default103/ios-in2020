/* Recovery-only inventory, not an XNU driver. No power/reset/calibration setup.
 * Camera ABI: OnePlusOSS SM8250 98719830, cam_defs.h / cam_sensor.h.
 * TFA identity: official audio 0ac91e89, tfa98xx.c regmap_read(0x03).
 * I2C writes only the register index before a two-byte read, not register data.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

struct cam_control { uint32_t op, size, handle_type, reserved; uint64_t handle; };
struct sensor_cap { uint32_t slot, secure, pitch, roll, yaw, actuator, eeprom, ois, flash, csiphy; };
struct i2c_msg { uint16_t addr, flags, len; uint8_t *buf; };
struct i2c_rdwr { struct i2c_msg *msgs; uint32_t nmsgs; };
_Static_assert(sizeof(struct cam_control) == 24, "camera control ABI");
_Static_assert(sizeof(struct sensor_cap) == 40, "sensor query ABI");
_Static_assert(sizeof(struct i2c_msg) == 16, "64-bit I2C ABI");

int main(void)
{
    for (unsigned n = 0; n < 64; ++n) {
        char path[128], name[80] = {0};
        snprintf(path, sizeof(path), "/sys/class/video4linux/v4l-subdev%u/name", n);
        FILE *f = fopen(path, "r");
        if (!f) continue;
        if (!fgets(name, sizeof(name), f)) name[0] = 0;
        fclose(f);
        if (strcmp(name, "cam-sensor-driver\n")) continue;
        snprintf(path, sizeof(path), "/dev/v4l-subdev%u", n);
        int fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0) { printf("CAM node=%u open_errno=%d\n", n, errno); continue; }
        struct sensor_cap cap = {0};
        struct cam_control ctl = {0x101, sizeof(cap), 1, 0, (uint64_t)(uintptr_t)&cap};
        int rc = ioctl(fd, _IOWR('V', 192, struct cam_control), &ctl);
        if (rc) printf("CAM node=%u query_errno=%d\n", n, errno);
        else printf("CAM node=%u slot=%u secure=%u pitch=%u roll=%u yaw=%u actuator=%u eeprom=%u ois=%u flash=%u csiphy=%u\n",
            n, cap.slot, cap.secure, cap.pitch, cap.roll, cap.yaw,
            cap.actuator, cap.eeprom, cap.ois, cap.flash, cap.csiphy);
        close(fd);
    }
    /* Refuse to access an amplifier already owned by a bound kernel driver. */
    for (unsigned addr = 0x34; addr <= 0x35; ++addr) {
        char path[128], link[256];
        snprintf(path, sizeof(path), "/sys/bus/i2c/devices/0-%04x/driver", addr);
        if (readlink(path, link, sizeof(link)) >= 0) {
            printf("TFA addr=%#x skipped_bound_driver\n", addr); continue;
        }
        int fd = open("/dev/i2c-0", O_RDWR | O_CLOEXEC);
        if (fd < 0) { printf("TFA addr=%#x open_errno=%d\n", addr, errno); continue; }
        uint8_t reg = 3, value[2] = {0};
        struct i2c_msg msgs[2] = {{addr, 0, 1, &reg}, {addr, 1, 2, value}};
        struct i2c_rdwr request = {msgs, 2};
        int rc = ioctl(fd, 0x0707, &request);
        if (rc != 2) printf("TFA addr=%#x read_rc=%d errno=%d\n", addr, rc, errno);
        else printf("TFA addr=%#x revision=%#06x\n", addr, ((unsigned)value[0] << 8) | value[1]);
        close(fd);
    }
    return 0;
}
