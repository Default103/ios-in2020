/* Fixed camera snapshot ABI, shared by XNU and UIKit. APSL2.0. */
#ifndef IN2020_CAMERA_USER_H
#define IN2020_CAMERA_USER_H
#include <sys/ioccom.h>
#include <stdint.h>
/* 1=front IMX471, 2=tele S5K3M5. Blocking finite capture; read I20FRAME after. */
#define IN2020_CAMERA_SNAPSHOT _IOW('C', 1, uint32_t)
#endif
