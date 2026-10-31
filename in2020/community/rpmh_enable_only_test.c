#ifdef NDEBUG
#undef NDEBUG
#endif
/* Host-only RSC/GIC model for the actual rp_vote implementation. */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define EFIAPI
typedef struct MockBootServices {
    void (*stall)(uint64_t);
    uint64_t (*raise_tpl)(uint64_t);
    void (*restore_tpl)(uint64_t);
} MockBootServices;
typedef struct MockSystemTable { MockBootServices *bs; } MockSystemTable;
static MockBootServices mock_bs;
static MockSystemTable mock_system_table = {&mock_bs};
#define system_table (&mock_system_table)

static uint32_t mock_rsc_read(uint32_t offset);
static uint32_t tp_read32(uint64_t address);
static void mock_rp_write(uint32_t offset, uint32_t value);
static uint32_t tp_resource[2], tp_matches[2], tp_rsc[3], tp_db_rc;
static int tp_inventory_done, tp_geometry_valid;

/* Python inserts the actual rp_vote implementation after replacing only the
 * MMIO store with mock_rp_write(). */
static int rp_poisoned, rp_latch_irq;
static uint32_t rp_last_irq,rp_last_status[3],rp_gic[3];
static uint32_t rp_read(uint32_t offset) { return tp_read32(0x18220d00ULL + offset); }
static void rp_write(uint32_t offset,uint32_t value) {mock_rp_write(offset,value);}
static int rp_sync(uint32_t offset, uint32_t value) {
    rp_write(offset, value);
    for (unsigned i = 0; i < 1000; ++i) {
        if (rp_read(offset) == value) return 1;
        system_table->bs->stall(1);
    }
    return 0;
}
static int rp_empty(void) {
    return rp_read(0x2a0+0x18) == 1 && rp_read(0x2a0+0x14) == 0x10000 &&
        rp_read(0x2a0+0x1c) == 0 && rp_read(0x2a0+0x10) == 0 && !(rp_read(4) & 2u);
}
static int rp_mask_source(void) {
    uint32_t value = rp_read(0);
    return value != UINT32_MAX && rp_sync(0, value & ~2u);
}
static int rp_restore_source(uint32_t original) {
    uint32_t value = rp_read(0);
    return value != UINT32_MAX && rp_sync(0, (value & ~2u) | (original & 2u));
}
static int rp_conflict(uint32_t address) {
    uint32_t enabled = rp_read(0x1c);
    if (enabled & ~0xffffu) return 1;
    for (unsigned c = 0; c < 16; ++c) {
        if (!(enabled & (1u << c))) continue;
        uint32_t target = rp_read(0x34+c*0x14);
        if (target == UINT32_MAX || (target & ~15u) == address) return 1;
    }
    return 0;
}
static uint32_t rp_vote(uint32_t address, uint32_t mv, int enable) {
    typedef uint64_t (EFIAPI *raise_fn)(uint64_t);
    typedef void (EFIAPI *restore_fn)(uint64_t);
    uint32_t saved[1][3], irq_before = 0, result = 1;
    uint64_t old_tpl;
    int claimed = 0;
    if (rp_poisoned) return 2;
    /* mv validates the pre-existing setpoint; it is NEVER transmitted.
     * This VIO-only diagnostic sends one VRM_ENABLE write, as Linux's
     * rpmh_regulator_set_enable_state(). No voltage/mode request. */
    int target_ok = address == tp_resource[1] && address == 0x42e00 && mv == 1800 && tp_matches[1] == 1;
    if (!target_ok ||
        (enable != 0 && enable != 1) || !tp_inventory_done || !tp_geometry_valid || tp_db_rc != 0 ||
        tp_rsc[0] != 0x20000 || tp_rsc[2] != 0x800c8104 ||
        !system_table->bs->raise_tpl || !system_table->bs->restore_tpl) return 3;
    /* Latch only when Group1/level/masked makes it safe. Otherwise keep our
     * RSC source masked and require observed execution progress while polling,
     * exactly as in the reference. Never change GIC configuration. */
    rp_gic[0] = tp_read32(0x17a00084);
    rp_gic[1] = tp_read32(0x17a00104);
    rp_gic[2] = tp_read32(0x17a00c08);
    if (rp_gic[0] == UINT32_MAX || rp_gic[1] == UINT32_MAX || rp_gic[2] == UINT32_MAX) return 4;
    rp_latch_irq = (rp_gic[0] & 32u) && !(rp_gic[1] & 32u) && !(rp_gic[2] & (3u << 10));
    old_tpl = ((raise_fn)system_table->bs->raise_tpl)(31);
    if (!rp_empty() || rp_conflict(address)) { result = 5; goto out; }
    irq_before = rp_read(0);
    if (irq_before == UINT32_MAX || (irq_before & 2u)) { result = 6; goto out; }
    for (unsigned c = 0; c < 1; ++c) {
        uint32_t b = 0x2a0+c*0x14;
        saved[c][0] = rp_read(b+0x30); saved[c][1] = rp_read(b+0x34); saved[c][2] = rp_read(b+0x38);
        if (saved[c][0] == UINT32_MAX || saved[c][1] == UINT32_MAX || saved[c][2] == UINT32_MAX) {
            result = 15; goto out;
        }
    }
    claimed = 1; rp_poisoned = 1;
    if (!rp_sync(0, irq_before & ~2u)) { result = 7; goto out; }
    if (!rp_empty()) { result = 8; goto out; }
    const uint32_t offsets[1] = {4}, values[1] = {(uint32_t)enable};
    for (unsigned c = 0; c < 1; ++c) {
        uint32_t b = 0x2a0+c*0x14;
        /* length=8, response requested, write: 0x10108. */
        if (!rp_sync(b+0x30, 0x10108) || !rp_sync(b+0x34, address+offsets[c]) ||
            !rp_sync(b+0x38, values[c])) { result = 9; goto out; }
    }
    if (!rp_sync(0x2a0+0x10, 1) || !rp_sync(0x2a0+0x1c, 1) ||
        !rp_sync(0x2a0+0x14, 0) || !rp_sync(0x2a0+0x14, 0x10000) ||
        (rp_latch_irq && !rp_sync(0, irq_before | 2u))) { result = 10; goto out; }
    rp_write(0x2a0+0x14, 0x1010000);
    int complete = 0, progress = 0;
    for (unsigned us = 0; us < 100000; ++us) {
        rp_last_irq = rp_read(4);
        uint32_t idle = rp_read(0x2a0+0x18);
        int commands_complete = 1;
        for (unsigned c = 0; c < 1; ++c) {
            rp_last_status[c] = rp_read(0x2a0+0x3c+c*0x14);
            commands_complete &= rp_last_status[c] != UINT32_MAX &&
                (rp_last_status[c] & 0x10100) == 0x10100;
        }
        /* As in the reference, an observed execution transition is fresh even
         * on a platform that does not latch IRQ status. Stale completion alone
         * is never enough. */
        progress |= idle == 0 || !commands_complete;
        complete = idle == 1 && ((rp_last_irq & 2u) || progress) && commands_complete;
        if (complete) break;
        system_table->bs->stall(1);
    }
    if (!complete) { result = 11; goto out; }
    if (!rp_mask_source() || !rp_sync(0x2a0+0x14, 0x10000) ||
        !rp_sync(0x2a0+0x1c, 0) || !rp_sync(0x2a0+0x10, 0)) { result = 12; goto out; }
    for (unsigned c = 0; c < 1; ++c) {
        uint32_t b = 0x2a0+c*0x14;
        if (!rp_sync(b+0x30, saved[c][0]) || !rp_sync(b+0x34, saved[c][1]) ||
            !rp_sync(b+0x38, saved[c][2])) { result = 13; goto out; }
    }
    rp_write(8, 2); /* clear only TCS1 completion */
    if ((rp_read(4) & 2u) || !rp_restore_source(irq_before) ||
        !rp_empty()) { result = 14; goto out; }
    rp_poisoned = 0; result = 0;
out:
    if (claimed && rp_poisoned) {
        /* Keep our source masked and slot quarantined. No reset, cancel or replay. */
        uint32_t value = rp_read(0);
        if (value != UINT32_MAX) rp_write(0, value & ~2u);
    }
    ((restore_fn)system_table->bs->restore_tpl)(old_tpl);
    return result;
}


#define RSC_BASE 0x18220000u
#define TCS_BASE 0x18220d00u
#define RSC_WORDS (0x4000u / 4u)
#define GIC_GROUP1 0x17a00084u
#define GIC_ENABLE 0x17a00104u
#define GIC_CONFIG 0x17a00c08u
#define TCS1_BASE 0x2a0u

static uint32_t rsc[RSC_WORDS];
static uint32_t gic_group, gic_enable, gic_config;
static uint32_t write_offset[256], write_value[256];
static unsigned write_count, read_gic_count, gic_write_count, source_unmasked_reads;
static unsigned source_unmasked_writes, tpl_level, tpl_raises, tpl_restores;
static unsigned stall_calls;
static uint32_t captured[3][3];
/* 0=normal IRQ completion, 1=observable progress then no-IRQ completion,
 * 2=stale complete fields without IRQ/progress, 3=never completes. */
static int completion_mode, trigger_waiting;

static uint32_t mock_rsc_read(uint32_t offset) {
    assert((offset & 3u) == 0 && offset < sizeof(rsc));
    return rsc[offset / 4];
}
static uint32_t tp_read32(uint64_t address) {
    if (address == GIC_GROUP1) { ++read_gic_count; return gic_group; }
    if (address == GIC_ENABLE) { ++read_gic_count; return gic_enable; }
    if (address == GIC_CONFIG) { ++read_gic_count; return gic_config; }
    assert(address >= RSC_BASE && address < RSC_BASE + sizeof(rsc));
    uint32_t offset = (uint32_t)(address - RSC_BASE);
    uint32_t value = mock_rsc_read(offset);
    if (address == TCS_BASE && (value & 2u)) ++source_unmasked_reads;
    return value;
}
static void mock_rp_write(uint32_t offset, uint32_t value) {
    assert(tpl_level >= 31); /* Every MMIO write is serialized at high TPL. */
    assert((offset & 3u) == 0 && offset < sizeof(rsc) - 0xd00u);
    assert(write_count < sizeof(write_offset) / sizeof(write_offset[0]));
    write_offset[write_count] = offset;
    write_value[write_count++] = value;
    if (offset == 0 && (value & 2u)) ++source_unmasked_writes;
    uint32_t bankoff = 0xd00u + offset;
    if (offset == 8) { /* W1C clears only the requested completion bit. */
        rsc[(0xd04u) / 4] &= ~value;
        return;
    }
    rsc[bankoff / 4] = value;
    if (offset == TCS1_BASE + 0x14 && value == 0x1010000u) {
        assert(rsc[(0xd00u+TCS1_BASE+0x1cu)/4]==1);
        assert(rsc[(0xd00u+TCS1_BASE+0x10u)/4]==1);
        assert(rsc[(0xd00u+TCS1_BASE+0x34u)/4]==0x42e04u);
        for (unsigned c = 0; c < 3; ++c) {
            uint32_t b = TCS1_BASE + c * 0x14;
            captured[c][0] = rsc[(0xd00u + b + 0x30u) / 4];
            captured[c][1] = rsc[(0xd00u + b + 0x34u) / 4];
            captured[c][2] = rsc[(0xd00u + b + 0x38u) / 4];
            rsc[(0xd00u + b + 0x3cu) / 4] =
                (completion_mode == 0 || completion_mode == 2) ? 0x10100u : 0;
        }
        if (completion_mode == 0) {
            rsc[(0xd00u + TCS1_BASE + 0x18u) / 4] = 1;
            rsc[0xd04u / 4] |= 2u;
        } else if (completion_mode == 1) {
            rsc[(0xd00u + TCS1_BASE + 0x18u) / 4] = 0;
            trigger_waiting = 1;
        } else if (completion_mode == 2) {
            /* Hardware fields falsely look complete from the first sample. */
            rsc[(0xd00u + TCS1_BASE + 0x18u) / 4] = 1;
        } else {
            rsc[(0xd00u + TCS1_BASE + 0x18u) / 4] = 0;
        }
    }
}
static void mock_stall(uint64_t usec) {
    (void)usec; ++stall_calls;
    if (completion_mode == 1 && trigger_waiting) {
        for (unsigned c = 0; c < 3; ++c) {
            uint32_t b = TCS1_BASE + c * 0x14;
            rsc[(0xd00u + b + 0x3cu) / 4] = 0x10100u;
        }
        rsc[(0xd00u + TCS1_BASE + 0x18u) / 4] = 1;
        trigger_waiting = 0;
    }
}
static uint64_t mock_raise_tpl(uint64_t requested) {
    uint64_t old = tpl_level;
    assert(requested == 31);
    ++tpl_raises;
    tpl_level = (unsigned)requested;
    return old;
}
static void mock_restore_tpl(uint64_t old) {
    assert(tpl_level >= 31);
    ++tpl_restores;
    tpl_level = (unsigned)old;
}

static void init_state(void) {
    memset(rsc, 0, sizeof(rsc));
    memset(write_offset, 0, sizeof(write_offset));
    memset(write_value, 0, sizeof(write_value));
    memset(captured, 0, sizeof(captured));
    write_count = read_gic_count = gic_write_count = source_unmasked_reads = 0;
    source_unmasked_writes = tpl_raises = tpl_restores = stall_calls = 0;
    tpl_level = 5; completion_mode = 0; trigger_waiting = 0;
    gic_group = 1u << 5;  /* IRQ 37: Group 1. */
    gic_enable = 0;       /* masked */
    gic_config = 0;       /* level-triggered */
    tp_resource[1] = 0x42e00; tp_matches[1] = 1;
    tp_rsc[0] = 0x20000; tp_rsc[1] = 0; tp_rsc[2] = 0x800c8104;
    tp_db_rc = 0; tp_inventory_done = 1; tp_geometry_valid = 1;
    rp_poisoned = 0;
    rp_last_irq = 0;
    memset(rp_last_status, 0, sizeof(rp_last_status));
    memset(rp_gic, 0, sizeof(rp_gic));
    rp_latch_irq = 0;
    mock_bs.stall = mock_stall;
    mock_bs.raise_tpl = mock_raise_tpl;
    mock_bs.restore_tpl = mock_restore_tpl;

    /* TCS1 source bit is initially clear; unrelated IRQ status must survive. */
    rsc[0xd00u / 4] = 0xa0;
    rsc[0xd04u / 4] = 0x20;
    /* TCS0 is inherited/busy and owns an unrelated command. */
    rsc[(0xd00u + 0x18u) / 4] = 2;
    rsc[(0xd00u + 0x14u) / 4] = 0x10001;
    rsc[(0xd00u + 0x1cu) / 4] = 1;
    rsc[(0xd00u + 0x34u) / 4] = 0x44000;
    rsc[(0xd00u + 0x38u) / 4] = 0xabcdef01;
    /* TCS1 is idle and has payload that must be restored after each vote. */
    rsc[(0xd00u + TCS1_BASE + 0x18u) / 4] = 1;
    rsc[(0xd00u + TCS1_BASE + 0x14u) / 4] = 0x10000;
    rsc[(0xd00u + TCS1_BASE + 0x1cu) / 4] = 0;
    rsc[(0xd00u + TCS1_BASE + 0x10u) / 4] = 0;
    for (unsigned c = 0; c < 3; ++c) {
        uint32_t b = TCS1_BASE + c * 0x14;
        rsc[(0xd00u + b + 0x30u) / 4] = 0xabc00000u + c;
        rsc[(0xd00u + b + 0x34u) / 4] = 0xdef00000u + c;
        rsc[(0xd00u + b + 0x38u) / 4] = 0x12340000u + c;
    }
}
static uint32_t get_rsc(uint32_t offset) { return mock_rsc_read(0xd00u + offset); }
static void assert_no_tcs0_writes(void) {
    for (unsigned i = 0; i < write_count; ++i) {
        uint32_t off = write_offset[i];
        /* Only shared IRQ enable/W1C controls or the TCS1 window are writable. */
        if (off == 0 || off == 8) continue;
        assert(off >= TCS1_BASE && off < TCS1_BASE + 0x2a0);
    }
}
static void assert_tcs1_restored(void) {
    assert(get_rsc(TCS1_BASE + 0x18) == 1);
    assert(get_rsc(TCS1_BASE + 0x14) == 0x10000);
    assert(get_rsc(TCS1_BASE + 0x1c) == 0);
    assert(get_rsc(TCS1_BASE + 0x10) == 0);
    for (unsigned c = 0; c < 3; ++c) {
        uint32_t b = TCS1_BASE + c * 0x14;
        assert(get_rsc(b + 0x30) == 0xabc00000u + c);
        assert(get_rsc(b + 0x34) == 0xdef00000u + c);
        assert(get_rsc(b + 0x38) == 0x12340000u + c);
    }
    assert(get_rsc(0) == 0xa0);
    assert(get_rsc(4) == 0x20); /* W1C did not clear unrelated IRQ status. */
}
static void assert_vote_payload(uint32_t address,uint32_t mv,int enable) {
 assert(mv==1800);
 assert(captured[0][0]==0x10108 && captured[0][1]==address+4 && captured[0][2]==(uint32_t)enable);
 for(unsigned c=1;c<3;c++) {
  assert(captured[c][0]==0xabc00000u+c && captured[c][1]==0xdef00000u+c && captured[c][2]==0x12340000u+c);
 }
}
static void assert_tpl_restored(void) {
    assert(tpl_level == 5 && tpl_raises == tpl_restores);
}
static void test_success_on_off_preserves_firmware_state(void) {
    init_state();
    assert(rp_vote(0x42e00, 1800, 1) == 0);
    assert_vote_payload(0x42e00, 1800, 1); assert_tcs1_restored(); assert_no_tcs0_writes();
    assert(write_count > 0 && read_gic_count == 3); assert_tpl_restored();
    assert(gic_group == (1u << 5) && gic_enable == 0 && gic_config == 0);

    unsigned writes_before_off = write_count;
    assert(rp_vote(0x42e00, 1800, 0) == 0);
    assert_vote_payload(0x42e00, 1800, 0); assert_tcs1_restored(); assert_no_tcs0_writes();
    assert(write_count > writes_before_off && read_gic_count == 6); assert_tpl_restored();
    assert(gic_group == (1u << 5) && gic_enable == 0 && gic_config == 0);
}

static void test_whitelisted_ldoa13_payload_and_other_targets_reject(void) {
 init_state();tp_resource[0]=0x40b00;tp_matches[0]=1;
 assert(rp_vote(0x40b00,3008,1)==3 && write_count==0);
 assert(rp_vote(0x42e00,1801,1)==3 && write_count==0);
 assert(rp_vote(0x44000,1800,1)==3 && write_count==0);
}
static void test_argument_inventory_and_geometry_guards(void) {
    init_state(); assert(rp_vote(0x43008, 1800, 1) == 3);
    assert(write_count == 0 && read_gic_count == 0 && tpl_raises == 0);
    init_state(); tp_geometry_valid = 0; assert(rp_vote(0x42e00, 1800, 1) == 3);
    assert(write_count == 0 && read_gic_count == 0 && tpl_raises == 0);
    init_state(); tp_db_rc = 1; assert(rp_vote(0x42e00, 1800, 1) == 3);
    assert(write_count == 0 && read_gic_count == 0 && tpl_raises == 0);
    init_state(); tp_matches[1] = 2; assert(rp_vote(0x42e00, 1800, 1) == 3);
    assert(write_count == 0 && read_gic_count == 0 && tpl_raises == 0);
    init_state(); tp_rsc[0] = 0xffffffff; assert(rp_vote(0x42e00, 1800, 1) == 3);
    assert(write_count == 0 && read_gic_count == 0 && tpl_raises == 0);
    init_state(); tp_rsc[2] = 0xffffffff; assert(rp_vote(0x42e00, 1800, 1) == 3);
    assert(write_count == 0 && read_gic_count == 0 && tpl_raises == 0);
}

static void assert_nonlatching_vote_keeps_source_masked(void) {
    completion_mode = 1; /* Busy/incomplete first, then complete without IRQ. */
    assert(rp_latch_irq == 0);
    assert(rp_vote(0x42e00, 1800, 1) == 0);
    assert(rp_latch_irq == 0 && rp_last_irq == 0x20);
    assert(source_unmasked_reads == 0 && source_unmasked_writes == 0);
    assert(gic_write_count == 0 && gic_group != 0xffffffffu);
    assert_tcs1_restored(); assert_no_tcs0_writes(); assert_tpl_restored();
}
static void test_nonlatching_gic_states_use_progress_without_irq(void) {
    init_state(); gic_group = 0; assert_nonlatching_vote_keeps_source_masked();
    assert(gic_group == 0 && gic_enable == 0 && gic_config == 0);
    init_state(); gic_enable = 1u << 5; assert_nonlatching_vote_keeps_source_masked();
    assert(gic_group == (1u << 5) && gic_enable == (1u << 5) && gic_config == 0);
    init_state(); gic_config = 1u << 10; assert_nonlatching_vote_keeps_source_masked();
    assert(gic_group == (1u << 5) && gic_enable == 0 && gic_config == (1u << 10));
}
static void test_all_ones_gic_rejects_without_writes(void) {
    init_state(); gic_group = UINT32_MAX;
    assert(rp_vote(0x42e00, 1800, 1) == 4);
    assert(write_count == 0 && tpl_raises == 0 && tpl_restores == 0);
    assert(gic_write_count == 0);
}

static void assert_preflight_reject(uint32_t expected) {
    assert(rp_vote(0x42e00, 1800, 1) == expected);
    assert(write_count == 0 && tpl_raises == 1 && tpl_restores == 1);
    assert_tpl_restored();
}
static void test_slot_irq_and_conflict_guards(void) {
    init_state(); rsc[(0xd00u + TCS1_BASE + 0x18u) / 4] = 0;
    assert_preflight_reject(5);

    init_state(); rsc[0xd04u / 4] |= 2u; /* pending TCS1 completion */
    assert_preflight_reject(5);

    init_state(); rsc[(0xd00u + 0x34u) / 4] = 0x42e00;
    assert_preflight_reject(5);
    assert(get_rsc(0x34) == 0x42e00); /* inherited TCS0 payload preserved */
}

static void test_all_ones_source_is_never_rmw_written(void) {
    init_state(); rsc[0xd00u / 4] = 0xffffffffu;
    assert_preflight_reject(6);
    assert(write_count == 0 && get_rsc(0) == 0xffffffffu);
}

static void test_timeout_poison_quarantines_and_blocks_retry(void) {
    init_state(); completion_mode = 3;
    assert(rp_vote(0x42e00, 1800, 1) == 11);
    assert(rp_poisoned == 1 && write_count > 0 && tpl_level == 5);
    assert(tpl_raises == 1 && tpl_restores == 1);
    assert((get_rsc(0) & 2u) == 0); /* uncertain source remains masked */
    unsigned writes_after_timeout = write_count;
    unsigned raises_after_timeout = tpl_raises;
    assert(rp_vote(0x42e00, 1800, 0) == 2);
    assert(write_count == writes_after_timeout && tpl_raises == raises_after_timeout);
    assert_tpl_restored();
}

static void test_no_irq_completes_only_after_observed_progress(void) {
    init_state(); gic_group = 0; completion_mode = 1;
    assert(rp_vote(0x42e00, 1800, 1) == 0);
    assert(rp_last_irq == 0x20 && !(rp_last_irq & 2u));
    assert(rp_latch_irq == 0 && source_unmasked_reads == 0 && source_unmasked_writes == 0);
    assert(stall_calls == 1 && rp_poisoned == 0);
    assert_tcs1_restored(); assert_no_tcs0_writes(); assert_tpl_restored();

    init_state(); gic_group = 0; completion_mode = 2;
    assert(rp_vote(0x42e00, 1800, 1) == 11);
    assert(rp_last_irq == 0x20 && !(rp_last_irq & 2u));
    assert(rp_latch_irq == 0 && source_unmasked_reads == 0 && source_unmasked_writes == 0);
    assert(stall_calls == 100000 && rp_poisoned == 1);
    assert(tpl_level == 5 && tpl_raises == 1 && tpl_restores == 1);
}

int main(void) {
    test_success_on_off_preserves_firmware_state();
    test_whitelisted_ldoa13_payload_and_other_targets_reject();
    test_argument_inventory_and_geometry_guards();
    test_nonlatching_gic_states_use_progress_without_irq();
    test_all_ones_gic_rejects_without_writes();
    test_slot_irq_and_conflict_guards();
    test_all_ones_source_is_never_rmw_written();
    test_timeout_poison_quarantines_and_blocks_retry();
    test_no_irq_completes_only_after_observed_progress();
    puts("RPMh enable-only host tests passed (9 groups; only VIO ENABLE command; mode/voltage untouched)");
    return 0;
}
