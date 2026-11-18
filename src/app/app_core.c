/*
 * app_core.c - RTOS objects, hardware bring-up sequence, logging, task creation.
 */
#include "app.h"
#include "bsp.h"
#include "board_io.h"
#include "board_pins.h"
#include "i2c_buses.h"
#include "led_matrix.h"
#include "model_infer.h"
#include "mini_printf.h"
#include "uart_console.h"
#include "ir_nec.h"
#include <stdarg.h>
#include <string.h>

app_state_t g_app;
app_counters_t g_cnt;
SemaphoreHandle_t g_state_mtx;
static SemaphoreHandle_t s_log_mtx;
QueueHandle_t g_q_imu, g_q_ui, g_q_service, g_q_evlog;
EventGroupHandle_t g_alive;
StreamBufferHandle_t g_log_sb, g_rx_sb;
TaskHandle_t g_task_sensor, g_task_infer, g_task_ui, g_task_health, g_task_console;

/* FreeRTOS heap in CCM RAM (configAPPLICATION_ALLOCATED_HEAP). CCM is CPU-only:
 * nothing allocated from this heap (task stacks, queues) is ever used as a DMA buffer. */
uint8_t ucHeap[configTOTAL_HEAP_SIZE] __attribute__((section(".ccmram"), aligned(8)));

#define LOG_SB_BYTES  8192
#define RX_SB_BYTES   256

void app_create_rtos_objects(void)
{
    g_state_mtx = xSemaphoreCreateMutex();
    s_log_mtx = xSemaphoreCreateMutex();
    g_q_imu = xQueueCreate(64, sizeof(imu_msg_t));
    g_q_ui = xQueueCreate(16, sizeof(ui_msg_t));
    g_q_service = xQueueCreate(8, sizeof(svc_msg_t));
    g_q_evlog = xQueueCreate(16, sizeof(evt_msg_t));
    g_alive = xEventGroupCreate();
    g_log_sb = xStreamBufferCreate(LOG_SB_BYTES, 1);
    g_rx_sb = xStreamBufferCreate(RX_SB_BYTES, 1);
    configASSERT(g_state_mtx && s_log_mtx && g_q_imu && g_q_ui && g_q_service && g_q_evlog &&
                 g_alive && g_log_sb && g_rx_sb);
    vQueueAddToRegistry(g_state_mtx, "state");
    vQueueAddToRegistry(s_log_mtx, "log");
    vQueueAddToRegistry(g_q_imu, "q_imu");
    vQueueAddToRegistry(g_q_ui, "q_ui");
    vQueueAddToRegistry(g_q_service, "q_svc");
    vQueueAddToRegistry(g_q_evlog, "q_evlog");
    i2c2_rtos_init();
    spi2_rtos_init();
}

uint32_t app_uptime_ms(void) { return sig_time_ms(); }

/* Formats one line and appends it to the log stream buffer. Never blocks the
 * caller for long: if the buffer cannot take the whole line it is dropped and
 * counted (the capture tool detects gaps from the sample sequence numbers). */
void sig_log(const char *fmt, ...)
{
    /* 384 B: the longest line (#HEALTH) was cut at 221 chars with the first 224 B buffer
     * (hardware finding 2026-11-18). Truncation is now counted, never silent. */
    char buf[384];
    va_list ap;
    va_start(ap, fmt);
    int n = sig_vsnprintf(buf, sizeof buf - 2, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n > (int)sizeof buf - 3) { n = (int)sizeof buf - 3; g_cnt.log_truncated++; }
    buf[n++] = '\r';
    buf[n++] = '\n';

    bool running = xTaskGetSchedulerState() == taskSCHEDULER_RUNNING;
    if (running && xSemaphoreTake(s_log_mtx, pdMS_TO_TICKS(5)) != pdTRUE) {
        g_cnt.log_dropped_lines++;
        g_cnt.log_dropped_bytes += (uint32_t)n;
        return;
    }
    if (xStreamBufferSpacesAvailable(g_log_sb) >= (size_t)n) {
        xStreamBufferSend(g_log_sb, buf, (size_t)n, 0);
    } else {
        g_cnt.log_dropped_lines++;
        g_cnt.log_dropped_bytes += (uint32_t)n;
    }
    if (running) xSemaphoreGive(s_log_mtx);
}

bool app_post_ui(const ui_msg_t *m)
{
    if (xQueueSend(g_q_ui, m, 0) != pdTRUE) { g_cnt.ui_queue_full++; return false; }
    return true;
}

bool app_post_service(svc_cmd_t cmd, uint32_t arg)
{
    svc_msg_t m = { (uint8_t)cmd, arg };
    if (xQueueSend(g_q_service, &m, 0) != pdTRUE) { g_cnt.svc_queue_full++; return false; }
    return true;
}

void app_post_event(evt_type_t type, uint8_t cls, uint16_t value)
{
    evt_msg_t e = { (uint8_t)type, cls, value, app_uptime_ms() };
    sig_log("#EVT,%lu,%s,%s,%u", (unsigned long)e.t_ms, evt_name(type), sig_class_name(cls), value);
    xQueueSend(g_q_evlog, &e, 0);
}

/* Translate product-logic event bits into feedback and records (no lock held). */
void app_handle_gl_events(uint32_t ev, uint32_t t_us)
{
    if (!ev) return;
    ui_msg_t m = { UI_MSG_EVENTS, 0, ev, t_us };
    app_post_ui(&m);
    STATE_LOCK();
    uint32_t rough = g_app.gl.rough_events, tests = g_app.gl.test_alerts;
    uint8_t ack_src = g_app.gl.last_ack_src;
    STATE_UNLOCK();
    if (ev & GL_EV_ROUGH_EVENT) app_post_event(EVT_ROUGH, SIG_CLASS_ROUGH, (uint16_t)rough);
    if (ev & GL_EV_TEST_ALERT) app_post_event(EVT_TEST_ALERT, SIG_CLASS_UNKNOWN, (uint16_t)tests);
    if (ev & GL_EV_ACKED) app_post_event(EVT_ACK, SIG_CLASS_UNKNOWN, ack_src);
    if (ev & GL_EV_FAULT_RAISED) app_post_event(EVT_FAULT, SIG_CLASS_UNKNOWN, 0);
    if (ev & GL_EV_FAULT_CLEARED) app_post_event(EVT_FAULT_CLEAR, SIG_CLASS_UNKNOWN, 0);
}

/* ---- option flags (RTC backup register, survive resets) ------------------ */
bool app_flag(uint32_t flag) { return (bkp_read(BKP_FLAGS) & flag) != 0; }

void app_set_flag(uint32_t flag, bool on)
{
    uint32_t f = bkp_read(BKP_FLAGS);
    bkp_write(BKP_FLAGS, on ? (f | flag) : (f & ~flag));
}

/* ---- proximity calibration persistence (RTC backup registers) ------------
 * BKP_PROX_THR carries a tag (0xC in bits 28-31, version 1 in bits 24-27), so
 * the registers the former lid calibration used are never read as thresholds. */
#define PROX_BKP_TAG  ((0xCu << 28) | (1u << 24))
#define PROX_BKP_MASK 0xFF000000u

void app_load_prox_cal(prox_cal_t *c)
{
    prox_cal_clear(c);
    if (bkp_read(BKP_MAGIC) != BKP_MAGIC_VALUE) return;
    uint32_t thr = bkp_read(BKP_PROX_THR);
    if ((thr & PROX_BKP_MASK) != PROX_BKP_TAG) return;
    uint32_t f = bkp_read(BKP_PROX_FAR), n = bkp_read(BKP_PROX_NEAR);
    c->far_mean = (uint16_t)(f & 0xFFFFu);  c->far_max = (uint16_t)(f >> 16);
    c->near_min = (uint16_t)(n & 0xFFFFu);  c->near_mean = (uint16_t)(n >> 16);
    c->thr_far = (uint16_t)(thr & 0xFFFu);
    c->thr_near = (uint16_t)((thr >> 12) & 0xFFFu);
    c->calibrated = c->thr_near > c->thr_far;
}

void app_save_prox_cal(const prox_cal_t *c)
{
    bkp_write(BKP_PROX_FAR, (uint32_t)c->far_mean | ((uint32_t)c->far_max << 16));
    bkp_write(BKP_PROX_NEAR, (uint32_t)c->near_min | ((uint32_t)c->near_mean << 16));
    bkp_write(BKP_PROX_THR, PROX_BKP_TAG | ((uint32_t)c->thr_far & 0xFFFu) | (((uint32_t)c->thr_near & 0xFFFu) << 12));
}

void app_clear_prox_cal(void)
{
    bkp_write(BKP_PROX_FAR, 0);
    bkp_write(BKP_PROX_NEAR, 0);
    bkp_write(BKP_PROX_THR, 0);
}

/* ---- hardware bring-up (before the scheduler; HAL_Delay runs on TIM6) ---- */
static void boot_lcd_line(int row, const char *txt, bool ok)
{
    if (!g_app.lcd_ok) return;
    lcd_text_field(8, 40 + row * 18, txt, 18, 2, ok ? C_GREEN : C_ORANGE, C_BLACK);
}

void app_init_hardware(void)
{
    char line[64];
    board_io_init();

    lcd_info_t li;
    g_app.lcd_ok = lcd_init(&li);
    g_app.lcd_id = li.id;
    sig_log("#INIT,lcd,%s,id=0x%04x", g_app.lcd_ok ? "ok" : "FAIL", li.id);
    if (g_app.lcd_ok) {
        lcd_fill_rect(0, 0, LCD_W, 30, C_NAVY);
        lcd_text(6, 7, "ITEM GUARDIAN boot", 2, C_WHITE, C_NAVY);
    }

    g_app.matrix_ok = matrix_init();
    if (g_app.matrix_ok) { matrix_fill(0, 0, 255); matrix_show(); }
    sig_log("#INIT,matrix,%s,leds=%d", g_app.matrix_ok ? "ok" : "FAIL", MATRIX_LED_COUNT);

    bool i2c_ok = i2c2_init();
    imu_info_t ii;
    g_app.imu_ok = i2c_ok && icm_init(&ii);
    g_app.imu_whoami = ii.whoami;
    g_app.imu_cfg_ok = ii.config_verified;
    sig_log("#INIT,imu,%s,whoami=0x%02x,config_readback=%s", g_app.imu_ok ? "ok" : "FAIL", ii.whoami,
            ii.config_verified ? "ok" : "mismatch");
    sig_snprintf(line, sizeof line, "IMU    %s", g_app.imu_ok ? "OK" : "FAIL");
    boot_lcd_line(0, line, g_app.imu_ok);

    g_app.als_ok = i2c_ok && ap3216c_init();
    sig_log("#INIT,als_ps,%s", g_app.als_ok ? "ok" : "FAIL");
    sig_snprintf(line, sizeof line, "LIGHT  %s", g_app.als_ok ? "OK" : "FAIL");
    boot_lcd_line(1, line, g_app.als_ok);

    sw_i2c_init();
    HAL_Delay(40);                                   /* AHT21 power-up time */
    g_app.aht_ok = aht21_init(&g_app.aht_status) == AHT_OK;
    sig_log("#INIT,aht21,%s,status=0x%02x", g_app.aht_ok ? "ok" : "FAIL", g_app.aht_status);
    sig_snprintf(line, sizeof line, "T/RH   %s", g_app.aht_ok ? "OK" : "FAIL");
    boot_lcd_line(2, line, g_app.aht_ok);

    g_app.flash_ok = spi2_init() && w25q_read_id(&g_app.flash);
    sig_log("#INIT,spi_flash,%s,jedec=%02x%02x%02x,size=%lu", g_app.flash_ok ? "ok" : "FAIL",
            g_app.flash.manufacturer, g_app.flash.mem_type, g_app.flash.capacity,
            (unsigned long)g_app.flash.size_bytes);
    sig_snprintf(line, sizeof line, "FLASH  %s", g_app.flash_ok ? "OK" : "FAIL");
    boot_lcd_line(3, line, g_app.flash_ok);

    g_app.rtc_src = rtc_init(&g_app.rtc_was_running);
    g_app.rtc_ok = g_app.rtc_src != RTC_SRC_NONE;
    if (bkp_read(BKP_MAGIC) != BKP_MAGIC_VALUE) {
        /* First boot of this firmware: take ownership of the backup-register map. */
        for (uint32_t i = 1; i < 20; i++) bkp_write(i, 0);
        bkp_write(BKP_MAGIC, BKP_MAGIC_VALUE);
    }
    sig_log("#INIT,rtc,%s,source=%s,was_running=%d,time_valid=%d", g_app.rtc_ok ? "ok" : "FAIL",
            rtc_src_name(g_app.rtc_src), g_app.rtc_was_running, rtc_time_valid());
    sig_snprintf(line, sizeof line, "RTC    %s", rtc_src_name(g_app.rtc_src));
    boot_lcd_line(4, line, g_app.rtc_src == RTC_SRC_LSE);

    ir_init();
    for (int k = 0; k < IRK_COUNT; k++) g_app.ir_code[k] = bkp_read(BKP_IR_TEST + (uint32_t)k);
    g_app.ir_relay = !app_flag(FLAG_IR_RELAY_OFF);
    sig_log("#INIT,ir,rx_idle=%d,relay=%d,paired_test=%d,paired_ack=%d,paired_next=%d", ir_rx_level(),
            g_app.ir_relay, (g_app.ir_code[IRK_TEST] & IR_CODE_VALID) != 0, (g_app.ir_code[IRK_ACK] & IR_CODE_VALID) != 0,
            (g_app.ir_code[IRK_NEXT] & IR_CODE_VALID) != 0);

    gl_init(&g_app.gl);
    prox_cal_t pcal;
    app_load_prox_cal(&pcal);
    prox_init(&g_app.prox, &pcal);
    sig_log("#INIT,prox_ack,%s,thr_far=%u,thr_near=%u,far_max=%u,near_min=%u", pcal.calibrated ? "calibrated" :
            "NOT_CALIBRATED(touchless ack disabled until 'prox cal')", pcal.thr_far, pcal.thr_near, pcal.far_max,
            pcal.near_min);
    g_app.screen = SCREEN_GUARD;
    for (int i = 0; i < 12; i++) strcpy(g_app.diag_short[i], "not run");

    sig_snprintf(line, sizeof line, "MODEL  %s", sig_model_is_placeholder() ? "PLACEHOLDER" : "trained");
    boot_lcd_line(5, line, !sig_model_is_placeholder());
    sig_log("#INIT,model,version=%s,placeholder=%d,nodes=%d,max_depth=%d,bytes=%lu",
            sig_model_version(), sig_model_is_placeholder(), sig_model_node_count(),
            sig_model_max_depth(), (unsigned long)sig_model_size_bytes());
}

void app_create_tasks(void)
{
    BaseType_t ok = pdPASS;
    ok &= xTaskCreate(task_sensor,  "sensor",  STACK_SENSOR,  NULL, PRIO_SENSOR,  &g_task_sensor);
    ok &= xTaskCreate(task_infer,   "infer",   STACK_INFER,   NULL, PRIO_INFER,   &g_task_infer);
    ok &= xTaskCreate(task_ui,      "ui",      STACK_UI,      NULL, PRIO_UI,      &g_task_ui);
    ok &= xTaskCreate(task_health,  "health",  STACK_HEALTH,  NULL, PRIO_HEALTH,  &g_task_health);
    ok &= xTaskCreate(task_console, "console", STACK_CONSOLE, NULL, PRIO_CONSOLE, &g_task_console);
    configASSERT(ok == pdPASS);
}
