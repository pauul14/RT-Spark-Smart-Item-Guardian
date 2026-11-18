/*
 * sd_logger.c - microSD product log (logging/communication function).
 *
 * When a FAT-formatted card is present the health task appends:
 *   SIGUARD/EVENTS.CSV  every product event (rough handling, test alert, gesture, ack, faults, boot)
 *   SIGUARD/CLIMATE.CSV climate every 5 min (temperature, humidity, light, proximity counts)
 * so the owner can read the device's history on a PC without any network.
 * Safety of the user's card: the card is never formatted (FatFs built without mkfs);
 * only this directory and these two files are created/appended; each record is
 * written with open-append-close so pulling the card loses at most one line.
 * Enabled by default; "sd off" disables it (kept in an RTC backup register).
 */
#include "app.h"
#include "bsp.h"
#include "sd_card.h"
#include "ff.h"
#include "mini_printf.h"
#include <string.h>

#define SD_RETRY_MS     30000u
#define CLIMATE_MS      300000u
#define DIR_NAME        "SIGUARD"
#define EVENTS_FILE     "SIGUARD/EVENTS.CSV"
#define CLIMATE_FILE    "SIGUARD/CLIMATE.CSV"

static FATFS s_fs;
static bool s_mounted;
static int s_last_fr = -1;
static uint32_t s_last_try, s_last_climate, s_lines, s_errors;

static const char *const FR_NAMES[] = { "OK", "DISK_ERR", "INT_ERR", "NOT_READY", "NO_FILE", "NO_PATH",
    "INVALID_NAME", "DENIED", "EXIST", "INVALID_OBJECT", "WRITE_PROTECTED", "INVALID_DRIVE", "NOT_ENABLED",
    "NO_FILESYSTEM", "MKFS_ABORTED", "TIMEOUT", "LOCKED", "NOT_ENOUGH_CORE", "TOO_MANY_OPEN_FILES",
    "INVALID_PARAMETER" };

static const char *fr_name(int fr) { return fr >= 0 && fr < (int)(sizeof FR_NAMES / sizeof FR_NAMES[0]) ? FR_NAMES[fr] : "?"; }

bool sdlog_enabled(void) { return !app_flag(FLAG_SD_OFF); }

static void unmount(void)
{
    if (s_mounted) f_mount(NULL, "", 0);
    s_mounted = false;
    sd_card_deinit();
}

static void stamp(char *b, size_t n)
{
    rtc_time_t t;
    if (rtc_time_valid() && rtc_get(&t)) {
        sig_snprintf(b, n, "%04u-%02u-%02u %02u:%02u:%02u", t.year, t.mon, t.day, t.hour, t.min, t.sec);
    } else {
        sig_snprintf(b, n, "rtc-not-set");
    }
}

static bool append(const char *path, const char *header, const char *line)
{
    FIL f;
    FRESULT fr = f_open(&f, path, FA_OPEN_APPEND | FA_WRITE);
    if (fr == FR_OK) {
        UINT w;
        if (f_size(&f) == 0 && header) fr = f_write(&f, header, (UINT)strlen(header), &w);
        if (fr == FR_OK) fr = f_write(&f, line, (UINT)strlen(line), &w);
        FRESULT fc = f_close(&f);
        if (fr == FR_OK) fr = fc;
    }
    s_last_fr = (int)fr;
    if (fr != FR_OK) {
        s_errors++;
        sig_log("#SDLOG,write_failed,%s,%s", path, fr_name(fr));
        unmount();
        return false;
    }
    s_lines++;
    return true;
}

static bool try_mount(void)
{
    sd_info_t si;
    if (!sd_card_init(&si)) { s_last_fr = FR_NOT_READY; return false; }
    FRESULT fr = f_mount(&s_fs, "", 1);
    s_last_fr = (int)fr;
    if (fr != FR_OK) {
        sig_log("#SDLOG,mount_failed,%s,card_mb=%lu (card left untouched; FAT12/16/32 required)", fr_name(fr),
                (unsigned long)(si.blocks / 2048u));
        unmount();
        return false;
    }
    fr = f_mkdir(DIR_NAME);
    if (fr != FR_OK && fr != FR_EXIST) {
        sig_log("#SDLOG,mkdir_failed,%s", fr_name(fr));
        unmount();
        return false;
    }
    s_mounted = true;
    sig_log("#SDLOG,mounted,card_mb=%lu,bus=%s,files=" EVENTS_FILE ";" CLIMATE_FILE,
            (unsigned long)(si.blocks / 2048u), si.wide_bus ? "4bit" : "1bit");
    return true;
}

void sdlog_poll(uint32_t now)
{
    if (!sdlog_enabled()) { if (s_mounted) unmount(); return; }
    if (!s_mounted && (s_last_try == 0 || now - s_last_try >= SD_RETRY_MS)) {
        s_last_try = now ? now : 1;
        try_mount();
    }
    if (s_mounted && (s_last_climate == 0 || now - s_last_climate >= CLIMATE_MS)) {
        s_last_climate = now ? now : 1;
        char ts[24], line[128];
        stamp(ts, sizeof ts);
        STATE_LOCK();
        bool v = g_app.aht_valid;
        float t = g_app.aht_t, h = g_app.aht_rh;
        als_reading_t a = g_app.als;
        bool humid = g_app.humid_warn;
        STATE_UNLOCK();
        if (v) sig_snprintf(line, sizeof line, "%s,%lu,%.2f,%.1f,%.1f,%u,%d\r\n", ts, (unsigned long)now, (double)t,
                            (double)h, (double)ap3216c_lux(a.als), a.ps, humid);
        else sig_snprintf(line, sizeof line, "%s,%lu,,,%.1f,%u,%d\r\n", ts, (unsigned long)now,
                          (double)ap3216c_lux(a.als), a.ps, humid);
        append(CLIMATE_FILE, "rtc_time,uptime_ms,temp_c,rh_pct,lux,ps_counts,humid_warning\r\n", line);
    }
}

void sdlog_event(const evt_msg_t *e)
{
    if (!s_mounted) return;
    char ts[24], line[128];
    stamp(ts, sizeof ts);
    sig_snprintf(line, sizeof line, "%s,%lu,%lu,%s,%s,%u\r\n", ts, (unsigned long)e->t_ms,
                 (unsigned long)sig_boot_count(), evt_name(e->type), sig_class_name(e->cls), e->value);
    append(EVENTS_FILE, "rtc_time,uptime_ms,boot,event,class,value\r\n", line);
}

void sdlog_set_enabled(bool en)
{
    app_set_flag(FLAG_SD_OFF, !en);
    s_last_try = 0;
}

void sdlog_after_diag(void)
{
    /* diag_sdcard re-initialised the card underneath FatFs: force a clean remount. */
    if (s_mounted) f_mount(NULL, "", 0);
    s_mounted = false;
    s_last_try = 0;
}

void sdlog_status(void)
{
    const sd_info_t *si = sd_card_info();
    sig_log("#SDLOG,enabled=%d,mounted=%d,card_present=%d,cd_pin=%d,last_result=%s,lines=%lu,errors=%lu,"
            "read_err=%lu,write_err=%lu", sdlog_enabled(), s_mounted, si->present, sd_card_detect_level(),
            s_last_fr < 0 ? "none" : fr_name(s_last_fr), (unsigned long)s_lines, (unsigned long)s_errors,
            (unsigned long)g_sd_read_err, (unsigned long)g_sd_write_err);
}

bool sdlog_mounted(void) { return s_mounted; }
