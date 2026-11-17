#!/usr/bin/env python3
"""Measure periodic tangent-space normal-map spectra in uncompressed 24-bit BMPs.

The analysis uses a square, power-of-two image and a periodic 2D FFT. Frequency
shells use max(abs(kx), abs(ky)), so diagonal frequencies inside the square
sampling Nyquist region are not incorrectly classified as out of band.
"""

from __future__ import annotations

import argparse
import json
import math
import struct
import sys
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Tuple


class SpectrumError(ValueError):
    """Invalid image data or incompatible spectrum request."""


_BAND_NAMES = (
    "0_to_25_percent_nyquist",
    "25_to_50_percent_nyquist",
    "50_to_75_percent_nyquist",
    "75_to_100_percent_nyquist",
)


def _read_normal_bmp(path: Path) -> Tuple[int, List[List[int]], List[List[int]]]:
    try:
        data = path.read_bytes()
    except OSError as exc:
        raise SpectrumError("Could not read BMP '{}': {}".format(path, exc)) from exc

    if len(data) < 54 or data[:2] != b"BM":
        raise SpectrumError("Not a complete Windows BMP file: {}".format(path))

    file_size = struct.unpack_from("<I", data, 2)[0]
    pixel_offset = struct.unpack_from("<I", data, 10)[0]
    dib_size = struct.unpack_from("<I", data, 14)[0]
    if dib_size < 40 or 14 + dib_size > len(data):
        raise SpectrumError("Unsupported or truncated BMP DIB header: {}".format(path))

    width, signed_height = struct.unpack_from("<ii", data, 18)
    planes, bits_per_pixel = struct.unpack_from("<HH", data, 26)
    compression = struct.unpack_from("<I", data, 30)[0]
    if width < 16 or signed_height == 0:
        raise SpectrumError("BMP dimensions must be at least 16 pixels: {}".format(path))
    height = abs(signed_height)
    if width != height:
        raise SpectrumError("Normal-map spectrum analysis requires a square BMP: {}".format(path))
    if width > 2048 or width & (width - 1):
        raise SpectrumError("BMP dimensions must be a power of two no larger than 2048: {}".format(path))
    if planes != 1 or bits_per_pixel != 24 or compression != 0:
        raise SpectrumError("Only uncompressed 24-bit BI_RGB BMPs are supported: {}".format(path))
    if pixel_offset < 14 + dib_size or pixel_offset > len(data):
        raise SpectrumError("BMP pixel offset is outside the image data: {}".format(path))

    row_bytes = width * 3
    stride = (row_bytes + 3) & ~3
    required_size = pixel_offset + stride * height
    if required_size > len(data):
        raise SpectrumError("BMP pixel array is truncated: {}".format(path))
    if file_size != 0 and (file_size < required_size or file_size > len(data)):
        raise SpectrumError("BMP file-size field is inconsistent with its pixel array: {}".format(path))

    red_rows: List[List[int]] = []
    green_rows: List[List[int]] = []
    for y in range(height):
        file_y = height - 1 - y if signed_height > 0 else y
        start = pixel_offset + file_y * stride
        row = data[start:start + row_bytes]
        red: List[int] = []
        green: List[int] = []
        for x in range(width):
            pixel = x * 3
            green.append(row[pixel + 1])
            red.append(row[pixel + 2])
        red_rows.append(red)
        green_rows.append(green)
    return width, red_rows, green_rows


def _fft_in_place(values: List[complex]) -> None:
    """Unnormalised iterative radix-2 Cooley-Tukey transform."""
    size = len(values)
    if size < 2 or size & (size - 1):
        raise SpectrumError("FFT input length must be a power of two of at least two")

    reversed_index = 0
    for index in range(1, size):
        bit = size >> 1
        while reversed_index & bit:
            reversed_index ^= bit
            bit >>= 1
        reversed_index ^= bit
        if index < reversed_index:
            values[index], values[reversed_index] = values[reversed_index], values[index]

    length = 2
    while length <= size:
        angle = -2.0 * math.pi / length
        twiddle_step = complex(math.cos(angle), math.sin(angle))
        half = length // 2
        for start in range(0, size, length):
            twiddle = 1.0 + 0.0j
            for offset in range(half):
                even = values[start + offset]
                odd = values[start + offset + half] * twiddle
                values[start + offset] = even + odd
                values[start + offset + half] = even - odd
                twiddle *= twiddle_step
        length *= 2


def _power_bands(rows: List[List[int]]) -> Tuple[List[float], float, float]:
    size = len(rows)
    pixel_count = size * size
    mean = sum(sum(row) for row in rows) / float(pixel_count)
    variance = sum(
        (float(value) - mean) ** 2
        for row in rows
        for value in row
    ) / float(pixel_count)
    bands = [0.0] * (size // 2 + 1)
    if variance <= 1.0e-24:
        return bands, mean, 0.0

    matrix = [
        [complex(float(value) - mean, 0.0) for value in row]
        for row in rows
    ]
    for row in matrix:
        _fft_in_place(row)

    # Parseval normalization makes total AC power equal the spatial variance
    # in byte-value squared, independent of image dimensions.
    normalization = float(pixel_count) * float(pixel_count)
    nyquist = size // 2
    for frequency_x in range(size):
        signed_x = frequency_x if frequency_x <= nyquist else frequency_x - size
        column = [matrix[y][frequency_x] for y in range(size)]
        _fft_in_place(column)
        for frequency_y, value in enumerate(column):
            signed_y = frequency_y if frequency_y <= nyquist else frequency_y - size
            shell = max(abs(signed_x), abs(signed_y))
            bands[shell] += (value.real * value.real + value.imag * value.imag) / normalization
    return bands, mean, variance


def _summarize_bands(bands: Sequence[float], size: int) -> Dict[str, object]:
    nyquist = size // 2
    total_power = sum(bands[1:])
    quarter = nyquist // 4
    half = nyquist // 2
    three_quarters = (3 * nyquist) // 4
    ranges = (
        (1, quarter),
        (quarter + 1, half),
        (half + 1, three_quarters),
        (three_quarters + 1, nyquist),
    )
    fractions: Dict[str, float] = {}
    for name, (first, last) in zip(_BAND_NAMES, ranges):
        power = sum(bands[first:last + 1]) if first <= last else 0.0
        fractions[name] = power / total_power if total_power > 0.0 else 0.0

    if total_power > 0.0:
        peak = max(range(1, nyquist + 1), key=lambda index: bands[index])
        centroid = sum(index * bands[index] for index in range(1, nyquist + 1)) / total_power
    else:
        peak = 0
        centroid = 0.0

    return {
        "total_ac_power": total_power,
        "peak_frequency_cycles_per_uv": peak,
        "spectral_centroid_cycles_per_uv": centroid,
        "near_nyquist_power_fraction": fractions[_BAND_NAMES[-1]],
        "energy_fraction_by_band": fractions,
    }


def _parse_source_frequencies(values: Sequence[str]) -> List[Tuple[str, float]]:
    parsed: List[Tuple[str, float]] = []
    seen = set()
    for value in values:
        if "=" not in value:
            raise SpectrumError("Source frequency must use LABEL=CYCLES_PER_UV: {}".format(value))
        label, raw_frequency = value.split("=", 1)
        label = label.strip()
        try:
            frequency = float(raw_frequency)
        except ValueError as exc:
            raise SpectrumError("Invalid source frequency '{}': {}".format(label, raw_frequency)) from exc
        if not label or label in seen or not math.isfinite(frequency) or frequency <= 0.0:
            raise SpectrumError("Source frequency labels must be unique and frequencies finite/positive")
        seen.add(label)
        parsed.append((label, frequency))
    return parsed


def fold_source_frequency(frequency: float, sample_count: int) -> float:
    """Fold an axis-aligned periodic frequency into the sampled Nyquist interval."""
    if not math.isfinite(frequency) or frequency < 0.0 or sample_count <= 0:
        raise SpectrumError("Frequency folding requires a finite non-negative frequency and positive sample count")
    return abs((frequency + sample_count * 0.5) % sample_count - sample_count * 0.5)


def analyze_bmp(path: Path, source_frequencies: Sequence[Tuple[str, float]] = ()) -> Dict[str, object]:
    size, red_rows, green_rows = _read_normal_bmp(Path(path))
    red_bands, red_mean, red_variance = _power_bands(red_rows)
    green_bands, green_mean, green_variance = _power_bands(green_rows)
    combined_bands = [red + green for red, green in zip(red_bands, green_bands)]

    report: Dict[str, object] = {
        "image": Path(path).name,
        "width": size,
        "height": size,
        "nyquist_cycles_per_uv": size // 2,
        "channels": {
            "red_x": dict(_summarize_bands(red_bands, size),
                mean_byte=red_mean, rms_ac_byte=math.sqrt(red_variance)),
            "green_y": dict(_summarize_bands(green_bands, size),
                mean_byte=green_mean, rms_ac_byte=math.sqrt(green_variance)),
        },
        "tangent_xy": dict(_summarize_bands(combined_bands, size),
            rms_ac_byte=math.sqrt(red_variance + green_variance)),
    }

    if source_frequencies:
        report["source_frequency_alias_estimates"] = [
            {
                "label": label,
                "source_cycles_per_uv": frequency,
                "above_nyquist": frequency > size / 2.0,
                "folded_cycles_per_uv": fold_source_frequency(frequency, size),
            }
            for label, frequency in source_frequencies
        ]
    return report


def compare_reports(candidate: Dict[str, object], baseline: Dict[str, object]) -> Dict[str, object]:
    if candidate["width"] != baseline["width"] or candidate["height"] != baseline["height"]:
        raise SpectrumError("Baseline comparison requires identical image dimensions")
    current = candidate["tangent_xy"]
    reference = baseline["tangent_xy"]
    current_bands = current["energy_fraction_by_band"]
    reference_bands = reference["energy_fraction_by_band"]
    return {
        "baseline_image": baseline["image"],
        "candidate_minus_baseline": {
            "near_nyquist_power_fraction": (
                current["near_nyquist_power_fraction"] - reference["near_nyquist_power_fraction"]
            ),
            "spectral_centroid_cycles_per_uv": (
                current["spectral_centroid_cycles_per_uv"] -
                reference["spectral_centroid_cycles_per_uv"]
            ),
            "rms_ac_byte": current["rms_ac_byte"] - reference["rms_ac_byte"],
            "energy_fraction_by_band": {
                name: current_bands[name] - reference_bands[name]
                for name in _BAND_NAMES
            },
        },
    }


def _print_human(report: Dict[str, object]) -> None:
    print("Normal-map spectrum: {} ({}x{})".format(
        report["image"], report["width"], report["height"]))
    print("Per-axis Nyquist: {} cycles/UV".format(report["nyquist_cycles_per_uv"]))
    for name, channel in report["channels"].items():
        print("  {}: mean {:.3f}, AC RMS {:.3f}, peak {} cycles/UV, centroid {:.3f}".format(
            name, channel["mean_byte"], channel["rms_ac_byte"],
            channel["peak_frequency_cycles_per_uv"],
            channel["spectral_centroid_cycles_per_uv"]))
    combined = report["tangent_xy"]
    print("  XY near-Nyquist power: {:.3%}".format(combined["near_nyquist_power_fraction"]))
    for name, fraction in combined["energy_fraction_by_band"].items():
        print("    {}: {:.3%}".format(name, fraction))
    for estimate in report.get("source_frequency_alias_estimates", []):
        print("  source {}: {:.3f} cycles/UV -> folded {:.3f} (above Nyquist: {})".format(
            estimate["label"], estimate["source_cycles_per_uv"],
            estimate["folded_cycles_per_uv"], estimate["above_nyquist"]))
    comparison = report.get("comparison")
    if comparison:
        delta = comparison["candidate_minus_baseline"]
        print("  candidate minus {}: near-Nyquist {:+.3%}, centroid {:+.3f} cycles/UV".format(
            comparison["baseline_image"], delta["near_nyquist_power_fraction"],
            delta["spectral_centroid_cycles_per_uv"]))


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(
        description="Measure periodic 2D power spectra of uncompressed 24-bit tangent-space normal BMPs."
    )
    parser.add_argument("image", type=Path, help="candidate normal-map BMP")
    parser.add_argument("--compare", type=Path, help="baseline BMP with identical dimensions")
    parser.add_argument(
        "--source-frequency", action="append", default=[], metavar="LABEL=CYCLES_PER_UV",
        help="optional axis-aligned source octave for a sampling-fold estimate; repeatable",
    )
    parser.add_argument("--json", action="store_true", help="write deterministic JSON")
    args = parser.parse_args(argv)

    try:
        frequencies = _parse_source_frequencies(args.source_frequency)
        report = analyze_bmp(args.image, frequencies)
        if args.compare:
            baseline = analyze_bmp(args.compare)
            report["comparison"] = compare_reports(report, baseline)
    except SpectrumError as exc:
        print("error: {}".format(exc), file=sys.stderr)
        return 2

    if args.json:
        print(json.dumps(report, sort_keys=True, indent=2, allow_nan=False))
    else:
        _print_human(report)
    return 0


if __name__ == "__main__":
    sys.exit(main())
