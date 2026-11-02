/*
 * What the Mi 10 port's audio DSP driver (sweet_adsp.c, xnu 0138) takes from the rest of HackiOS,
 * for apollo: the locks it shares with HackiOS's other DSP driver (here it has none to share
 * with but RPMh, which sweet_rpmh_hold() covers), and stand-ins for what is not here:
 *   - HackiOS's loader, which reads a log of its own and to which a run can return: nothing
 *   - the loader's RAM image of files: none (the firmware is read from /usr/local/fw/)
 *   - the battery manager's reports (battmgr=1): said, not passed on (SweetBattery reads the gauge)
 *   - the speakers' and microphones' paths (hakios.sound.c, hakios.mic.c): not yet ported; the
 *     DSP is brought up and left so
 */
#if defined(ARM64_BOARD_CONFIG_SWEET)
#include <sys/param.h>
#include <sys/systm.h>
#include <kern/locks.h>
#include <pexpert/device_tree.h>
#include "hakios.audio.h"
#include "hakios.input.h"

void loader_log_putc(char c);
void loader_return(void);
const uint8_t *hakios_image_file(const char *dir, const char *name, uint64_t *size, int *disk_out);

static LCK_GRP_DECLARE(sweet_audio_lock_group, "sweet-audio");
LCK_MTX_DECLARE(hakios_aop_mailbox_lock, &sweet_audio_lock_group);
LCK_MTX_DECLARE(hakios_smem_alloc_lock, &sweet_audio_lock_group);
LCK_MTX_DECLARE(hakios_rsc_lock, &sweet_audio_lock_group);

void
loader_log_putc(__unused char c)
{
}

void
loader_return(void)
{
}

const uint8_t *
hakios_image_file(const char *dir, const char *name, uint64_t *size, int *disk_out)
{
    DTEntry node;
    const void *value = NULL;
    unsigned length = 0;
    if (disk_out) *disk_out = 0;
    if (!dir || !name || !size || strcmp(dir, "fw") ||
        strncmp(name, "adsp.", 5) || strlen(name) > 16) return NULL;
    if (SecureDTLookupEntry(NULL, "/in2020-adsp", &node) != kSuccess ||
        SecureDTGetProperty(node, name, &value, &length) != kSuccess ||
        !length || length > 0x2500000) return NULL;
    *size = length;
    return value;
}


void
hakios_battery_report(const struct hakios_battery_state *state)
{
	static unsigned said;

	if (said < 4) {
		said++;
		printf("adsp: battmgr: %d.%02d %%, %d mV, %d mA%s\n", state->percent_hundredths / 100,
		    state->percent_hundredths % 100, state->voltage_uv / 1000, state->current_ua / 1000,
		    state->external ? ", on external power" : "");
	}
}

void
hakios_sound_dsp_up(void)
{
    extern void in2020_afe_probe(void);
	printf("adsp: the audio services are up; entering IN2020 audio path\n");
    in2020_afe_probe();
}

void
hakios_sound_dsp_down(void)
{
}

void
hakios_sound_quiesce(void)
{
}

void
hakios_mic_dsp_up(void)
{
}

void
hakios_mic_dsp_down(void)
{
}
#endif /* ARM64_BOARD_CONFIG_SWEET */
