/*
 * parity_main.c - host harness for tools/ml/verify_c_model.py.
 * Compiles the firmware's own src/core/features.c and src/core/model_infer.c
 * (with the generated src/model headers) and runs them on windows exported by
 * the trainer. Prints, per window, the C prediction, the C baseline and the
 * raw IEEE-754 bit patterns of the 12 features so the Python side can compare
 * bit-for-bit.
 * Input lines: id label python_pred bias0 bias1 bias2 then 200 x 6 ints.
 */
#include <stdio.h>
#include <string.h>
#include "features.h"
#include "model_infer.h"

static uint32_t bits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

int main(int argc, char **argv)
{
    if (argc != 3) { fprintf(stderr, "usage: parity in.txt out.txt\n"); return 2; }
    FILE *in = fopen(argv[1], "r"), *out = fopen(argv[2], "w");
    if (!in || !out) { perror("open"); return 2; }
    static sig_raw_imu_t w[SIG_WINDOW_SAMPLES];
    char id[160];
    int label, ppred, n = 0;
    float b[3];
    fprintf(out, "# model=%s placeholder=%d nodes=%d depth=%d model_bytes=%u\n", sig_model_version(),
            (int)sig_model_is_placeholder(), sig_model_node_count(), sig_model_max_depth(),
            (unsigned)sig_model_size_bytes());
    while (fscanf(in, "%159s %d %d %f %f %f", id, &label, &ppred, &b[0], &b[1], &b[2]) == 6) {
        for (int i = 0; i < SIG_WINDOW_SAMPLES; i++) {
            int v[6];
            if (fscanf(in, "%d %d %d %d %d %d", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6) {
                fprintf(stderr, "truncated window %s\n", id);
                return 3;
            }
            w[i].ax = (int16_t)v[0]; w[i].ay = (int16_t)v[1]; w[i].az = (int16_t)v[2];
            w[i].gx = (int16_t)v[3]; w[i].gy = (int16_t)v[4]; w[i].gz = (int16_t)v[5];
        }
        float f[SIG_N_FEATURES];
        static float scratch[SIG_FEATURE_SCRATCH(SIG_WINDOW_SAMPLES)];
        sig_features_compute(w, SIG_WINDOW_SAMPLES, b, f, scratch);
        sig_prediction_t p;
        bool ok = sig_model_predict(f, &p);
        fprintf(out, "%s %d %d", id, ok ? p.cls : 255, sig_baseline_predict(f));
        for (int k = 0; k < SIG_N_FEATURES; k++) fprintf(out, " %08x", (unsigned)bits(f[k]));
        fputc('\n', out);
        n++;
    }
    fclose(out);
    fprintf(stderr, "processed %d windows\n", n);
    return 0;
}
