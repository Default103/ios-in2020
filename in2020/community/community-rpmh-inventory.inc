/* Read-only OnePlus 8 Pro apps-DRV2/CmdDB inventory. No TCS/GIC/PMIC writes.
 * Bounds and board inputs: artifacts/touch-bringup/rpmh-platform-reference.json.
 * CmdDB layout: Linux cmd-db.c; comparable fixed UMI client is recorded in
 * artifacts/driver-research/umi-rpmh.c. No address from the DB is dereferenced.
 */
#define TP_DB_BASE 0x80860000ULL
#define TP_DB_SIZE 0x20000u
#define TP_DB_HEADER 144u
static uint32_t tp_rsc[3], tp_tcs[2][4], tp_irq[2];
static uint32_t tp_db_header[4], tp_resource[2], tp_matches[2], tp_aux[2][2];
static uint32_t tp_db_rc;
static int tp_inventory_done, tp_geometry_valid;
static uint32_t tp_read32(uint64_t address) {
    uint32_t value = *(volatile uint32_t *)(uintptr_t)address;
    __asm__ volatile("dmb oshld" ::: "memory");
    return value;
}
static int tp_db_range(uint32_t offset, uint32_t length) {
    return offset <= TP_DB_SIZE && length <= TP_DB_SIZE - offset;
}
static uint32_t tp_db32(uint32_t offset) {
    return tp_read32(TP_DB_BASE + offset);
}
static uint16_t tp_db16(uint32_t offset) {
    return (uint16_t)(tp_db32(offset & ~3u) >> (8u * (offset & 3u)));
}
static uint32_t tp_parse_db(void) {
    uint32_t keys[2][2] = {{0,0},{0,0}};
    tp_resource[0] = tp_resource[1] = tp_matches[0] = tp_matches[1] = 0;
    for (unsigned n = 0; n < 2; ++n)
        for (unsigned i = 0; i < 8 && (n ? "ldoc1" : "ldoa13")[i]; ++i)
            keys[n][i/4] |= (uint32_t)(uint8_t)(n ? "ldoc1" : "ldoa13")[i] << ((i%4)*8);
    tp_db_header[0] = tp_db32(0);
    tp_db_header[1] = tp_db32(4);
    tp_db_header[2] = tp_db32(136);
    tp_db_header[3] = tp_db32(140);
    if (tp_db_header[0] != 1 || tp_db_header[1] != 0x0c0330db || (tp_db_header[3] & 1u)) return 1;
    for (unsigned i = 0; i < 8; ++i) {
        uint32_t h = 8 + 16*i;
        unsigned slave = tp_db16(h);
        if (slave == 0) break;
        uint32_t entries = TP_DB_HEADER + tp_db16(h+2);
        uint32_t aux = TP_DB_HEADER + tp_db16(h+4);
        uint32_t count = tp_db16(h+6);
        if (slave < 3 || slave > 5 || (entries & 3u) ||
            !tp_db_range(entries, count*24u) || !tp_db_range(aux, 0)) return 2;
        for (uint32_t j = 0; j < count; ++j) {
            uint32_t e = entries + 24*j;
            for (unsigned n = 0; n < 2; ++n) {
                if (tp_db32(e) != keys[n][0] || tp_db32(e+4) != keys[n][1]) continue;
                uint32_t address = tp_db32(e+16);
                uint32_t aux_offset = aux + tp_db16(e+22), aux_len = tp_db16(e+20);
                if (slave != 4 || (address >> 16) != 4 || (address & 15u) ||
                    !tp_db_range(aux_offset, aux_len)) return 3;
                if (tp_matches[n] && tp_resource[n] != address) return 4;
                tp_resource[n] = address; ++tp_matches[n];
                tp_aux[n][0] = aux_len; tp_aux[n][1] = tp_db16(h+8);
            }
        }
    }
    return tp_matches[0] && tp_matches[1] ? 0 : 5;
}
static const char *tp_rpmh_inventory(void) {
    tp_geometry_valid = 0;
    tp_rsc[0] = tp_read32(0x18220000);
    tp_rsc[1] = tp_read32(0x18220004);
    tp_rsc[2] = tp_read32(0x1822000c);
    tp_db_rc = tp_parse_db();
    unsigned major = (tp_rsc[0] >> 16) & 255;
    unsigned count = (tp_rsc[2] >> 12) & 63;
    unsigned commands = (tp_rsc[2] >> 27) & 31;
    tp_inventory_done = 1;
    if ((major != 1 && major != 2) || count < 8 || count > 16 || commands < 3 || commands > 16)
        return "FAILRSC geometry unsupported; DB result still available";
    tp_geometry_valid = 1;
    tp_irq[0] = tp_read32(0x18220d00);
    tp_irq[1] = tp_read32(0x18220d04);
    for (unsigned i = 0; i < 2; ++i) {
        uint64_t base = 0x18220d00ULL + i*0x2a0;
        tp_tcs[i][0] = tp_read32(base+0x18); /* status */
        tp_tcs[i][1] = tp_read32(base+0x14); /* control */
        tp_tcs[i][2] = tp_read32(base+0x1c); /* command enable */
        tp_tcs[i][3] = tp_read32(base+0x10); /* wait mask */
    }
    if (tp_db_rc != 0) return "FAILCmdDB verification failed; snapshot saved";
    return "OKAYread-only RSC and CmdDB snapshot saved";
}
