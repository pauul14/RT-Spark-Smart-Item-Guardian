/*
 * model_infer.c - decision-tree walk and baseline rule.
 * The walk is bounded by SIG_MODEL_MAX_DEPTH + 1 iterations; there is no
 * recursion and no dynamic memory, so worst-case time is a constant.
 */
#include "model_infer.h"
#include "model_tree.h"
#include "model_config.h"
#include "baseline_params.h"
#include <math.h>

_Static_assert(SIG_MODEL_N_FEATURES == SIG_N_FEATURES,
               "model_config.h feature count differs from features.h - regenerate the model");
_Static_assert(SIG_MODEL_WINDOW_SAMPLES == SIG_WINDOW_SAMPLES,
               "model was trained with a different window length");
_Static_assert(SIG_MODEL_FS_HZ == SIG_FS_HZ, "model was trained at a different sample rate");

bool sig_model_predict(const float *x, sig_prediction_t *out)
{
    out->cls = SIG_CLASS_UNKNOWN;
    out->confidence = 0;
    out->leaf = 0;
    out->depth = 0;

    for (int i = 0; i < SIG_N_FEATURES; i++) {
        if (!isfinite(x[i])) {
            return false;
        }
    }

    int node = 0;
    for (int steps = 0; steps <= SIG_MODEL_MAX_DEPTH; steps++) {
        if (node < 0 || node >= SIG_MODEL_N_NODES) {
            return false;                      /* corrupt table */
        }
        int f = sig_tree_feature[node];
        if (f < 0) {                           /* leaf */
            if (sig_tree_class[node] >= SIG_CLASS_COUNT) {
                return false;
            }
            out->cls = sig_tree_class[node];
            out->confidence = sig_tree_purity_pct[node];
            out->leaf = (uint16_t)node;
            out->depth = (uint8_t)steps;
            return true;
        }
        if (f >= SIG_N_FEATURES) {
            return false;
        }
        node = (x[f] <= sig_tree_threshold[node]) ? sig_tree_left[node] : sig_tree_right[node];
    }
    return false;                              /* deeper than declared: corrupt */
}

uint8_t sig_baseline_predict(const float *x)
{
    /* Explainable two-stage rule (see tools/ml/sigml/baseline.py). */
    if (x[SIG_F_ACC_STD] <= SIG_BASE_STILL_ACC_STD && x[SIG_F_GYRO_MAX] <= SIG_BASE_STILL_GYRO_MAX) {
        return SIG_CLASS_STATIONARY;
    }
    if (x[SIG_F_ACC_DEV_MAX] > SIG_BASE_ROUGH_DEV_MAX || x[SIG_F_GYRO_MAX] > SIG_BASE_ROUGH_GYRO_MAX) {
        return SIG_CLASS_ROUGH;
    }
    return SIG_CLASS_NORMAL;
}

const char *sig_model_version(void) { return SIG_MODEL_VERSION; }
bool sig_model_is_placeholder(void) { return SIG_MODEL_IS_PLACEHOLDER != 0; }
int sig_model_node_count(void) { return SIG_MODEL_N_NODES; }
int sig_model_max_depth(void) { return SIG_MODEL_MAX_DEPTH; }

uint32_t sig_model_size_bytes(void)
{
    return (uint32_t)(sizeof(sig_tree_feature) + sizeof(sig_tree_threshold) +
                      sizeof(sig_tree_left) + sizeof(sig_tree_right) +
                      sizeof(sig_tree_class) + sizeof(sig_tree_purity_pct));
}
