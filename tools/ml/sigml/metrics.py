"""Classification metrics without third-party libraries."""


def confusion(y_true, y_pred, n_classes=3):
    m = [[0] * n_classes for _ in range(n_classes)]
    for t, p in zip(y_true, y_pred):
        m[t][p] += 1
    return m


def per_class(m):
    n = len(m)
    out = []
    for c in range(n):
        tp = m[c][c]
        fp = sum(m[r][c] for r in range(n)) - tp
        fn = sum(m[c]) - tp
        prec = tp / (tp + fp) if tp + fp else 0.0
        rec = tp / (tp + fn) if tp + fn else 0.0
        f1 = 2 * prec * rec / (prec + rec) if prec + rec else 0.0
        out.append({"precision": prec, "recall": rec, "f1": f1, "support": sum(m[c])})
    return out


def summary(y_true, y_pred, n_classes=3):
    m = confusion(y_true, y_pred, n_classes)
    pc = per_class(m)
    total = sum(sum(r) for r in m)
    acc = sum(m[i][i] for i in range(n_classes)) / total if total else 0.0
    present = [p for p in pc if p["support"] > 0]
    macro_f1 = sum(p["f1"] for p in present) / len(present) if present else 0.0
    return {"confusion": m, "per_class": pc, "accuracy": acc, "macro_f1": macro_f1, "n": total}


def format_confusion(m, names):
    w = max(len(n) for n in names) + 2
    head = "true\\pred".ljust(w) + "".join(n.rjust(w) for n in names)
    rows = [head] + [names[i].ljust(w) + "".join(str(v).rjust(w) for v in m[i]) for i in range(len(m))]
    return "\n".join(rows)
