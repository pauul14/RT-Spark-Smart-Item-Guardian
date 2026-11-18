/*
 * eventlog.c - product event records.
 *
 * Every event is always printed as "#EVT" and kept in a RAM ring. Persisting
 * to the W25Q128 is OPT-IN ("log enable", flag kept in an RTC backup register)
 * because the external flash may hold data from earlier firmware. The region is
 * the last 64 KB of the detected chip (0x7F0000.. on the W25Q64 fitted to this board). It is claimed only if it is completely
 * blank (all 0xFF) or already carries this firmware's header; foreign data is
 * never erased automatically. Records are appended; when full, logging stops
 * until the operator runs "log erase CONFIRM" (which erases only this region).
 */
#include "app.h"
#include "spi_flash.h"
#include "rtc_clock.h"
#include <string.h>

#define REGION_BASE   s_base            /* last 64 KB of the DETECTED chip (W25Q64 found on this board) */
#define REGION_SIZE   0x00010000u
#define REC_SIZE      16u
#define HDR_MAGIC     "SIGLOG01"
#define REC_MAGIC     0xA5u
#define RAM_EVENTS    16

typedef struct __attribute__((packed)) {
    uint8_t  magic, type, cls, sum;
    uint16_t value, rsv;
    uint32_t seq;
    uint32_t t_ms;
} rec_t;
_Static_assert(sizeof(rec_t) == REC_SIZE, "record size");

typedef enum { RS_UNKNOWN = 0, RS_DISABLED, RS_FOREIGN, RS_READY, RS_FULL, RS_ERROR } region_state_t;
static region_state_t s_state;
static uint32_t s_base;
static uint32_t s_next;            /* offset of the next free record */
static uint32_t s_seq;
static evt_msg_t s_ram[RAM_EVENTS];
static uint32_t s_ram_n;

const char *evt_name(uint8_t t)
{
    static const char *const n[] = { "BOOT", "ROUGH", "TEST_ALERT", "PROX_GESTURE", "ACK",
                                     "SENSOR_FAULT", "FAULT_CLEAR", "HUMID", "IR_REMOTE" };
    return t < sizeof(n) / sizeof(n[0]) ? n[t] : "?";
}

static const char *state_name(void)
{
    static const char *const n[] = { "unknown", "disabled", "foreign-data(not touched)", "ready", "full", "error" };
    return n[s_state];
}

bool evlog_writes_enabled(void) { return app_flag(FLAG_EVLOG_ON); }

void evlog_set_enabled(bool en)
{
    app_set_flag(FLAG_EVLOG_ON, en);
    s_state = RS_UNKNOWN;
}

static uint8_t rec_sum(const rec_t *r)
{
    const uint8_t *p = (const uint8_t *)r;
    uint8_t s = 0;
    for (unsigned i = 0; i < REC_SIZE; i++) if (i != 3) s = (uint8_t)(s + p[i]);
    return s;
}

static bool all_ff(const uint8_t *p, unsigned n)
{
    for (unsigned i = 0; i < n; i++) if (p[i] != 0xFF) return false;
    return true;
}

/* Scan the region; caller holds the SPI2 lock. */
static void probe_region(void)
{
    uint8_t buf[256];
    if (!w25q_read(REGION_BASE, buf, sizeof buf)) { s_state = RS_ERROR; return; }
    if (memcmp(buf, HDR_MAGIC, 8) != 0) {
        /* Blank region? Check all of it before claiming. */
        for (uint32_t off = 0; off < REGION_SIZE; off += sizeof buf) {
            if (!w25q_read(REGION_BASE + off, buf, sizeof buf)) { s_state = RS_ERROR; return; }
            if (!all_ff(buf, sizeof buf)) { s_state = RS_FOREIGN; return; }
        }
        static const uint8_t hdr[16] = { 'S', 'I', 'G', 'L', 'O', 'G', '0', '1', 0, 0, 0, 0, 0, 0, 0, 0 };
        if (!w25q_program_page(REGION_BASE, hdr, sizeof hdr)) { s_state = RS_ERROR; return; }
        s_next = REC_SIZE;
        s_seq = 0;
        s_state = RS_READY;
        return;
    }
    /* Ours: find the first blank record slot. */
    s_next = REGION_SIZE;
    for (uint32_t off = 0; off < REGION_SIZE; off += sizeof buf) {
        if (!w25q_read(REGION_BASE + off, buf, sizeof buf)) { s_state = RS_ERROR; return; }
        for (uint32_t i = (off == 0 ? REC_SIZE : 0); i < sizeof buf; i += REC_SIZE) {
            const rec_t *r = (const rec_t *)(buf + i);
            if (all_ff(buf + i, REC_SIZE)) { s_next = off + i; goto found; }
            if (r->magic == REC_MAGIC && r->seq >= s_seq) s_seq = r->seq + 1;
        }
    }
found:
    s_state = s_next >= REGION_SIZE ? RS_FULL : RS_READY;
}

void evlog_init(void)
{
    s_state = RS_UNKNOWN;
    if (!g_app.flash_ok || g_app.flash.size_bytes < 2u * REGION_SIZE) { s_state = RS_ERROR; return; }
    s_base = g_app.flash.size_bytes - REGION_SIZE;
    if (!evlog_writes_enabled()) { s_state = RS_DISABLED; return; }
    if (spi2_lock(500)) { probe_region(); spi2_unlock(); }
}

void evlog_handle(const evt_msg_t *e)
{
    s_ram[s_ram_n % RAM_EVENTS] = *e;
    s_ram_n++;
    if (s_state == RS_UNKNOWN) evlog_init();
    if (s_state != RS_READY) return;
    rec_t r = { REC_MAGIC, e->type, e->cls, 0, e->value, 0, s_seq, e->t_ms };
    r.sum = rec_sum(&r);
    bool ok = false;
    if (spi2_lock(200)) {
        ok = w25q_program_page(REGION_BASE + s_next, (const uint8_t *)&r, REC_SIZE);
        spi2_unlock();
    }
    if (ok) {
        s_seq++;
        s_next += REC_SIZE;
        g_cnt.evlog_written++;
        if (s_next >= REGION_SIZE) s_state = RS_FULL;
    } else {
        g_cnt.evlog_failed++;
    }
}

void evlog_status(void)
{
    sig_log("#LOG,state=%s,writes_enabled=%d,region=0x%06lx+0x%05lx,next=%lu,records=%lu,ram_events=%lu",
            state_name(), evlog_writes_enabled(), (unsigned long)REGION_BASE, (unsigned long)REGION_SIZE,
            (unsigned long)s_next, (unsigned long)(s_next >= REC_SIZE ? s_next / REC_SIZE - 1 : 0),
            (unsigned long)s_ram_n);
}

void evlog_dump(uint32_t max)
{
    uint32_t n = s_ram_n < RAM_EVENTS ? s_ram_n : RAM_EVENTS;
    for (uint32_t i = 0; i < n; i++) {
        const evt_msg_t *e = &s_ram[(s_ram_n - n + i) % RAM_EVENTS];
        sig_log("#LOGRAM,%lu,%s,%s,%u", (unsigned long)e->t_ms, evt_name(e->type), sig_class_name(e->cls), e->value);
    }
    if (s_state != RS_READY && s_state != RS_FULL) { evlog_status(); return; }
    if (!max) max = 50;
    uint32_t total = s_next / REC_SIZE - 1;
    uint32_t first = total > max ? total - max : 0;
    for (uint32_t i = first; i < total; i++) {
        rec_t r;
        bool ok = false;
        if (spi2_lock(200)) { ok = w25q_read(REGION_BASE + (i + 1) * REC_SIZE, (uint8_t *)&r, REC_SIZE); spi2_unlock(); }
        if (!ok) break;
        sig_log("#LOGREC,%lu,%lu,%s,%s,%u,%s", (unsigned long)r.seq, (unsigned long)r.t_ms, evt_name(r.type),
                sig_class_name(r.cls), r.value, (r.magic == REC_MAGIC && r.sum == rec_sum(&r)) ? "ok" : "BAD");
    }
}

void evlog_erase(void)
{
    if (s_state != RS_READY && s_state != RS_FULL) {
        sig_log("#LOG,erase_refused,state=%s", state_name());
        return;
    }
    bool locked = spi2_lock(1000), ok = locked;
    for (uint32_t off = 0; ok && off < REGION_SIZE; off += W25Q_SECTOR) {
        ok = w25q_erase_sector(REGION_BASE + off);
        health_wd_poll();                    /* worst case 16 x 400 ms exceeds the IWDG period */
    }
    if (locked) spi2_unlock();
    s_state = RS_UNKNOWN;
    sig_log("#LOG,erase,%s", ok ? "done" : "FAILED");
    evlog_init();
}
