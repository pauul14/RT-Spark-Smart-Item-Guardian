/*
 * task_health.c - priority 2. Supervision and service functions:
 *  - independent watchdog (IWDG, ~4 s) refreshed only when every task has
 *    signalled its heartbeat bit during the last second;
 *  - AHT21 temperature/humidity (2 s), MCU temperature/VDDA (5 s), RTC (1 s);
 *  - periodic "#HEALTH" report: heap, per-task stack high-water marks, counters;
 *  - module diagnostics requested by the console or the MODULES screen;
 *  - event log persistence (W25Q128, opt-in) from q_evlog.
 */
#include "app.h"
#include "bsp.h"
#include "board_io.h"
#include "i2c_buses.h"
#include "led_matrix.h"
#include "module_diag.h"
#include "mini_printf.h"
#include "uart_console.h"
#include "wired_link.h"
#include "sd_card.h"
#include <string.h>

#define HEALTH_PERIOD_MS  100
#define HUMID_WARN_PCT    70.0f

static IWDG_HandleTypeDef s_iwdg;

static void iwdg_start(void)
{
    s_iwdg.Instance = IWDG;
    s_iwdg.Init.Prescaler = IWDG_PRESCALER_64;   /* LSI ~32 kHz / 64 = 500 Hz */
    s_iwdg.Init.Reload = 4095;                   /* ~8.2 s nominal; LSI 17..47 kHz (DS8626) -> 5.6..15.4 s */
    HAL_IWDG_Init(&s_iwdg);
}

/* Watchdog supervision: once per second, refresh the IWDG only if every task
 * set its heartbeat bit since the previous check. Also called from long
 * service operations running inside this task. */
static uint32_t s_t_wd;
void health_wd_poll(void)
{
    uint32_t now = app_uptime_ms();
    if (now - s_t_wd < 1000) return;
    s_t_wd = now;
    EventBits_t b = xEventGroupClearBits(g_alive, ALIVE_ALL);
    if ((b & ALIVE_ALL) == ALIVE_ALL) {
        HAL_IWDG_Refresh(&s_iwdg);
        g_cnt.iwdg_refresh++;
    } else {
        g_cnt.task_stall++;
        sig_fault_record(8, NULL, (uint32_t)(~b & ALIVE_ALL), "task-stall");
        sig_log("#WARN,task_stall,missing_mask=0x%02lx", (unsigned long)(~b & ALIVE_ALL));
    }
}

static void diag_store(int idx, diag_result_t r, const char *txt)
{
    char s[24];
    sig_snprintf(s, sizeof s, "%s", diag_result_name(r));
    (void)txt;
    STATE_LOCK();
    strncpy(g_app.diag_short[idx % 12], s, sizeof g_app.diag_short[0] - 1);
    STATE_UNLOCK();
}

void health_report_tasks(void)
{
    TaskStatus_t st[10];               /* called from console and health: no shared static */
    uint32_t total_rt = 0;
    UBaseType_t n = uxTaskGetSystemState(st, 10, &total_rt);
    for (UBaseType_t i = 0; i < n; i++) {
        uint32_t pct10 = total_rt ? (uint32_t)((uint64_t)st[i].ulRunTimeCounter * 1000u / total_rt) : 0;
        static const char st_name[] = "XRBSDI";
        sig_log("#TASK,%s,prio=%lu,state=%c,stack_hwm_words=%u,runtime_pct=%lu.%lu", st[i].pcTaskName,
                (unsigned long)st[i].uxCurrentPriority, st_name[st[i].eCurrentState < 5 ? st[i].eCurrentState : 5],
                (unsigned)st[i].usStackHighWaterMark, (unsigned long)(pct10 / 10), (unsigned long)(pct10 % 10));
    }
    sig_log("#HEAP,total=%u,free=%u,min_ever_free=%u", (unsigned)configTOTAL_HEAP_SIZE,
            (unsigned)xPortGetFreeHeapSize(), (unsigned)xPortGetMinimumEverFreeHeapSize());
}

static void report_health(uint32_t now)
{
    extern volatile uint32_t g_i2c2_errors, g_i2c2_recoveries, g_swi2c_errors;
    sig_log("#HEALTH,t_ms=%lu,heap_free=%u,heap_min=%u,hwm_sensor=%u,hwm_infer=%u,hwm_ui=%u,hwm_health=%u,hwm_console=%u,"
            "samples=%lu,invalid=%lu,missed=%lu,drdy_to=%lu,qfull=%lu,i2c_err=%lu,i2c_rec=%lu,swi2c_err=%lu,"
            "log_drop=%lu,uart_ovr=%lu,iwdg=%lu,stall=%lu,imu_read_avg_us=%lu,imu_read_max_us=%lu,"
            "sensor_busy_avg_us=%lu,sensor_busy_max_us=%lu,log_trunc=%lu",
            (unsigned long)now, (unsigned)xPortGetFreeHeapSize(), (unsigned)xPortGetMinimumEverFreeHeapSize(),
            (unsigned)uxTaskGetStackHighWaterMark(g_task_sensor), (unsigned)uxTaskGetStackHighWaterMark(g_task_infer),
            (unsigned)uxTaskGetStackHighWaterMark(g_task_ui), (unsigned)uxTaskGetStackHighWaterMark(g_task_health),
            (unsigned)uxTaskGetStackHighWaterMark(g_task_console), (unsigned long)g_cnt.imu_samples,
            (unsigned long)g_cnt.imu_invalid, (unsigned long)g_cnt.imu_missed, (unsigned long)g_cnt.imu_drdy_timeout,
            (unsigned long)g_cnt.imu_queue_full, (unsigned long)g_i2c2_errors, (unsigned long)g_i2c2_recoveries,
            (unsigned long)g_swi2c_errors, (unsigned long)g_cnt.log_dropped_lines, (unsigned long)g_uart_rx_overrun,
            (unsigned long)g_cnt.iwdg_refresh, (unsigned long)g_cnt.task_stall,
            (unsigned long)(g_cnt.imu_samples ? g_cnt.imu_read_us_sum / g_cnt.imu_samples : 0),
            (unsigned long)g_cnt.imu_read_us_max,
            (unsigned long)(g_cnt.imu_samples ? g_cnt.sensor_busy_us_sum / g_cnt.imu_samples : 0),
            (unsigned long)g_cnt.sensor_busy_us_max, (unsigned long)g_cnt.log_truncated);
}

static void report_wired(void)
{
    wired_stats_t w;
    wired_stats(&w);
    sig_log("#WIRE,can=%d,can_tx_ok=%lu,can_tx_fail=%lu,can_ack_err=%lu,can_busy=%lu,tec=%u,rec=%u,lec=%u,"
            "err_passive=%d,bus_off=%d,rs485=%d,rs485_lines=%lu,rs485_rx_bytes=%lu,rs485_queries=%lu",
            w.can_on, (unsigned long)w.can_tx_ok, (unsigned long)w.can_tx_fail, (unsigned long)w.can_ack_err,
            (unsigned long)w.can_busy, w.can_tec, w.can_rec, w.can_lec, w.can_error_passive, w.can_bus_off,
            w.rs485_on, (unsigned long)w.rs485_tx_lines, (unsigned long)w.rs485_rx_bytes, (unsigned long)w.rs485_queries);
}

static void send_heartbeat(uint32_t now)
{
    STATE_LOCK();
    gl_state_t gl = g_app.gl;
    STATE_UNLOCK();
    wired_heartbeat(gl.display_class, sig_class_short(gl.display_class), (uint8_t)gl.alert, gl_alert_name(gl.alert),
                    gl.alert_acked, gl.rough_events, gl.sensor_fault, now);
}

static void i2c_scan(void)
{
    char out[160];
    int n = sig_snprintf(out, sizeof out, "#DIAG,i2c2_scan,found=");
    for (uint8_t a = 0x08; a < 0x78; a++) {
        if (i2c2_probe(a) && n < (int)sizeof out - 6) n += sig_snprintf(out + n, sizeof out - (size_t)n, "0x%02x ", a);
    }
    sig_log("%s(expected 0x10 ES8388, 0x1e AP3216C, 0x68 ICM-20608)", out);
}

static void run_service(const svc_msg_t *m)
{
    char out[200];
    diag_result_t r;
    switch (m->cmd) {
    case SVC_DIAG_ALL:
        for (int c = SVC_DIAG_IMU; c <= SVC_DIAG_I2CSCAN; c++) {
            svc_msg_t one = { (uint8_t)c, 0 };
            run_service(&one);
            health_wd_poll();
        }
        sig_log("#DIAG,all,DONE");
        break;
    case SVC_DIAG_IMU: {
        STATE_LOCK();
        sig_raw_imu_t s = g_app.imu;
        bool ok = g_app.imu_ok, cfg = g_app.imu_cfg_ok, drdy = g_app.imu_drdy_mode;
        uint8_t who = g_app.imu_whoami;
        int16_t tr = g_app.imu_temp_raw;
        STATE_UNLOCK();
        sig_log("#DIAG,imu,%s,whoami=0x%02x,config=%s,drdy=%s,raw=%d,%d,%d,%d,%d,%d,die_temp=%.1fC,samples=%lu,invalid=%lu",
                ok ? "PASS" : "FAIL", who, cfg ? "verified" : "mismatch", drdy ? "irq" : "poll",
                s.ax, s.ay, s.az, s.gx, s.gy, s.gz, (double)icm_temp_c(tr), (unsigned long)g_cnt.imu_samples,
                (unsigned long)g_cnt.imu_invalid);
        break;
    }
    case SVC_DIAG_ALS: {
        STATE_LOCK();
        als_reading_t a = g_app.als;
        bool v = g_app.als_valid, ok = g_app.als_ok;
        prox_det_t px = g_app.prox;
        STATE_UNLOCK();
        sig_log("#DIAG,als_ps,%s,als=%u,lux=%.1f,ps=%u,ir=%u,ps_valid=%d,prox_zone=%s,prox_cal=%s,thr_far=%u,thr_near=%u,"
                "far_max=%u,near_min=%u,gestures=%lu,invalid_ps=%lu",
                ok && v ? "PASS" : "FAIL", a.als, (double)ap3216c_lux(a.als), a.ps, a.ir, a.ps_valid,
                prox_zone_name(px.zone), px.cal.calibrated ? "measured" : "none", px.cal.thr_far, px.cal.thr_near,
                px.cal.far_max, px.cal.near_min, (unsigned long)px.gestures, (unsigned long)px.invalid_readings);
        break;
    }
    case SVC_DIAG_AHT: {
        STATE_LOCK();
        bool v = g_app.aht_valid;
        float t = g_app.aht_t, h = g_app.aht_rh;
        STATE_UNLOCK();
        sig_log("#DIAG,aht21,%s,temp=%.2fC,rh=%.2f%%,init_status=0x%02x,errors=%lu", v ? "PASS" : "FAIL",
                (double)t, (double)h, g_app.aht_status, (unsigned long)g_cnt.aht_err);
        break;
    }
    case SVC_DIAG_FLASH: {
        w25q_info_t fi = {0};
        uint8_t sr = 0, head[8] = {0};
        bool ok = spi2_lock(200);
        if (ok) { ok = w25q_read_id(&fi) && w25q_read_status(&sr) && w25q_read(0, head, 8); spi2_unlock(); }
        sig_log("#DIAG,spi_flash,%s,jedec=%02x%02x%02x,size=%lu,sr1=0x%02x,first8=%02x%02x%02x%02x%02x%02x%02x%02x (read-only)",
                ok ? "PASS" : "FAIL", fi.manufacturer, fi.mem_type, fi.capacity, (unsigned long)fi.size_bytes, sr,
                head[0], head[1], head[2], head[3], head[4], head[5], head[6], head[7]);
        evlog_status();
        break;
    }
    case SVC_DIAG_SD:
        r = diag_sdcard(out, sizeof out);
        sig_log("#DIAG,sdcard,%s,%s", diag_result_name(r), out);
        diag_store(SVC_DIAG_SD, r, out);
        sdlog_after_diag();                     /* remount the CSV log on the next poll */
        sdlog_status();
        break;
    case SVC_DIAG_RTC: {
        rtc_time_t t = {0};
        bool ok = rtc_get(&t);
        if (ok) sig_log("#DIAG,rtc,PASS,source=%s,time_valid=%d,now=%04u-%02u-%02u %02u:%02u:%02u",
                        rtc_src_name(g_app.rtc_src), rtc_time_valid(), t.year, t.mon, t.day, t.hour, t.min, t.sec);
        else sig_log("#DIAG,rtc,FAIL,source=%s,time_valid=0,bdcr=0x%08lx", rtc_src_name(g_app.rtc_src),
                     (unsigned long)RCC->BDCR);
        break;
    }
    case SVC_DIAG_IR:
        r = diag_ir(out, sizeof out);
        sig_log("#DIAG,ir,%s,%s", diag_result_name(r), out);
        diag_store(SVC_DIAG_IR, r, out);
        break;
    case SVC_DIAG_WIFI:
        r = diag_rw007(out, sizeof out);
        sig_log("#DIAG,wifi_rw007,%s,%s", diag_result_name(r), out);
        diag_store(SVC_DIAG_WIFI, r, out);
        break;
    case SVC_DIAG_CODEC:
        r = diag_es8388(out, sizeof out);
        sig_log("#DIAG,codec_es8388,%s,%s", diag_result_name(r), out);
        diag_store(SVC_DIAG_CODEC, r, out);
        break;
    case SVC_DIAG_CAN:
        r = diag_can_loopback(out, sizeof out);
        sig_log("#DIAG,can1,%s,%s", diag_result_name(r), out);
        diag_store(SVC_DIAG_CAN, r, out);
        wired_reinit();                         /* restore the telemetry configuration */
        report_wired();
        break;
    case SVC_DIAG_MCU: {
        float t, v;
        r = diag_mcu_adc(out, sizeof out, &t, &v);
        sig_log("#DIAG,mcu_adc,%s,%s", diag_result_name(r), out);
        diag_store(SVC_DIAG_MCU, r, out);
        break;
    }
    case SVC_DIAG_I2CSCAN: i2c_scan(); break;
    case SVC_LOG_DUMP: evlog_dump(m->arg); break;
    case SVC_LOG_ERASE: evlog_erase(); break;
    case SVC_HEALTH_NOW: report_health(app_uptime_ms()); if (m->arg) health_report_tasks(); break;
    case SVC_SD_STATUS: sdlog_status(); break;
    case SVC_WIRE_CONFIG:
        app_set_flag(FLAG_CAN_ON, (m->arg & 1u) != 0);
        app_set_flag(FLAG_RS485_ON, (m->arg & 2u) != 0);
        wired_init((m->arg & 1u) != 0, (m->arg & 2u) != 0);
        report_wired();
        break;
    case SVC_WIRE_STATUS: report_wired(); break;
    default: break;
    }
}

void task_health(void *arg)
{
    (void)arg;
    TickType_t wake = xTaskGetTickCount();
    uint32_t t_aht = 0, t_mcu = 0, t_rtc = 0, t_rep = 0, aht_trig = 0;
    bool aht_pending = false;
    uint32_t humid_since = 0, t_hb = 0;
    evlog_init();
    wired_init(app_flag(FLAG_CAN_ON), app_flag(FLAG_RS485_ON));
    app_post_event(EVT_BOOT, SIG_CLASS_UNKNOWN, (uint16_t)sig_boot_count());
    iwdg_start();
    xEventGroupClearBits(g_alive, ALIVE_ALL);
    sig_log("#RTOS,scheduler_running,tasks=%u,iwdg_timeout_ms~8190", (unsigned)uxTaskGetNumberOfTasks());
    health_report_tasks();

    for (;;) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(HEALTH_PERIOD_MS));
        uint32_t now = app_uptime_ms();

        health_wd_poll();

        svc_msg_t sm;
        while (xQueueReceive(g_q_service, &sm, 0) == pdTRUE) run_service(&sm);
        evt_msg_t em;
        while (xQueueReceive(g_q_evlog, &em, 0) == pdTRUE) {
            evlog_handle(&em);
            sdlog_event(&em);
            wired_event(em.type, evt_name(em.type), em.cls, sig_class_name(em.cls), em.value, em.t_ms);
        }
        sdlog_poll(now);
        if (wired_rs485_query_pending() || now - t_hb >= 5000) {
            t_hb = now;
            send_heartbeat(now);
        }

        if (!aht_pending && now - t_aht >= 2000) {
            t_aht = now;
            if (aht21_trigger() == AHT_OK) { aht_pending = true; aht_trig = now; }
            else { g_cnt.aht_err++; STATE_LOCK(); g_app.aht_valid = false; STATE_UNLOCK(); }
        }
        if (aht_pending && now - aht_trig >= 100) {        /* >= 80 ms conversion */
            aht_pending = false;
            float t = 0, h = 0;
            aht_status_t st = aht21_fetch(&t, &h);
            if (st != AHT_OK) g_cnt.aht_err++;
            bool humid;
            STATE_LOCK();
            g_app.aht_valid = st == AHT_OK;
            if (st == AHT_OK) { g_app.aht_t = t; g_app.aht_rh = h; }
            if (st == AHT_OK && h >= HUMID_WARN_PCT) { if (!humid_since) humid_since = now; }
            else if (st == AHT_OK) humid_since = 0;
            humid = humid_since && (now - humid_since) >= 60000u;
            bool changed = humid != g_app.humid_warn;
            g_app.humid_warn = humid;
            STATE_UNLOCK();
            if (changed && humid) app_post_event(EVT_HUMID, SIG_CLASS_UNKNOWN, (uint16_t)h);
        }
        if (now - t_mcu >= 5000) {
            t_mcu = now;
            char out[96];
            float t, v;
            diag_mcu_adc(out, sizeof out, &t, &v);
            STATE_LOCK();
            g_app.mcu_t = t;
            g_app.vdda = v;
            STATE_UNLOCK();
        }
        if (now - t_rtc >= 1000) {
            t_rtc = now;
            rtc_time_t t;
            bool ok = rtc_get(&t) && rtc_time_valid();
            STATE_LOCK();
            g_app.rtc_valid = ok;
            if (ok) g_app.rtc = t;
            STATE_UNLOCK();
        }
        if (now - t_rep >= 10000) {
            t_rep = now;
            report_health(now);
            STATE_LOCK();
            bool v = g_app.aht_valid;
            float t = g_app.aht_t, h = g_app.aht_rh;
            als_reading_t a = g_app.als;
            prox_zone_t pz = g_app.prox.zone;
            float mt = g_app.mcu_t, vd = g_app.vdda;
            STATE_UNLOCK();
            sig_log("#ENV,t_ms=%lu,aht_valid=%d,temp_c=%.2f,rh_pct=%.1f,als=%u,ps=%u,ir=%u,prox_zone=%s,mcu_c=%.1f,vdda=%.3f",
                    (unsigned long)now, v, (double)t, (double)h, a.als, a.ps, a.ir, prox_zone_name(pz), (double)mt, (double)vd);
        }
    }
}
