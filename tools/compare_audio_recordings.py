#!/usr/bin/env python3
"""Analysis-only comparison of two supplied recordings against native PCM stems.

Requires numpy and ffmpeg on the host, never used by the ROM build. Fits are
approximate power-domain source attribution, not isolated original channels or
proof of analogue hardware gains. No captured audio becomes a playback asset.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

import numpy as np

RATE, WINDOW, HOP = 22050, 8192, 441
CENTERS = 2 ** np.arange(np.log2(110), np.log2(7000), 1 / 36)
FREQUENCIES = np.fft.rfftfreq(WINDOW, 1 / RATE)
LOW = np.searchsorted(FREQUENCIES, CENTERS / 2 ** (1 / 36))
HIGH = np.maximum(LOW + 1, np.searchsorted(FREQUENCIES, CENTERS * 2 ** (1 / 36)))
GROUPS = {"F0": [0], "F1": [1], "F2": [2], "F3": [3],
          "S1": [7], "S2": [8], "rhythm": list(range(9, 15))}


def features(samples):
    frames = np.lib.stride_tricks.sliding_window_view(samples, WINDOW)[::HOP]
    frames = (frames - frames.mean(axis=1, keepdims=True)) * np.hanning(WINDOW)
    power = abs(np.fft.rfft(frames, axis=1)) ** 2
    integrated = np.pad(np.cumsum(power, axis=1), ((0, 0), (1, 0)))
    return integrated[:, HIGH] - integrated[:, LOW]


def decode(path):
    raw = subprocess.check_output(["ffmpeg", "-v", "error", "-i", str(path),
        "-f", "f32le", "-ac", "1", "-ar", str(RATE), "pipe:1"])
    samples = np.frombuffer(raw, dtype="<f4").copy()
    return samples, dict(path=str(path), sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
        duration_seconds=len(samples) / RATE, decoded_peak=float(np.max(abs(samples))),
        decoded_rms=float(np.sqrt(np.mean(samples ** 2))))


def whiten(power):
    # Capture gain invariant, with a floor so codec noise does not dominate.
    values = np.log(np.maximum(power, power.max() * 1e-5))
    values -= values.mean(axis=1, keepdims=True)
    return values / np.maximum(np.linalg.norm(values, axis=1, keepdims=True), 1e-12)


def align(template, observed):
    mask = CENTERS >= 550  # align changing melodic partials, not the low sustained SSG alone
    a, b = whiten(template[:, mask]), whiten(observed[:, mask])
    qt = (np.arange(len(b)) * HOP + WINDOW / 2) / RATE
    best = (-np.inf, 0., 1.)
    max_shift = max(0, (len(a) - len(b)) * HOP / RATE)
    for scale in np.linspace(.975, 1.025, 21):
        for shift in np.arange(0, max_shift + .001, .02):
            positions = (shift + scale * qt - WINDOW / 2 / RATE) * RATE / HOP
            indices = np.rint(positions).astype(int)
            valid = (indices >= 0) & (indices < len(a))
            if valid.mean() < .98:
                continue
            score = float(np.mean(np.sum(a[indices[valid]] * b[valid], axis=1)))
            if score > best[0] + 1e-12 or (abs(score - best[0]) <= 1e-12 and abs(scale - 1) < abs(best[2] - 1)):
                best = score, float(shift), float(scale)
    # Refine fractional-frame lag without changing the recording's audio.
    score, shift, scale = best
    for s in np.arange(max(0, shift - .025), shift + .026, .001):
        positions = (s + scale * qt - WINDOW / 2 / RATE) * RATE / HOP
        lo = np.floor(positions).astype(int)
        valid = (lo >= 0) & (lo + 1 < len(a))
        frac = positions[valid] - lo[valid]
        interpolated = a[lo[valid]] * (1 - frac[:, None]) + a[lo[valid] + 1] * frac[:, None]
        value = float(np.mean(np.sum(interpolated * b[valid], axis=1)))
        if value > score:
            score, shift = value, float(s)
    return dict(cosine_score=score, stem_shift_seconds=shift, time_scale=scale)


def nnls(a, y):
    norm = np.maximum(np.linalg.norm(a, axis=0), 1e-20)
    normalized = a / norm
    gram, rhs = normalized.T @ normalized, normalized.T @ y
    x = np.zeros(a.shape[1])
    for _ in range(2000):
        old = x.copy()
        for i in range(len(x)):
            x[i] = max(0, x[i] + (rhs[i] - gram[i] @ x) / max(gram[i, i], 1e-12))
        if np.max(abs(x - old)) < 1e-8 * max(1, np.max(x)):
            break
    return x / norm, float(np.linalg.cond(gram))


def fit(basis, observed, alignment, select):
    qt = (np.arange(len(observed)) * HOP + WINDOW / 2) / RATE
    pos = (alignment["stem_shift_seconds"] + alignment["time_scale"] * qt - WINDOW / 2 / RATE) * RATE / HOP
    lo = np.floor(pos).astype(int)
    valid = select & (lo >= 0) & (lo + 1 < len(basis))
    fraction = pos[valid] - lo[valid]
    interpolated = basis[lo[valid]] * (1 - fraction[:, None, None]) + basis[lo[valid] + 1] * fraction[:, None, None]
    y = observed[valid]
    # Balance frequency bands rather than letting one loud fundamental dictate
    # all channel weights. Robust reweighting reduces codec/unmodeled partials.
    scale = np.maximum(y.mean(axis=0), y.max() * 1e-4)
    a = (interpolated / scale[None, None, :]).transpose(0, 2, 1).reshape(-1, len(GROUPS))
    target = (y / scale).reshape(-1)
    weight = np.ones_like(target)
    for _ in range(4):
        coefficients, condition = nnls(a * weight[:, None], target * weight)
        residual = a @ coefficients - target
        threshold = max(.1, float(np.quantile(abs(residual), .7)))
        weight = np.sqrt(np.minimum(1, threshold / np.maximum(abs(residual), 1e-12)))
    prediction = a @ coefficients
    error = float(np.linalg.norm((prediction - target) * weight) / np.linalg.norm(target * weight))
    return dict(power_coefficients=dict(zip(GROUPS, map(float, coefficients))),
                normalized_residual=error, gram_condition=condition, frames=int(valid.sum()),
                usable_for_gain_calibration=bool(error < .25 and condition < 10000))


def dominant_band_comparison(basis, observed, alignments, transition):
    """Direct recording-to-recording ratios; stems select bands, not gains.

    A modeled source must dominate other modeled sources by at least 13 dB.
    This reduces (but cannot eliminate) shared-partial/phase attribution errors.
    """
    times = (np.arange(len(basis)) * HOP + WINDOW / 2) / RATE
    aligned = {}
    valid = np.ones(len(times), dtype=bool)
    for label, power in observed.items():
        a = alignments[label]
        query_times = (times - a["stem_shift_seconds"]) / a["time_scale"]
        position = (query_times - WINDOW / 2 / RATE) * RATE / HOP
        lo = np.floor(position).astype(int)
        valid &= (lo >= 0) & (lo + 1 < len(power))
        lo = np.clip(lo, 0, len(power) - 2)
        fraction = np.clip(position - lo, 0, 1)
        aligned[label] = power[lo] * (1 - fraction[:, None]) + power[lo + 1] * fraction[:, None]
    original, ds = aligned["original"], aligned["ds"]
    delta = 10 * np.log10(np.maximum(ds, 1e-20) / np.maximum(original, 1e-20))
    sources = {"FM": basis[:, :4].sum(axis=1), "S1": basis[:, 4], "S2": basis[:, 5], "rhythm": basis[:, 6]}
    total = sum(sources.values())
    signal = (original > original.max() * 1e-3) & (ds > ds.max() * 1e-3)
    result = {}
    for section, select in (("before", times < transition - .2), ("after", times > transition + .2),
                            ("all", times >= 0)):
        metrics = {}
        for name, own in sources.items():
            mask = (own > 20 * np.maximum(total - own, 1e-20)) & (own > total.max() * .001)
            mask &= signal & (valid & select)[:, None]
            values = delta[mask]
            if len(values) < 10:
                metrics[name] = None
                continue
            metrics[name] = dict(median_db=float(np.median(values)),
                p10_db=float(np.quantile(values, .1)), p90_db=float(np.quantile(values, .9)),
                selected_cells=int(mask.sum()), frames=int(np.any(mask, axis=1).sum()),
                frequencies_hz=list(map(float, CENTERS[np.any(mask, axis=0)])))
        anchor = metrics["FM"]["median_db"] if metrics["FM"] else 0
        for item in metrics.values():
            if item is not None:
                item["relative_to_fm_db"] = item["median_db"] - anchor
        result[section] = metrics
    return result


def source_seconds(blob, tick):
    import struct
    h = struct.unpack_from("<8s14I", blob)
    track = struct.unpack_from("<8I", blob, h[7])
    tempo, previous, seconds = h[10], 0, 0.
    for i in range(track[3]):
        t, op, slot, a, b, c = struct.unpack_from("<IBBHII", blob, h[8] + (track[2] + i) * 16)
        if t > tick:
            break
        if op == 10:
            seconds += (t - previous) * 60 / (tempo * 48)
            previous, tempo = t, a
    return seconds + (tick - previous) * 60 / (tempo * 48)


def compare(original, ds, stem_path, meta_path, sequence_path):
    meta = json.loads(meta_path.read_text())
    assert meta["rate"] == RATE and meta["channels"] == 15
    stems = np.fromfile(stem_path, dtype="<f4").reshape(-1, 15)
    basis = np.stack([features(stems[:, indices].sum(axis=1)) for indices in GROUPS.values()], axis=1)
    template = basis.sum(axis=1)
    transition = source_seconds(sequence_path.read_bytes(), 77 * 48) - meta["source_start_seconds"]
    report = dict(method="Approximate native-stem power-domain attribution; not original isolated stems or analogue calibration",
        native_sequence_sha256=hashlib.sha256(sequence_path.read_bytes()).hexdigest(),
        ds_backend="NATIVE (confirmed by user)",
        limitations=["FM sample timbres/envelopes and PSG phase differ from original hardware",
                     "Correlated/overlapping partials and MP3 artifacts limit individual gain estimates",
                     "Decoded MP3 peaks above unity do not by themselves establish source clipping"],
        stems_sha256=hashlib.sha256(stem_path.read_bytes()).hexdigest(), recordings={})
    powers, alignments = {}, {}
    for label, path in (("original", original), ("ds", ds)):
        samples, facts = decode(path)
        observed = features(samples)
        alignment = align(template, observed)
        powers[label], alignments[label] = observed, alignment
        cut = (transition - alignment["stem_shift_seconds"]) / alignment["time_scale"]
        qt = (np.arange(len(observed)) * HOP + WINDOW / 2) / RATE
        fits = {}
        for name, select in (("all", qt >= 0), ("before", qt < cut - .15), ("after", qt > cut + .15),
                             ("after_first_half", (qt > cut + .15) & (qt < (cut + len(samples) / RATE) / 2)),
                             ("after_second_half", qt > (cut + len(samples) / RATE) / 2)):
            fits[name] = fit(basis, observed, alignment, select)
        report["recordings"][label] = dict(facts=facts, alignment=alignment, inferred_beat77_seconds=cut, fits=fits)
    relative = {}
    for section in report["recordings"]["ds"]["fits"]:
        d = report["recordings"]["ds"]["fits"][section]["power_coefficients"]
        o = report["recordings"]["original"]["fits"][section]["power_coefficients"]
        db = {k: float(10 * np.log10(d[k] / o[k])) if d[k] > 0 and o[k] > 0 else None for k in GROUPS}
        anchor = float(np.median([db[k] for k in ("F0", "F1", "F2", "F3") if db[k] is not None]))
        relative[section] = dict(capture_and_channel_difference_db=db, fm_anchor_db=anchor,
            excess_relative_to_fm_db={k: v - anchor if v is not None else None for k, v in db.items()})
    report["exploratory_regression_relative_fits"] = relative
    report["regression_calibration_accepted"] = all(
        fit["usable_for_gain_calibration"] for item in report["recordings"].values() for fit in item["fits"].values())
    report["dominant_band_comparison"] = dominant_band_comparison(basis, powers, alignments, transition)
    report["calibration_basis"] = "Direct dominant-band recording ratios relative to FM; do not use rejected regression gains"
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("original", type=Path)
    parser.add_argument("ds", type=Path)
    parser.add_argument("--stems", type=Path, required=True)
    parser.add_argument("--metadata", type=Path, required=True)
    parser.add_argument("--sequence", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    result = compare(args.original, args.ds, args.stems, args.metadata, args.sequence)
    args.out.write_text(json.dumps(result, indent=2) + "\n")
    for label, item in result["recordings"].items():
        print(label, item["alignment"], "beat77", item["inferred_beat77_seconds"])
        print("fit residuals", {k: v["normalized_residual"] for k, v in item["fits"].items()})
    print("Exploratory regression accepted:", result["regression_calibration_accepted"])
    print("Dominant-band comparisons:")
    print(json.dumps(result["dominant_band_comparison"], indent=2))
