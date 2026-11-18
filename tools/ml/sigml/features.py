"""Bit-exact Python mirror of src/core/features.c.

Every arithmetic result is rounded to IEEE-754 binary32 immediately, in the
same order as the C code. For + - * / and sqrt, computing in binary64 and then
rounding to binary32 gives the correctly-rounded binary32 result (53 >= 2*24+2),
so these values equal what the STM32 FPU (and the host build with SSE math and
-ffp-contract=off) produces. tools/ml/verify_c_model.py checks this claim on
every held-out window instead of assuming it.
"""
import math
import struct

FS_HZ = 100
WINDOW_SAMPLES = 200
HOP_SAMPLES = 50
ACC_LSB_PER_G = 4096.0
GYRO_LSB_PER_DPS = 32.8
HIGH_G_DEV = 0.40

FEATURE_NAMES = (
    "acc_mean", "acc_std", "acc_min", "acc_max", "acc_dev_max",
    "jerk_mean", "jerk_max", "gyro_mean", "gyro_std", "gyro_max",
    "high_g_frac", "axis_std_max",
)
N_FEATURES = len(FEATURE_NAMES)

_pack = struct.Struct("<f")


def f32(x: float) -> float:
    """Round a Python float to the nearest binary32 value."""
    return _pack.unpack(_pack.pack(x))[0]


K_ACC = f32(1.0 / ACC_LSB_PER_G)      # 2**-12, exact
K_GYRO = f32(GYRO_LSB_PER_DPS)        # 32.8f
HIGH_G = f32(HIGH_G_DEV)              # 0.40f
FS_F = f32(float(FS_HZ))
ONE = f32(1.0)


def _acc_mag(s):
    x = f32(s[0] * K_ACC)
    y = f32(s[1] * K_ACC)
    z = f32(s[2] * K_ACC)
    return f32(math.sqrt(f32(f32(f32(x * x) + f32(y * y)) + f32(z * z))))


def _gyro_mag(s, b):
    x = f32(f32(f32(float(s[3])) - b[0]) / K_GYRO)
    y = f32(f32(f32(float(s[4])) - b[1]) / K_GYRO)
    z = f32(f32(f32(float(s[5])) - b[2]) / K_GYRO)
    return f32(math.sqrt(f32(f32(f32(x * x) + f32(y * y)) + f32(z * z))))


def compute(window, gyro_bias):
    """window: sequence of (ax, ay, az, gx, gy, gz) raw int counts, oldest first.
    gyro_bias: three floats (raw LSB); rounded to binary32 like the C struct.
    Returns a list of N_FEATURES binary32 values (as Python floats)."""
    n = len(window)
    if n < 2:
        raise ValueError("window needs >= 2 samples")
    b = [f32(v) for v in gyro_bias]
    a_sum = w_sum = ax_sum = ay_sum = az_sum = 0.0
    a_min, a_max, w_max, dev_max = f32(1e30), 0.0, 0.0, 0.0
    jerk_sum = jerk_max = 0.0
    high_g = 0
    a_prev = 0.0
    mags = []
    for i, s in enumerate(window):
        a = _acc_mag(s)
        w = _gyro_mag(s, b)
        mags.append((a, w))
        dev = abs(f32(a - ONE))
        a_sum = f32(a_sum + a)
        w_sum = f32(w_sum + w)
        ax_sum = f32(ax_sum + f32(s[0] * K_ACC))
        ay_sum = f32(ay_sum + f32(s[1] * K_ACC))
        az_sum = f32(az_sum + f32(s[2] * K_ACC))
        if a < a_min:
            a_min = a
        if a > a_max:
            a_max = a
        if w > w_max:
            w_max = w
        if dev > dev_max:
            dev_max = dev
        if dev > HIGH_G:
            high_g += 1
        if i > 0:
            j = f32(abs(f32(a - a_prev)) * FS_F)
            jerk_sum = f32(jerk_sum + j)
            if j > jerk_max:
                jerk_max = j
        a_prev = a
    fn = f32(float(n))
    a_mean = f32(a_sum / fn)
    w_mean = f32(w_sum / fn)
    mx, my, mz = f32(ax_sum / fn), f32(ay_sum / fn), f32(az_sum / fn)

    a_var = w_var = vx = vy = vz = 0.0
    for s, (a, w) in zip(window, mags):
        da = f32(a - a_mean)
        dw = f32(w - w_mean)
        dx = f32(f32(s[0] * K_ACC) - mx)
        dy = f32(f32(s[1] * K_ACC) - my)
        dz = f32(f32(s[2] * K_ACC) - mz)
        a_var = f32(a_var + f32(da * da))
        w_var = f32(w_var + f32(dw * dw))
        vx = f32(vx + f32(dx * dx))
        vy = f32(vy + f32(dy * dy))
        vz = f32(vz + f32(dz * dz))
    sx = f32(math.sqrt(f32(vx / fn)))
    sy = f32(math.sqrt(f32(vy / fn)))
    sz = f32(math.sqrt(f32(vz / fn)))
    axis_std_max = sx
    if sy > axis_std_max:
        axis_std_max = sy
    if sz > axis_std_max:
        axis_std_max = sz
    return [
        a_mean,
        f32(math.sqrt(f32(a_var / fn))),
        a_min,
        a_max,
        dev_max,
        f32(jerk_sum / f32(float(n - 1))),
        jerk_max,
        w_mean,
        f32(math.sqrt(f32(w_var / fn))),
        w_max,
        f32(f32(float(high_g)) / fn),
        axis_std_max,
    ]
