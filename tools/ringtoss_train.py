#!/usr/bin/env python3
"""Train small linear models for the CCC2026 ringtoss controller.

Input is the CSV exported from ringtoss-collect.html. The script groups rows by
trial_id, subtracts the gyro baseline measured while pulling back, extracts
compact one-throw features, fits ridge-regularized linear models, and writes an
Arduino header for prototypes/ringtoss_inference.

No third-party Python packages are required.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


FEATURE_NAMES = [
    "duration_s",
    "sample_count_scaled",
    "release_t_s",
    "release_ratio",
    "peak_gyro",
    "peak_gx",
    "peak_gy",
    "peak_gz",
    "mean_gx",
    "mean_gy",
    "mean_gz",
    "int_gx",
    "int_gy",
    "int_gz",
    "max_gx",
    "max_gy",
    "max_gz",
    "min_gx",
    "min_gy",
    "min_gz",
]

LABEL_NAMES = [
    "left_top",
    "top",
    "right_top",
    "left",
    "center",
    "right",
    "left_bottom",
    "bottom",
    "right_bottom",
]


@dataclass
class Trial:
    trial_id: str
    label_id: int
    target: list[float]
    features: list[float]
    samples: int


@dataclass
class Model:
    feature_mean: list[float]
    feature_scale: list[float]
    class_intercept: list[float]
    class_coef: list[list[float]]
    vector_intercept: list[float]
    vector_coef: list[list[float]]
    train_accuracy: float


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Train ringtoss linear models.")
    parser.add_argument("csv_path", type=Path, help="CSV exported from ringtoss-collect.html")
    parser.add_argument(
        "--header",
        type=Path,
        default=Path("prototypes/ringtoss_inference/ringtoss_model_coefficients.h"),
        help="Arduino header output path",
    )
    parser.add_argument(
        "--summary",
        type=Path,
        default=None,
        help="Optional JSON summary path",
    )
    parser.add_argument(
        "--web-model",
        type=Path,
        default=None,
        help="Optional web visualizer model JSON path",
    )
    parser.add_argument(
        "--alpha",
        type=float,
        default=0.15,
        help="Ridge regularization strength",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    grouped = read_grouped_rows(args.csv_path)
    trials = [extract_trial(trial_id, rows) for trial_id, rows in grouped.items()]
    trials = [trial for trial in trials if trial is not None]

    if not trials:
        raise SystemExit("No usable trials found in the CSV.")

    labels = sorted({trial.label_id for trial in trials})
    if len(labels) < 3:
        print("warning: fewer than 3 labels are present; collect more balanced data before using the model.")

    model = train_model(trials, alpha=args.alpha)
    write_header(args.header, model)

    summary_path = args.summary or args.header.with_suffix(".summary.json")
    web_model_path = args.web_model or args.header.with_name("ringtoss_model.web.json")
    write_summary(summary_path, args.csv_path, trials, model, args.alpha)
    write_web_model(web_model_path, args.csv_path, trials, model, args.alpha)

    print(f"trials: {len(trials)}")
    print(f"labels: {labels}")
    print(f"training accuracy: {model.train_accuracy:.3f}")
    print(f"wrote: {args.header}")
    print(f"wrote: {summary_path}")
    print(f"wrote: {web_model_path}")


def read_grouped_rows(path: Path) -> dict[str, list[dict[str, str]]]:
    grouped: dict[str, list[dict[str, str]]] = {}
    with path.open("r", encoding="utf-8-sig", newline="") as f:
        reader = csv.DictReader(f)
        for index, row in enumerate(reader):
            trial_id = row.get("trial_id") or row.get("serial_trial") or f"row-{index}"
            grouped.setdefault(trial_id, []).append(row)

    for rows in grouped.values():
        rows.sort(key=lambda row: int(float(row.get("seq") or 0)))
    return grouped


def extract_trial(trial_id: str, rows: list[dict[str, str]]) -> Trial | None:
    if not rows:
        return None

    numeric = [coerce_row(row) for row in rows]
    baseline_rows = [row for row in numeric if row["phase"] == "baseline"]
    if not baseline_rows:
        baseline_rows = numeric[: min(8, len(numeric))]

    throw_rows = [row for row in numeric if row["phase"] != "baseline"]
    if not throw_rows:
        throw_rows = numeric[len(baseline_rows) :]
    if len(throw_rows) < 2:
        return None

    base_gx = average(row["gx"] for row in baseline_rows)
    base_gy = average(row["gy"] for row in baseline_rows)
    base_gz = average(row["gz"] for row in baseline_rows)

    rel_rows = []
    for row in throw_rows:
        gx = row["gx"] - base_gx
        gy = row["gy"] - base_gy
        gz = row["gz"] - base_gz
        rel_rows.append(
            {
                "t_ms": row["t_ms"],
                "gx": gx,
                "gy": gy,
                "gz": gz,
                "mag": math.sqrt(gx * gx + gy * gy + gz * gz),
            }
        )

    first_t = rel_rows[0]["t_ms"]
    last_t = rel_rows[-1]["t_ms"]
    duration_s = max((last_t - first_t) / 1000.0, 0.01)
    sample_count = len(rel_rows)

    peak = max(rel_rows, key=lambda row: row["mag"])
    release_t_s = max((peak["t_ms"] - first_t) / 1000.0, 0.0)
    release_ratio = clamp(release_t_s / duration_s, 0.0, 1.0)

    sum_gx = 0.0
    sum_gy = 0.0
    sum_gz = 0.0
    int_gx = 0.0
    int_gy = 0.0
    int_gz = 0.0
    max_gx = -1e9
    max_gy = -1e9
    max_gz = -1e9
    min_gx = 1e9
    min_gy = 1e9
    min_gz = 1e9
    prev_t = None

    for row in rel_rows:
        dt = 0.0 if prev_t is None else max((row["t_ms"] - prev_t) / 1000.0, 0.0)
        prev_t = row["t_ms"]

        sum_gx += row["gx"]
        sum_gy += row["gy"]
        sum_gz += row["gz"]
        int_gx += row["gx"] * dt
        int_gy += row["gy"] * dt
        int_gz += row["gz"] * dt
        max_gx = max(max_gx, row["gx"])
        max_gy = max(max_gy, row["gy"])
        max_gz = max(max_gz, row["gz"])
        min_gx = min(min_gx, row["gx"])
        min_gy = min(min_gy, row["gy"])
        min_gz = min(min_gz, row["gz"])

    n = float(sample_count)
    features = [
        duration_s,
        n / 100.0,
        release_t_s,
        release_ratio,
        peak["mag"],
        peak["gx"],
        peak["gy"],
        peak["gz"],
        sum_gx / n,
        sum_gy / n,
        sum_gz / n,
        int_gx,
        int_gy,
        int_gz,
        max_gx,
        max_gy,
        max_gz,
        min_gx,
        min_gy,
        min_gz,
    ]

    label_id = int(float(rows[0].get("label_id") or 0))
    if not 0 <= label_id <= 8:
        raise ValueError(f"label_id out of range in {trial_id}: {label_id}")

    target = target_vector(rows[0], label_id)
    return Trial(trial_id=trial_id, label_id=label_id, target=target, features=features, samples=sample_count)


def coerce_row(row: dict[str, str]) -> dict[str, float | str]:
    return {
        "phase": (row.get("phase") or "").strip(),
        "t_ms": to_float(row.get("t_ms")),
        "gx": to_float(row.get("gx")),
        "gy": to_float(row.get("gy")),
        "gz": to_float(row.get("gz")),
    }


def target_vector(row: dict[str, str], label_id: int) -> list[float]:
    x = row.get("target_x")
    y = row.get("target_y")
    if x not in (None, "") and y not in (None, ""):
        vx = to_float(x)
        vy = to_float(y)
    else:
        row_index = label_id // 3
        col_index = label_id % 3
        vx = float(col_index - 1)
        vy = float(1 - row_index)

    vz = 1.0
    strength = math.sqrt(vx * vx + vy * vy + vz * vz)
    return [vx, vy, vz, strength]


def train_model(trials: list[Trial], alpha: float) -> Model:
    x = [trial.features for trial in trials]
    means = [average(row[i] for row in x) for i in range(len(FEATURE_NAMES))]
    scales = []
    for i, mean in enumerate(means):
        variance = average((row[i] - mean) ** 2 for row in x)
        scale = math.sqrt(variance)
        scales.append(scale if scale > 1e-9 else 1.0)

    x_norm = [
        [(value - means[i]) / scales[i] for i, value in enumerate(row)]
        for row in x
    ]

    y_class = [
        [1.0 if trial.label_id == cls else 0.0 for cls in range(9)]
        for trial in trials
    ]
    y_vector = [trial.target for trial in trials]

    class_intercept, class_coef = fit_linear(x_norm, y_class, alpha=alpha)
    vector_intercept, vector_coef = fit_linear(x_norm, y_vector, alpha=alpha)

    correct = 0
    for row, trial in zip(x_norm, trials):
        scores = predict(row, class_intercept, class_coef)
        if argmax(scores) == trial.label_id:
            correct += 1

    return Model(
        feature_mean=means,
        feature_scale=scales,
        class_intercept=class_intercept,
        class_coef=class_coef,
        vector_intercept=vector_intercept,
        vector_coef=vector_coef,
        train_accuracy=correct / len(trials),
    )


def fit_linear(x: list[list[float]], y: list[list[float]], alpha: float) -> tuple[list[float], list[list[float]]]:
    if not x:
        raise ValueError("cannot fit an empty dataset")

    rows = len(x)
    features = len(x[0])
    outputs = len(y[0])
    width = features + 1

    xtx = [[0.0 for _ in range(width)] for _ in range(width)]
    xty = [[0.0 for _ in range(outputs)] for _ in range(width)]

    for row_index in range(rows):
        xb = [1.0] + x[row_index]
        for i in range(width):
            for j in range(width):
                xtx[i][j] += xb[i] * xb[j]
            for out in range(outputs):
                xty[i][out] += xb[i] * y[row_index][out]

    for i in range(1, width):
        xtx[i][i] += alpha

    intercepts: list[float] = []
    coefficients: list[list[float]] = []

    for out in range(outputs):
        solution = solve_linear([row[:] for row in xtx], [xty[i][out] for i in range(width)])
        intercepts.append(solution[0])
        coefficients.append(solution[1:])

    return intercepts, coefficients


def solve_linear(a: list[list[float]], b: list[float]) -> list[float]:
    n = len(b)
    for col in range(n):
        pivot = max(range(col, n), key=lambda row: abs(a[row][col]))
        if abs(a[pivot][col]) < 1e-12:
            a[pivot][col] += 1e-8
        if pivot != col:
            a[col], a[pivot] = a[pivot], a[col]
            b[col], b[pivot] = b[pivot], b[col]

        pivot_value = a[col][col]
        for j in range(col, n):
            a[col][j] /= pivot_value
        b[col] /= pivot_value

        for row in range(n):
            if row == col:
                continue
            factor = a[row][col]
            if factor == 0.0:
                continue
            for j in range(col, n):
                a[row][j] -= factor * a[col][j]
            b[row] -= factor * b[col]

    return b


def predict(row: list[float], intercept: list[float], coef: list[list[float]]) -> list[float]:
    outputs = []
    for out, base in enumerate(intercept):
        value = base
        for i, feature_value in enumerate(row):
            value += coef[out][i] * feature_value
        outputs.append(value)
    return outputs


def write_header(path: Path, model: Model) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    text = "\n".join(
        [
            "#ifndef RINGTOSS_MODEL_COEFFICIENTS_H",
            "#define RINGTOSS_MODEL_COEFFICIENTS_H",
            "",
            "// Generated by tools/ringtoss_train.py.",
            "// Feature order must match ringtoss_inference.ino.",
            "",
            "#define RINGTOSS_MODEL_READY 1",
            "",
            f"const uint8_t RINGTOSS_FEATURE_COUNT = {len(FEATURE_NAMES)};",
            "const uint8_t RINGTOSS_CLASS_COUNT = 9;",
            "const uint8_t RINGTOSS_VECTOR_OUTPUT_COUNT = 4;",
            "",
            c_array("RINGTOSS_FEATURE_MEAN", model.feature_mean),
            "",
            c_array("RINGTOSS_FEATURE_SCALE", model.feature_scale),
            "",
            c_array("RINGTOSS_CLASS_INTERCEPT", model.class_intercept),
            "",
            c_matrix("RINGTOSS_CLASS_COEF", model.class_coef),
            "",
            c_array("RINGTOSS_VECTOR_INTERCEPT", model.vector_intercept),
            "",
            c_matrix("RINGTOSS_VECTOR_COEF", model.vector_coef),
            "",
            "#endif",
            "",
        ]
    )
    path.write_text(text, encoding="utf-8")


def write_summary(path: Path, csv_path: Path, trials: list[Trial], model: Model, alpha: float) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    label_counts = {str(i): 0 for i in range(9)}
    for trial in trials:
        label_counts[str(trial.label_id)] += 1

    summary = {
        "csv_path": str(csv_path),
        "trial_count": len(trials),
        "sample_count": sum(trial.samples for trial in trials),
        "label_counts": label_counts,
        "feature_names": FEATURE_NAMES,
        "label_names": LABEL_NAMES,
        "alpha": alpha,
        "training_accuracy": model.train_accuracy,
    }
    path.write_text(json.dumps(summary, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def write_web_model(path: Path, csv_path: Path, trials: list[Trial], model: Model, alpha: float) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    payload = {
        "kind": "ccc2026-ringtoss-linear-model",
        "version": 1,
        "csv_path": str(csv_path),
        "feature_names": FEATURE_NAMES,
        "label_names": LABEL_NAMES,
        "feature_mean": model.feature_mean,
        "feature_scale": model.feature_scale,
        "class_intercept": model.class_intercept,
        "class_coef": model.class_coef,
        "vector_intercept": model.vector_intercept,
        "vector_coef": model.vector_coef,
        "alpha": alpha,
        "training_accuracy": model.train_accuracy,
        "trial_count": len(trials),
        "sample_count": sum(trial.samples for trial in trials),
    }
    path.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def c_array(name: str, values: list[float]) -> str:
    return (
        f"const float {name}[{len(values)}] = {{\n"
        f"{wrap_values(values, indent='  ')}\n"
        "};"
    )


def c_matrix(name: str, rows: list[list[float]]) -> str:
    row_count = len(rows)
    col_count = len(rows[0]) if rows else 0
    rendered_rows = []
    for row in rows:
        rendered_rows.append("  {" + ", ".join(c_float(value) for value in row) + "}")
    return (
        f"const float {name}[{row_count}][{col_count}] = {{\n"
        + ",\n".join(rendered_rows)
        + "\n};"
    )


def wrap_values(values: list[float], indent: str) -> str:
    chunks = []
    line: list[str] = []
    for value in values:
        line.append(c_float(value))
        if len(line) >= 8:
            chunks.append(indent + ", ".join(line))
            line = []
    if line:
        chunks.append(indent + ", ".join(line))
    return ",\n".join(chunks)


def c_float(value: float) -> str:
    if abs(value) < 5e-10:
        value = 0.0
    return f"{value:.8g}f"


def to_float(value: str | None) -> float:
    if value in (None, ""):
        return 0.0
    return float(value)


def average(values: Iterable[float]) -> float:
    items = list(values)
    return sum(items) / len(items) if items else 0.0


def clamp(value: float, low: float, high: float) -> float:
    if value < low:
        return low
    if value > high:
        return high
    return value


def argmax(values: list[float]) -> int:
    return max(range(len(values)), key=lambda index: values[index])


if __name__ == "__main__":
    main()
