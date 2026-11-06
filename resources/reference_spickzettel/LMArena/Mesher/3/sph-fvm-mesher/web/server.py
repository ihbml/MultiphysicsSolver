#!/usr/bin/env python3
"""Small dependency-free HTTP adapter for the C++ mesher CLI.

Numerical work remains in C++; Python validates requests, starts the executable,
and turns its CSV output into a bounded canvas preview / downloadable artifacts.
"""

from __future__ import annotations

import argparse
import csv
import io
import json
import math
import os
from pathlib import Path
import re
import shutil
import subprocess
import threading
import time
import traceback
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import unquote, urlsplit
import zipfile

ROOT = Path(__file__).resolve().parents[1]
WEB = Path(__file__).resolve().parent
DEFAULT_RUNS = ROOT / "build" / "web-runs"
DEFAULT_BINARY = ROOT / "build" / "hybridmesh"
MAX_BODY = 7 * 1024 * 1024
MAX_FILE = 2 * 1024 * 1024
MAX_GRID_SITES = 150_000
MAX_CELLS = 30_000
MAX_JOBS = 8
MAX_PREVIEW_CELLS_2D = 3_500
MAX_PREVIEW_CELLS_3D = 9_000
MAX_PREVIEW_PARTICLES = 12_000
ALLOWED_OUTPUTS = {
    "summary.txt", "mesh_points.csv", "cells.csv", "faces.csv", "patches.csv",
    "particles.csv", "mesh.vtk", "particles.vtk", "sph_neighbors.csv",
    "fvm_graph.csv", "mesh_to_particle.csv", "particle_to_mesh.csv",
}
EXAMPLES = {
    "example_gmsh": (ROOT / "tests/fixtures/jittered_triangles.msh", 2, ([0, 0], [1, 1])),
    "example_foam": (ROOT / "tests/fixtures/foam", 3, ([0, 0, 0], [2, 1, 1])),
    "example_poly": (ROOT / "tests/fixtures/poly_prism", 3, ([0, 0, 0], [2, 2, 1])),
}
FILE_ID = re.compile(r"[0-9a-f]{32}\Z")
FLOAT_RE = r"[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[Ee][+-]?\d+)?"
FOAM_POINT = re.compile(r"\(\s*(" + FLOAT_RE + r")\s+(" + FLOAT_RE + r")\s+(" + FLOAT_RE + r")\s*\)")


class BadRequest(ValueError):
    pass


def finite(value: object, label: str, *, minimum: float | None = None,
           maximum: float | None = None) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise BadRequest(f"{label} must be a number")
    number = float(value)
    if not math.isfinite(number) or (minimum is not None and number < minimum) or (
        maximum is not None and number > maximum
    ):
        raise BadRequest(f"{label} must be finite and in the allowed range")
    return number


def dimension_count(value: object, label: str) -> int:
    if type(value) is not int or not 1 <= value <= 180:
        raise BadRequest(f"{label} must be an integer from 1 to 180")
    return value


def text_input(value: object, label: str) -> str:
    if not isinstance(value, str) or not value.strip() or "\x00" in value:
        raise BadRequest(f"{label} must be nonempty text")
    if len(value.encode("utf-8")) > MAX_FILE:
        raise BadRequest(f"{label} is too large (2 MB maximum)")
    return value


def gmsh_dimensions(text: str) -> tuple[int, tuple[list[float], list[float]]]:
    """Read only the Gmsh 2.2 node coordinates for a resource estimate."""
    lines = text.splitlines()
    try:
        at = next(i for i, line in enumerate(lines) if line.strip() == "$Nodes")
        count = int(lines[at + 1].strip())
    except (ValueError, StopIteration, IndexError) as exc:
        raise BadRequest("uploaded Gmsh file has no $Nodes section") from exc
    if not 1 <= count <= 100_000 or len(lines) < at + count + 2:
        raise BadRequest("uploaded Gmsh has an invalid/oversized node list")
    low = [math.inf] * 3
    high = [-math.inf] * 3
    try:
        for line in lines[at + 2:at + 2 + count]:
            parts = line.split()
            if len(parts) != 4:
                raise ValueError("invalid coordinate line")
            coords = [float(c) for c in parts[1:]]
            for axis in range(3):
                v = finite(coords[axis], "Gmsh coordinate", minimum=-1e7, maximum=1e7)
                low[axis] = min(low[axis], v)
                high[axis] = max(high[axis], v)
    except (ValueError, OverflowError) as exc:
        raise BadRequest("invalid coordinates in uploaded Gmsh nodes") from exc
    dim = 3 if high[2] - low[2] > 1e-9 * max(1.0, high[0] - low[0], high[1] - low[1]) else 2
    return dim, (low[:dim], high[:dim])


def foam_dimensions(files: dict[str, str]) -> tuple[int, tuple[list[float], list[float]]]:
    matches = FOAM_POINT.findall(files["points"])
    if not 4 <= len(matches) <= 100_000:
        raise BadRequest("OpenFOAM points must contain 4–100,000 coordinate triples")
    low = [math.inf] * 3
    high = [-math.inf] * 3
    for triple in matches:
        for axis in range(3):
            v = finite(float(triple[axis]), "OpenFOAM coordinate", minimum=-1e7, maximum=1e7)
            low[axis] = min(low[axis], v)
            high[axis] = max(high[axis], v)
    return 3, (low, high)


def prepare_run(payload: object, job: Path) -> list[str]:
    if not isinstance(payload, dict):
        raise BadRequest("expected a JSON object")
    source = payload.get("source")
    if not isinstance(source, dict):
        raise BadRequest("choose a mesh source")
    kind = source.get("type")
    source_args: list[str]
    if kind in ("box2", "box3"):
        dim = 2 if kind == "box2" else 3
        counts = [dimension_count(source.get(a), a) for a in ("nx", "ny", "nz")[:dim]]
        if math.prod(counts) > MAX_CELLS:
            raise BadRequest(f"the web tester is limited to {MAX_CELLS:,} box cells")
        lengths = [finite(source.get(a), a, minimum=0.001, maximum=1e5)
                   for a in ("lx", "ly", "lz")[:dim]]
        source_args = ["--box2" if dim == 2 else "--box3", *map(str, counts), *map(str, lengths)]
        bounds: tuple[list[float], list[float]] = ([0.0] * dim, lengths)
    elif kind in EXAMPLES:
        path, dim, bounds = EXAMPLES[kind]
        source_args = ["--gmsh" if kind == "example_gmsh" else "--openfoam", str(path)]
    elif kind == "gmsh_upload":
        text = text_input(source.get("text"), "Gmsh upload")
        if "$MeshFormat" not in text or "$Elements" not in text:
            raise BadRequest("upload a first-order ASCII Gmsh MSH 2.2 file")
        dim, bounds = gmsh_dimensions(text)
        uploaded = job / "input.msh"
        uploaded.write_text(text, encoding="utf-8")
        source_args = ["--gmsh", str(uploaded)]
    elif kind == "foam_upload":
        files = source.get("files")
        required = {"points", "faces", "owner", "neighbour", "boundary"}
        if not isinstance(files, dict) or set(files) != required:
            raise BadRequest("select the 5 OpenFOAM polyMesh files: points, faces, owner, neighbour, boundary")
        clean = {name: text_input(files[name], name) for name in required}
        if sum(len(v.encode("utf-8")) for v in clean.values()) > 5 * MAX_FILE:
            raise BadRequest("OpenFOAM upload is too large")
        dim, bounds = foam_dimensions(clean)
        folder = job / "polyMesh"
        folder.mkdir()
        for name, content in clean.items():
            (folder / name).write_text(content, encoding="utf-8")
        source_args = ["--openfoam", str(folder)]
    else:
        raise BadRequest("unknown mesh source")
    spacing = finite(payload.get("spacing"), "particle spacing", minimum=1e-6, maximum=1e5)
    support = finite(payload.get("support", 2.6), "support ratio", minimum=1.01, maximum=6)
    band = finite(payload.get("band", 2), "mesh band", minimum=0.1, maximum=10)
    est_sites = math.prod(max(1.0, (bounds[1][i] - bounds[0][i]) / spacing) for i in range(dim))
    if est_sites > MAX_GRID_SITES:
        raise BadRequest(f"particle spacing is too fine for this browser test (~{est_sites:,.0f} lattice sites). "
                         "Increase spacing; the C++ library itself has no such web limit.")
    core = payload.get("core")
    if not isinstance(core, dict):
        raise BadRequest("choose a particle core")
    if core.get("type") == "all":
        core_args = ["--core-all"]
    elif core.get("type") == "box":
        lo, hi = core.get("min"), core.get("max")
        if not isinstance(lo, list) or not isinstance(hi, list) or len(lo) != dim or len(hi) != dim:
            raise BadRequest(f"box core needs {dim} minimum and {dim} maximum coordinates")
        lows = [finite(v, "box minimum", minimum=-1e7, maximum=1e7) for v in lo]
        highs = [finite(v, "box maximum", minimum=-1e7, maximum=1e7) for v in hi]
        if any(lows[i] >= highs[i] for i in range(dim)):
            raise BadRequest("every box maximum must exceed its minimum")
        core_args = ["--core-box", *map(str, lows), *map(str, highs)]
    elif core.get("type") == "ball":
        center = core.get("center")
        if not isinstance(center, list) or len(center) != dim:
            raise BadRequest(f"core center needs {dim} coordinates")
        center = [finite(v, "ball center", minimum=-1e7, maximum=1e7) for v in center]
        radius = finite(core.get("radius"), "core radius", minimum=1e-6, maximum=1e6)
        core_args = ["--core-ball", *map(str, center), str(radius)]
    else:
        raise BadRequest("unknown particle core")
    output = job / "output"
    args = [*source_args, "--spacing", str(spacing), *core_args,
            "--support", str(support), "--band", str(band), "--output", str(output)]
    if payload.get("include_links", False) is not True:
        args.append("--no-links")
    adapt = payload.get("adapt")
    if adapt is not None:
        if core.get("type") not in ("box", "all") or not isinstance(adapt, dict):
            raise BadRequest("adaptation requires a box or full-domain core")
        point_csv = text_input(adapt.get("csv"), "interface-point CSV")
        margin = finite(adapt.get("margin"), "adaptation margin", minimum=0, maximum=1e6)
        step = finite(adapt.get("step"), "adaptation step", minimum=1e-6, maximum=1e6)
        csv_path = job / "interface.csv"
        csv_path.write_text(point_csv, encoding="utf-8")
        args += ["--adapt-points", str(csv_path), "--margin", str(margin), "--step", str(step)]
    return args


def read_summary(output: Path) -> dict[str, int]:
    result: dict[str, int] = {}
    for line in (output / "summary.txt").read_text(encoding="utf-8").splitlines():
        key, value = line.split(maxsplit=1)
        result[key] = int(value)
    return result


def preview_data(output: Path, summary: dict[str, int], spacing: float) -> dict:
    dim = summary["dimension"]
    nodes: dict[int, list[float]] = {}
    bounds_lo = [math.inf] * 3
    bounds_hi = [-math.inf] * 3
    with (output / "mesh_points.csv").open(newline="", encoding="utf-8") as file:
        for row in csv.DictReader(file):
            xyz = [float(row[axis]) for axis in ("x", "y", "z")]
            for axis, val in enumerate(xyz):
                bounds_lo[axis] = min(bounds_lo[axis], val)
                bounds_hi[axis] = max(bounds_hi[axis], val)
            if dim == 2:
                nodes[int(row["id"])] = xyz
    max_cells = MAX_PREVIEW_CELLS_2D if dim == 2 else MAX_PREVIEW_CELLS_3D
    cell_stride = max(1, math.ceil(summary["cells"] / max_cells))
    cells = []
    used_nodes = set()
    with (output / "cells.csv").open(newline="", encoding="utf-8") as file:
        for i, row in enumerate(csv.DictReader(file)):
            if i % cell_stride:
                continue
            cell = {"id": int(row["id"]), "x": float(row["center_x"]),
                    "y": float(row["center_y"]), "z": float(row["center_z"]),
                    "role": row["mesh_role"]}
            if dim == 2:
                vertex_ids = [int(v) for v in row["vertices"].split(";")]
                cell["vertices"] = vertex_ids
                used_nodes.update(vertex_ids)
            cells.append(cell)
    particle_stride = max(1, math.ceil(summary["particles"] / MAX_PREVIEW_PARTICLES))
    boundary_stride = max(1, math.ceil(summary["physical_boundary_particles"] / 1_000))
    particles = []
    boundary_index = 0
    with (output / "particles.csv").open(newline="", encoding="utf-8") as file:
        for i, row in enumerate(csv.DictReader(file)):
            is_boundary = row["physical_boundary"] == "1"
            if is_boundary:
                boundary_index += 1
            if i % particle_stride and not (is_boundary and boundary_index % boundary_stride == 0):
                continue
            particles.append({"id": int(row["id"]), "x": float(row["x"]),
                              "y": float(row["y"]), "z": float(row["z"]),
                              "role": row["role"], "boundary": is_boundary,
                              "owner": int(row["owner_cell"]),
                              "patch": int(row["wall_patch"])})
    patches = {}
    with (output / "patches.csv").open(newline="", encoding="utf-8") as file:
        for row in csv.DictReader(file):
            patches[row["id"]] = row["name"]
    return {"dimension": dim, "bounds": [bounds_lo, bounds_hi],
            "spacing": spacing, "nodes": {str(i): nodes[i] for i in used_nodes},
            "cells": cells, "particles": particles, "patches": patches,
            "limited": len(cells) < summary["cells"] or len(particles) < summary["particles"]}


def prune_jobs(jobs: Path) -> None:
    ordered = sorted((p for p in jobs.iterdir() if p.is_dir()),
                     key=lambda p: p.stat().st_mtime, reverse=True)
    for old in ordered[MAX_JOBS:]:
        shutil.rmtree(old, ignore_errors=True)


class AppServer(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True

    def __init__(self, address: tuple[str, int], *, runs: Path = DEFAULT_RUNS,
                 binary: Path = DEFAULT_BINARY):
        super().__init__(address, AppHandler)
        self.runs = runs
        self.binary = binary
        self.run_lock = threading.Lock()
        self.archive_lock = threading.Lock()
        self.runs.mkdir(parents=True, exist_ok=True)


class AppHandler(BaseHTTPRequestHandler):
    server: AppServer
    server_version = "HybridMeshWeb/1.0"

    def _send(self, content: bytes, content_type: str, status: int = 200,
              download_name: str | None = None) -> None:
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(content)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        if download_name:
            self.send_header("Content-Disposition", f'attachment; filename="{download_name}"')
        self.end_headers()
        self.wfile.write(content)

    def _send_file(self, path: Path, content_type: str, download_name: str) -> None:
        self.send_response(200)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(path.stat().st_size))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Content-Disposition", f'attachment; filename="{download_name}"')
        self.end_headers()
        with path.open("rb") as source:
            shutil.copyfileobj(source, self.wfile, length=1024 * 1024)

    def _json(self, payload: object, status: int = 200) -> None:
        data = json.dumps(payload, separators=(",", ":"), allow_nan=False).encode("utf-8")
        self._send(data, "application/json; charset=utf-8", status)

    def _error(self, message: str, status: int) -> None:
        self._json({"error": message}, status)

    def _job(self, identifier: str) -> Path | None:
        if not FILE_ID.fullmatch(identifier):
            return None
        candidate = self.server.runs / identifier / "output"
        return candidate if candidate.is_dir() else None

    def do_GET(self) -> None:
        path = unquote(urlsplit(self.path).path)
        static = {
            "/": (WEB / "index.html", "text/html; charset=utf-8"),
            "/app.js": (WEB / "app.js", "text/javascript; charset=utf-8"),
            "/style.css": (WEB / "style.css", "text/css; charset=utf-8"),
        }
        if path in static:
            file, content_type = static[path]
            self._send(file.read_bytes(), content_type)
            return
        if path == "/api/health":
            self._json({"ready": self.server.binary.is_file(), "engine": "C++20 CLI"})
            return
        components = path.strip("/").split("/")
        if len(components) >= 4 and components[:2] == ["api", "jobs"]:
            output = self._job(components[2])
            if output is None:
                self._error("run not found (results are temporary)", 404)
                return
            if components[3:] == ["download"]:
                archive = output.parent / "results.zip"
                with self.server.archive_lock:
                    if not archive.exists():
                        temporary = output.parent / "results.tmp"
                        with zipfile.ZipFile(temporary, "w", compression=zipfile.ZIP_DEFLATED) as z:
                            for file in sorted(output.iterdir()):
                                if file.name in ALLOWED_OUTPUTS and file.is_file():
                                    z.write(file, arcname=file.name)
                        temporary.replace(archive)
                self._send_file(archive, "application/zip", "hybridmesh-results.zip")
                return
            if len(components) == 5 and components[3] == "file" and components[4] in ALLOWED_OUTPUTS:
                file = output / components[4]
                if file.is_file():
                    content_type = "text/csv; charset=utf-8" if file.suffix == ".csv" else "text/plain; charset=utf-8"
                    self._send_file(file, content_type, file.name)
                    return
        self._error("route or file not found", 404)

    def do_POST(self) -> None:
        if urlsplit(self.path).path != "/api/run":
            self._error("route not found", 404)
            return
        if not self.server.binary.is_file():
            self._error("C++ backend is not built; run 'make -j4' first", 503)
            return
        if not self.headers.get("Content-Type", "").startswith("application/json"):
            self._error("send a JSON request", 415)
            return
        try:
            count = int(self.headers.get("Content-Length", ""))
        except ValueError:
            self._error("Content-Length is required", 411)
            return
        if not 0 < count <= MAX_BODY:
            self._error("request too large or empty (7 MB maximum)", 413)
            return
        try:
            payload = json.loads(self.rfile.read(count))
        except (UnicodeError, json.JSONDecodeError):
            self._error("invalid JSON", 400)
            return
        if not self.server.run_lock.acquire(blocking=False):
            self._error("another meshing run is in progress; retry shortly", 429)
            return
        job = self.server.runs / uuid.uuid4().hex
        response: dict = {}
        response_status = 200
        try:
            job.mkdir()
            args = prepare_run(payload, job)
            t0 = time.monotonic()
            try:
                result = subprocess.run([str(self.server.binary), *args], cwd=ROOT,
                                        text=True, capture_output=True, timeout=60, check=False)
            except subprocess.TimeoutExpired as exc:
                raise BadRequest("the run exceeded the 60-second web limit; try a coarser mesh") from exc
            if result.returncode:
                reason = (result.stderr or result.stdout).strip()[-2000:]
                raise BadRequest(reason or f"C++ backend exited with status {result.returncode}")
            output = job / "output"
            summary = read_summary(output)
            preview = preview_data(output, summary, float(payload["spacing"]))
            files = sorted(p.name for p in output.iterdir() if p.is_file() and p.name in ALLOWED_OUTPUTS)
            response = {"job_id": job.name, "summary": summary, "preview": preview,
                        "files": files, "seconds": round(time.monotonic() - t0, 3),
                        "log": result.stdout[-4000:]}
            prune_jobs(self.server.runs)
        except BadRequest as exc:
            shutil.rmtree(job, ignore_errors=True)
            response, response_status = {"error": str(exc)}, 422
        except Exception:
            traceback.print_exc()
            shutil.rmtree(job, ignore_errors=True)
            response, response_status = {"error": "the web adapter failed; check its terminal log"}, 500
        finally:
            self.server.run_lock.release()
        self._json(response, response_status)

    def log_message(self, format: str, *args: object) -> None:
        print("web:", format % args, flush=True)


def main() -> None:
    parser = argparse.ArgumentParser(description="Run the HybridMesh browser frontend (stdlib only)")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=int(os.environ.get("PORT", "8000")))
    args = parser.parse_args()
    with AppServer((args.host, args.port)) as server:
        print(f"HybridMesh UI listening on http://{args.host}:{server.server_port}/", flush=True)
        if not server.binary.is_file():
            print("Backend missing: run 'make -j4' in the project directory", flush=True)
        try:
            server.serve_forever(poll_interval=0.2)
        except KeyboardInterrupt:
            print("Stopping HybridMesh UI", flush=True)


if __name__ == "__main__":
    main()
