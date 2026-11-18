/*
 * app.h - Smart Item Guardian application: shared state, RTOS objects, tasks.
 *
 * Task table (also docs/freertos_task_table.md):
 *   name     prio  stack(words)  activation                 communication
 *   sensor    5     512          ICM DRDY EXTI (100 Hz) /    -> q_imu (queue), state mutex, q_ui
 *                                10 ms vTaskDelayUntil fallback
 *   infer     4     1024         q_imu items; every 50 samples   <- q_imu, -> q_ui, q_evlog, state mutex
 *   ui        3     1024         q_ui or 20 ms timeout          <- q_ui (events, keys), state mutex
 *   health    2     1024         100 ms vTaskDelayUntil         <- q_service, q_evlog; IWDG; event group
 *   console   1     1024         log stream / UART RX / 10 ms   <- log stream buffer, rx stream buffer
 *   (idle     0     160)
 */
#ifndef SIG_APP_H
#define SIG_APP_H

#include <stdbool.h>
#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "event_groups.h"
#include "stream_buffer.h"
#include "sig_types.h"
#include "features.h"
#include "guardian_logic.h"
#include "prox_ack.h"
#include "latency_stats.h"
#include "sensors.h"
#include "spi_flash.h"
#include "rtc_clock.h"
#include "sig_lcd.h"

#define FW_NAME     "SmartItemGuardian"
#define FW_VERSION  "1.0.0"

/* ---- task priorities / stacks (words) ---------------------------------- */
#define PRIO_SENSOR   5
#define PRIO_INFER    4
#define PRIO_UI       3
#define PRIO_HEALTH   2
#define PRIO_CONSOLE  1
#define STACK_SENSOR  512
#define STACK_INFER   1024
#define STACK_UI      1024
#define STACK_HEALTH  1024
#define STACK_CONSOLE 1024

/* ---- heartbeat bits (event group) --------------------------------------- */
#define ALIVE_SENSOR   (1u << 0)
#define ALIVE_INFER    (1u << 1)
#define ALIVE_UI       (1u << 2)
#define ALIVE_CONSOLE  (1u << 3)
#define ALIVE_ALL      (ALIVE_SENSOR | ALIVE_INFER | ALIVE_UI | ALIVE_CONSOLE)

/* ---- messages ----------------------------------------------------------- */
#define IMU_FLAG_INVALID  0x01u
#define IMU_FLAG_GAP      0x02u     /* one or more samples were missed before this one */
#define IMU_FLAG_POLLED   0x04u     /* DRDY interrupt absent, timed polling */
typedef struct {
    uint32_t seq;
    uint32_t t_us;                  /* DRDY edge (or poll) timestamp, TIM2 1 MHz */
    sig_raw_imu_t s;
    uint8_t flags;
    uint8_t missed;                 /* samples missed before this one (saturates at 255) */
} imu_msg_t;

typedef enum { UI_MSG_EVENTS = 0, UI_MSG_KEY, UI_MSG_TEST, UI_MSG_SCREEN, UI_MSG_PROXCAL, UI_MSG_PROMPT,
               UI_MSG_CUE, UI_MSG_IRLEARN, UI_MSG_IRSEND } ui_msg_type_t;
typedef struct {
    uint8_t  type;                  /* ui_msg_type_t */
    uint8_t  arg;
    uint32_t events;                /* GL_EV_* bits for UI_MSG_EVENTS */
    uint32_t t_us;                  /* origin timestamp (newest sample / key edge) */
} ui_msg_t;

typedef enum {
    SVC_DIAG_ALL = 0, SVC_DIAG_IMU, SVC_DIAG_ALS, SVC_DIAG_AHT, SVC_DIAG_FLASH, SVC_DIAG_SD, SVC_DIAG_RTC,
    SVC_DIAG_IR, SVC_DIAG_WIFI, SVC_DIAG_CODEC, SVC_DIAG_CAN, SVC_DIAG_MCU, SVC_DIAG_I2CSCAN,
    SVC_LOG_DUMP, SVC_LOG_ERASE, SVC_HEALTH_NOW, SVC_SD_STATUS, SVC_WIRE_CONFIG, SVC_WIRE_STATUS, SVC_COUNT
} svc_cmd_t;
typedef struct { uint8_t cmd; uint32_t arg; } svc_msg_t;

/* EVT_ACK value = gl_ack_src_t; EVT_PROX_GESTURE value = 1 if it acknowledged an alert */
typedef enum { EVT_BOOT = 0, EVT_ROUGH, EVT_TEST_ALERT, EVT_PROX_GESTURE, EVT_ACK,
               EVT_FAULT, EVT_FAULT_CLEAR, EVT_HUMID, EVT_IR_REMOTE } evt_type_t;
typedef struct { uint8_t type; uint8_t cls; uint16_t value; uint32_t t_ms; } evt_msg_t;

/* ---- persistent option flags (RTC backup register BKP_FLAGS) -------------- */
#define FLAG_EVLOG_ON      (1u << 0)   /* W25Q event-log writes (opt-in) */
/* bit 1 is used by rtc_clock.c: calendar set */
#define FLAG_SD_OFF        (1u << 2)   /* microSD CSV log disabled (default: enabled) */
#define FLAG_CAN_ON        (1u << 3)   /* CAN telemetry (opt-in) */
#define FLAG_RS485_ON      (1u << 4)   /* RS485 telemetry (opt-in) */
#define FLAG_IR_RELAY_OFF  (1u << 5)   /* IR alert relay disabled (default: enabled) */

/* ---- operator prompts shown on the LCD (console 'say' / 'ask') ----------- */
typedef enum { PROMPT_NONE = 0, PROMPT_SAY, PROMPT_ASK } prompt_kind_t;
#define PROMPT_TEXT 81                  /* 4 LCD rows x 20 characters */

/* ---- IR remote pairing (codes in BKP_IR_TEST..BKP_IR_NEXT) ---------------- */
typedef enum { IRK_TEST = 0, IRK_ACK, IRK_NEXT, IRK_COUNT } ir_key_t;
#define IR_CODE_VALID 0x80000000u       /* | addr << 8 | cmd */

/* ---- shared state (protected by g_state_mtx) ----------------------------- */
typedef enum { SCREEN_GUARD = 0, SCREEN_SENSORS, SCREEN_SYSTEM, SCREEN_MODULES, SCREEN_COUNT } screen_t;

typedef struct {
    /* boot-time module status */
    bool lcd_ok, imu_ok, imu_cfg_ok, als_ok, aht_ok, flash_ok, matrix_ok, rtc_ok, rtc_was_running;
    uint16_t lcd_id;
    uint8_t imu_whoami, aht_status;
    w25q_info_t flash;
    rtc_src_t rtc_src;
    /* live sensor data */
    sig_raw_imu_t imu;
    int16_t imu_temp_raw;
    bool imu_drdy_mode;
    als_reading_t als;
    bool als_valid;
    uint32_t als_seq;               /* +1 per AP3216C reading (5 Hz) */
    float aht_t, aht_rh;
    bool aht_valid;
    float mcu_t, vdda;
    rtc_time_t rtc;
    bool rtc_valid;
    /* gyro bias (raw LSB) */
    float gyro_bias[3];
    bool gyro_bias_ok;
    /* inference */
    uint8_t last_cls, last_base, last_conf;
    bool last_valid;
    float last_feat[SIG_N_FEATURES];
    uint32_t last_inf_cycles;
    uint32_t inf_count, inf_valid, model_base_agree;
    /* product logic */
    gl_state_t gl;
    prox_det_t prox;
    bool prox_cal_active;           /* calibration wizard running: gestures suspended */
    bool humid_warn;
    /* data capture */
    bool capture;
    char cap_label[20];
    uint16_t cap_session, cap_trial;
    uint32_t cap_samples, cap_start_ms;
    /* service */
    bool inject_imu_fault;
    bool verbose_inf;
    screen_t screen;
    char diag_short[12][24];        /* last diag result per module, for the LCD */
    /* operator prompt */
    uint8_t prompt_kind;            /* prompt_kind_t */
    uint16_t prompt_id;
    uint32_t prompt_until_ms;
    char prompt[PROMPT_TEXT];
    /* IR remote control + relay */
    uint32_t ir_code[IRK_COUNT];
    bool ir_relay;
    /* latency rings (us) */
    lat_ring_t lat_inf;             /* feature extraction + tree + baseline (cycle counter) */
    lat_ring_t lat_s2d;             /* newest sample timestamp -> decision */
    lat_ring_t lat_s2r;             /* newest sample timestamp -> alert actuation (buzzer/LED) */
    lat_ring_t lat_key;             /* key edge -> audible/visible acknowledgement */
} app_state_t;

/* ---- counters (single writer each, read anywhere) ----------------------- */
typedef struct {
    volatile uint32_t imu_samples, imu_invalid, imu_bus_err, imu_stuck, imu_missed, imu_drdy_timeout,
                      imu_queue_full, imu_reinit, als_err, aht_err, log_dropped_lines, log_dropped_bytes,
                      ui_frames, key_events, iwdg_refresh, task_stall, evlog_written, evlog_failed,
                      svc_queue_full, ui_queue_full,
                      imu_read_us_sum, imu_read_us_max, sensor_busy_us_sum, sensor_busy_us_max,  /* sensor timing */
                      log_truncated;
} app_counters_t;

extern app_state_t      g_app;
extern app_counters_t   g_cnt;
extern SemaphoreHandle_t g_state_mtx;
extern QueueHandle_t    g_q_imu, g_q_ui, g_q_service, g_q_evlog;
extern EventGroupHandle_t g_alive;
extern StreamBufferHandle_t g_log_sb, g_rx_sb;
extern TaskHandle_t     g_task_sensor, g_task_infer, g_task_ui, g_task_health, g_task_console;

#define STATE_LOCK()   xSemaphoreTake(g_state_mtx, portMAX_DELAY)
#define STATE_UNLOCK() xSemaphoreGive(g_state_mtx)

/* ---- logging (lines end with "\r\n" automatically) ----------------------- */
void sig_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* ---- app services -------------------------------------------------------- */
void app_create_rtos_objects(void);
void app_init_hardware(void);
void app_create_tasks(void);
bool app_post_ui(const ui_msg_t *m);
bool app_post_service(svc_cmd_t cmd, uint32_t arg);
void app_post_event(evt_type_t type, uint8_t cls, uint16_t value);
void app_handle_gl_events(uint32_t ev, uint32_t t_us);   /* call WITHOUT the state lock */
void app_request_gyro_cal(void);
void app_load_prox_cal(prox_cal_t *c);
void app_save_prox_cal(const prox_cal_t *c);
void app_clear_prox_cal(void);
uint32_t app_uptime_ms(void);

/* task entry points */
void task_sensor(void *arg);
void task_infer(void *arg);
void task_ui(void *arg);
void task_health(void *arg);
void task_console(void *arg);

/* console / golden self-test / event log */
void cli_execute(char *line);
void health_wd_poll(void);
void health_report_tasks(void);
void golden_run(void);
void evlog_init(void);
void evlog_handle(const evt_msg_t *e);
void evlog_status(void);
void evlog_dump(uint32_t max);
void evlog_erase(void);
bool evlog_writes_enabled(void);
void evlog_set_enabled(bool en);
const char *evt_name(uint8_t t);

/* microSD CSV log (sd_logger.c, health task) */
void sdlog_poll(uint32_t now);
void sdlog_event(const evt_msg_t *e);
void sdlog_status(void);
void sdlog_set_enabled(bool en);
bool sdlog_enabled(void);
bool sdlog_mounted(void);
void sdlog_after_diag(void);

/* option flags */
bool app_flag(uint32_t flag);
void app_set_flag(uint32_t flag, bool on);

#endif /* SIG_APP_H */
