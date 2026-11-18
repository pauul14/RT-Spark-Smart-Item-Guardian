/*
 * task_sensor.c - priority 5. Owns the ICM-20608 and AP3216C sampling.
 *
 * Timing: each sample is triggered by the ICM DATA_RDY interrupt (100 Hz from
 * the sensor's own sample clock) and timestamped in the ISR. If DRDY stops
 * arriving (5 consecutive 25 ms timeouts) the task falls back to timed polling
 * with vTaskDelayUntil(10 ms) and flags the samples, so sampling never depends
 * on a busy-wait loop. Invalid reads are flagged, never forwarded as data.
 */
#include "app.h"
#include "bsp.h"
#include "board_io.h"
#include "i2c_buses.h"
#include <math.h>
#include <string.h>

#define DRDY_TIMEOUT_MS        25
#define DRDY_FALLBACK_AFTER    5
#define FAULT_AFTER_INVALID    10    /* 100 ms of invalid samples -> sensor fault */
#define CLEAR_AFTER_VALID      20
#define REINIT_EVERY_MS        1000
#define ALS_EVERY_N            20    /* 5 Hz */
#define BIAS_SAMPLES           200
#define BIAS_MAX_ACC_STD_G     0.02f
#define BIAS_MAX_GYRO_SPAN     (int)(6.0f * SIG_GYRO_LSB_PER_DPS)   /* +/-3 dps span */

static volatile uint32_t s_drdy_t_us;
static volatile bool s_bias_request = true;   /* calibrate at boot */

void app_imu_drdy_isr(uint32_t t_us)
{
    s_drdy_t_us = t_us;
    BaseType_t woken = pdFALSE;
    if (g_task_sensor) vTaskNotifyGiveFromISR(g_task_sensor, &woken);
    portYIELD_FROM_ISR(woken);
}

void app_request_gyro_cal(void) { s_bias_request = true; }

/* Gyro bias estimation over BIAS_SAMPLES consecutive still samples. */
typedef struct {
    int n;
    float sum[3];
    int16_t gmin[3], gmax[3];
    float amag_sum, amag_sq;
} bias_acc_t;

static void bias_reset(bias_acc_t *b)
{
    memset(b, 0, sizeof *b);
    for (int i = 0; i < 3; i++) { b->gmin[i] = INT16_MAX; b->gmax[i] = INT16_MIN; }
}

static bool bias_add(bias_acc_t *b, const sig_raw_imu_t *s, float out[3], bool *done)
{
    const int16_t g[3] = { s->gx, s->gy, s->gz };
    for (int i = 0; i < 3; i++) {
        b->sum[i] += g[i];
        if (g[i] < b->gmin[i]) b->gmin[i] = g[i];
        if (g[i] > b->gmax[i]) b->gmax[i] = g[i];
    }
    float ax = s->ax / SIG_ACC_LSB_PER_G, ay = s->ay / SIG_ACC_LSB_PER_G, az = s->az / SIG_ACC_LSB_PER_G;
    float a = sqrtf(ax * ax + ay * ay + az * az);
    b->amag_sum += a;
    b->amag_sq += a * a;
    b->n++;
    *done = false;
    if (b->n < BIAS_SAMPLES) return false;
    *done = true;
    float mean = b->amag_sum / b->n;
    float var = b->amag_sq / b->n - mean * mean;
    bool still = var < BIAS_MAX_ACC_STD_G * BIAS_MAX_ACC_STD_G;
    for (int i = 0; i < 3; i++) still = still && (b->gmax[i] - b->gmin[i]) <= BIAS_MAX_GYRO_SPAN;
    if (still) for (int i = 0; i < 3; i++) out[i] = b->sum[i] / b->n;
    return still;
}

static void read_light(uint32_t now_ms, uint32_t t_us)
{
    als_reading_t r = {0};
    bool ok = ap3216c_read(&r);
    if (!ok) g_cnt.als_err++;
    uint32_t ev = 0;
    bool gesture = false;
    const char *why = "";
    STATE_LOCK();
    g_app.als_valid = ok;
    if (ok) g_app.als = r;
    g_app.als_seq++;
    /* touchless acknowledgement: one deliberate near-hold gesture (see prox_ack.h) */
    if (!g_app.prox_cal_active) gesture = prox_update(&g_app.prox, r.ps, ok && r.ps_valid);
    if (gesture) ev = gl_prox_ack(&g_app.gl, now_ms, &why);
    STATE_UNLOCK();
    if (gesture) {
        sig_log("#PROX,gesture,%s,ps=%u,t_us=%lu", why, r.ps, (unsigned long)t_us);
        app_post_event(EVT_PROX_GESTURE, SIG_CLASS_UNKNOWN, ev ? 1u : 0u);
    }
    app_handle_gl_events(ev, t_us);
}

void task_sensor(void *arg)
{
    (void)arg;
    imu_drdy_irq_init();
    sig_raw_imu_t dummy;
    (void)icm_read(&dummy, NULL);                  /* clear a pending DRDY */

    bool drdy_mode = true;
    int timeouts_in_row = 0;
    uint32_t seq = 0, last_t = 0, invalid_run = 0, valid_run = 0, last_reinit = 0;
    bool have_last = false, fault = false;
    bias_acc_t bias;
    bias_reset(&bias);
    TickType_t wake = xTaskGetTickCount();

    for (;;) {
        uint32_t t_us;
        uint8_t flags = 0;
        if (drdy_mode) {
            if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(DRDY_TIMEOUT_MS))) {
                t_us = s_drdy_t_us;
                timeouts_in_row = 0;
            } else {
                g_cnt.imu_drdy_timeout++;
                t_us = sig_time_us();
                flags |= IMU_FLAG_POLLED;
                if (++timeouts_in_row >= DRDY_FALLBACK_AFTER) {
                    drdy_mode = false;
                    wake = xTaskGetTickCount();
                    sig_log("#WARN,imu,drdy_absent,falling_back_to_10ms_polling");
                }
            }
        } else {
            vTaskDelayUntil(&wake, pdMS_TO_TICKS(10));
            t_us = sig_time_us();
            flags |= IMU_FLAG_POLLED;
        }
        xEventGroupSetBits(g_alive, ALIVE_SENSOR);
        uint32_t busy0 = sig_time_us();
        uint32_t now_ms = app_uptime_ms();

        imu_msg_t m;
        memset(&m, 0, sizeof m);
        m.seq = seq++;
        m.t_us = t_us;
        int16_t temp_raw = 0;
        bool inject;
        STATE_LOCK();
        inject = g_app.inject_imu_fault;
        STATE_UNLOCK();
        uint32_t r0 = sig_time_us();
        imu_status_t st = inject ? IMU_ERR_BUS : icm_read(&m.s, &temp_raw);
        uint32_t rd = sig_time_us() - r0;
        g_cnt.imu_read_us_sum += rd;
        if (rd > g_cnt.imu_read_us_max) g_cnt.imu_read_us_max = rd;
        if (st != IMU_OK) {
            flags |= IMU_FLAG_INVALID;
            memset(&m.s, 0, sizeof m.s);
            g_cnt.imu_invalid++;
            if (st == IMU_ERR_BUS) g_cnt.imu_bus_err++; else g_cnt.imu_stuck++;
        }
        if (have_last) {
            uint32_t dt = t_us - last_t;
            if (dt > 15000u) {
                uint32_t missed = (dt + 5000u) / 10000u - 1u;
                g_cnt.imu_missed += missed;
                m.missed = (uint8_t)(missed > 255 ? 255 : missed);
                flags |= IMU_FLAG_GAP;
            }
        }
        last_t = t_us;
        have_last = true;
        m.flags = flags;
        g_cnt.imu_samples++;

        if (xQueueSend(g_q_imu, &m, 0) != pdTRUE) g_cnt.imu_queue_full++;

        /* Fault state machine: raise after a run of invalid samples, try to
         * re-initialise the sensor once per second, clear after a valid run. */
        if (flags & IMU_FLAG_INVALID) {
            valid_run = 0;
            if (++invalid_run >= FAULT_AFTER_INVALID && !fault) {
                fault = true;
                STATE_LOCK();
                uint32_t ev = gl_on_sensor_fault(&g_app.gl, true, now_ms);
                STATE_UNLOCK();
                sig_log("#FAULT,imu,raised,consecutive_invalid=%lu,injected=%d", (unsigned long)invalid_run, inject);
                app_handle_gl_events(ev, t_us);
            }
            if (fault && !inject && (now_ms - last_reinit) >= REINIT_EVERY_MS) {
                last_reinit = now_ms;
                imu_info_t ii;
                g_cnt.imu_reinit++;
                i2c2_recover();
                if (icm_init(&ii)) sig_log("#INFO,imu,reinit_ok");
            }
        } else {
            invalid_run = 0;
            if (fault && ++valid_run >= CLEAR_AFTER_VALID) {
                fault = false;
                STATE_LOCK();
                uint32_t ev = gl_on_sensor_fault(&g_app.gl, false, now_ms);
                STATE_UNLOCK();
                sig_log("#FAULT,imu,cleared");
                app_handle_gl_events(ev, t_us);
            }
        }

        bool cap;
        STATE_LOCK();
        if (!(flags & IMU_FLAG_INVALID)) { g_app.imu = m.s; g_app.imu_temp_raw = temp_raw; }
        g_app.imu_drdy_mode = drdy_mode;
        cap = g_app.capture;
        if (cap) g_app.cap_samples++;
        STATE_UNLOCK();

        if (cap) {
            sig_log("S,%lu,%lu,%d,%d,%d,%d,%d,%d,%u", (unsigned long)m.seq, (unsigned long)m.t_us,
                    m.s.ax, m.s.ay, m.s.az, m.s.gx, m.s.gy, m.s.gz, m.flags);
        }

        /* Gyro bias: needs BIAS_SAMPLES consecutive still, valid samples. */
        if (s_bias_request) {
            if (flags & (IMU_FLAG_INVALID | IMU_FLAG_GAP)) {
                bias_reset(&bias);
            } else {
                float b[3];
                bool done;
                bool still = bias_add(&bias, &m.s, b, &done);
                if (done) {
                    if (still) {
                        STATE_LOCK();
                        memcpy(g_app.gyro_bias, b, sizeof b);
                        g_app.gyro_bias_ok = true;
                        STATE_UNLOCK();
                        s_bias_request = false;
                        sig_log("#CAL,gyro_bias,ok,%.2f,%.2f,%.2f", (double)b[0], (double)b[1], (double)b[2]);
                    } else {
                        sig_log("#CAL,gyro_bias,retry,not_still");
                    }
                    bias_reset(&bias);
                }
            }
        }

        if ((m.seq % ALS_EVERY_N) == 0) read_light(now_ms, t_us);
        uint32_t busy = sig_time_us() - busy0;
        g_cnt.sensor_busy_us_sum += busy;
        if (busy > g_cnt.sensor_busy_us_max) g_cnt.sensor_busy_us_max = busy;
    }
}
