/*
 * guardian_logic.c - see guardian_logic.h. Time arithmetic uses unsigned
 * differences so the 49.7-day millisecond wrap is harmless.
 */
#include "guardian_logic.h"
#include <string.h>

static int alert_rank(gl_alert_t a)
{
    switch (a) {
    case GL_ALERT_SENSOR_FAULT: return 3;
    case GL_ALERT_ROUGH:        return 2;
    case GL_ALERT_TEST:         return 1;
    default:                    return 0;
    }
}

static void raise_alert(gl_state_t *g, gl_alert_t a, uint32_t now)
{
    if (g->alert == GL_ALERT_NONE || g->alert_acked || alert_rank(a) >= alert_rank(g->alert)) {
        g->alert = a;
        g->alert_acked = false;
        g->alert_since_ms = now;
    }
}

void gl_init(gl_state_t *g)
{
    memset(g, 0, sizeof(*g));
    g->display_class = SIG_CLASS_UNKNOWN;
    g->alert = GL_ALERT_NONE;
}

static uint8_t majority(const gl_state_t *g)
{
    int cnt[SIG_CLASS_COUNT] = {0};
    for (int i = 0; i < g->vote_count; i++) {
        if (g->votes[i] < SIG_CLASS_COUNT) cnt[g->votes[i]]++;
    }
    /* newest vote breaks ties */
    uint8_t newest = g->votes[(g->vote_pos + GL_DISPLAY_VOTES - 1) % GL_DISPLAY_VOTES];
    uint8_t best = newest;
    for (int c = 0; c < SIG_CLASS_COUNT; c++) {
        if (cnt[c] > cnt[best]) best = (uint8_t)c;
    }
    return best;
}

uint32_t gl_on_prediction(gl_state_t *g, uint8_t cls, bool valid, uint32_t now)
{
    uint32_t ev = 0;
    if (!valid || cls >= SIG_CLASS_COUNT) {
        g->windows_invalid++;
        return 0;                       /* invalid windows never drive the UI state */
    }
    g->windows_valid++;

    g->votes[g->vote_pos] = cls;
    g->vote_pos = (uint8_t)((g->vote_pos + 1) % GL_DISPLAY_VOTES);
    if (g->vote_count < GL_DISPLAY_VOTES) g->vote_count++;

    uint8_t shown = (cls == SIG_CLASS_ROUGH) ? (uint8_t)SIG_CLASS_ROUGH : majority(g);
    if (shown != g->display_class) {
        g->display_class = shown;
        ev |= GL_EV_STATE_CHANGED;
    }

    if (cls == SIG_CLASS_ROUGH) {
        bool new_event = !g->rough_seen || (uint32_t)(now - g->last_rough_ms) >= GL_ROUGH_COOLDOWN_MS;
        g->last_rough_ms = now;
        g->rough_seen = true;
        if (new_event) {
            g->rough_events++;
            raise_alert(g, GL_ALERT_ROUGH, now);
            ev |= GL_EV_ROUGH_EVENT;
        }
    }
    return ev;
}

uint32_t gl_raise_test(gl_state_t *g, uint32_t now)
{
    if (g->alert != GL_ALERT_NONE && !g->alert_acked && alert_rank(g->alert) > alert_rank(GL_ALERT_TEST)) {
        return 0;                       /* a higher-priority alert is active: the test is not shown */
    }
    raise_alert(g, GL_ALERT_TEST, now); /* (re)starts the alert and its age */
    g->test_alerts++;
    return GL_EV_TEST_ALERT;
}

uint32_t gl_acknowledge(gl_state_t *g, gl_ack_src_t src, uint32_t now)
{
    (void)now;
    if (g->alert == GL_ALERT_NONE || g->alert_acked) return 0;
    g->alert_acked = true;
    g->last_ack_src = (uint8_t)src;
    if ((unsigned)src < 4u) g->acks[src]++;
    if (g->alert != GL_ALERT_SENSOR_FAULT) {
        g->alert = GL_ALERT_NONE;       /* acknowledged events clear immediately */
    }
    return GL_EV_ACKED;
}

uint32_t gl_prox_ack(gl_state_t *g, uint32_t now, const char **why)
{
    const char *r;
    uint32_t ev = 0;
    if (g->alert == GL_ALERT_NONE || g->alert_acked) r = "no_alert";
    else if ((uint32_t)(now - g->alert_since_ms) < GL_PROX_MIN_ALERT_AGE_MS) r = "alert_too_new";
    else if (g->display_class == SIG_CLASS_ROUGH) r = "still_rough";
    else {
        ev = gl_acknowledge(g, GL_ACK_PROX, now);
        r = "ack";
    }
    if (why) *why = r;
    return ev;
}

uint32_t gl_on_sensor_fault(gl_state_t *g, bool fault, uint32_t now)
{
    if (fault == g->sensor_fault) return 0;
    g->sensor_fault = fault;
    if (fault) {
        g->display_class = SIG_CLASS_UNKNOWN;
        g->vote_count = 0;
        g->vote_pos = 0;
        raise_alert(g, GL_ALERT_SENSOR_FAULT, now);
        return GL_EV_FAULT_RAISED | GL_EV_STATE_CHANGED;
    }
    if (g->alert == GL_ALERT_SENSOR_FAULT) {
        g->alert = GL_ALERT_NONE;
        g->alert_acked = false;
    }
    return GL_EV_FAULT_CLEARED;
}

uint32_t gl_tick(gl_state_t *g, uint32_t now)
{
    if (g->alert != GL_ALERT_NONE && g->alert != GL_ALERT_SENSOR_FAULT &&
        (uint32_t)(now - g->alert_since_ms) >= GL_ALERT_VISUAL_MS) {
        g->alert = GL_ALERT_NONE;
        g->alert_acked = false;
        return GL_EV_STATE_CHANGED;
    }
    return 0;
}

bool gl_buzzer_wanted(const gl_state_t *g)
{
    return g->alert != GL_ALERT_NONE && !g->alert_acked;
}

const char *gl_alert_name(gl_alert_t a)
{
    switch (a) {
    case GL_ALERT_ROUGH:        return "ROUGH HANDLING";
    case GL_ALERT_TEST:         return "TEST ALERT";
    case GL_ALERT_SENSOR_FAULT: return "SENSOR FAULT";
    default:                    return "NONE";
    }
}

const char *gl_ack_src_name(uint8_t src)
{
    static const char *const n[] = { "key", "prox", "ir", "console" };
    return src < 4u ? n[src] : "?";
}
