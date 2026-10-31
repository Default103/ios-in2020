static int rp_poisoned, rp_latch_irq;
static uint32_t rp_last_irq,rp_last_status[3],rp_gic[3];
static uint32_t rp_read(uint32_t offset) { return tp_read32(0x18220d00ULL + offset); }
static void rp_write(uint32_t offset, uint32_t value) {
    __asm__ volatile("dmb oshst" ::: "memory");
    *(volatile uint32_t *)(uintptr_t)(0x18220d00ULL + offset) = value;
    __asm__ volatile("dsb osh" ::: "memory");
}
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
    uint32_t saved[3][3], irq_before = 0, result = 1;
    uint64_t old_tpl;
    int claimed = 0;
    if (rp_poisoned) return 2;
    int target_ok = (address == tp_resource[1] && address == 0x43000 && mv == 1800 && tp_matches[1] == 1) ||
        (address == tp_resource[0] && address == 0x40d00 && mv == 3008 && tp_matches[0] == 1);
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
    for (unsigned c = 0; c < 3; ++c) {
        uint32_t b = 0x2a0+c*0x14;
        saved[c][0] = rp_read(b+0x30); saved[c][1] = rp_read(b+0x34); saved[c][2] = rp_read(b+0x38);
        if (saved[c][0] == UINT32_MAX || saved[c][1] == UINT32_MAX || saved[c][2] == UINT32_MAX) {
            result = 15; goto out;
        }
    }
    claimed = 1; rp_poisoned = 1;
    if (!rp_sync(0, irq_before & ~2u)) { result = 7; goto out; }
    if (!rp_empty()) { result = 8; goto out; }
    const uint32_t offsets[3] = {0, 8, 4}, values[3] = {mv, 7, (uint32_t)enable};
    for (unsigned c = 0; c < 3; ++c) {
        uint32_t b = 0x2a0+c*0x14;
        /* length=8, response requested, write: 0x10108. */
        if (!rp_sync(b+0x30, 0x10108) || !rp_sync(b+0x34, address+offsets[c]) ||
            !rp_sync(b+0x38, values[c])) { result = 9; goto out; }
    }
    if (!rp_sync(0x2a0+0x10, 7) || !rp_sync(0x2a0+0x1c, 7) ||
        !rp_sync(0x2a0+0x14, 0) || !rp_sync(0x2a0+0x14, 0x10000) ||
        (rp_latch_irq && !rp_sync(0, irq_before | 2u))) { result = 10; goto out; }
    rp_write(0x2a0+0x14, 0x1010000);
    int complete = 0, progress = 0;
    for (unsigned us = 0; us < 100000; ++us) {
        rp_last_irq = rp_read(4);
        uint32_t idle = rp_read(0x2a0+0x18);
        int commands_complete = 1;
        for (unsigned c = 0; c < 3; ++c) {
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
    for (unsigned c = 0; c < 3; ++c) {
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
