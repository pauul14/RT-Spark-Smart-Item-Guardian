/*
 * task_infer.c - priority 4. Windowing, feature extraction, decision tree,
 * baseline comparison and product decision.
 *
 * A 200-sample (2 s) ring buffer is evaluated every 50 new samples (0.5 s).
 * A window is valid only if none of its samples is flagged invalid and at most
 * MAX_GAPS samples were missed inside it; invalid windows produce no prediction.
 */
#include "app.h"
#include "bsp.h"
#include "model_infer.h"
#include <string.h>

#define MAX_GAPS 2

static sig_raw_imu_t s_ring[SIG_WINDOW_SAMPLES];   /* SRAM (.bss), owned by this task */
static uint8_t s_bad[SIG_WINDOW_SAMPLES];          /* 1 = invalid sample */
static uint8_t s_gap[SIG_WINDOW_SAMPLES];          /* samples missed before this one */
static float s_scratch[SIG_FEATURE_SCRATCH(SIG_WINDOW_SAMPLES)];   /* infer task only */

void task_infer(void *arg)
{
    (void)arg;
    int pos = -1, filled = 0, since = 0;
    int bad_sum = 0, gap_sum = 0;
    memset(s_bad, 0, sizeof s_bad);
    memset(s_gap, 0, sizeof s_gap);

    for (;;) {
        imu_msg_t m;
        if (xQueueReceive(g_q_imu, &m, pdMS_TO_TICKS(100)) != pdTRUE) {
            xEventGroupSetBits(g_alive, ALIVE_INFER);
            continue;
        }
        xEventGroupSetBits(g_alive, ALIVE_INFER);

        pos = (pos + 1) % SIG_WINDOW_SAMPLES;
        bad_sum -= s_bad[pos];
        gap_sum -= s_gap[pos];
        s_ring[pos] = m.s;
        s_bad[pos] = (m.flags & IMU_FLAG_INVALID) ? 1 : 0;
        s_gap[pos] = m.missed;
        bad_sum += s_bad[pos];
        gap_sum += s_gap[pos];
        if (filled < SIG_WINDOW_SAMPLES) filled++;
        if (++since < SIG_HOP_SAMPLES || filled < SIG_WINDOW_SAMPLES) continue;
        since = 0;

        float bias[3];
        bool bias_ok;
        STATE_LOCK();
        memcpy(bias, g_app.gyro_bias, sizeof bias);
        bias_ok = g_app.gyro_bias_ok;
        STATE_UNLOCK();

        bool valid = bad_sum == 0 && gap_sum <= MAX_GAPS;
        float f[SIG_N_FEATURES];
        sig_prediction_t p = { SIG_CLASS_UNKNOWN, 0, 0, 0 };
        uint8_t base = SIG_CLASS_UNKNOWN;
        uint32_t c0 = sig_cycles();
        if (valid) {
            int start = (pos + 1) % SIG_WINDOW_SAMPLES;   /* oldest sample */
            sig_features_compute_ring(s_ring, SIG_WINDOW_SAMPLES, start, SIG_WINDOW_SAMPLES, bias, f, s_scratch);
            valid = sig_model_predict(f, &p);
            base = sig_baseline_predict(f);
        }
        uint32_t cycles = sig_cycles() - c0;
        uint32_t t_dec = sig_time_us();
        uint32_t s2d = t_dec - m.t_us;
        uint32_t now_ms = app_uptime_ms();

        uint32_t ev;
        bool verbose;
        STATE_LOCK();
        ev = gl_on_prediction(&g_app.gl, p.cls, valid, now_ms);
        g_app.inf_count++;
        g_app.last_valid = valid;
        if (valid) {
            g_app.inf_valid++;
            g_app.last_cls = p.cls;
            g_app.last_conf = p.confidence;
            g_app.last_base = base;
            memcpy(g_app.last_feat, f, sizeof f);
            if (base == p.cls) g_app.model_base_agree++;
            lat_add(&g_app.lat_inf, cycles / (SystemCoreClock / 1000000u));
            lat_add(&g_app.lat_s2d, s2d);
        }
        g_app.last_inf_cycles = cycles;
        verbose = g_app.verbose_inf;
        uint8_t shown = g_app.gl.display_class;
        STATE_UNLOCK();

        /* #INF,seq,t_ms,valid,class,conf,baseline,shown,cycles,s2d_us,bias_ok[,12 features] */
        if (valid && verbose) {
            sig_log("#INF,%lu,%lu,1,%s,%u,%s,%s,%lu,%lu,%d,%.4f,%.4f,%.4f,%.4f,%.4f,%.3f,%.3f,%.3f,%.3f,%.3f,%.4f,%.4f",
                    (unsigned long)m.seq, (unsigned long)now_ms, sig_class_short(p.cls), p.confidence,
                    sig_class_short(base), sig_class_short(shown), (unsigned long)cycles, (unsigned long)s2d, bias_ok,
                    (double)f[0], (double)f[1], (double)f[2], (double)f[3], (double)f[4], (double)f[5],
                    (double)f[6], (double)f[7], (double)f[8], (double)f[9], (double)f[10], (double)f[11]);
        } else {
            sig_log("#INF,%lu,%lu,%d,%s,%u,%s,%s,%lu,%lu,%d", (unsigned long)m.seq, (unsigned long)now_ms, valid,
                    sig_class_short(p.cls), p.confidence, sig_class_short(base), sig_class_short(shown),
                    (unsigned long)cycles, (unsigned long)s2d, bias_ok);
        }
        app_handle_gl_events(ev, m.t_us);
    }
}
