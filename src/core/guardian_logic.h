/*
 * guardian_logic.h - product decision logic (portable, unit-tested on host).
 *
 * Handheld handling-warning prototype: per-window predictions, button presses,
 * touchless (proximity) gestures and sensor faults become user-facing state and
 * alert events. There is no enclosure and no lid context in this product version.
 */
#ifndef SIG_GUARDIAN_LOGIC_H
#define SIG_GUARDIAN_LOGIC_H

#include "sig_types.h"

#define GL_ROUGH_COOLDOWN_MS     5000u   /* repeated ROUGH windows inside this = same event */
#define GL_ALERT_VISUAL_MS      10000u   /* banner auto-clears after this if not acknowledged */
#define GL_DISPLAY_VOTES            3    /* majority vote length for the displayed state */
#define GL_PROX_MIN_ALERT_AGE_MS 1000u   /* a gesture cannot acknowledge an alert younger than this */

typedef enum {
    GL_ALERT_NONE = 0,
    GL_ALERT_ROUGH,          /* rough handling detected */
    GL_ALERT_TEST,           /* user-requested test alert (UP key / 'alert test') */
    GL_ALERT_SENSOR_FAULT,   /* IMU data invalid: predictions suspended */
} gl_alert_t;

typedef enum { GL_ACK_KEY = 0, GL_ACK_PROX, GL_ACK_IR, GL_ACK_CONSOLE } gl_ack_src_t;

/* Event bits returned by the gl_* functions. */
#define GL_EV_STATE_CHANGED   (1u << 0)
#define GL_EV_ROUGH_EVENT     (1u << 1)   /* new rough-handling event (counted, logged, beeped) */
#define GL_EV_TEST_ALERT      (1u << 2)
#define GL_EV_ACKED           (1u << 4)
#define GL_EV_FAULT_RAISED    (1u << 5)
#define GL_EV_FAULT_CLEARED   (1u << 6)

typedef struct {
    /* user-facing state */
    uint8_t   display_class;      /* sig_class_t (majority-voted, ROUGH shown immediately) */
    gl_alert_t alert;             /* highest-priority active alert */
    bool      alert_acked;        /* buzzer silenced, banner still shown until timeout */
    bool      sensor_fault;
    uint8_t   last_ack_src;       /* gl_ack_src_t of the most recent acknowledgement */
    /* counters */
    uint32_t  rough_events;
    uint32_t  test_alerts;
    uint32_t  acks[4];            /* per gl_ack_src_t */
    uint32_t  windows_valid;
    uint32_t  windows_invalid;
    /* internals */
    uint8_t   votes[GL_DISPLAY_VOTES];
    uint8_t   vote_count;
    uint8_t   vote_pos;
    uint32_t  last_rough_ms;
    bool      rough_seen;
    uint32_t  alert_since_ms;
} gl_state_t;

void     gl_init(gl_state_t *g);
uint32_t gl_on_prediction(gl_state_t *g, uint8_t cls, bool valid, uint32_t now_ms);
uint32_t gl_raise_test(gl_state_t *g, uint32_t now_ms);
uint32_t gl_acknowledge(gl_state_t *g, gl_ack_src_t src, uint32_t now_ms);
/* A deliberate proximity gesture (prox_update returned true). Acknowledges only an
 * unacknowledged alert that is at least GL_PROX_MIN_ALERT_AGE_MS old, and not while
 * the device is shown as ROUGH (it is still being shaken). *why gets the reason. */
uint32_t gl_prox_ack(gl_state_t *g, uint32_t now_ms, const char **why);
uint32_t gl_on_sensor_fault(gl_state_t *g, bool fault, uint32_t now_ms);
uint32_t gl_tick(gl_state_t *g, uint32_t now_ms);
/* True while the buzzer should sound for the current alert. */
bool     gl_buzzer_wanted(const gl_state_t *g);

const char *gl_alert_name(gl_alert_t a);
const char *gl_ack_src_name(uint8_t src);

#endif /* SIG_GUARDIAN_LOGIC_H */
