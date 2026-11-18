/*
 * test_core.c - host unit tests for the portable product logic in src/core:
 * guardian_logic (alerts, cooldown, ack sources, test alert, fault, proximity gate),
 * prox_ack (calibration, hysteresis, deliberate one-shot gestures), latency_stats (percentiles) and
 * mini_printf (formatting used in every log line).
 */
#include <stdio.h>
#include <string.h>
#include "guardian_logic.h"
#include "prox_ack.h"
#include "latency_stats.h"
#include "mini_printf.h"
#include "model_infer.h"
#include "ir_nec_codec.h"

static int g_fail, g_pass;
#define CHECK(c) do { if (c) g_pass++; else { g_fail++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

static void test_logic(void)
{
    gl_state_t g;
    gl_init(&g);
    CHECK(g.display_class == SIG_CLASS_UNKNOWN);
    /* invalid windows never change the state */
    CHECK(gl_on_prediction(&g, SIG_CLASS_ROUGH, false, 0) == 0 && g.windows_invalid == 1 && g.rough_events == 0);
    /* majority vote for the displayed state */
    gl_on_prediction(&g, SIG_CLASS_STATIONARY, true, 100);
    gl_on_prediction(&g, SIG_CLASS_STATIONARY, true, 600);
    gl_on_prediction(&g, SIG_CLASS_NORMAL, true, 1100);
    CHECK(g.display_class == SIG_CLASS_STATIONARY);
    /* ROUGH shows immediately and raises one event per cooldown */
    uint32_t ev = gl_on_prediction(&g, SIG_CLASS_ROUGH, true, 2000);
    CHECK((ev & GL_EV_ROUGH_EVENT) && g.display_class == SIG_CLASS_ROUGH && g.rough_events == 1);
    CHECK(g.alert == GL_ALERT_ROUGH && gl_buzzer_wanted(&g));
    ev = gl_on_prediction(&g, SIG_CLASS_ROUGH, true, 2500);
    CHECK(!(ev & GL_EV_ROUGH_EVENT) && g.rough_events == 1);
    ev = gl_on_prediction(&g, SIG_CLASS_ROUGH, true, 2500 + GL_ROUGH_COOLDOWN_MS);
    CHECK((ev & GL_EV_ROUGH_EVENT) && g.rough_events == 2);
    /* acknowledge silences and clears; the source is recorded */
    CHECK(gl_acknowledge(&g, GL_ACK_KEY, 9000) == GL_EV_ACKED && !gl_buzzer_wanted(&g) && g.alert == GL_ALERT_NONE);
    CHECK(g.last_ack_src == GL_ACK_KEY && g.acks[GL_ACK_KEY] == 1);
    CHECK(gl_acknowledge(&g, GL_ACK_KEY, 9100) == 0 && g.acks[GL_ACK_KEY] == 1);
    /* test alert: raised on request, lowest priority */
    ev = gl_raise_test(&g, 10000);
    CHECK((ev & GL_EV_TEST_ALERT) && g.alert == GL_ALERT_TEST && g.test_alerts == 1 && gl_buzzer_wanted(&g));
    gl_on_prediction(&g, SIG_CLASS_ROUGH, true, 30000);               /* rough outranks a test alert */
    CHECK(g.alert == GL_ALERT_ROUGH);
    CHECK(gl_raise_test(&g, 30100) == 0 && g.alert == GL_ALERT_ROUGH && g.test_alerts == 1);
    /* visual auto-clear */
    gl_tick(&g, 30000 + GL_ALERT_VISUAL_MS);
    CHECK(g.alert == GL_ALERT_NONE);
    /* sensor fault: raised, outranks everything, not cleared by ack, cleared by recovery */
    ev = gl_on_sensor_fault(&g, true, 50000);
    CHECK((ev & GL_EV_FAULT_RAISED) && g.alert == GL_ALERT_SENSOR_FAULT && g.display_class == SIG_CLASS_UNKNOWN);
    gl_acknowledge(&g, GL_ACK_CONSOLE, 50100);
    CHECK(g.alert == GL_ALERT_SENSOR_FAULT && !gl_buzzer_wanted(&g) && g.last_ack_src == GL_ACK_CONSOLE);
    gl_tick(&g, 50000 + 3 * GL_ALERT_VISUAL_MS);
    CHECK(g.alert == GL_ALERT_SENSOR_FAULT);
    ev = gl_on_sensor_fault(&g, false, 70000);
    CHECK((ev & GL_EV_FAULT_CLEARED) && g.alert == GL_ALERT_NONE);
    /* millisecond wrap-around in the cooldown */
    gl_init(&g);
    gl_on_prediction(&g, SIG_CLASS_ROUGH, true, 0xFFFFFF00u);
    ev = gl_on_prediction(&g, SIG_CLASS_ROUGH, true, 0x00000100u);
    CHECK(!(ev & GL_EV_ROUGH_EVENT) && g.rough_events == 1);
}

static void test_prox_gate(void)
{
    /* gl_prox_ack: deliberate gestures acknowledge only a settled, active alert */
    gl_state_t g;
    const char *why = NULL;
    gl_init(&g);
    CHECK(gl_prox_ack(&g, 1000, &why) == 0 && !strcmp(why, "no_alert"));
    gl_raise_test(&g, 2000);
    CHECK(gl_prox_ack(&g, 2000 + GL_PROX_MIN_ALERT_AGE_MS - 1, &why) == 0 && !strcmp(why, "alert_too_new"));
    CHECK(gl_prox_ack(&g, 2000 + GL_PROX_MIN_ALERT_AGE_MS, &why) == GL_EV_ACKED && !strcmp(why, "ack"));
    CHECK(g.last_ack_src == GL_ACK_PROX && g.acks[GL_ACK_PROX] == 1 && g.alert == GL_ALERT_NONE);
    CHECK(gl_prox_ack(&g, 5000, &why) == 0 && !strcmp(why, "no_alert"));     /* nothing left to ack */
    /* while the device is still shown as ROUGH a gesture is refused (hand busy shaking it) */
    gl_on_prediction(&g, SIG_CLASS_ROUGH, true, 10000);
    CHECK(gl_prox_ack(&g, 12000, &why) == 0 && !strcmp(why, "still_rough") && g.alert == GL_ALERT_ROUGH);
    for (int i = 0; i < GL_DISPLAY_VOTES; i++) gl_on_prediction(&g, SIG_CLASS_STATIONARY, true, 12500 + 500u * i);
    CHECK(g.display_class == SIG_CLASS_STATIONARY);
    CHECK(gl_prox_ack(&g, 14500, &why) == GL_EV_ACKED && g.acks[GL_ACK_PROX] == 2);
}

static void test_prox(void)
{
    prox_cal_t c;
    prox_det_t d;
    /* uncalibrated: never fires, whatever the readings */
    prox_cal_clear(&c);
    prox_init(&d, &c);
    int fired = 0;
    for (int i = 0; i < 20; i++) fired += prox_update(&d, (uint16_t)(i < 10 ? 0 : 1000), true);
    CHECK(fired == 0 && !d.cal.calibrated);
    /* calibration from measured phases */
    CHECK(prox_cal_from_stats(&c, 5, 9, 15, 300, 450, 15) == PROX_CAL_OK && c.calibrated);
    CHECK(c.thr_far == 9 + 291 / 4 && c.thr_near == 9 + 291 / 2 && c.thr_far < c.thr_near);
    prox_cal_t before = c;
    CHECK(prox_cal_from_stats(&c, 5, 9, 15, 25, 30, 15) == PROX_CAL_FAIL_LOW_CONTRAST);   /* gap 16 < 20 */
    CHECK(prox_cal_from_stats(&c, 5, 9, 4, 300, 450, 15) == PROX_CAL_FAIL_NO_SAMPLES);
    CHECK(prox_cal_from_stats(&c, 5, 300, 15, 290, 450, 15) == PROX_CAL_FAIL_LOW_CONTRAST); /* near < far */
    CHECK(memcmp(&before, &c, sizeof c) == 0);                                           /* failures change nothing */
    /* already near at start: never fires, however long it stays */
    prox_init(&d, &c);
    fired = 0;
    for (int i = 0; i < 50; i++) fired += prox_update(&d, 500, true);
    CHECK(fired == 0 && d.zone == PROX_ZONE_NEAR);
    /* far long enough, then a deliberate hold: exactly one gesture */
    for (int i = 0; i < PROX_REARM_SAMPLES; i++) fired += prox_update(&d, 5, true);
    CHECK(fired == 0 && d.ready);
    for (int i = 0; i < PROX_HOLD_SAMPLES - 1; i++) fired += prox_update(&d, 500, true);
    CHECK(fired == 0);
    fired += prox_update(&d, 500, true);
    CHECK(fired == 1 && d.gestures == 1 && d.latched);
    /* hand stays near: no repeat */
    for (int i = 0; i < 100; i++) fired += prox_update(&d, 500, true);
    CHECK(fired == 1);
    /* brief far (shorter than re-arm) does not re-arm */
    for (int i = 0; i < PROX_REARM_SAMPLES - 1; i++) fired += prox_update(&d, 5, true);
    for (int i = 0; i < 10; i++) fired += prox_update(&d, 500, true);
    CHECK(fired == 1);
    /* readings inside the hysteresis band keep the zone (no re-arm from mid values) */
    for (int i = 0; i < 10; i++) fired += prox_update(&d, (uint16_t)((c.thr_far + c.thr_near) / 2), true);
    CHECK(fired == 1 && d.zone == PROX_ZONE_NEAR);
    /* real release, then a brief pass shorter than the hold: no gesture */
    for (int i = 0; i < PROX_REARM_SAMPLES; i++) fired += prox_update(&d, 5, true);
    for (int i = 0; i < PROX_HOLD_SAMPLES - 1; i++) fired += prox_update(&d, 500, true);
    fired += prox_update(&d, 5, true);
    CHECK(fired == 1);
    /* a second deliberate gesture after the release fires again */
    for (int i = 0; i < PROX_REARM_SAMPLES; i++) fired += prox_update(&d, 5, true);
    for (int i = 0; i < PROX_HOLD_SAMPLES; i++) fired += prox_update(&d, 500, true);
    CHECK(fired == 2 && d.gestures == 2);
    /* invalid readings (IR overflow / I2C error) are ignored and do not break a hold */
    for (int i = 0; i < PROX_REARM_SAMPLES; i++) fired += prox_update(&d, 5, true);
    fired += prox_update(&d, 500, true);
    fired += prox_update(&d, 1023, false);
    fired += prox_update(&d, 500, true);
    CHECK(fired == 2 && d.invalid_readings == 1);
    fired += prox_update(&d, 500, true);
    CHECK(fired == 3);
    CHECK(!strcmp(prox_zone_name(PROX_ZONE_NEAR), "NEAR") && !strcmp(prox_cal_result_name(PROX_CAL_OK), "ok"));
}

static void test_lat(void)
{
    static lat_ring_t r;
    static uint32_t s[LAT_RING];
    lat_summary_t o;
    lat_reset(&r);
    lat_summarise(&r, s, &o);
    CHECK(o.n == 0);
    for (uint32_t i = 100; i >= 1; i--) lat_add(&r, i);           /* 1..100 in reverse */
    lat_summarise(&r, s, &o);
    CHECK(o.n == 100 && o.min == 1 && o.max == 100 && o.median == 50 && o.p95 == 95);
    for (uint32_t i = 0; i < 1000; i++) lat_add(&r, 7);
    lat_summarise(&r, s, &o);
    CHECK(o.n == LAT_RING && o.total == 1100 && o.median == 7 && o.p95 == 7);
}

static void test_printf(void)
{
    char b[64];
    sig_snprintf(b, sizeof b, "%d|%5d|%-4d|%05d|%u|%x|%X", -12, 34, 7, -42, 4000000000u, 0xbeefu, 0xabu);
    CHECK(!strcmp(b, "-12|   34|7   |-0042|4000000000|beef|AB"));
    sig_snprintf(b, sizeof b, "%.3f|%.1f|%+.2f|%.0f|%8.2f", 1.0005, -0.05, 2.5, 2.4, 3.14159);
    CHECK(!strcmp(b, "1.001|-0.1|+2.50|2|    3.14"));
    sig_snprintf(b, sizeof b, "%s|%.3s|%c|%lu|%%", "abc", "abcdef", 'z', 123456789ul);
    CHECK(!strcmp(b, "abc|abc|z|123456789|%"));
    int n = sig_snprintf(b, 8, "%s", "0123456789");
    CHECK(n == 10 && !strcmp(b, "0123456"));
    sig_snprintf(b, sizeof b, "%02x%02x", 0x0a, 0xff);
    CHECK(!strcmp(b, "0aff"));
}

static void test_model_guards(void)
{
    float x[SIG_N_FEATURES] = {0};
    sig_prediction_t p;
    CHECK(sig_model_predict(x, &p) && p.cls < SIG_CLASS_COUNT);
    x[3] = 0.0f / 0.0f;                                           /* NaN must not become a prediction */
    CHECK(!sig_model_predict(x, &p) && p.cls == SIG_CLASS_UNKNOWN);
}

/* Feed an encoded frame through the decoder; jitter_us is added to marks and
 * removed from spaces, as the IRM-H638T lengthens marks. */
static int ir_feed_frame(ir_nec_dec_t *d, uint16_t addr, uint8_t cmd, int jitter_us, ir_frame_t *out)
{
    uint16_t dur[70];
    int n = ir_nec_encode(addr, cmd, dur, 70);
    int r = 0;
    ir_nec_feed(d, 40000, 1, out);                                  /* idle before the frame */
    for (int i = 0; i < n; i++) {
        int mark = (i % 2) == 0;
        int dd = (int)dur[i] + (mark ? jitter_us : -jitter_us);
        int rr = ir_nec_feed(d, (uint32_t)dd, mark ? 0 : 1, out);
        if (rr) r = rr;
    }
    return r;
}

static void test_ir(void)
{
    ir_nec_dec_t d;
    ir_frame_t f;
    memset(&d, 0, sizeof d);
    uint16_t dur[70];
    CHECK(ir_nec_encode(0x5A, 0x01, dur, 70) == 67 && dur[0] == 9000 && dur[1] == 4500 && dur[66] == 562 && dur[67] == 0);
    CHECK(ir_nec_encode(0x5A, 0x01, dur, 10) == 0);
    CHECK(ir_feed_frame(&d, 0x5A, 0x01, 0, &f) == 1 && f.addr == 0x5A && f.cmd == 0x01 && !f.repeat);
    CHECK(ir_feed_frame(&d, 0x00, 0xFF, 120, &f) == 1 && f.addr == 0x00 && f.cmd == 0xFF);    /* +120 us marks */
    CHECK(ir_feed_frame(&d, 0x10, 0x45, -150, &f) == 1 && f.addr == 0x10 && f.cmd == 0x45);
    CHECK(ir_feed_frame(&d, 0x1234, 0x12, 60, &f) == 1 && f.addr == 0x1234 && f.cmd == 0x12); /* extended (0x12 != ~0x34) */
    CHECK(ir_feed_frame(&d, 0xBF40, 0x13, 0, &f) == 1 && f.addr == 0x40);          /* 0xBF == ~0x40: standard */
    /* repeat code: 9 ms mark, 2.25 ms space, stop mark */
    ir_nec_feed(&d, 40000, 1, &f);
    ir_nec_feed(&d, 9000, 0, &f);
    ir_nec_feed(&d, 2250, 1, &f);
    CHECK(ir_nec_feed(&d, 560, 0, &f) == 2 && f.repeat);
    /* corrupted command inverse -> rejected, error counted, decoder recovers */
    uint32_t e0 = d.errors;
    int n = ir_nec_encode(0x5A, 0x03, dur, 70);
    dur[2 + 2 * 24 + 1] = dur[2 + 2 * 24 + 1] == 562 ? 1687 : 562;     /* flip first bit of ~cmd */
    int r = 0;
    for (int i = 0; i < n; i++) { int rr = ir_nec_feed(&d, dur[i], (i % 2) ? 1 : 0, &f); if (rr) r = rr; }
    CHECK(r == 0 && d.errors == e0 + 1);
    CHECK(ir_feed_frame(&d, 0x5A, 0x04, 0, &f) == 1 && f.cmd == 0x04);
    /* garbage durations never produce a frame */
    r = 0;
    for (int i = 0; i < 200; i++) { int rr = ir_nec_feed(&d, (uint32_t)(137 * i % 12000), i & 1, &f); if (rr) r = rr; }
    CHECK(r == 0);
}

int main(void)
{
    test_ir();
    test_logic();
    test_prox();
    test_prox_gate();
    test_lat();
    test_printf();
    test_model_guards();
    printf("host core tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
