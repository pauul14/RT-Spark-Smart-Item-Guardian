/*
 * task_ui.c - priority 3. Sole owner of the LCD, LED matrix, buzzer, LEDs and keys.
 *
 * Feedback is redundant on purpose (accessibility): every alert is shown as
 * text AND colour on the LCD, as a colour/flash pattern on the LED matrix, and
 * as a distinct buzzer rhythm, so it does not depend on colour vision or hearing alone.
 *
 * Handheld prototype (no enclosure). Keys (short press / hold 1.5 s):
 *   UP    : raise a TEST alert (try the alert path and touchless acknowledgement)
 *   DOWN  : acknowledge (silence) the current alert
 *   LEFT  : next screen (GUARD -> SENSORS -> SYSTEM -> MODULES)
 *   RIGHT : screen action (GUARD hold: proximity calibration wizard; SENSORS: gyro
 *           re-calibration; SYSTEM: task/heap report to console; MODULES: self-test)
 * Touchless acknowledgement: hold a hand over the light/proximity sensor for
 * about 0.6 s after taking it away (see prox_ack.h); needs a measured calibration.
 */
#include "app.h"
#include "bsp.h"
#include "board_io.h"
#include "board_pins.h"
#include "led_matrix.h"
#include "model_infer.h"
#include "module_diag.h"
#include "ir_nec.h"
#include "mini_printf.h"
#include <string.h>

#define UI_PERIOD_MS     20
#define LONG_PRESS_MS    1500
#define RELEASE_MS       30
#define ROWS             13
#define COLS             20

/* ---------------- buzzer patterns (ms on, ms off, ...; 0-terminated) ------ */
static const uint16_t PAT_CHIRP[]  = { 40, 0 };
static const uint16_t PAT_ROUGH[]  = { 120, 80, 120, 80, 120, 0 };
static const uint16_t PAT_TEST[]   = { 400, 200, 400, 0 };
static const uint16_t PAT_FAULT[]  = { 800, 0 };
static const uint16_t PAT_CAL_OK[] = { 60, 60, 60, 0 };
static const uint16_t PAT_ACK[]    = { 30, 40, 30, 0 };
static const uint16_t PAT_TICK[]   = { 15, 0 };
/* operator cues for guided sessions (console 'cue <name>') */
static const uint16_t PAT_CUE_START[] = { 300, 0 };                     /* begin the action now */
static const uint16_t PAT_CUE_STOP[]  = { 80, 80, 80, 80, 80, 0 };      /* stop, put the board down */
static const uint16_t PAT_CUE_OK[]    = { 40, 60, 40, 0 };
static const uint16_t PAT_CUE_ERR[]   = { 500, 150, 500, 0 };
static const uint16_t PAT_CUE_ATTN[]  = { 30, 120, 30, 120, 30, 0 };

static const uint16_t *s_pat;
static int s_pat_i;
static uint32_t s_pat_next_ms;

static void buzz_start(const uint16_t *p, uint32_t now)
{
    s_pat = p;
    s_pat_i = 0;
    buzzer_set(true);
    s_pat_next_ms = now + p[0];
}

static void buzz_step(uint32_t now)
{
    if (!s_pat || (int32_t)(now - s_pat_next_ms) < 0) return;
    s_pat_i++;
    if (s_pat[s_pat_i] == 0) { buzzer_set(false); s_pat = NULL; return; }
    buzzer_set((s_pat_i & 1) == 0);
    s_pat_next_ms = now + s_pat[s_pat_i];
}

static void buzz_stop(void) { s_pat = NULL; buzzer_set(false); }

/* ---------------- keys ---------------------------------------------------- */
typedef struct { bool down; bool long_fired; uint32_t t_ms; uint32_t up_since; } key_state_t;
static key_state_t s_keys[KEY_COUNT];

void app_key_isr(key_id_t k, uint32_t t_us)
{
    key_irq_enable(k, false);                 /* debounce: re-enabled after release */
    ui_msg_t m = { UI_MSG_KEY, (uint8_t)k, 0, t_us };
    BaseType_t woken = pdFALSE;
    if (g_q_ui) xQueueSendFromISR(g_q_ui, &m, &woken);
    portYIELD_FROM_ISR(woken);
}

/* ---------------- proximity calibration wizard ----------------------------
 * Phase FAR : hands away (3 s to get ready, then 3 s of readings)
 * Phase NEAR: hand held 2-5 cm over the sensor (3 s to get ready, 3 s of readings)
 * Only fresh AP3216C readings (als_seq changed, PS valid) are counted. The
 * thresholds come from the measured statistics (prox_cal_from_stats); on failure
 * the previous calibration (or "not calibrated") stays in force. */
typedef enum { PC_IDLE = 0, PC_FAR_WAIT, PC_FAR_SAMPLE, PC_NEAR_WAIT, PC_NEAR_SAMPLE } pc_state_t;
static pc_state_t s_pc;
static uint32_t s_pc_t0, s_pc_seq;
typedef struct { uint32_t n, sum; uint16_t mn, mx; } ps_stat_t;
static ps_stat_t s_pc_far, s_pc_near;

static void ps_stat_reset(ps_stat_t *s) { s->n = s->sum = 0; s->mn = 0xFFFF; s->mx = 0; }

static void proxcal_start(uint32_t now)
{
    s_pc = PC_FAR_WAIT;
    s_pc_t0 = now;
    ps_stat_reset(&s_pc_far);
    ps_stat_reset(&s_pc_near);
    STATE_LOCK();
    g_app.prox_cal_active = true;
    g_app.screen = SCREEN_GUARD;              /* the wizard text is shown on the GUARD screen */
    STATE_UNLOCK();
    sig_log("#CAL,prox,started: hands AWAY, then hold a hand 2-5 cm over the sensor when the screen says so");
    buzz_start(PAT_TICK, now);
}

static void proxcal_sample(ps_stat_t *s)
{
    STATE_LOCK();
    uint32_t seq = g_app.als_seq;
    bool v = g_app.als_valid && g_app.als.ps_valid;
    uint16_t ps = g_app.als.ps;
    STATE_UNLOCK();
    if (seq == s_pc_seq) return;
    s_pc_seq = seq;
    if (!v) return;
    s->n++;
    s->sum += ps;
    if (ps < s->mn) s->mn = ps;
    if (ps > s->mx) s->mx = ps;
}

static void proxcal_step(uint32_t now, char *banner)
{
    uint32_t el = now - s_pc_t0;
    switch (s_pc) {
    case PC_IDLE: return;
    case PC_FAR_WAIT:
        sig_snprintf(banner, COLS + 1, "HANDS AWAY ... %lus", (unsigned long)(3 - el / 1000));
        if (el >= 3000) { s_pc = PC_FAR_SAMPLE; s_pc_t0 = now; }
        return;
    case PC_FAR_SAMPLE:
        sig_snprintf(banner, COLS + 1, "MEASURING: AWAY");
        proxcal_sample(&s_pc_far);
        if (el >= 3000) { s_pc = PC_NEAR_WAIT; s_pc_t0 = now; buzz_start(PAT_TICK, now); }
        return;
    case PC_NEAR_WAIT:
        sig_snprintf(banner, COLS + 1, "HAND OVER SENSOR %lus", (unsigned long)(3 - el / 1000));
        if (el >= 3000) { s_pc = PC_NEAR_SAMPLE; s_pc_t0 = now; }
        return;
    case PC_NEAR_SAMPLE: {
        sig_snprintf(banner, COLS + 1, "MEASURING: HOLD");
        proxcal_sample(&s_pc_near);
        if (el < 3000) return;
        uint16_t far_mean = (uint16_t)(s_pc_far.n ? s_pc_far.sum / s_pc_far.n : 0);
        uint16_t near_mean = (uint16_t)(s_pc_near.n ? s_pc_near.sum / s_pc_near.n : 0);
        uint16_t far_max = s_pc_far.n ? s_pc_far.mx : 0, near_min = s_pc_near.n ? s_pc_near.mn : 0;
        prox_cal_t cal;
        STATE_LOCK();
        cal = g_app.prox.cal;
        prox_cal_result_t r = prox_cal_from_stats(&cal, far_mean, far_max, s_pc_far.n, near_min, near_mean,
                                                  s_pc_near.n);
        if (r == PROX_CAL_OK) prox_init(&g_app.prox, &cal);   /* fresh detector: must see FAR first */
        g_app.prox_cal_active = false;
        STATE_UNLOCK();
        if (r == PROX_CAL_OK) app_save_prox_cal(&cal);
        sig_log("#CAL,prox,%s,far_n=%lu,far_mean=%u,far_min=%u,far_max=%u,near_n=%lu,near_min=%u,near_mean=%u,"
                "near_max=%u,thr_far=%u,thr_near=%u,calibrated=%d", prox_cal_result_name(r),
                (unsigned long)s_pc_far.n, far_mean, s_pc_far.n ? s_pc_far.mn : 0, far_max,
                (unsigned long)s_pc_near.n, near_min, near_mean, s_pc_near.n ? s_pc_near.mx : 0,
                cal.thr_far, cal.thr_near, cal.calibrated);
        sig_snprintf(banner, COLS + 1, r == PROX_CAL_OK ? "PROX CAL OK" : "PROX CAL FAILED");
        buzz_start(r == PROX_CAL_OK ? PAT_CAL_OK : PAT_FAULT, now);
        s_pc = PC_IDLE;
        return;
    }
    }
}

/* ---------------- screen rendering ---------------------------------------- */
static char s_cache[ROWS][COLS + 1];
static uint16_t s_cache_fg[ROWS], s_cache_bg[ROWS];
static int s_cache_screen = -1;
static uint8_t s_box_cls = 0xFE;
static uint16_t s_box_bg;
static bool s_box_fault;
static uint32_t s_ban_key = 0xFFFFFFFFu;       /* GUARD banner background currently painted */

static void row(int r, const char *txt, uint16_t fg, uint16_t bg)
{
    if (r < 0 || r >= ROWS) return;
    if (strncmp(s_cache[r], txt, COLS) == 0 && s_cache_fg[r] == fg && s_cache_bg[r] == bg) return;
    strncpy(s_cache[r], txt, COLS);
    s_cache[r][COLS] = 0;
    s_cache_fg[r] = fg;
    s_cache_bg[r] = bg;
    int y = r < 1 ? 7 : 34 + (r - 1) * 18;
    lcd_text_field(0, y, txt, COLS, 2, fg, bg);
}

static void screen_reset(int scr)
{
    memset(s_cache, 0, sizeof s_cache);
    for (int i = 0; i < ROWS; i++) { s_cache_fg[i] = 0xFFFF; s_cache_bg[i] = 0x0001; }
    s_cache_screen = scr;
    s_box_cls = 0xFE;
    s_ban_key = 0xFFFFFFFFu;
    lcd_clear(C_BLACK);
}

/* Copy the ring under the lock, summarise outside it (UI task only). */
static void fmt_lat(char *o, const char *name, const lat_ring_t *r)
{
    static lat_ring_t copy;
    static uint32_t scratch[LAT_RING];
    STATE_LOCK();
    copy = *r;
    STATE_UNLOCK();
    lat_summary_t s;
    lat_summarise(&copy, scratch, &s);
    sig_snprintf(o, COLS + 1, "%s %lu/%lu", name, (unsigned long)s.median, (unsigned long)s.p95);
}

/* Word-wrap a prompt into 4 rows of 20 characters. */
static void prompt_wrap(const char *src, char out[4][COLS + 1])
{
    int r = 0, c = 0;
    memset(out, 0, 4 * (COLS + 1));
    const char *p = src;
    while (*p && r < 4) {
        while (*p == ' ') p++;
        const char *w = p;
        while (*p && *p != ' ') p++;
        int wl = (int)(p - w);
        if (wl > COLS) wl = COLS;
        if (c && c + 1 + wl > COLS) { r++; c = 0; if (r >= 4) break; }
        if (c) out[r][c++] = ' ';
        memcpy(&out[r][c], w, (size_t)wl);
        c += wl;
    }
}

static void prompt_set(uint8_t kind, uint32_t secs, const char *text, uint32_t now)
{
    STATE_LOCK();
    g_app.prompt_kind = kind;
    g_app.prompt_until_ms = now + secs * 1000u;
    strncpy(g_app.prompt, text, PROMPT_TEXT - 1);
    g_app.prompt[PROMPT_TEXT - 1] = 0;
    if (kind != PROMPT_NONE) g_app.screen = SCREEN_GUARD;
    STATE_UNLOCK();
}

static void prompt_finish(const char *answer, uint32_t now)
{
    STATE_LOCK();
    uint8_t kind = g_app.prompt_kind;
    uint16_t id = g_app.prompt_id;
    uint32_t until = g_app.prompt_until_ms;
    g_app.prompt_kind = PROMPT_NONE;
    STATE_UNLOCK();
    if (kind == PROMPT_ASK) sig_log("#ANSWER,%u,%s,remaining_ms=%ld", id, answer, (long)(int32_t)(until - now));
    else if (kind == PROMPT_SAY) sig_log("#PROMPT,%u,done", id);
}

/* ---------------- IR remote: pairing, control, relay ---------------------- */
static const char *const IRK_NAMES[IRK_COUNT] = { "TEST ALERT", "ACK", "NEXT SCREEN" };
static int s_irlearn = -1;               /* -1 idle, else index of the key being paired */
static uint32_t s_ir_last_code, s_ir_last_ms;

static void irlearn_prompt(uint32_t now)
{
    char t[PROMPT_TEXT];
    sig_snprintf(t, sizeof t, "IR PAIRING: press the remote key for %s (LEFT = skip)", IRK_NAMES[s_irlearn]);
    prompt_set(PROMPT_SAY, 15, t, now);
    buzz_start(PAT_CUE_ATTN, now);
}

static void irlearn_next(uint32_t now)
{
    if (++s_irlearn >= IRK_COUNT) {
        s_irlearn = -1;
        prompt_set(PROMPT_NONE, 0, "", now);
        buzz_start(PAT_CUE_OK, now);
        sig_log("#IR,pairing,done,test=0x%08lx,ack=0x%08lx,next=0x%08lx", (unsigned long)g_app.ir_code[IRK_TEST],
                (unsigned long)g_app.ir_code[IRK_ACK], (unsigned long)g_app.ir_code[IRK_NEXT]);
        return;
    }
    irlearn_prompt(now);
}

static void ir_handle(const ir_frame_t *f, uint32_t now)
{
    if (f->repeat) return;                                   /* held key: ignored */
    if (f->addr == IR_RELAY_ADDR) {                          /* our own relay / test frame */
        sig_log("#IR,rx,own_frame,cmd=0x%02x", f->cmd);
        return;
    }
    uint32_t code = IR_CODE_VALID | ((uint32_t)f->addr << 8) | f->cmd;
    if (code == s_ir_last_code && (now - s_ir_last_ms) < 400u) return;   /* debounce */
    s_ir_last_code = code;
    s_ir_last_ms = now;
    if (s_irlearn >= 0) {
        STATE_LOCK();
        g_app.ir_code[s_irlearn] = code;
        STATE_UNLOCK();
        bkp_write(BKP_IR_TEST + (uint32_t)s_irlearn, code);
        sig_log("#IR,paired,%s,addr=0x%04x,cmd=0x%02x", IRK_NAMES[s_irlearn], f->addr, f->cmd);
        irlearn_next(now);
        return;
    }
    int match = -1;
    STATE_LOCK();
    for (int k = 0; k < IRK_COUNT; k++) if (g_app.ir_code[k] == code) match = k;
    uint32_t ev = 0;
    if (match == IRK_TEST) ev = gl_raise_test(&g_app.gl, now);
    else if (match == IRK_ACK) ev = gl_acknowledge(&g_app.gl, GL_ACK_IR, now);
    else if (match == IRK_NEXT) g_app.screen = (screen_t)((g_app.screen + 1) % SCREEN_COUNT);
    STATE_UNLOCK();
    sig_log("#IR,rx,addr=0x%04x,cmd=0x%02x,action=%s", f->addr, f->cmd, match < 0 ? "none(unpaired)" : IRK_NAMES[match]);
    if (match >= 0) {
        if (!s_pat) buzz_start(PAT_CHIRP, now);
        app_post_event(EVT_IR_REMOTE, SIG_CLASS_UNKNOWN, (uint16_t)match);
    }
    if (ev) app_handle_gl_events(ev, sig_time_us());
}

static void ir_relay(uint32_t ev)
{
    STATE_LOCK();
    bool on = g_app.ir_relay;
    STATE_UNLOCK();
    if (!on) return;
    uint8_t cmd = (ev & GL_EV_FAULT_RAISED) ? IR_CMD_FAULT : (ev & GL_EV_ROUGH_EVENT) ? IR_CMD_ROUGH :
                  (ev & GL_EV_TEST_ALERT) ? IR_CMD_TEST_ALERT : (ev & GL_EV_ACKED) ? IR_CMD_ACK : 0;
    if (cmd && ir_tx_send(IR_RELAY_ADDR, cmd)) sig_log("#IR,relay,cmd=0x%02x", cmd);
}

/* GUARD screen layout (240 x 240 px, 2x font = 12 x 16 px per character, 20 columns):
 *   y   0- 23  header bar
 *   y  26- 91  state box: predicted activity, 4x font (24 x 32 px), high-contrast colours
 *   y  96-139  alert banner: two lines, full width (or the wizard text, or "No alert")
 *   y 146-161  touchless acknowledgement status (hand ack)
 *   y 166-181  rough-event count
 *   y 186-201  confidence + temperature / humidity
 *   y 206-221  status (recording / placeholder model)
 *   y 222-239  key legend
 * Operator prompts (guided sessions, IR pairing) use y 96-199. Every slot is a full-width
 * text field cached per line, so only changed lines are redrawn. Diagnostics stay on
 * the SENSORS / SYSTEM / MODULES screens. */
#define G_HDR_Y      4
#define G_BOX_Y      26
#define G_BOX_H      66
#define G_BAN_Y      96
#define G_BAN_H      44
static const int G_Y[ROWS] = { G_HDR_Y, 0, 0, 0, 146, 166, 186, 206, G_BAN_Y + 4, G_BAN_Y + 24, 0, 0, 223 };
static const int G_PROMPT_Y[5] = { 96, 116, 136, 156, 180 };
static uint8_t s_guard_pkind = 0xFF;

static void gline(int slot, int y, const char *txt, uint16_t fg, uint16_t bg)
{
    if (strncmp(s_cache[slot], txt, COLS) == 0 && s_cache_fg[slot] == fg && s_cache_bg[slot] == bg) return;
    strncpy(s_cache[slot], txt, COLS);
    s_cache[slot][COLS] = 0;
    s_cache_fg[slot] = fg;
    s_cache_bg[slot] = bg;
    lcd_text_field(0, y, txt, COLS, 2, fg, bg);
}

static void banner_bg(uint32_t key, uint16_t bg)
{
    if (key == s_ban_key) return;
    s_ban_key = key;
    lcd_fill_rect(0, G_BAN_Y, LCD_W, G_BAN_H, bg);
    s_cache[8][0] = s_cache[9][0] = 1;           /* force both banner lines to redraw */
}

static void draw_guard(uint32_t now, const char *wizard)
{
    char t[48];
    STATE_LOCK();
    gl_state_t gl = g_app.gl;
    bool pcal = g_app.prox.cal.calibrated;
    prox_zone_t pzone = g_app.prox.zone;
    uint16_t ps = g_app.als.ps;
    uint8_t conf = g_app.last_conf;
    bool valid = g_app.last_valid, aht = g_app.aht_valid, cap = g_app.capture, humid = g_app.humid_warn;
    float tc = g_app.aht_t, rh = g_app.aht_rh;
    char lab[20];
    strncpy(lab, g_app.cap_label, sizeof lab);
    uint16_t ses = g_app.cap_session, tri = g_app.cap_trial;
    uint32_t cap_ms = now - g_app.cap_start_ms;
    char ptxt[PROMPT_TEXT];
    uint8_t pkind = g_app.prompt_kind;
    uint32_t puntil = g_app.prompt_until_ms;
    memcpy(ptxt, g_app.prompt, sizeof ptxt);
    STATE_UNLOCK();

    /* a prompt appearing or disappearing changes the layout: repaint everything once */
    if (pkind != s_guard_pkind) {
        if (s_guard_pkind != 0xFF) screen_reset(SCREEN_GUARD);
        s_guard_pkind = pkind;
        s_ban_key = 0xFFFFFFFFu;
    }

    if (!s_cache[0][0]) lcd_fill_rect(0, 0, LCD_W, 24, C_NAVY);   /* full header bar after a repaint */
    gline(0, G_HDR_Y, "  SMART ITEM GUARD", C_WHITE, C_NAVY);

    /* state box: the predicted activity, as large as the screen allows */
    uint16_t bg = gl.sensor_fault ? C_ORANGE :
                  gl.display_class == SIG_CLASS_STATIONARY ? C_GREEN :
                  gl.display_class == SIG_CLASS_NORMAL ? C_CYAN :
                  gl.display_class == SIG_CLASS_ROUGH ? C_RED : C_DKGREY;
    uint16_t fg = (bg == C_RED || bg == C_DKGREY) ? C_WHITE : C_BLACK;
    if (gl.display_class != s_box_cls || bg != s_box_bg || gl.sensor_fault != s_box_fault) {
        s_box_cls = gl.display_class;
        s_box_bg = bg;
        s_box_fault = gl.sensor_fault;
        lcd_fill_rect(0, G_BOX_Y, LCD_W, G_BOX_H, bg);
        const char *w = gl.sensor_fault ? "FAULT" : gl.display_class == SIG_CLASS_UNKNOWN ? "----" :
                        sig_class_short(gl.display_class);
        int len = (int)strlen(w);
        lcd_text((LCD_W - len * 24) / 2, G_BOX_Y + (G_BOX_H - 32) / 2, w, 4, fg, bg);
    }

    if (pkind != PROMPT_NONE) {                  /* operator prompt replaces banner + status lines */
        char lines[4][COLS + 1];
        prompt_wrap(ptxt, lines);
        if (s_ban_key != 0xFFFFFFFEu) {
            s_ban_key = 0xFFFFFFFEu;
            lcd_fill_rect(0, G_PROMPT_Y[0] - 2, LCD_W, G_PROMPT_Y[4] + 18 - G_PROMPT_Y[0], C_YELLOW);
        }
        for (int i = 0; i < 4; i++) gline(8 + i, G_PROMPT_Y[i], lines[i], C_BLACK, C_YELLOW);
        uint32_t left = (int32_t)(puntil - now) > 0 ? (puntil - now + 999u) / 1000u : 0;
        if (pkind == PROMPT_ASK) sig_snprintf(t, sizeof t, "UP=yes DN=no  %lus", (unsigned long)left);
        else sig_snprintf(t, sizeof t, "%lus", (unsigned long)left);
        gline(4, G_PROMPT_Y[4], t, C_BLACK, C_YELLOW);
        gline(12, G_Y[12], "UP test DN ack LT pg", C_WHITE, C_DKGREY);
        return;
    }

    /* alert banner: the active warning, full width, high contrast */
    if (wizard && wizard[0]) {
        banner_bg(1, C_YELLOW);
        gline(8, G_Y[8], wizard, C_BLACK, C_YELLOW);
        gline(9, G_Y[9], "follow the screen", C_BLACK, C_YELLOW);
    } else if (gl.alert != GL_ALERT_NONE) {
        bool test = gl.alert == GL_ALERT_TEST;
        uint16_t abg = gl.alert_acked ? C_DKGREY : test ? C_ORANGE : C_RED;
        uint16_t afg = (abg == C_ORANGE) ? C_BLACK : C_WHITE;
        banner_bg(0x100u | ((uint32_t)gl.alert << 1) | gl.alert_acked, abg);
        bool blink = ((now / 400) & 1) != 0 && !gl.alert_acked;
        sig_snprintf(t, sizeof t, " !! %s", gl_alert_name(gl.alert));
        gline(8, G_Y[8], t, blink ? abg : afg, blink ? afg : abg);
        gline(9, G_Y[9], gl.alert_acked ? " acknowledged" : (pcal ? " DOWN or hold hand" : " press DOWN to ack"),
              afg, abg);
    } else {
        banner_bg(2, C_BLACK);
        gline(8, G_Y[8], " No alert", C_GREEN, C_BLACK);
        gline(9, G_Y[9], "", C_WHITE, C_BLACK);
    }

    /* touchless acknowledgement and other status */
    if (pcal) sig_snprintf(t, sizeof t, "Hand ack %-5s %4u", pzone == PROX_ZONE_NEAR ? "NEAR" : "ready", ps);
    else sig_snprintf(t, sizeof t, "Hand ack: no cal");
    gline(4, G_Y[4], t, pcal ? (pzone == PROX_ZONE_NEAR ? C_YELLOW : C_WHITE) : C_GREY, C_BLACK);
    sig_snprintf(t, sizeof t, "Rough events: %lu", (unsigned long)gl.rough_events);
    gline(5, G_Y[5], t, C_WHITE, C_BLACK);
    char env[24];
    if (aht) sig_snprintf(env, sizeof env, "%.1fC %.0f%%", (double)tc, (double)rh);
    else sig_snprintf(env, sizeof env, "T/RH --");
    if (valid) sig_snprintf(t, sizeof t, "Conf %3u%% %s", conf, env);
    else sig_snprintf(t, sizeof t, "No window %s", env);
    gline(6, G_Y[6], t, humid ? C_YELLOW : C_WHITE, C_BLACK);
    if (cap) {
        sig_snprintf(t, sizeof t, "REC %.6s S%u T%u %lus", lab, ses, tri, (unsigned long)(cap_ms / 1000));
        gline(7, G_Y[7], t, C_BLACK, C_YELLOW);
    } else if (sig_model_is_placeholder()) {
        gline(7, G_Y[7], "PLACEHOLDER MODEL", C_BLACK, C_ORANGE);
    } else if (humid) {
        gline(7, G_Y[7], "HUMIDITY WARNING", C_BLACK, C_YELLOW);
    } else {
        gline(7, G_Y[7], "", C_WHITE, C_BLACK);
    }
    gline(12, G_Y[12], "UP test DN ack LT pg", C_WHITE, C_DKGREY);
}

static void draw_sensors(void)
{
    char t[48];
    STATE_LOCK();
    sig_raw_imu_t s = g_app.imu;
    als_reading_t a = g_app.als;
    bool av = g_app.als_valid, hv = g_app.aht_valid, bias = g_app.gyro_bias_ok, drdy = g_app.imu_drdy_mode;
    float b[3] = { g_app.gyro_bias[0], g_app.gyro_bias[1], g_app.gyro_bias[2] };
    float tc = g_app.aht_t, rh = g_app.aht_rh, mt = g_app.mcu_t, vd = g_app.vdda;
    prox_cal_t pc = g_app.prox.cal;
    STATE_UNLOCK();
    const float k = 1.0f / SIG_ACC_LSB_PER_G, kg = 1.0f / SIG_GYRO_LSB_PER_DPS;
    row(0, "SENSORS", C_WHITE, C_NAVY);
    sig_snprintf(t, sizeof t, "ax%+.3f ay%+.3f", (double)(s.ax * k), (double)(s.ay * k)); row(1, t, C_WHITE, C_BLACK);
    sig_snprintf(t, sizeof t, "az%+.3f g", (double)(s.az * k)); row(2, t, C_WHITE, C_BLACK);
    sig_snprintf(t, sizeof t, "gx%+.1f gy%+.1f", (double)((s.gx - b[0]) * kg), (double)((s.gy - b[1]) * kg)); row(3, t, C_WHITE, C_BLACK);
    sig_snprintf(t, sizeof t, "gz%+.1f dps", (double)((s.gz - b[2]) * kg)); row(4, t, C_WHITE, C_BLACK);
    if (av) sig_snprintf(t, sizeof t, "Lux %.1f PS %u", (double)ap3216c_lux(a.als), a.ps);
    else sig_snprintf(t, sizeof t, "ALS/PS read error");
    row(5, t, av ? C_WHITE : C_ORANGE, C_BLACK);
    sig_snprintf(t, sizeof t, "IR %u PSvalid %s", a.ir, a.ps_valid ? "Y" : "N"); row(6, t, C_WHITE, C_BLACK);
    if (hv) sig_snprintf(t, sizeof t, "AHT %.1fC %.1f%%", (double)tc, (double)rh);
    else sig_snprintf(t, sizeof t, "AHT21 unavailable");
    row(7, t, hv ? C_WHITE : C_ORANGE, C_BLACK);
    sig_snprintf(t, sizeof t, "MCU %.1fC %.2fV", (double)mt, (double)vd); row(8, t, C_WHITE, C_BLACK);
    sig_snprintf(t, sizeof t, "Bias %s DRDY %s", bias ? "OK" : "--", drdy ? "irq" : "poll"); row(9, t, C_WHITE, C_BLACK);
    if (pc.calibrated) sig_snprintf(t, sizeof t, "Prox thr %u/%u", pc.thr_far, pc.thr_near);
    else sig_snprintf(t, sizeof t, "Prox not calibrated");
    row(10, t, C_WHITE, C_BLACK);
    row(11, "", C_WHITE, C_BLACK);
    row(12, "RT: recal gyro", C_GREY, C_BLACK);
}

static void draw_system(uint32_t now)
{
    char t[48];
    row(0, "SYSTEM", C_WHITE, C_NAVY);
    uint32_t s = now / 1000;
    sig_snprintf(t, sizeof t, "Up %02lu:%02lu:%02lu", (unsigned long)(s / 3600), (unsigned long)(s / 60 % 60),
                 (unsigned long)(s % 60));
    row(1, t, C_WHITE, C_BLACK);
    sig_snprintf(t, sizeof t, "Heap %u min %u", (unsigned)xPortGetFreeHeapSize(), (unsigned)xPortGetMinimumEverFreeHeapSize());
    row(2, t, C_WHITE, C_BLACK);
    fmt_lat(t, "Inf us", &g_app.lat_inf); row(3, t, C_WHITE, C_BLACK);
    fmt_lat(t, "S2D us", &g_app.lat_s2d); row(4, t, C_WHITE, C_BLACK);
    STATE_LOCK();
    uint32_t infc = g_app.inf_count, infv = g_app.inf_valid, agree = g_app.model_base_agree;
    STATE_UNLOCK();
    sig_snprintf(t, sizeof t, "Inf %lu ok %lu", (unsigned long)infc, (unsigned long)infv); row(5, t, C_WHITE, C_BLACK);
    sig_snprintf(t, sizeof t, "Base agree %lu", (unsigned long)agree); row(6, t, C_WHITE, C_BLACK);
    sig_snprintf(t, sizeof t, "Miss %lu Inv %lu", (unsigned long)g_cnt.imu_missed, (unsigned long)g_cnt.imu_invalid);
    row(7, t, (g_cnt.imu_missed || g_cnt.imu_invalid) ? C_YELLOW : C_WHITE, C_BLACK);
    extern volatile uint32_t g_i2c2_errors, g_i2c2_recoveries;
    sig_snprintf(t, sizeof t, "I2C err %lu rec %lu", (unsigned long)g_i2c2_errors, (unsigned long)g_i2c2_recoveries);
    row(8, t, C_WHITE, C_BLACK);
    sig_snprintf(t, sizeof t, "Log drop %lu", (unsigned long)g_cnt.log_dropped_lines); row(9, t, C_WHITE, C_BLACK);
    sig_snprintf(t, sizeof t, "Reset %s", g_reset.cause); row(10, t, C_WHITE, C_BLACK);
    sig_snprintf(t, sizeof t, "%s", sig_model_version()); row(11, t, sig_model_is_placeholder() ? C_ORANGE : C_GREEN, C_BLACK);
    row(12, "RT: task report", C_GREY, C_BLACK);
}

static void draw_modules(void)
{
    char t[48];
    row(0, "MODULES", C_WHITE, C_NAVY);
    STATE_LOCK();
    sig_snprintf(t, sizeof t, "LCD %04x IMU %02x", g_app.lcd_id, g_app.imu_whoami); row(1, t, C_WHITE, C_BLACK);
    sig_snprintf(t, sizeof t, "ALS %s AHT %s", g_app.als_ok ? "ok" : "--", g_app.aht_ok ? "ok" : "--"); row(2, t, C_WHITE, C_BLACK);
    sig_snprintf(t, sizeof t, "FLASH %02x%02x%02x", g_app.flash.manufacturer, g_app.flash.mem_type, g_app.flash.capacity);
    row(3, t, g_app.flash_ok ? C_WHITE : C_ORANGE, C_BLACK);
    if (g_app.rtc_valid) sig_snprintf(t, sizeof t, "RTC %s %02u:%02u:%02u", rtc_src_name(g_app.rtc_src), g_app.rtc.hour, g_app.rtc.min, g_app.rtc.sec);
    else sig_snprintf(t, sizeof t, "RTC %s not set", rtc_src_name(g_app.rtc_src));
    row(4, t, C_WHITE, C_BLACK);
    static const char *const names[] = { "SD", "IR", "WIFI", "CODEC", "CAN", "MCU" };
    static const int idx[] = { SVC_DIAG_SD, SVC_DIAG_IR, SVC_DIAG_WIFI, SVC_DIAG_CODEC, SVC_DIAG_CAN, SVC_DIAG_MCU };
    for (int i = 0; i < 6; i++) {
        sig_snprintf(t, sizeof t, "%-5s %s", names[i], g_app.diag_short[idx[i] % 12]);
        row(5 + i, t, C_WHITE, C_BLACK);
    }
    STATE_UNLOCK();
    sig_snprintf(t, sizeof t, "IR %lu/%lu K%c%c%c%c", (unsigned long)ir_rx_frames(), (unsigned long)ir_tx_frames(),
                 key_is_down(KEY_UP) ? 'U' : '-', key_is_down(KEY_DOWN) ? 'D' : '-',
                 key_is_down(KEY_LEFT) ? 'L' : '-', key_is_down(KEY_RIGHT) ? 'R' : '-');
    row(11, t, C_WHITE, C_BLACK);
    row(12, "RT: run self-test", C_GREY, C_BLACK);
}

/* ---------------- LED matrix / LEDs --------------------------------------- */
static void render_matrix(uint32_t now)
{
    STATE_LOCK();
    gl_state_t gl = g_app.gl;
    bool cap = g_app.capture;
    STATE_UNLOCK();
    bool blink = ((now / 250) & 1) != 0;
    if (gl.alert == GL_ALERT_ROUGH && !gl.alert_acked)          matrix_fill(blink ? 255 : 0, 0, 0);
    else if (gl.alert == GL_ALERT_TEST && !gl.alert_acked)      matrix_fill(blink ? 255 : 0, blink ? 120 : 0, 0);
    else if (gl.sensor_fault)                                   matrix_fill(((now / 1000) & 1) ? 160 : 0, 0, 160);
    else if (cap) { matrix_fill(0, 0, 0); matrix_set((int)((now / 100) % MATRIX_LED_COUNT), 255, 255, 255); }
    else {
        uint8_t r = 0, g = 0, b = 0;
        if (gl.display_class == SIG_CLASS_STATIONARY) g = 255;
        else if (gl.display_class == SIG_CLASS_NORMAL) { g = 200; b = 255; }
        else if (gl.display_class == SIG_CLASS_ROUGH) r = 255;
        else b = 120;
        matrix_fill(0, 0, 0);
        for (int i = 0; i < MATRIX_LED_COUNT; i++) matrix_set(i, r, g, b);
    }
    matrix_show();
    led_set(LED_RED, gl.alert != GL_ALERT_NONE && (blink || gl.alert_acked));
    led_set(LED_BLUE, ((now / 500) & 1) != 0);                 /* alive heartbeat */
}

/* ---------------- self tests requested over the console ------------------- */
static void run_ui_test(uint8_t which, uint32_t now)
{
    switch (which) {
    case 0: {   /* matrix: R, G, B, W, off */
        static const uint8_t c[4][3] = { {255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 255} };
        for (int i = 0; i < 4; i++) {
            matrix_fill(c[i][0], c[i][1], c[i][2]); matrix_show(); vTaskDelay(pdMS_TO_TICKS(400));
            xEventGroupSetBits(g_alive, ALIVE_UI);
        }
        for (int i = 0; i < MATRIX_LED_COUNT; i++) {
            matrix_fill(0, 0, 0); matrix_set(i, 0, 255, 0); matrix_show(); vTaskDelay(pdMS_TO_TICKS(60));
            xEventGroupSetBits(g_alive, ALIVE_UI);
        }
        sig_log("#DIAG,matrix,DONE,frames=%lu (operator confirms colours R,G,B,W and the 19-LED chase)",
                (unsigned long)matrix_frames_sent());
        break;
    }
    case 1: buzz_start(PAT_ROUGH, now); sig_log("#DIAG,buzzer,DONE,pattern=rough (operator confirms 3 beeps)"); break;
    case 2:
        for (int i = 0; i < 3; i++) {
            led_set(LED_RED, true); led_set(LED_BLUE, false); vTaskDelay(pdMS_TO_TICKS(250));
            led_set(LED_RED, false); led_set(LED_BLUE, true); vTaskDelay(pdMS_TO_TICKS(250));
            xEventGroupSetBits(g_alive, ALIVE_UI);
        }
        sig_log("#DIAG,leds,DONE (operator confirms red/blue alternate x3)");
        break;
    case 3: {
        static const uint16_t bars[] = { C_RED, C_GREEN, C_BLUE, C_WHITE, C_BLACK };
        uint32_t t0 = sig_time_us();
        for (int i = 0; i < 5; i++) lcd_fill_rect(0, i * 48, LCD_W, 48, bars[i]);
        uint32_t us = sig_time_us() - t0;
        vTaskDelay(pdMS_TO_TICKS(800));
        sig_log("#DIAG,lcd,DONE,full_screen_fill_us=%lu,id=0x%04x (operator confirms R,G,B,W,K bars)",
                (unsigned long)us, g_app.lcd_id);
        screen_reset(s_cache_screen);
        break;
    }
    default: break;
    }
}

void task_ui(void *arg)
{
    (void)arg;
    if (g_app.lcd_ok) lcd_clear(C_BLACK);
    for (int k = 0; k < KEY_COUNT; k++) s_keys[k].up_since = 0;
    uint32_t last_draw = 0, last_matrix = 0;
    char wizard[COLS + 1] = "";
    bool silenced = true;

    for (;;) {
        ui_msg_t m;
        bool got = xQueueReceive(g_q_ui, &m, pdMS_TO_TICKS(UI_PERIOD_MS)) == pdTRUE;
        xEventGroupSetBits(g_alive, ALIVE_UI);
        uint32_t now = app_uptime_ms();

        if (got && m.type == UI_MSG_EVENTS) {
            uint32_t ev = m.events;
            const uint16_t *pat = NULL;
            if (ev & GL_EV_FAULT_RAISED) pat = PAT_FAULT;
            else if (ev & GL_EV_ROUGH_EVENT) pat = PAT_ROUGH;
            else if (ev & GL_EV_TEST_ALERT) pat = PAT_TEST;
            if (pat) {
                buzz_start(pat, now);
                led_set(LED_RED, true);
                render_matrix(now);
                uint32_t s2r = sig_time_us() - m.t_us;
                if (ev & GL_EV_ROUGH_EVENT) {
                    STATE_LOCK();
                    lat_add(&g_app.lat_s2r, s2r);
                    STATE_UNLOCK();
                }
                silenced = false;
                if (g_app.lcd_ok && s_cache_screen == SCREEN_GUARD) draw_guard(now, wizard);
                uint32_t s2lcd = sig_time_us() - m.t_us;
                sig_log("#RESP,%s,s2r_us=%lu,s2lcd_us=%lu", (ev & GL_EV_ROUGH_EVENT) ? "rough" :
                        (ev & GL_EV_TEST_ALERT) ? "test" : "fault", (unsigned long)s2r, (unsigned long)s2lcd);
            }
            if (ev & GL_EV_ACKED) {
                buzz_stop();
                silenced = true;
                buzz_start(PAT_ACK, now);           /* short double chirp confirms the acknowledgement */
                STATE_LOCK();
                uint8_t src = g_app.gl.last_ack_src;
                STATE_UNLOCK();
                sig_log("#RESP,ack,src=%s,ack_to_feedback_us=%lu", gl_ack_src_name(src),
                        (unsigned long)(sig_time_us() - m.t_us));
            }
            ir_relay(ev);
        } else if (got && m.type == UI_MSG_KEY && m.arg < KEY_COUNT) {
            key_id_t k = (key_id_t)m.arg;
            s_keys[k].down = true;
            s_keys[k].long_fired = false;
            s_keys[k].t_ms = now;
            s_keys[k].up_since = 0;
            g_cnt.key_events++;
            if (!s_pat) buzz_start(PAT_CHIRP, now);
            led_set(LED_BLUE, true);
            uint32_t lat = sig_time_us() - m.t_us;
            STATE_LOCK();
            lat_add(&g_app.lat_key, lat);
            STATE_UNLOCK();
            sig_log("#KEY,%s,down,feedback_us=%lu", key_name(k), (unsigned long)lat);
            STATE_LOCK();
            uint8_t pk = g_app.prompt_kind;
            STATE_UNLOCK();
            if (pk == PROMPT_ASK) {                 /* answer the operator prompt; no product action */
                s_keys[k].long_fired = true;
                prompt_finish(key_name(k), now);
            } else if (s_irlearn >= 0 && k == KEY_LEFT) {
                s_keys[k].long_fired = true;        /* skip this pairing step */
                sig_log("#IR,pairing,skipped,%s", IRK_NAMES[s_irlearn]);
                irlearn_next(now);
            }
        } else if (got && m.type == UI_MSG_TEST) {
            run_ui_test(m.arg, now);
        } else if (got && m.type == UI_MSG_SCREEN) {
            STATE_LOCK();
            g_app.screen = (screen_t)(m.arg % SCREEN_COUNT);
            STATE_UNLOCK();
        } else if (got && m.type == UI_MSG_PROXCAL) {
            if (s_pc == PC_IDLE) proxcal_start(now);
        } else if (got && m.type == UI_MSG_PROMPT) {
            screen_reset(SCREEN_GUARD);              /* immediate redraw with the new text */
            last_draw = 0;
            buzz_start(PAT_CUE_ATTN, now);
        } else if (got && m.type == UI_MSG_CUE) {
            static const uint16_t *const cues[] = { PAT_CUE_START, PAT_CUE_STOP, PAT_CUE_OK, PAT_CUE_ERR, PAT_CUE_ATTN, PAT_TICK };
            if (m.arg < sizeof cues / sizeof cues[0]) buzz_start(cues[m.arg], now);
        } else if (got && m.type == UI_MSG_IRLEARN) {
            s_irlearn = 0;
            irlearn_prompt(now);
        } else if (got && m.type == UI_MSG_IRSEND) {
            bool ok = ir_tx_send((uint16_t)(m.events >> 8), (uint8_t)m.events);
            sig_log("#IR,send,addr=0x%04lx,cmd=0x%02lx,%s", (unsigned long)(m.events >> 8), (unsigned long)(m.events & 0xFF),
                    ok ? "sent" : "busy");
        }

        /* IR remote frames (decoded here: the UI task owns user input) */
        ir_frame_t irf;
        while (ir_rx_poll(&irf)) ir_handle(&irf, now);

        /* prompt / pairing timeout */
        STATE_LOCK();
        uint8_t pkind = g_app.prompt_kind;
        uint32_t puntil = g_app.prompt_until_ms;
        STATE_UNLOCK();
        if (pkind != PROMPT_NONE && (int32_t)(now - puntil) >= 0) {
            if (s_irlearn >= 0) { sig_log("#IR,pairing,timeout,%s", IRK_NAMES[s_irlearn]); irlearn_next(now); }
            else prompt_finish("TIMEOUT", now);
        }

        /* key release / long press detection */
        for (int k = 0; k < KEY_COUNT; k++) {
            key_state_t *ks = &s_keys[k];
            if (!ks->down) continue;
            bool held = key_is_down((key_id_t)k);
            if (held) {
                ks->up_since = 0;
                if (!ks->long_fired && (now - ks->t_ms) >= LONG_PRESS_MS) {
                    ks->long_fired = true;
                    sig_log("#KEY,%s,long", key_name((key_id_t)k));
                    if (k == KEY_RIGHT && g_app.screen == SCREEN_GUARD && s_pc == PC_IDLE) proxcal_start(now);
                }
                continue;
            }
            if (!ks->up_since) { ks->up_since = now ? now : 1; continue; }
            if ((now - ks->up_since) < RELEASE_MS) continue;
            ks->down = false;
            key_irq_enable((key_id_t)k, true);
            if (ks->long_fired) continue;
            uint32_t ev = 0;
            screen_t scr;
            STATE_LOCK();
            scr = g_app.screen;
            if (k == KEY_UP) ev = gl_raise_test(&g_app.gl, now);
            else if (k == KEY_DOWN) ev = gl_acknowledge(&g_app.gl, GL_ACK_KEY, now);
            else if (k == KEY_LEFT) g_app.screen = (screen_t)((g_app.screen + 1) % SCREEN_COUNT);
            STATE_UNLOCK();
            sig_log("#KEY,%s,short", key_name((key_id_t)k));
            if (k == KEY_RIGHT) {
                if (scr == SCREEN_SENSORS) { app_request_gyro_cal(); sig_log("#CAL,gyro_bias,requested"); }
                else if (scr == SCREEN_SYSTEM) app_post_service(SVC_HEALTH_NOW, 1);
                else if (scr == SCREEN_MODULES) { app_post_service(SVC_DIAG_ALL, 0); run_ui_test(0, now); }
            }
            if (ev) app_handle_gl_events(ev, sig_time_us());
        }

        /* buzzer: pattern engine, and periodic re-alert while an alert is unacknowledged */
        buzz_step(now);
        STATE_LOCK();
        uint32_t tev = gl_tick(&g_app.gl, now);
        bool want = gl_buzzer_wanted(&g_app.gl);
        gl_alert_t al = g_app.gl.alert;
        uint32_t since = g_app.gl.alert_since_ms;
        STATE_UNLOCK();
        (void)tev;
        if (!want && s_pat && !silenced && al == GL_ALERT_NONE) { buzz_stop(); silenced = true; }
        if (want && !s_pat && ((now - since) % 3000u) < UI_PERIOD_MS) {
            buzz_start(al == GL_ALERT_TEST ? PAT_TEST : al == GL_ALERT_SENSOR_FAULT ? PAT_FAULT : PAT_ROUGH, now);
        }

        proxcal_step(now, wizard);
        if (s_pc == PC_IDLE && wizard[0] && (now % 3000u) < UI_PERIOD_MS) wizard[0] = 0;   /* result shown <= 3 s */

        if (g_app.matrix_ok && (now - last_matrix) >= 100) { last_matrix = now; render_matrix(now); }
        if (g_app.lcd_ok && (now - last_draw) >= 200) {
            last_draw = now;
            screen_t scr = g_app.screen;
            if ((int)scr != s_cache_screen) screen_reset(scr);
            switch (scr) {
            case SCREEN_GUARD:   draw_guard(now, wizard); break;
            case SCREEN_SENSORS: draw_sensors(); break;
            case SCREEN_SYSTEM:  draw_system(now); break;
            default:             draw_modules(); break;
            }
            g_cnt.ui_frames++;
        }
    }
}
