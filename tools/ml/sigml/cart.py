"""Small CART decision tree (Gini impurity), deterministic, standard library only.

Thresholds are binary32 values chosen between two adjacent distinct binary32
training values, and prediction uses `x <= threshold` exactly like
src/core/model_infer.c, so Python and C traverse identical paths.
"""
from dataclasses import dataclass
from .features import f32


@dataclass
class Node:
    feature: int = -1          # -1 = leaf
    threshold: float = 0.0
    left: int = -1
    right: int = -1
    cls: int = 0
    counts: tuple = ()
    depth: int = 0

    @property
    def purity_pct(self):
        n = sum(self.counts)
        return int(round(100.0 * max(self.counts) / n)) if n else 0


def _gini(counts, n):
    if n == 0:
        return 0.0
    return 1.0 - sum((c / n) ** 2 for c in counts)


def _majority(counts):
    best = 0
    for c in range(len(counts)):
        if counts[c] > counts[best]:
            best = c
    return best


def _split_threshold(lo, hi):
    """binary32 t with lo <= t < hi (lo < hi are binary32 values)."""
    t = f32((lo + hi) / 2.0)
    if not (lo <= t < hi):
        t = lo
    return t


class DecisionTree:
    def __init__(self, max_depth=4, min_samples_leaf=3, n_classes=3):
        self.max_depth = max_depth
        self.min_samples_leaf = min_samples_leaf
        self.n_classes = n_classes
        self.nodes = []

    # ---------------------------------------------------------------- fit
    def fit(self, X, y):
        self.nodes = []
        self._grow(list(range(len(X))), X, y, 0)
        return self

    def _counts(self, idx, y):
        c = [0] * self.n_classes
        for i in idx:
            c[y[i]] += 1
        return c

    def _best_split(self, idx, X, y):
        n = len(idx)
        parent = self._counts(idx, y)
        best = None                      # (gain, feature, threshold)
        g_parent = _gini(parent, n)
        for f in range(len(X[0])):
            order = sorted(idx, key=lambda i: (X[i][f], i))
            left = [0] * self.n_classes
            right = parent[:]
            for k in range(n - 1):
                cls = y[order[k]]
                left[cls] += 1
                right[cls] -= 1
                nl, nr = k + 1, n - k - 1
                v, v_next = X[order[k]][f], X[order[k + 1]][f]
                if v == v_next or nl < self.min_samples_leaf or nr < self.min_samples_leaf:
                    continue
                g = g_parent - (nl / n) * _gini(left, nl) - (nr / n) * _gini(right, nr)
                if g > 1e-12 and (best is None or g > best[0] + 1e-12):
                    best = (g, f, _split_threshold(v, v_next))
        return best

    def _grow(self, idx, X, y, depth):
        counts = self._counts(idx, y)
        me = len(self.nodes)
        self.nodes.append(Node(cls=_majority(counts), counts=tuple(counts), depth=depth))
        if depth >= self.max_depth or max(counts) == len(idx):
            return me
        split = self._best_split(idx, X, y)
        if split is None:
            return me
        _, f, t = split
        li = [i for i in idx if X[i][f] <= t]
        ri = [i for i in idx if X[i][f] > t]
        node = self.nodes[me]
        node.feature, node.threshold = f, t
        node.left = self._grow(li, X, y, depth + 1)
        node.right = self._grow(ri, X, y, depth + 1)
        return me

    # ------------------------------------------------------------ predict
    def predict_one(self, x):
        node = 0
        for _ in range(self.max_depth + 1):
            nd = self.nodes[node]
            if nd.feature < 0:
                return nd.cls
            node = nd.left if x[nd.feature] <= nd.threshold else nd.right
        raise RuntimeError("tree deeper than max_depth")

    def predict(self, X):
        return [self.predict_one(x) for x in X]

    @property
    def depth(self):
        return max((n.depth for n in self.nodes), default=0)

    def used_features(self):
        return sorted({n.feature for n in self.nodes if n.feature >= 0})

    def to_dict(self, feature_names):
        return {
            "max_depth_param": self.max_depth, "min_samples_leaf": self.min_samples_leaf,
            "depth": self.depth, "n_nodes": len(self.nodes),
            "nodes": [{"id": i, "feature": n.feature, "feature_name": feature_names[n.feature] if n.feature >= 0 else None,
                       "threshold": n.threshold, "left": n.left, "right": n.right, "class": n.cls,
                       "counts": list(n.counts), "purity_pct": n.purity_pct} for i, n in enumerate(self.nodes)],
        }

    def describe(self, feature_names, class_names):
        lines = []

        def rec(i, ind):
            n = self.nodes[i]
            if n.feature < 0:
                lines.append(f"{ind}-> {class_names[n.cls]}  counts={list(n.counts)}")
                return
            lines.append(f"{ind}if {feature_names[n.feature]} <= {n.threshold:.6g}:")
            rec(n.left, ind + "    ")
            lines.append(f"{ind}else:")
            rec(n.right, ind + "    ")
        rec(0, "")
        return "\n".join(lines)
