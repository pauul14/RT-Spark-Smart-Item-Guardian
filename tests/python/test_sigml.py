"""Unit tests for tools/ml/sigml (run: python -m unittest discover -s tests/python)."""
import json
import os
import random
import sys
import tempfile
import unittest

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools", "ml"))

from sigml import baseline, dataset, metrics          # noqa: E402
from sigml import features as F                       # noqa: E402
from sigml.cart import DecisionTree                   # noqa: E402


def still_window(bias=(0.0, 0.0, 0.0)):
    return [(0, 0, 4096, int(bias[0]), int(bias[1]), int(bias[2]))] * F.WINDOW_SAMPLES


class FeatureTests(unittest.TestCase):
    def test_still_gravity(self):
        f = F.compute(still_window((10, -5, 3)), (10, -5, 3))
        self.assertEqual(f[0], 1.0)                      # acc_mean exactly 1 g
        self.assertEqual(f[1], 0.0)                      # acc_std
        self.assertEqual(f[7], 0.0)                      # gyro_mean after bias removal
        self.assertEqual(f[10], 0.0)                     # high_g_frac
        self.assertEqual(len(f), F.N_FEATURES)

    def test_all_binary32(self):
        rng = random.Random(1)
        w = [tuple(rng.randint(-20000, 20000) for _ in range(6)) for _ in range(F.WINDOW_SAMPLES)]
        for v in F.compute(w, (1.5, -2.25, 0.1)):
            self.assertEqual(v, F.f32(v))

    def test_jerk_and_high_g(self):
        w = still_window()
        w = w[:100] + [(0, 0, 3 * 4096, 0, 0, 0)] + w[101:]
        f = F.compute(w, (0, 0, 0))
        self.assertAlmostEqual(f[F.FEATURE_NAMES.index("acc_dev_max")], 2.0, places=6)
        self.assertAlmostEqual(f[F.FEATURE_NAMES.index("jerk_max")], 200.0, places=3)
        self.assertAlmostEqual(f[F.FEATURE_NAMES.index("high_g_frac")], 1 / 200, places=7)


class CartTests(unittest.TestCase):
    def test_separable(self):
        X = [[float(i), 0.0] for i in range(30)]
        y = [0] * 10 + [1] * 10 + [2] * 10
        t = DecisionTree(max_depth=3, min_samples_leaf=2).fit(X, y)
        self.assertEqual(t.predict(X), y)
        self.assertLessEqual(t.depth, 3)
        for n in t.nodes:
            if n.feature >= 0:
                self.assertEqual(n.threshold, F.f32(n.threshold))

    def test_depth_bound_and_leaf_size(self):
        rng = random.Random(3)
        X = [[rng.random(), rng.random()] for _ in range(200)]
        y = [rng.randint(0, 2) for _ in range(200)]
        t = DecisionTree(max_depth=2, min_samples_leaf=20).fit(X, y)
        self.assertLessEqual(t.depth, 2)
        for n in t.nodes:
            if n.feature < 0:
                self.assertGreaterEqual(sum(n.counts), 20)

    def test_adjacent_float_threshold(self):
        a = F.f32(1.0)
        b = F.f32(1.0000001)                             # next binary32 above 1.0
        self.assertLess(a, b)
        t = DecisionTree(max_depth=1, min_samples_leaf=1).fit([[a], [b]], [0, 1])
        self.assertEqual(t.predict([[a], [b]]), [0, 1])


class MetricsTests(unittest.TestCase):
    def test_confusion_and_prf(self):
        s = metrics.summary([0, 0, 1, 1, 2, 2], [0, 1, 1, 1, 2, 0])
        self.assertEqual(s["confusion"], [[1, 1, 0], [0, 2, 0], [1, 0, 1]])
        self.assertAlmostEqual(s["per_class"][1]["precision"], 2 / 3)
        self.assertAlmostEqual(s["per_class"][1]["recall"], 1.0)
        self.assertAlmostEqual(s["accuracy"], 4 / 6)


class BaselineTests(unittest.TestCase):
    def test_rule_order(self):
        p = {"still_acc_std": 0.01, "still_gyro_max": 5.0, "rough_dev_max": 1.0, "rough_gyro_max": 300.0}
        x = [0.0] * F.N_FEATURES
        self.assertEqual(baseline.predict_one(x, p), 0)
        x[baseline.I_ACC_STD] = 0.2
        self.assertEqual(baseline.predict_one(x, p), 1)
        x[baseline.I_DEV_MAX] = 1.5
        self.assertEqual(baseline.predict_one(x, p), 2)


class SplitTests(unittest.TestCase):
    def test_overlapping_sessions_rejected(self):
        with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False) as fh:
            json.dump({"train_sessions": ["S01", "S02"], "test_sessions": ["S02"]}, fh)
        try:
            with self.assertRaises(ValueError):
                dataset.load_splits(fh.name)
        finally:
            os.unlink(fh.name)

    def test_gap_and_invalid_windows_excluded(self):
        # 1000 samples -> 995 after the gap -> 795 after trimming 100 at each end
        # -> 3 candidate windows: clean, gap of 5 (> 2 allowed), invalid sample.
        rows = [(i, i * 10000, 0, 0, 4096, 0, 0, 0, 0) for i in range(1000)]
        rows = rows[:350] + rows[355:]                   # 5 missing samples inside window 2
        rows[600] = rows[600][:8] + (1,)                 # invalid sample inside window 3
        t = dataset.Trial("S01", "T1", "STATIONARY", (0.0, 0.0, 0.0), rows, {}, "x.csv", "0")
        ex = []
        ws = dataset.window_trial(t, ex)
        self.assertTrue(all(len(w.raw) == F.WINDOW_SAMPLES for w in ws))
        self.assertEqual(len(ex), 2)
        self.assertEqual(len(ws), 1)


if __name__ == "__main__":
    unittest.main()
