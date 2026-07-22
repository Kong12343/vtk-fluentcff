#!/usr/bin/env python3
"""Inspect Fluent CFF transient testdata; write docs/transient-testdata-LCSanalysis.md/json."""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path
from typing import Any

try:
    import h5py
except ImportError as exc:
    raise SystemExit("h5py required: pip install h5py") from exc

DEFAULT_TESTDATA = Path(r"E:/klj/LCSanalysis/testdata")


def read_scalar_string(dataset) -> str:
    raw = dataset[()]
    if hasattr(raw, "__len__") and not isinstance(raw, (str, bytes)):
        raw = raw[0]
    if isinstance(raw, bytes):
        return raw.decode("utf-8", errors="replace")
    return str(raw)


def parse_data_variables(text: str) -> dict[str, Any]:
    out: dict[str, Any] = {}
    m = re.search(r"\(flow-time\s+([0-9.eE+-]+)\)", text)
    if m:
        out["flow_time"] = float(m.group(1))
    m = re.search(r"\(time-step\s+(\d+)\)", text)
    if m:
        out["time_step"] = int(m.group(1))
    m = re.search(r"\(Case File\s+([^)]+)\)", text)
    if m:
        out["case_file"] = m.group(1).strip()
    return out


def list_fields(h5file: h5py.File, path: str) -> list[str]:
    try:
        fields_ds = h5file[path]
    except KeyError:
        return []
    text = read_scalar_string(fields_ds)
    return [p for p in text.split(";") if p]


def walk_tree(group: h5py.Group, prefix: str, depth: int, max_depth: int, lines: list[str]) -> None:
    if depth > max_depth:
        return
    for key in sorted(group.keys()):
        item = group[key]
        name = f"{prefix}/{key}" if prefix else key
        if isinstance(item, h5py.Group):
            lines.append(f"{name}/")
            walk_tree(item, name, depth + 1, max_depth, lines)
        else:
            lines.append(f"{name}  shape={item.shape} dtype={item.dtype}")


def inspect_file(path: Path) -> dict[str, Any]:
    info: dict[str, Any] = {"path": str(path), "name": path.name, "size_bytes": path.stat().st_size}
    with h5py.File(path, "r") as f:
        info["top_level"] = list(f.keys())
        tree_lines: list[str] = []
        walk_tree(f, "", 0, 3, tree_lines)
        info["tree_depth3"] = tree_lines
        if "settings" in f and "Data Variables" in f["settings"]:
            dv_text = read_scalar_string(f["settings"]["Data Variables"])
            info["data_variables"] = parse_data_variables(dv_text)
        if "results" in f and "1" in f["results"]:
            phase = "results/1/phase-1"
            info["cell_fields"] = list_fields(f, f"{phase}/cells/fields")
            info["face_fields"] = list_fields(f, f"{phase}/faces/fields")
            info["cell_field_count"] = len(info.get("cell_fields", []))
            info["face_field_count"] = len(info.get("face_fields", []))
        if "meshes" in f and "1" in f["meshes"]:
            m1 = f["meshes"]["1"]
            for attr in ("nodeCount", "cellCount", "faceCount"):
                if attr in m1.attrs:
                    info[attr] = int(m1.attrs[attr][0])
    return info


def write_markdown(out_path: Path, testdata: Path, records: list[dict[str, Any]]) -> None:
    lines = [
        "# LCSanalysis transient testdata (Fluent CFF)",
        "",
        f"Source: `{testdata}`",
        "",
        "## dat files summary",
        "",
        "| file | flow-time | time-step | case ref | cell fields | face fields | size (GB) |",
        "|------|-----------|-----------|----------|-------------|-------------|-----------|",
    ]
    for rec in records:
        if not rec["name"].endswith(".dat.h5"):
            continue
        dv = rec.get("data_variables", {})
        ft = dv.get("flow_time", "")
        ts = dv.get("time_step", "")
        cf = dv.get("case_file", "")
        cf_count = rec.get("cell_field_count", "")
        ff_count = rec.get("face_field_count", "")
        gb = rec["size_bytes"] / (1024**3)
        lines.append(
            f"| {rec['name']} | {ft} | {ts} | {cf} | {cf_count} | {ff_count} | {gb:.2f} |"
        )
    lines.extend(["", "## cas files", ""])
    for rec in records:
        if not rec["name"].endswith(".cas.h5"):
            continue
        cc = rec.get("cellCount", "")
        nc = rec.get("nodeCount", "")
        fc = rec.get("faceCount", "")
        gb = rec["size_bytes"] / (1024**3)
        lines.append(f"- **{rec['name']}**: nodes={nc}, cells={cc}, faces={fc}, {gb:.2f} GB")
    out_path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--testdata", type=Path, default=DEFAULT_TESTDATA)
    parser.add_argument(
        "--json",
        type=Path,
        default=Path("docs/transient-testdata-LCSanalysis.json"),
    )
    parser.add_argument(
        "--md",
        type=Path,
        default=Path("docs/transient-testdata-LCSanalysis.md"),
    )
    args = parser.parse_args()
    testdata: Path = args.testdata
    if not testdata.is_dir():
        raise SystemExit(f"testdata not found: {testdata}")

    files = sorted(testdata.glob("*.h5"))
    records = [inspect_file(p) for p in files]
    args.json.parent.mkdir(parents=True, exist_ok=True)
    args.md.parent.mkdir(parents=True, exist_ok=True)
    args.json.write_text(json.dumps(records, indent=2), encoding="utf-8")
    write_markdown(args.md, testdata, records)
    print(f"Wrote {args.md} and {args.json} ({len(records)} files)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
