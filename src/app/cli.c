/*
 * cli.c - console commands (115200 8N1, LF or CR terminated). Output lines
 * start with '#' tags so the tools in tools/hw can parse them; raw samples start "S,".
 * Hardware owned by another task is reached through that task's queue.
 */
#include "app.h"
#include "bsp.h"
#include "model_infer.h"
#include "mini_printf.h"
#include "led_matrix.h"
#include "uart_console.h"
#include "ir_nec.h"
#include <stdlib.h>
#include <string.h>

static int split(char *s, char **argv, int max)
{
    int n = 0;
    while (*s && n < max) {
        while (*s == ' ') s++;
        if (!*s) break;
        argv[n++] = s;
        while (*s && *s != ' ') s++;
        if (*s) *s++ = 0;
    }
    return n;
}

static void cmd_help(void)
{
    static const char *const h[] = {
        "help | ver | status | tasks | heap | cnt | lat [reset]",
        "cap start <label> <session> <trial> | cap stop        (raw 100 Hz stream 'S,...')",
        "diag all|imu|als|aht|flash|sd|rtc|ir|wifi|codec|can|mcu|i2cscan|lcd|matrix|buzzer|leds",
        "golden            run the embedded held-out windows through features+model on target",
        "fault imu on|off  inject an IMU read failure (controlled failure test)",
        "ack | screen <0-3> | verbose on|off",
        "prox | prox cal | prox clear   proximity status / calibration wizard (hands away, then hand over sensor) / forget",
        "alert test        raise a TEST alert (acknowledge with DOWN or a held hand gesture)",
        "cal gyro          re-estimate gyro bias (keep the board still for 2 s)",
        "time | time set YYYY-MM-DD HH:MM:SS",
        "log status | log enable | log disable | log dump [n] | log erase CONFIRM",
        "sd | sd on | sd off      microSD CSV event + climate log (SIGUARD/EVENTS.CSV, CLIMATE.CSV)",
        "ir | ir learn | ir relay on|off | ir clear | ir send <addr> <cmd>   remote control + alert relay",
        "wire | wire can on|off | wire rs485 on|off   opt-in wired event/heartbeat telemetry",
        "say <secs> <text> | ask <secs> <text> | cue start|stop|ok|err|attn|tick   operator prompts",
    };
    for (unsigned i = 0; i < sizeof h / sizeof h[0]; i++) sig_log("#HELP,%s", h[i]);
}

static void cmd_status(void)
{
    STATE_LOCK();
    gl_state_t gl = g_app.gl;
    prox_det_t px = g_app.prox;
    uint8_t cls = g_app.last_cls, base = g_app.last_base, conf = g_app.last_conf;
    bool cap = g_app.capture, fault = g_app.inject_imu_fault;
    STATE_UNLOCK();
    sig_log("#STATUS,t_ms=%lu,shown=%s,last=%s,conf=%u,baseline=%s,alert=%s,acked=%d,rough_events=%lu,"
            "test_alerts=%lu,ack_key=%lu,ack_prox=%lu,ack_ir=%lu,ack_console=%lu,prox_cal=%d,prox_zone=%s,"
            "prox_gestures=%lu,valid_windows=%lu,invalid_windows=%lu,capture=%d,fault_injected=%d,sensor_fault=%d",
            (unsigned long)app_uptime_ms(), sig_class_short(gl.display_class), sig_class_short(cls), conf,
            sig_class_short(base), gl_alert_name(gl.alert), gl.alert_acked, (unsigned long)gl.rough_events,
            (unsigned long)gl.test_alerts, (unsigned long)gl.acks[GL_ACK_KEY], (unsigned long)gl.acks[GL_ACK_PROX],
            (unsigned long)gl.acks[GL_ACK_IR], (unsigned long)gl.acks[GL_ACK_CONSOLE], px.cal.calibrated,
            prox_zone_name(px.zone), (unsigned long)px.gestures, (unsigned long)gl.windows_valid,
            (unsigned long)gl.windows_invalid, cap, fault, gl.sensor_fault);
}

static void cmd_lat(bool reset)
{
    /* Copy each ring out under the lock, sort outside it, so the priority-5
     * sensor task never waits on the state mutex for a sort. */
    static lat_ring_t copy;
    static uint32_t scratch[LAT_RING];
    static const char *const names[] = { "inference_us", "sample_to_decision_us", "sample_to_response_us", "key_to_feedback_us" };
    lat_ring_t *rings[] = { &g_app.lat_inf, &g_app.lat_s2d, &g_app.lat_s2r, &g_app.lat_key };
    for (int i = 0; i < 4; i++) {
        STATE_LOCK();
        if (reset) lat_reset(rings[i]); else copy = *rings[i];
        STATE_UNLOCK();
        if (reset) continue;
        lat_summary_t s;
        lat_summarise(&copy, scratch, &s);
        sig_log("#LAT,%s,n=%lu,total=%lu,min=%lu,median=%lu,p95=%lu,max=%lu", names[i], (unsigned long)s.n,
                (unsigned long)s.total, (unsigned long)s.min, (unsigned long)s.median, (unsigned long)s.p95,
                (unsigned long)s.max);
    }
    STATE_LOCK();
    uint32_t cyc = g_app.last_inf_cycles;
    STATE_UNLOCK();
    if (reset) sig_log("#LAT,reset");
    else sig_log("#LAT,last_inference_cycles=%lu,cpu_hz=%lu", (unsigned long)cyc, (unsigned long)SystemCoreClock);
}

static void cmd_cnt(void)
{
    extern volatile uint32_t g_i2c2_errors, g_i2c2_recoveries, g_swi2c_errors;
    sig_log("#CNT,imu_samples=%lu,imu_invalid=%lu,imu_bus_err=%lu,imu_stuck=%lu,imu_missed=%lu,drdy_timeout=%lu,"
            "imu_qfull=%lu,imu_reinit=%lu,als_err=%lu,aht_err=%lu,i2c2_err=%lu,i2c2_recover=%lu,swi2c_err=%lu",
            (unsigned long)g_cnt.imu_samples, (unsigned long)g_cnt.imu_invalid, (unsigned long)g_cnt.imu_bus_err,
            (unsigned long)g_cnt.imu_stuck, (unsigned long)g_cnt.imu_missed, (unsigned long)g_cnt.imu_drdy_timeout,
            (unsigned long)g_cnt.imu_queue_full, (unsigned long)g_cnt.imu_reinit, (unsigned long)g_cnt.als_err,
            (unsigned long)g_cnt.aht_err, (unsigned long)g_i2c2_errors, (unsigned long)g_i2c2_recoveries,
            (unsigned long)g_swi2c_errors);
    sig_log("#CNT,log_drop_lines=%lu,log_drop_bytes=%lu,uart_rx_overrun=%lu,uart_rx_dropped=%lu,ui_frames=%lu,"
            "key_events=%lu,iwdg_refresh=%lu,task_stall=%lu,evlog_written=%lu,evlog_failed=%lu,ui_qfull=%lu,svc_qfull=%lu",
            (unsigned long)g_cnt.log_dropped_lines, (unsigned long)g_cnt.log_dropped_bytes,
            (unsigned long)g_uart_rx_overrun, (unsigned long)g_uart_rx_dropped, (unsigned long)g_cnt.ui_frames,
            (unsigned long)g_cnt.key_events, (unsigned long)g_cnt.iwdg_refresh, (unsigned long)g_cnt.task_stall,
            (unsigned long)g_cnt.evlog_written, (unsigned long)g_cnt.evlog_failed, (unsigned long)g_cnt.ui_queue_full,
            (unsigned long)g_cnt.svc_queue_full);
}

static void cmd_cap(int argc, char **argv)
{
    if (argc >= 2 && !strcmp(argv[1], "stop")) {
        STATE_LOCK();
        bool was = g_app.capture;
        g_app.capture = false;
        uint32_t n = g_app.cap_samples;
        STATE_UNLOCK();
        sig_log("#CAP,STOP,%d,samples=%lu,t_ms=%lu,log_drop_lines=%lu", was, (unsigned long)n,
                (unsigned long)app_uptime_ms(), (unsigned long)g_cnt.log_dropped_lines);
        return;
    }
    if (argc < 5 || strcmp(argv[1], "start")) { sig_log("#ERR,usage: cap start <label> <session> <trial>"); return; }
    STATE_LOCK();
    strncpy(g_app.cap_label, argv[2], sizeof g_app.cap_label - 1);
    g_app.cap_label[sizeof g_app.cap_label - 1] = 0;
    g_app.cap_session = (uint16_t)atoi(argv[3]);
    g_app.cap_trial = (uint16_t)atoi(argv[4]);
    g_app.cap_samples = 0;
    g_app.cap_start_ms = app_uptime_ms();
    float b[3] = { g_app.gyro_bias[0], g_app.gyro_bias[1], g_app.gyro_bias[2] };
    bool bok = g_app.gyro_bias_ok;
    char lab[20];
    strcpy(lab, g_app.cap_label);
    uint16_t ses = g_app.cap_session, tri = g_app.cap_trial;
    STATE_UNLOCK();
    /* Header first, then enable streaming, so the host sees the bias before any sample. */
    sig_log("#CAP,START,label=%s,session=%u,trial=%u,t_ms=%lu,fs_hz=%d,acc_lsb_per_g=4096,gyro_lsb_per_dps=32.8,"
            "gyro_bias=%.3f;%.3f;%.3f,bias_ok=%d,fw=%s,model=%s", lab, ses, tri, (unsigned long)app_uptime_ms(),
            SIG_FS_HZ, (double)b[0], (double)b[1], (double)b[2], bok, FW_VERSION, sig_model_version());
    STATE_LOCK();
    g_app.capture = true;
    STATE_UNLOCK();
}

static bool parse_time(const char *d, const char *t, rtc_time_t *o)
{
    int Y, M, D, h, m, s;
    if (strlen(d) != 10 || strlen(t) != 8) return false;
    Y = atoi(d); M = atoi(d + 5); D = atoi(d + 8);
    h = atoi(t); m = atoi(t + 3); s = atoi(t + 6);
    if (M < 1 || M > 12 || D < 1 || D > 31 || h > 23 || m > 59 || s > 59) return false;
    o->year = (uint16_t)Y; o->mon = (uint8_t)M; o->day = (uint8_t)D;
    o->hour = (uint8_t)h; o->min = (uint8_t)m; o->sec = (uint8_t)s;
    return true;
}

static const struct { const char *name; uint8_t svc; } s_diag[] = {
    {"all", SVC_DIAG_ALL}, {"imu", SVC_DIAG_IMU}, {"als", SVC_DIAG_ALS}, {"aht", SVC_DIAG_AHT},
    {"flash", SVC_DIAG_FLASH}, {"sd", SVC_DIAG_SD}, {"rtc", SVC_DIAG_RTC}, {"ir", SVC_DIAG_IR},
    {"wifi", SVC_DIAG_WIFI}, {"codec", SVC_DIAG_CODEC}, {"can", SVC_DIAG_CAN}, {"mcu", SVC_DIAG_MCU},
    {"i2cscan", SVC_DIAG_I2CSCAN},
};

void cli_execute(char *line)
{
    char *argv[8];
    int argc = split(line, argv, 8);
    if (!argc) return;
    const char *c = argv[0];
    uint32_t now = app_uptime_ms();
    sig_log("#CMD,%s", c);

    if (!strcmp(c, "help")) cmd_help();
    else if (!strcmp(c, "ver")) {
        sig_log("#VER,fw=%s,version=%s,built=%s %s,model=%s,placeholder=%d,model_bytes=%lu,nodes=%d,depth=%d,"
                "freertos=%s,hal=0x%08lx", FW_NAME, FW_VERSION, __DATE__, __TIME__, sig_model_version(),
                sig_model_is_placeholder(), (unsigned long)sig_model_size_bytes(), sig_model_node_count(),
                sig_model_max_depth(), tskKERNEL_VERSION_NUMBER, (unsigned long)HAL_GetHalVersion());
    }
    else if (!strcmp(c, "status")) cmd_status();
    else if (!strcmp(c, "tasks")) health_report_tasks();
    else if (!strcmp(c, "heap")) sig_log("#HEAP,total=%u,free=%u,min_ever_free=%u", (unsigned)configTOTAL_HEAP_SIZE,
                                         (unsigned)xPortGetFreeHeapSize(), (unsigned)xPortGetMinimumEverFreeHeapSize());
    else if (!strcmp(c, "cnt")) cmd_cnt();
    else if (!strcmp(c, "lat")) cmd_lat(argc > 1 && !strcmp(argv[1], "reset"));
    else if (!strcmp(c, "cap")) cmd_cap(argc, argv);
    else if (!strcmp(c, "golden")) golden_run();
    else if (!strcmp(c, "diag") && argc > 1) {
        const char *w = argv[1];
        ui_msg_t m = { UI_MSG_TEST, 0, 0, sig_time_us() };
        if (!strcmp(w, "matrix")) { m.arg = 0; app_post_ui(&m); return; }
        if (!strcmp(w, "buzzer")) { m.arg = 1; app_post_ui(&m); return; }
        if (!strcmp(w, "leds"))   { m.arg = 2; app_post_ui(&m); return; }
        if (!strcmp(w, "lcd"))    { m.arg = 3; app_post_ui(&m); return; }
        if (!strcmp(w, "keys"))   { sig_log("#DIAG,keys,press each arrow key: '#KEY,<name>,down' lines confirm"); return; }
        for (unsigned i = 0; i < sizeof s_diag / sizeof s_diag[0]; i++) {
            if (!strcmp(w, s_diag[i].name)) {
                if (!app_post_service((svc_cmd_t)s_diag[i].svc, 0)) sig_log("#ERR,service_queue_full");
                return;
            }
        }
        sig_log("#ERR,unknown diag '%s'", w);
    }
    else if (!strcmp(c, "fault") && argc == 3 && !strcmp(argv[1], "imu")) {
        bool on = !strcmp(argv[2], "on");
        STATE_LOCK();
        g_app.inject_imu_fault = on;
        STATE_UNLOCK();
        sig_log("#FAULTINJ,imu,%s,t_ms=%lu", on ? "on" : "off", (unsigned long)now);
    }
    else if (!strcmp(c, "alert") && argc > 1 && !strcmp(argv[1], "test")) {
        STATE_LOCK();
        uint32_t ev = gl_raise_test(&g_app.gl, now);
        STATE_UNLOCK();
        if (!ev) sig_log("#ALERT,test,not_raised,reason=higher_priority_alert_active");
        app_handle_gl_events(ev, sig_time_us());
    }
    else if (!strcmp(c, "ack")) {
        STATE_LOCK();
        uint32_t ev = gl_acknowledge(&g_app.gl, GL_ACK_CONSOLE, now);
        STATE_UNLOCK();
        app_handle_gl_events(ev, sig_time_us());
    }
    else if (!strcmp(c, "screen") && argc > 1) {
        ui_msg_t m = { UI_MSG_SCREEN, (uint8_t)atoi(argv[1]), 0, 0 };
        app_post_ui(&m);
    }
    else if (!strcmp(c, "verbose") && argc > 1) {
        STATE_LOCK();
        g_app.verbose_inf = !strcmp(argv[1], "on");
        STATE_UNLOCK();
    }
    else if (!strcmp(c, "prox")) {
        if (argc > 1 && !strcmp(argv[1], "cal")) {
            ui_msg_t m = { UI_MSG_PROXCAL, 0, 0, 0 };
            app_post_ui(&m);
        } else if (argc > 1 && !strcmp(argv[1], "clear")) {
            prox_cal_t pc;
            prox_cal_clear(&pc);
            STATE_LOCK();
            prox_init(&g_app.prox, &pc);
            STATE_UNLOCK();
            app_clear_prox_cal();
            sig_log("#CAL,prox,cleared (touchless acknowledgement disabled until 'prox cal')");
        } else {
            app_post_service(SVC_DIAG_ALS, 0);
        }
    }
    else if (!strcmp(c, "cal") && argc > 1 && !strcmp(argv[1], "gyro")) {
        app_request_gyro_cal();
        sig_log("#CAL,gyro_bias,requested");
    }
    else if (!strcmp(c, "time")) {
        rtc_time_t t;
        if (argc == 4 && !strcmp(argv[1], "set")) {
            bool ok = parse_time(argv[2], argv[3], &t) && rtc_set(&t);
            sig_log("#TIME,set,%s", ok ? "ok" : "FAILED (format YYYY-MM-DD HH:MM:SS)");
        }
        app_post_service(SVC_DIAG_RTC, 0);
    }
    else if (!strcmp(c, "log")) {
        const char *w = argc > 1 ? argv[1] : "status";
        if (!strcmp(w, "enable")) { evlog_set_enabled(true); sig_log("#LOG,writes_enabled=1"); }
        else if (!strcmp(w, "disable")) { evlog_set_enabled(false); sig_log("#LOG,writes_enabled=0"); }
        else if (!strcmp(w, "dump")) app_post_service(SVC_LOG_DUMP, argc > 2 ? (uint32_t)atoi(argv[2]) : 0);
        else if (!strcmp(w, "erase") && argc > 2 && !strcmp(argv[2], "CONFIRM")) app_post_service(SVC_LOG_ERASE, 0);
        else if (!strcmp(w, "erase")) sig_log("#ERR,erase requires 'log erase CONFIRM' (erases only the 64 KB log region)");
        else app_post_service(SVC_DIAG_FLASH, 0);
    }
    else if (!strcmp(c, "say") || !strcmp(c, "ask")) {
        if (argc < 3) { sig_log("#ERR,usage: %s <secs> <text>", c); return; }
        uint32_t secs = (uint32_t)atoi(argv[1]);
        if (secs < 1) secs = 1;
        if (secs > 600) secs = 600;
        char text[PROMPT_TEXT];
        size_t n = 0;
        for (int i = 2; i < argc && n < sizeof text - 1; i++) {
            size_t l = strlen(argv[i]);
            if (n && n < sizeof text - 1) text[n++] = ' ';
            if (n + l > sizeof text - 1) l = sizeof text - 1 - n;
            memcpy(text + n, argv[i], l);
            n += l;
        }
        text[n] = 0;
        STATE_LOCK();
        g_app.prompt_id++;
        uint16_t id = g_app.prompt_id;
        g_app.prompt_kind = c[0] == 'a' ? PROMPT_ASK : PROMPT_SAY;
        g_app.prompt_until_ms = now + secs * 1000u;
        memcpy(g_app.prompt, text, n + 1);
        g_app.screen = SCREEN_GUARD;
        STATE_UNLOCK();
        ui_msg_t m = { UI_MSG_PROMPT, 0, 0, 0 };
        app_post_ui(&m);
        sig_log("#PROMPT,%u,%s,%lu", id, c, (unsigned long)secs);
    }
    else if (!strcmp(c, "cue") && argc > 1) {
        static const char *const names[] = { "start", "stop", "ok", "err", "attn", "tick" };
        for (unsigned i = 0; i < sizeof names / sizeof names[0]; i++) {
            if (!strcmp(argv[1], names[i])) { ui_msg_t m = { UI_MSG_CUE, (uint8_t)i, 0, 0 }; app_post_ui(&m); return; }
        }
        sig_log("#ERR,unknown cue '%s'", argv[1]);
    }
    else if (!strcmp(c, "ir")) {
        const char *w = argc > 1 ? argv[1] : "status";
        if (!strcmp(w, "learn")) { ui_msg_t m = { UI_MSG_IRLEARN, 0, 0, 0 }; app_post_ui(&m); return; }
        if (!strcmp(w, "relay") && argc > 2) {
            bool on = !strcmp(argv[2], "on");
            app_set_flag(FLAG_IR_RELAY_OFF, !on);
            STATE_LOCK(); g_app.ir_relay = on; STATE_UNLOCK();
        } else if (!strcmp(w, "clear")) {
            STATE_LOCK();
            for (int k = 0; k < IRK_COUNT; k++) { g_app.ir_code[k] = 0; bkp_write(BKP_IR_TEST + (uint32_t)k, 0); }
            STATE_UNLOCK();
        } else if (!strcmp(w, "send") && argc > 3) {
            uint32_t a = (uint32_t)strtoul(argv[2], NULL, 0), cmd = (uint32_t)strtoul(argv[3], NULL, 0);
            ui_msg_t m = { UI_MSG_IRSEND, 0, ((a & 0xFFFFu) << 8) | (cmd & 0xFFu), 0 };
            app_post_ui(&m);
            return;
        }
        STATE_LOCK();
        uint32_t c0 = g_app.ir_code[IRK_TEST], c1 = g_app.ir_code[IRK_ACK], c2 = g_app.ir_code[IRK_NEXT];
        bool relay = g_app.ir_relay;
        STATE_UNLOCK();
        sig_log("#IR,status,rx_idle=%d,rx_edges=%lu,rx_frames=%lu,rx_errors=%lu,own_test_echoes=%lu,tx_frames=%lu,relay=%d,"
                "test=0x%08lx,ack=0x%08lx,next=0x%08lx", ir_rx_level(), (unsigned long)ir_rx_edges(),
                (unsigned long)ir_rx_frames(), (unsigned long)ir_rx_errors(), (unsigned long)ir_rx_test_echoes(),
                (unsigned long)ir_tx_frames(), relay, (unsigned long)c0, (unsigned long)c1, (unsigned long)c2);
    }
    else if (!strcmp(c, "sd")) {
        if (argc > 1 && (!strcmp(argv[1], "on") || !strcmp(argv[1], "off"))) sdlog_set_enabled(!strcmp(argv[1], "on"));
        app_post_service(SVC_SD_STATUS, 0);
    }
    else if (!strcmp(c, "wire")) {
        if (argc > 2 && (!strcmp(argv[1], "can") || !strcmp(argv[1], "rs485"))) {
            bool on = !strcmp(argv[2], "on");
            uint32_t bits = (app_flag(FLAG_CAN_ON) ? 1u : 0u) | (app_flag(FLAG_RS485_ON) ? 2u : 0u);
            uint32_t b = !strcmp(argv[1], "can") ? 1u : 2u;
            bits = on ? (bits | b) : (bits & ~b);
            app_post_service(SVC_WIRE_CONFIG, bits);
        } else {
            app_post_service(SVC_WIRE_STATUS, 0);
        }
    }
    else sig_log("#ERR,unknown command '%s' (try help)", c);
}
