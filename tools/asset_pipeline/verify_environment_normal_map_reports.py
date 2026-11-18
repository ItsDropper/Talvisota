#!/usr/bin/env python3
"""Verify the integrity and provenance contract of generated normal-map reports.

This detects corruption and internal inconsistency, not authenticity: the manifest
is unsigned and must be obtained from a trusted CI artifact channel.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path
from typing import Dict, List, Optional, Sequence

import generate_environment_normal_map_reports as report_generator
import normal_map_spectrum


class ReportVerificationError(RuntimeError):
    """A report artifact is incomplete, corrupted, or inconsistent."""


_REPORTS = {
    "bark": ("bark-normal.bmp", "bark-normal-spectrum.json", 840.0),
    "granite": ("granite-normal.bmp", "granite-normal-spectrum.json", 1536.0),
}
_MANIFEST_NAME = "environment-normal-map-spectrum-manifest.json"
_MANIFEST_KEYS = {
    "schema_version", "processor_file", "processor_sha256",
    "analyzer_file", "analyzer_sha256", "reports",
}
_RECORD_KEYS = {
    "label", "source_file", "source_sha256", "processed_texture_sha256",
    "report_file", "report_sha256", "width", "height", "mip_count",
    "source_cycles_per_uv",
}
_HASH_PATTERN = re.compile(r"^[0-9a-f]{64}$")
_MAX_JSON_BYTES = 16 * 1024 * 1024


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    try:
        with path.open("rb") as stream:
            for block in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(block)
    except OSError as exc:
        raise ReportVerificationError("Could not hash '{}': {}".format(path, exc)) from exc
    return digest.hexdigest()


def _read_json(path: Path) -> Dict[str, object]:
    try:
        if path.is_symlink() or not path.is_file():
            raise ReportVerificationError("Expected a regular, non-symlink file: {}".format(path))
        size = path.stat().st_size
        if size <= 0 or size > _MAX_JSON_BYTES:
            raise ReportVerificationError("JSON file size is outside the allowed range: {}".format(path))
        data = path.read_bytes()
        if len(data) != size:
            raise ReportVerificationError("JSON file changed while being read: {}".format(path))

        def unique_object(pairs):
            result = {}
            for key, value in pairs:
                if key in result:
                    raise ReportVerificationError(
                        "JSON file contains duplicate object key '{}': {}".format(key, path)
                    )
                result[key] = value
            return result

        def reject_constant(value):
            raise ReportVerificationError(
                "JSON file contains non-finite numeric constant '{}': {}".format(value, path)
            )

        value = json.loads(
            data.decode("utf-8"),
            object_pairs_hook=unique_object,
            parse_constant=reject_constant,
        )
    except ReportVerificationError:
        raise
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise ReportVerificationError("Could not parse JSON '{}': {}".format(path, exc)) from exc
    if not isinstance(value, dict):
        raise ReportVerificationError("JSON root must be an object: {}".format(path))
    return value


def _require_hash(value: object, description: str) -> str:
    if not isinstance(value, str) or not _HASH_PATTERN.fullmatch(value):
        raise ReportVerificationError("{} must be a lowercase SHA-256 digest.".format(description))
    return value


def _require_plain_filename(value: object, description: str) -> str:
    if (
        not isinstance(value, str)
        or not value
        or value in (".", "..")
        or Path(value).name != value
        or "/" in value
        or "\\" in value
    ):
        raise ReportVerificationError("{} must be a plain filename.".format(description))
    return value


def verify_reports(
    manifest_path: Path,
    source_root: Path,
    texture_processor: Optional[Path] = None,
) -> List[str]:
    manifest_path = Path(manifest_path)
    if manifest_path.name != _MANIFEST_NAME:
        raise ReportVerificationError("Manifest must be named {}.".format(_MANIFEST_NAME))
    if manifest_path.is_symlink():
        raise ReportVerificationError("Manifest must not be a symbolic link.")
    try:
        manifest_path = manifest_path.resolve(strict=True)
        source_directory = Path(source_root).resolve(strict=True)
    except (OSError, RuntimeError) as exc:
        raise ReportVerificationError("Could not resolve verification inputs: {}".format(exc)) from exc
    if not manifest_path.is_file() or not source_directory.is_dir():
        raise ReportVerificationError("Manifest and source root must be regular file/directory paths.")

    manifest = _read_json(manifest_path)
    if (
        set(manifest) != _MANIFEST_KEYS
        or type(manifest.get("schema_version")) is not int
        or manifest.get("schema_version") != 1
    ):
        raise ReportVerificationError("Manifest fields or schema_version are unsupported.")

    processor_name = _require_plain_filename(manifest.get("processor_file"), "processor_file")
    processor_hash = _require_hash(manifest.get("processor_sha256"), "processor_sha256")
    analyzer_name = _require_plain_filename(manifest.get("analyzer_file"), "analyzer_file")
    analyzer_hash = _require_hash(manifest.get("analyzer_sha256"), "analyzer_sha256")

    analyzer_path = Path(normal_map_spectrum.__file__).resolve(strict=True)
    if analyzer_name != analyzer_path.name or _sha256(analyzer_path) != analyzer_hash:
        raise ReportVerificationError(
            "Analyzer provenance does not match the verifier's normal_map_spectrum.py."
        )

    if texture_processor is not None:
        processor_path = Path(texture_processor).resolve(strict=True)
        if not processor_path.is_file():
            raise ReportVerificationError("Texture processor is not a regular file.")
        if processor_path.name != processor_name or _sha256(processor_path) != processor_hash:
            raise ReportVerificationError("Texture processor provenance does not match the manifest.")

    records = manifest.get("reports")
    if not isinstance(records, list) or len(records) != len(_REPORTS):
        raise ReportVerificationError("Manifest must contain exactly the bark and granite report records.")

    indexed = {}
    for record in records:
        if not isinstance(record, dict) or set(record) != _RECORD_KEYS:
            raise ReportVerificationError("A report record has unsupported or missing fields.")
        label = record.get("label")
        if not isinstance(label, str) or label not in _REPORTS or label in indexed:
            raise ReportVerificationError("Manifest contains an unknown or duplicate report label.")
        indexed[label] = record
    if set(indexed) != set(_REPORTS):
        raise ReportVerificationError("Manifest must contain exactly bark and granite reports.")

    verified: List[str] = []
    for label, (source_name, report_name, frequency) in _REPORTS.items():
        record = indexed[label]
        if record.get("source_file") != source_name or record.get("report_file") != report_name:
            raise ReportVerificationError("{} report record uses an unexpected filename.".format(label))
        _require_plain_filename(record.get("source_file"), "{} source_file".format(label))
        _require_plain_filename(record.get("report_file"), "{} report_file".format(label))
        source_hash = _require_hash(record.get("source_sha256"), "{} source_sha256".format(label))
        _require_hash(record.get("processed_texture_sha256"), "{} processed_texture_sha256".format(label))
        report_hash = _require_hash(record.get("report_sha256"), "{} report_sha256".format(label))

        source_path = source_directory / source_name
        if source_path.is_symlink() or not source_path.is_file():
            raise ReportVerificationError("{} source map is missing or not a regular file.".format(label))
        if _sha256(source_path) != source_hash:
            raise ReportVerificationError("{} source map SHA-256 does not match the manifest.".format(label))

        report_path = manifest_path.parent / report_name
        if report_path.is_symlink() or not report_path.is_file():
            raise ReportVerificationError("{} report is missing or not a regular file.".format(label))
        if _sha256(report_path) != report_hash:
            raise ReportVerificationError("{} report SHA-256 does not match the manifest.".format(label))
        report = _read_json(report_path)
        try:
            report_generator._validate_report(report, label, frequency)
        except (report_generator.ReportGenerationError, KeyError, TypeError, AttributeError) as exc:
            raise ReportVerificationError("{} report structure is invalid: {}".format(label, exc)) from exc

        if (
            type(record.get("width")) is not int
            or type(record.get("height")) is not int
            or type(record.get("mip_count")) is not int
            or record.get("width") != report.get("width")
            or record.get("height") != report.get("height")
            or record.get("mip_count") != report.get("mip_count")
            or record.get("source_cycles_per_uv") != frequency
        ):
            raise ReportVerificationError("{} manifest metadata disagrees with its report.".format(label))
        verified.append(label)

    return verified


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(
        description="Verify generated bark/granite normal-map report hashes and provenance."
    )
    parser.add_argument("manifest", type=Path, help="manifest from the downloaded report artifact")
    parser.add_argument(
        "--source-root", type=Path, default=Path("assets/textures/environment"),
        help="directory containing the original bark and granite BMP source maps",
    )
    parser.add_argument(
        "--texture-processor", type=Path,
        help="optional original texture processor executable for binary provenance verification",
    )
    args = parser.parse_args(argv)
    try:
        labels = verify_reports(args.manifest, args.source_root, args.texture_processor)
    except ReportVerificationError as exc:
        print("error: {}".format(exc), file=sys.stderr)
        return 2
    print("Verified normal-map report artifact: {}".format(", ".join(labels)))
    print("Note: SHA-256 checks establish integrity relative to the supplied unsigned manifest, not artifact authenticity.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
