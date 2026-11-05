/* OnePlus IN2020 front route, measured recovery FDT + pinned kona sources.
 * Hardware constants only; sensor mode tables/calibration stay local.
 */
#ifndef IN2020_CAMERA_BOARD_H
#define IN2020_CAMERA_BOARD_H
#include <stdint.h>
#define IN2020_CAMERA_SLOT 2u
#define IN2020_CAMERA_ID 0x0471u
#define IN2020_CAMERA_SLAVE 0x10u /* stock 8-bit slave field 0x20 */
#define IN2020_CAMERA_CCI 0x0ac50000u
#define IN2020_CAMERA_MASTER 1u
#define IN2020_CAMERA_PHY 3u
#define IN2020_CAMERA_PHY_BASE 0x0ac70000u
#define IN2020_CAMERA_CSID_LITE 0x0acd9200u
#define IN2020_CAMERA_IFE_LITE 0x0acd9000u
#define IN2020_CAMERA_CAMCC 0x0ad00000u
#define IN2020_CAMERA_CAMCC_SIZE 0x10000u
#define IN2020_CAMERA_MCLK_CMD 0x5054u
#define IN2020_CAMERA_MCLK_BRANCH 0x506cu
#define IN2020_CAMERA_MCLK_HZ 19200000u
#define IN2020_CAMERA_VDIG_RESOURCE "ldof3"
#define IN2020_CAMERA_VDIG_UV 1056000u
#define IN2020_CAMERA_VIO_RESOURCE "ldof7"
#define IN2020_CAMERA_VIO_UV 1800000u
#define IN2020_CAMERA_VANA_RESOURCE "bobc1"
/* Shared BOB parent must be preserved; GPIO24 gates front VANA. */
#define IN2020_CAMERA_VANA_MIN_UV 3008000u
#define IN2020_CAMERA_VANA_MAX_UV 3960000u
#define IN2020_CAMERA_SID_MASK 0x400u
#define IN2020_CAMERA_IOVA_BEGIN 0x07400000u
#define IN2020_CAMERA_IOVA_SIZE 0xd8c00000u
static const uint16_t in2020_camera_sids[8]={0x800,0x801,0x840,0x841,0xc00,0xc01,0xc40,0xc41};
struct in2020_camera_pin {unsigned number;uint32_t base;const char *name;};
static const struct in2020_camera_pin in2020_camera_pins[5]={
    {97,0x0f961000,"MCLK3"},{78,0x0f94e000,"RESET"},{24,0x0f518000,"VANA"},
    {107,0x0f96b000,"CCI1-M1-SDA"},{108,0x0f96c000,"CCI1-M1-SCL"}};
#endif
