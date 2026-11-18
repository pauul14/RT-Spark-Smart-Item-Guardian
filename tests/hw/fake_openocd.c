/*
 * fake_openocd.c - stand-in for openocd.exe in the offline self-test of
 * tools/hw/gate1_probe.ps1. Never touches hardware. Scenario from the
 * FAKE_OCD_SCENARIO environment variable; every argv is appended to the file
 * named by FAKE_OCD_ARGLOG so the test can check argument quoting.
 * Output imitates OpenOCD 0.12 (log on stderr, "0xADDR: value" lines).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *sc;
static int is(const char *s) { return strcmp(sc, s) == 0; }

static int probe(void)
{
    if (is("probe_exit")) { fprintf(stderr, "Error: open failed\n"); return 1; }
    fprintf(stderr, "Info : STLINK V2J45M30 (API v2) VID:PID 0483:374B\nInfo : [stm32f4x.cpu] Cortex-M4 r0p1 processor detected\n");
    fprintf(stderr, "0xe0042000: %s \n", is("bad_id") ? "10006419" : "10076413");
    fprintf(stderr, "0x1fff7a22: %s \n", is("bad_flash") ? "0200" : "0400");
    if (!is("missing"))
        fprintf(stderr, "0x40023c14: %s \n", is("rdp1") ? "0fff55ed" : is("wrp") ? "0ffeaaed" : "0fffaaed");
    if (is("short_vec")) fprintf(stderr, "0x08000000: 20020000 \n0x0abcdef0: 11111111 \n");
    else fprintf(stderr, "0x08000000: 20020000 0800fb05 \n");
    fprintf(stderr, "shutdown command invoked\n");
    return 0;
}

static int dump(const char *cmd)
{
    const char *a = strchr(cmd, '{'), *b = a ? strchr(a, '}') : NULL;
    if (!a || !b) { fprintf(stderr, "Error: dump_image path not braced\n"); return 1; }
    char path[1024];
    size_t n = (size_t)(b - a - 1);
    if (n >= sizeof path) return 1;
    memcpy(path, a + 1, n);
    path[n] = 0;
    if (strchr(path, '\\')) { fprintf(stderr, "Error: backslash in Tcl path: %s\n", path); return 1; }
    if (is("dump_exit")) { fprintf(stderr, "Error: target not halted\n"); return 1; }
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "Error: cannot open %s\n", path); return 1; }
    size_t total = is("dump_short") ? 1000 : 1048576;
    unsigned char vec[8] = {0x00, 0x00, 0x02, 0x20, 0x05, 0xfb, 0x00, 0x08};
    if (is("dump_mismatch")) vec[4] = 0x99;
    fwrite(vec, 1, 8, f);
    for (size_t i = 8; i < total; i++) fputc(0xFF, f);
    fclose(f);
    fprintf(stderr, "dumped %u bytes in 3.1s (330 KiB/s)\n", (unsigned)total);
    return 0;
}

int main(int argc, char **argv)
{
    sc = getenv("FAKE_OCD_SCENARIO");
    if (!sc) sc = "ok";
    const char *log = getenv("FAKE_OCD_ARGLOG");
    if (log) {
        FILE *f = fopen(log, "a");
        if (f) { for (int i = 1; i < argc; i++) fprintf(f, "[%s]", argv[i]); fputc('\n', f); fclose(f); }
    }
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-c") && i + 1 < argc) {
            if (!strncmp(argv[i + 1], "dump_image", 10)) return dump(argv[i + 1]);
            if (!strncmp(argv[i + 1], "program", 7) || strstr(argv[i + 1], "erase") || strstr(argv[i + 1], "reset")) {
                fprintf(stderr, "FAKE: forbidden command %s\n", argv[i + 1]);
                return 99;
            }
        }
    }
    return probe();
}
