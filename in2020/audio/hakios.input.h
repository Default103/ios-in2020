/*
 * apollo (xnu 0138): the part of the Mi 10 port's bsd/sys/hakios.input.h
 * (github.com/JopangJovan/hackios-xnu-kernel-umi; APSL 2.0) that sweet_adsp.c uses: how the
 * battery is, as the audio DSP's battery manager says (boot-arg battmgr=1). Nothing takes it here
 * yet (sweet_audio_glue.c): the battery is read from its gauge (SweetBattery).
 */
#ifndef SWEET_HAKIOS_INPUT_H
#define SWEET_HAKIOS_INPUT_H
#include <stdint.h>

struct hakios_battery_state {
	uint32_t size;                  /* sizeof(struct hakios_battery_state) */
	int32_t percent_hundredths;     /* the charge, 0..10000 */
	int32_t voltage_uv;
	int32_t current_ua;             /* more than 0 while the battery takes charge */
	int32_t temperature_hundredths; /* of a degree Celsius */
	int32_t present;                /* 1 if there is a battery */
	int32_t external;               /* 1 if a charger or a computer is on the USB port */
	int32_t status;                 /* Linux's POWER_SUPPLY_STATUS_*: 1 charging, 2 discharging, 3 not charging, 4 full */
	int32_t charge_uah, full_uah, design_uah, cycles;      /* 0 when not known (yet) */
	int32_t usb_type;               /* Linux's POWER_SUPPLY_USB_TYPE_*: 1 a computer's port, 2 a charger, ... */
	int32_t usb_voltage_uv, usb_current_ua;                 /* what the port gives */
};
void hakios_battery_report(const struct hakios_battery_state *state);
#endif
