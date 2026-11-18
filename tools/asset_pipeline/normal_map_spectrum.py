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



_TTEX_MAGIC = b"TVTTEX01"
_MAX_TTEX_BYTES = 64 * 1024 * 1024
_MAX_TTEX_DIMENSION = 2048
_MIN_SPECTRUM_DIMENSION = 16

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


def _mean_variance(rows: List[List[int]]) -> Tuple[float, float]:
    size = len(rows)
    pixel_count = size * size
    mean = sum(sum(row) for row in rows) / float(pixel_count)
    variance = sum(
        (float(value) - mean) ** 2
        for row in rows
        for value in row
    ) / float(pixel_count)
    return mean, variance


def _power_bands(rows: List[List[int]]) -> Tuple[List[float], float, float]:
    size = len(rows)
    pixel_count = size * size
    mean, variance = _mean_variance(rows)
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


def _analyze_channel_pair(
    red_rows: List[List[int]], green_rows: List[List[int]]
) -> Tuple[Dict[str, object], Dict[str, object]]:
    size = len(red_rows)
    red_bands, red_mean, red_variance = _power_bands(red_rows)
    green_bands, green_mean, green_variance = _power_bands(green_rows)
    combined_bands = [red + green for red, green in zip(red_bands, green_bands)]
    channels: Dict[str, object] = {
        "red_x": dict(_summarize_bands(red_bands, size),
            mean_byte=red_mean, rms_ac_byte=math.sqrt(red_variance)),
        "green_y": dict(_summarize_bands(green_bands, size),
            mean_byte=green_mean, rms_ac_byte=math.sqrt(green_variance)),
    }
    tangent = dict(_summarize_bands(combined_bands, size),
        rms_ac_byte=math.sqrt(red_variance + green_variance))
    return channels, tangent


def _source_alias_estimates(
    source_frequencies: Sequence[Tuple[str, float]], sample_count: int
) -> List[Dict[str, object]]:
    return [
        {
            "label": label,
            "source_cycles_per_uv": frequency,
            "above_nyquist": frequency > sample_count / 2.0,
            "folded_cycles_per_uv": fold_source_frequency(frequency, sample_count),
        }
        for label, frequency in source_frequencies
    ]


def analyze_bmp(path: Path, source_frequencies: Sequence[Tuple[str, float]] = ()) -> Dict[str, object]:
    size, red_rows, green_rows = _read_normal_bmp(Path(path))
    channels, tangent = _analyze_channel_pair(red_rows, green_rows)
    report: Dict[str, object] = {
        "image": Path(path).name,
        "width": size,
        "height": size,
        "nyquist_cycles_per_uv": size // 2,
        "channels": channels,
        "tangent_xy": tangent,
    }

    if source_frequencies:
        report["source_frequency_alias_estimates"] = _source_alias_estimates(
            source_frequencies, size
        )
    return report


def _read_processed_texture(path: Path) -> Dict[str, object]:
    path = Path(path)
    try:
        file_size = path.stat().st_size
        if file_size < 32 or file_size > _MAX_TTEX_BYTES:
            raise SpectrumError(
                "Processed texture size must be between 32 bytes and {} MiB: {}".format(
                    _MAX_TTEX_BYTES // (1024 * 1024), path
                )
            )
        data = path.read_bytes()
    except SpectrumError:
        raise
    except OSError as exc:
        raise SpectrumError("Could not read processed texture '{}': {}".format(path, exc)) from exc

    if len(data) != file_size:
        raise SpectrumError("Processed texture changed while it was being read: {}".format(path))
    if data[:8] != _TTEX_MAGIC:
        raise SpectrumError("Processed texture has an invalid TVTTEX01 signature: {}".format(path))

    version, base_width, base_height, pixel_format, color_space, mip_count = struct.unpack_from(
        "<6I", data, 8
    )
    if version != 1:
        raise SpectrumError("Processed texture version must be 1, found {}.".format(version))
    if base_width == 0 or base_height == 0:
        raise SpectrumError("Processed texture base dimensions must be non-zero.")
    if base_width > _MAX_TTEX_DIMENSION or base_height > _MAX_TTEX_DIMENSION:
        raise SpectrumError(
            "Processed texture dimensions exceed the {}-pixel spectrum-analysis limit.".format(
                _MAX_TTEX_DIMENSION
            )
        )
    if pixel_format != 1:
        raise SpectrumError("Only RGBA8 TVTTEX01 containers are supported.")
    if color_space not in (0, 1):
        raise SpectrumError("Processed texture colour-space value is unsupported.")
    if mip_count == 0 or mip_count > 32:
        raise SpectrumError("Processed texture mip count must be between 1 and 32.")

    expected_mip_count = 1
    expected_width = base_width
    expected_height = base_height
    while expected_width > 1 or expected_height > 1:
        expected_width = max(1, (expected_width + 1) // 2)
        expected_height = max(1, (expected_height + 1) // 2)
        expected_mip_count += 1
    if mip_count != expected_mip_count:
        raise SpectrumError(
            "Processed texture must contain the complete mip chain (expected {}, found {}).".format(
                expected_mip_count, mip_count
            )
        )

    cursor = 32
    total_pixel_bytes = 0
    mips: List[Tuple[int, int, bytes]] = []
    expected_width = base_width
    expected_height = base_height
    for level in range(mip_count):
        if len(data) - cursor < 12:
            raise SpectrumError("Processed texture mip {} header is truncated.".format(level))
        width, height, byte_count = struct.unpack_from("<3I", data, cursor)
        cursor += 12
        if width != expected_width or height != expected_height:
            raise SpectrumError(
                "Processed texture mip {} dimensions are inconsistent: expected {}x{}, found {}x{}.".format(
                    level, expected_width, expected_height, width, height
                )
            )
        if width == 0 or height == 0:
            raise SpectrumError("Processed texture mip {} has zero dimensions.".format(level))
        expected_bytes = width * height * 4
        if byte_count != expected_bytes:
            raise SpectrumError(
                "Processed texture mip {} byte count is invalid: expected {}, found {}.".format(
                    level, expected_bytes, byte_count
                )
            )
        if expected_bytes > _MAX_TTEX_BYTES - total_pixel_bytes:
            raise SpectrumError("Processed texture mip data exceeds the analysis size limit.")
        if expected_bytes > len(data) - cursor:
            raise SpectrumError("Processed texture mip {} pixel data is truncated.".format(level))

        mips.append((width, height, data[cursor:cursor + expected_bytes]))
        cursor += expected_bytes
        total_pixel_bytes += expected_bytes
        expected_width = max(1, (expected_width + 1) // 2)
        expected_height = max(1, (expected_height + 1) // 2)

    if mips[-1][0:2] != (1, 1):
        raise SpectrumError("Processed texture mip chain does not terminate at 1x1.")
    if cursor != len(data):
        raise SpectrumError("Processed texture contains trailing bytes after the final mip.")

    return {
        "version": version,
        "width": base_width,
        "height": base_height,
        "pixel_format": "RGBA8",
        "color_space": "linear" if color_space == 0 else "srgb",
        "mips": mips,
    }


def _rgba_channel_rows(
    rgba: bytes, width: int, height: int
) -> Tuple[List[List[int]], List[List[int]]]:
    red_rows: List[List[int]] = []
    green_rows: List[List[int]] = []
    row_stride = width * 4
    for y in range(height):
        start = y * row_stride
        red_rows.append([rgba[start + x * 4] for x in range(width)])
        green_rows.append([rgba[start + x * 4 + 1] for x in range(width)])
    return red_rows, green_rows


def _scalar_channel_summary(rows: List[List[int]]) -> Tuple[Dict[str, float], float]:
    mean, variance = _mean_variance(rows)
    return {
        "mean_byte": mean,
        "rms_ac_byte": math.sqrt(variance),
    }, variance


def analyze_processed_texture(
    path: Path, source_frequencies: Sequence[Tuple[str, float]] = ()
) -> Dict[str, object]:
    container = _read_processed_texture(Path(path))
    if container["color_space"] != "linear":
        raise SpectrumError(
            "Normal-map spectrum analysis requires a linear-colour-space TVTTEX01 container."
        )
    width = int(container["width"])
    height = int(container["height"])
    if width != height:
        raise SpectrumError(
            "Processed normal-map spectrum analysis requires a square base texture; found {}x{}.".format(
                width, height
            )
        )
    if width < _MIN_SPECTRUM_DIMENSION or width & (width - 1):
        raise SpectrumError(
            "Processed normal-map base dimensions must be a power of two between {} and {}.".format(
                _MIN_SPECTRUM_DIMENSION, _MAX_TTEX_DIMENSION
            )
        )

    mip_reports: List[Dict[str, object]] = []
    for level, (mip_width, mip_height, rgba) in enumerate(container["mips"]):
        red_rows, green_rows = _rgba_channel_rows(rgba, mip_width, mip_height)
        mip_report: Dict[str, object] = {
            "level": level,
            "width": mip_width,
            "height": mip_height,
            "nyquist_cycles_per_uv": mip_width / 2.0,
        }
        if mip_width >= _MIN_SPECTRUM_DIMENSION:
            channels, tangent = _analyze_channel_pair(red_rows, green_rows)
            mip_report["analysis_status"] = "spectrum"
            mip_report["channels"] = channels
            mip_report["tangent_xy"] = tangent
        else:
            red_summary, red_variance = _scalar_channel_summary(red_rows)
            green_summary, green_variance = _scalar_channel_summary(green_rows)
            mip_report["analysis_status"] = "insufficient_frequency_resolution"
            mip_report["minimum_spectrum_dimension"] = _MIN_SPECTRUM_DIMENSION
            mip_report["channels"] = {
                "red_x": red_summary,
                "green_y": green_summary,
            }
            mip_report["tangent_xy"] = {
                "total_ac_power": red_variance + green_variance,
                "rms_ac_byte": math.sqrt(red_variance + green_variance),
            }
        if source_frequencies:
            mip_report["source_frequency_alias_estimates"] = _source_alias_estimates(
                source_frequencies, mip_width
            )
        mip_reports.append(mip_report)

    return {
        "image": Path(path).name,
        "container": "TVTTEX01",
        "version": container["version"],
        "width": width,
        "height": height,
        "pixel_format": container["pixel_format"],
        "color_space": container["color_space"],
        "mip_count": len(mip_reports),
        "mips": mip_reports,
    }


def compare_processed_reports(
    candidate: Dict[str, object], baseline: Dict[str, object]
) -> Dict[str, object]:
    if any(candidate[key] != baseline[key] for key in (
        "width", "height", "pixel_format", "color_space", "mip_count"
    )):
        raise SpectrumError("Processed texture comparison requires identical dimensions and mip metadata.")

    deltas: List[Dict[str, object]] = []
    for current, reference in zip(candidate["mips"], baseline["mips"]):
        if (current["width"], current["height"], current["analysis_status"]) != (
            reference["width"], reference["height"], reference["analysis_status"]
        ):
            raise SpectrumError("Processed texture comparison has incompatible mip levels.")
        current_tangent = current["tangent_xy"]
        reference_tangent = reference["tangent_xy"]
        delta: Dict[str, object] = {
            "level": current["level"],
            "width": current["width"],
            "height": current["height"],
            "rms_ac_byte": current_tangent["rms_ac_byte"] - reference_tangent["rms_ac_byte"],
        }
        if current["analysis_status"] == "spectrum":
            current_bands = current_tangent["energy_fraction_by_band"]
            reference_bands = reference_tangent["energy_fraction_by_band"]
            delta.update({
                "near_nyquist_power_fraction": (
                    current_tangent["near_nyquist_power_fraction"] -
                    reference_tangent["near_nyquist_power_fraction"]
                ),
                "spectral_centroid_cycles_per_uv": (
                    current_tangent["spectral_centroid_cycles_per_uv"] -
                    reference_tangent["spectral_centroid_cycles_per_uv"]
                ),
                "energy_fraction_by_band": {
                    name: current_bands[name] - reference_bands[name]
                    for name in _BAND_NAMES
                },
            })
        deltas.append(delta)

    return {
        "baseline_image": baseline["image"],
        "candidate_minus_baseline_by_mip": deltas,
    }


def _print_processed_human(report: Dict[str, object]) -> None:
    print("Normal-map mip spectrum: {} ({}x{}, {} levels)".format(
        report["image"], report["width"], report["height"], report["mip_count"]))
    print("Container: TVTTEX01, {}, {}".format(
        report["pixel_format"], report["color_space"]))
    for mip in report["mips"]:
        tangent = mip["tangent_xy"]
        if mip["analysis_status"] == "spectrum":
            print("  mip {}: {}x{}, Nyquist {:.1f} cycles/UV, near-Nyquist {:.3%}, centroid {:.3f}".format(
                mip["level"], mip["width"], mip["height"],
                mip["nyquist_cycles_per_uv"], tangent["near_nyquist_power_fraction"],
                tangent["spectral_centroid_cycles_per_uv"]))
        else:
            print("  mip {}: {}x{}, insufficient frequency resolution (AC RMS {:.3f})".format(
                mip["level"], mip["width"], mip["height"], tangent["rms_ac_byte"]))
        for estimate in mip.get("source_frequency_alias_estimates", []):
            print("    source {}: {:.3f} cycles/UV -> folded {:.3f} (above Nyquist: {})".format(
                estimate["label"], estimate["source_cycles_per_uv"],
                estimate["folded_cycles_per_uv"], estimate["above_nyquist"]))
    comparison = report.get("comparison")
    if comparison:
        for delta in comparison["candidate_minus_baseline_by_mip"]:
            if "near_nyquist_power_fraction" in delta:
                print("  mip {} delta vs {}: near-Nyquist {:+.3%}, centroid {:+.3f}".format(
                    delta["level"], comparison["baseline_image"],
                    delta["near_nyquist_power_fraction"],
                    delta["spectral_centroid_cycles_per_uv"]))
            else:
                print("  mip {} delta vs {}: AC RMS {:+.3f}".format(
                    delta["level"], comparison["baseline_image"], delta["rms_ac_byte"]))


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
    parser.add_argument("--json", action="store_true", help="write deterministic JSON to stdout")
    parser.add_argument("--json-output", type=Path,
        help="write deterministic UTF-8 JSON to this file while keeping the normal console summary")
    args = parser.parse_args(argv)

    try:
        frequencies = _parse_source_frequencies(args.source_frequency)
        image_suffix = args.image.suffix.lower()
        if image_suffix == ".ttex":
            if args.compare and args.compare.suffix.lower() != ".ttex":
                raise SpectrumError("Processed texture comparison requires another .ttex container.")
            report = analyze_processed_texture(args.image, frequencies)
            if args.compare:
                baseline = analyze_processed_texture(args.compare)
                report["comparison"] = compare_processed_reports(report, baseline)
        elif image_suffix == ".bmp":
            if args.compare and args.compare.suffix.lower() != ".bmp":
                raise SpectrumError("BMP comparison requires another BMP image.")
            report = analyze_bmp(args.image, frequencies)
            if args.compare:
                baseline = analyze_bmp(args.compare)
                report["comparison"] = compare_reports(report, baseline)
        else:
            raise SpectrumError("Input must be an uncompressed 24-bit BMP or a TVTTEX01 .ttex container.")

        json_text = json.dumps(report, sort_keys=True, indent=2, allow_nan=False)
        if args.json_output:
            try:
                output_path = args.json_output.resolve()
                input_paths = {args.image.resolve()}
                if args.compare:
                    input_paths.add(args.compare.resolve())
            except (OSError, RuntimeError) as exc:
                raise SpectrumError("Could not resolve JSON report or input path: {}".format(exc)) from exc
            if output_path in input_paths:
                raise SpectrumError("JSON report path must not overwrite an input BMP")
            try:
                args.json_output.write_text(json_text + "\n", encoding="utf-8")
            except OSError as exc:
                raise SpectrumError("Could not write JSON report '{}': {}".format(
                    args.json_output, exc
                )) from exc
    except SpectrumError as exc:
        print("error: {}".format(exc), file=sys.stderr)
        return 2

    if args.json:
        print(json_text)
    elif image_suffix == ".ttex":
        _print_processed_human(report)
    else:
        _print_human(report)
    return 0


if __name__ == "__main__":
    sys.exit(main())
