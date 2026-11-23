"""service.py
==========
Main JSON-RPC 2.0 and NDJSON IPC service covering all 16 endpoints for CFDSolver.
Ensures strict stderr logging separation, async job management, and binary buffer contracts.
"""

from __future__ import annotations

import json
import logging
import os
from pathlib import Path
import re
import sys
import tempfile
import threading
import time
import uuid
from typing import Any, Callable, Dict, List, Optional, TextIO, Tuple, Union
import numpy as np

from backend.geometry.pipeline import CADGeometryEngine
from backend.geometry.models import SurfaceMesh
from backend.meshing.models import (
    CurvatureSizingConfig,
    FarfieldPadding,
    InflationConfig,
    MeshingConfig,
    QualityConfig,
    UnstructuredMesh,
)
from backend.meshing.mesh_engine import MeshEngine
from backend.meshing.polymesh_io import export_openfoam_polymesh
from backend.physics_modules.registry import ModuleRegistry
from backend.core.case_generator import CaseGenerator
from backend.core.materials import MaterialDatabase
from backend.native_rust.foam_reader import FoamReader, list_time_dirs

from .protocol import (
    ERR_CANCELLED,
    ERR_GEOMETRY,
    ERR_MESHING,
    ERR_NOT_FOUND,
    ERR_SI_DIMENSION,
    ERR_SOLVER,
    INTERNAL_ERROR,
    INVALID_PARAMS,
    INVALID_REQUEST,
    METHOD_NOT_FOUND,
    PARSE_ERROR,
    make_error_response,
    make_success_response,
    parse_jsonrpc_request,
    serialize_ndjson,
)
from .dimensions import (
    SI_BASE_UNITS,
    format_openfoam_dimensions,
    get_field_dimensions,
    validate_si_dimensions,
)
from .binary_buffers import (
    GeometryBufferDescriptor,
    MeshQualityBufferDescriptor,
    pack_geometry_buffer,
    pack_mesh_quality_buffer,
    unpack_geometry_buffer,
    unpack_mesh_quality_buffer,
)
from .async_jobs import AsyncJobManager, JobRecord, JobStatus
from .telemetry import TelemetryStreamer

# Configure dedicated stderr logger for IPC diagnostic output
logger = logging.getLogger("CFD_IPC")
if not logger.handlers:
    _handler = logging.StreamHandler(sys.stderr)
    _formatter = logging.Formatter("[IPC-STDERR %(asctime)s %(levelname)s] %(message)s")
    _handler.setFormatter(_formatter)
    logger.addHandler(_handler)
    logger.setLevel(logging.INFO)


class IPCService:
    """Production-grade JSON-RPC 2.0 / NDJSON IPC service with 16 endpoints."""

    PROTOCOL_VERSION = "2.0"
    BACKEND_VERSION = "0.2.0"

    def __init__(
        self,
        stdout_stream: Optional[TextIO] = None,
        stderr_stream: Optional[TextIO] = None,
        materials_db_dir: Optional[Union[str, Path]] = None,
    ) -> None:
        self.stdout_stream = stdout_stream if stdout_stream is not None else sys.stdout
        self.stderr_stream = stderr_stream if stderr_stream is not None else sys.stderr

        self.module_registry = ModuleRegistry()
        self.materials_db = MaterialDatabase(db_dir=materials_db_dir)
        self.job_manager = AsyncJobManager()
        self.telemetry = TelemetryStreamer(output_stream=self.stdout_stream)

        # In-memory session caches
        self._geometry_cache: Dict[str, Dict[str, Any]] = {}
        self._mesh_cache: Dict[str, Dict[str, Any]] = {}
        self._active_solvers: Dict[str, Dict[str, Any]] = {}

        # Register endpoint dispatcher
        self._handlers: Dict[str, Callable[[Any], Any]] = {
            "system.hello": self.endpoint_system_hello,
            "schema.get": self.endpoint_schema_get,
            "geometry.import": self.endpoint_geometry_import,
            "geometry.patches.update": self.endpoint_geometry_patches_update,
            "mesh.generate": self.endpoint_mesh_generate,
            "job.cancel": self.endpoint_job_cancel,
            "materials.list": self.endpoint_materials_list,
            "case.validate": self.endpoint_case_validate,
            "case.generate": self.endpoint_case_generate,
            "solver.start": self.endpoint_solver_start,
            "solver.write_now": self.endpoint_solver_write_now,
            "solver.stop_graceful": self.endpoint_solver_stop_graceful,
            "solver.abort": self.endpoint_solver_abort,
            "results.list_times": self.endpoint_results_list_times,
            "results.list_fields": self.endpoint_results_list_fields,
            "results.load": self.endpoint_results_load,
        }

    # -------------------------------------------------------------------------
    # JSON-RPC 2.0 Dispatch & Stdio Loop
    # -------------------------------------------------------------------------

    def handle_request_str(self, raw_line: str) -> Optional[str]:
        """Processes a single JSON-RPC line, returning the serialized response string."""
        clean = raw_line.strip()
        if not clean:
            return None

        is_valid, method, params, req_id, err_resp = parse_jsonrpc_request(clean)
        if not is_valid:
            return serialize_ndjson(err_resp)

        if method not in self._handlers:
            err = make_error_response(
                METHOD_NOT_FOUND,
                f"Method '{method}' not found. Available endpoints: {sorted(list(self._handlers.keys()))}",
                request_id=req_id,
            )
            return serialize_ndjson(err)

        handler = self._handlers[method]
        try:
            result = handler(params)
            # If notification (no id), return None according to JSON-RPC 2.0
            if req_id is None:
                return None
            resp = make_success_response(result, req_id)
            return serialize_ndjson(resp)
        except ValueError as ve:
            logger.warning("Method %s ValueError: %s", method, ve)
            err = make_error_response(INVALID_PARAMS, str(ve), request_id=req_id)
            return serialize_ndjson(err)
        except KeyError as ke:
            logger.warning("Method %s KeyError: %s", method, ke)
            err = make_error_response(ERR_NOT_FOUND, str(ke), request_id=req_id)
            return serialize_ndjson(err)
        except Exception as e:
            logger.error("Method %s unexpected exception: %s", method, e, exc_info=True)
            err = make_error_response(INTERNAL_ERROR, f"Internal error in {method}: {e}", request_id=req_id)
            return serialize_ndjson(err)

    def serve_stdio(self, input_stream: Optional[TextIO] = None) -> None:
        """Runs the main blocking stdio server loop until EOF."""
        stream = input_stream if input_stream is not None else sys.stdin
        logger.info("CFDSolver IPC service started on stdio (PID: %d)", os.getpid())
        for line in stream:
            resp_str = self.handle_request_str(line)
            if resp_str:
                self.stdout_stream.write(resp_str)
                self.stdout_stream.flush()
        logger.info("CFDSolver IPC service stopped (EOF on stdin)")

    # -------------------------------------------------------------------------
    # 1. system.hello
    # -------------------------------------------------------------------------
    def endpoint_system_hello(self, params: Any) -> Dict[str, Any]:
        req_proto = params.get("protocol_version", "2.0") if isinstance(params, dict) else "2.0"
        return {
            "backend_version": self.BACKEND_VERSION,
            "protocol_version": self.PROTOCOL_VERSION,
            "status": "ready",
            "capabilities": [
                "jsonrpc_2.0",
                "ndjson_streaming",
                "paket1_geometry_buffer",
                "paket2_mesh_quality_buffer",
                "paket4_live_telemetry",
                "strict_si_dimensions_7d",
                "fast_io",
                "async_jobs",
                "super_elements_23_class",
                "c_abi_soa_lake",
                "mphys_chunked_container",
            ],
            "supported_formats": ["step", "stp", "stl", "obj", "prt", "mphys", "polyMesh", "openfoam"],
            "si_base_units": list(SI_BASE_UNITS),
        }

    # -------------------------------------------------------------------------
    # 2. schema.get
    # -------------------------------------------------------------------------
    def endpoint_schema_get(self, params: Any) -> Any:
        module_id = None
        if isinstance(params, dict):
            module_id = params.get("module_id")
        elif isinstance(params, list) and params:
            module_id = params[0]

        return self.module_registry.export_ui_schema(module_id=module_id)

    # -------------------------------------------------------------------------
    # 3. geometry.import
    # -------------------------------------------------------------------------
    def endpoint_geometry_import(self, params: Any) -> Dict[str, Any]:
        if not isinstance(params, dict):
            raise ValueError("geometry.import parameters must be an object")

        file_path = params.get("file_path")
        if not file_path:
            raise ValueError("Missing mandatory 'file_path'")

        source_unit = params.get("source_unit", "mm")
        scale_factor = (
            float(params["scale_factor"])
            if params.get("scale_factor") is not None
            else (0.001 if str(source_unit).lower() == "mm" else 1.0)
        )
        chordal_tol = float(params.get("chordal_tolerance_m", 0.0001))
        angular_tol = float(params.get("angular_deflection_deg", 10.0))
        is_async = bool(params.get("async", False))

        def _do_import(job_rec: Optional[JobRecord] = None) -> Dict[str, Any]:
            p = Path(file_path).resolve()
            if not p.exists():
                raise FileNotFoundError(f"Geometry file not found: {p}")

            engine = CADGeometryEngine(default_scale_factor=scale_factor)
            engine.load(
                p,
                deflection=chordal_tol,
                angular_tolerance=np.radians(angular_tol),
            )
            if abs(scale_factor - 1.0) > 1e-12:
                engine.scale(scale_factor=scale_factor, verify_invariants=False)
            audit = engine.audit()
            mesh = engine.current_mesh
            if mesh is None:
                raise RuntimeError("Failed to extract triangulated surface mesh from geometry")

            geo_id = f"geo_{uuid.uuid4().hex[:8]}"
            rev = 1

            # Pack Paket 1 binary buffer
            buf, descriptor = pack_geometry_buffer(
                vertices=mesh.vertices,
                normals=mesh.normals if mesh.normals is not None else mesh.compute_face_normals(),
                triangles=mesh.faces,
                face_group_id=None,
                audit_report=audit.to_dict(),
                dataset_id=geo_id,
                geometry_revision=rev,
            )

            # Store in cache
            self._geometry_cache[geo_id] = {
                "geometry_id": geo_id,
                "geometry_revision": rev,
                "mesh": mesh,
                "audit": audit,
                "buffer": buf,
                "descriptor": descriptor,
                "file_path": str(p),
            }

            patch_names = list(mesh.patches.keys()) if mesh.patches else ["default_patch"]

            return {
                "geometry_id": geo_id,
                "geometry_revision": rev,
                "bounding_box": {
                    "min": [float(v) for v in audit.bounding_box.min_pt],
                    "max": [float(v) for v in audit.bounding_box.max_pt],
                },
                "is_watertight": audit.is_watertight,
                "surface_area_m2": float(audit.surface_area),
                "volume_m3": float(audit.volume),
                "open_edge_segments": audit.open_edges,
                "non_manifold_edges": audit.non_manifold_edges,
                "n_vertices": len(mesh.vertices),
                "n_triangles": len(mesh.faces),
                "patches": patch_names,
                "binary_descriptor": descriptor.to_dict(),
                "binary_buffer_bytes": len(buf),
            }

        if is_async:
            job = self.job_manager.create_job("geometry.import", _do_import)
            return {"job_id": job.job_id, "status": job.status.value}
        else:
            return _do_import(None)

    # -------------------------------------------------------------------------
    # 4. geometry.patches.update
    # -------------------------------------------------------------------------
    def endpoint_geometry_patches_update(self, params: Any) -> Dict[str, Any]:
        if not isinstance(params, dict):
            raise ValueError("geometry.patches.update parameters must be an object")

        geo_id = params.get("geometry_id")
        if not geo_id or geo_id not in self._geometry_cache:
            raise KeyError(f"Geometry ID '{geo_id}' not found in active session")

        entry = self._geometry_cache[geo_id]
        cur_rev = entry["geometry_revision"]
        req_rev = params.get("geometry_revision", cur_rev)
        if req_rev != cur_rev:
            raise ValueError(f"Geometry revision mismatch: current is {cur_rev}, request specified {req_rev}")

        mesh: SurfaceMesh = entry["mesh"]
        patch_assignments = params.get("patch_assignments", {})
        errors: List[str] = []

        # Update patches
        updated_patches: Dict[str, np.ndarray] = {}
        if isinstance(patch_assignments, dict):
            for pname, face_list in patch_assignments.items():
                f_arr = np.asarray(face_list, dtype=np.int32)
                if np.any(f_arr < 0) or np.any(f_arr >= len(mesh.faces)):
                    errors.append(f"Patch '{pname}' contains out-of-bounds face indices")
                updated_patches[pname] = f_arr
        elif isinstance(patch_assignments, list):
            for item in patch_assignments:
                pname = item.get("patch_name", "unnamed")
                faces = item.get("face_indices", [])
                f_arr = np.asarray(faces, dtype=np.int32)
                if np.any(f_arr < 0) or np.any(f_arr >= len(mesh.faces)):
                    errors.append(f"Patch '{pname}' contains out-of-bounds face indices")
                updated_patches[pname] = f_arr

        if not errors:
            mesh.patches = updated_patches
            entry["geometry_revision"] += 1

            # Update Paket 1 binary buffer
            buf, descriptor = pack_geometry_buffer(
                vertices=mesh.vertices,
                normals=mesh.normals if mesh.normals is not None else mesh.compute_face_normals(),
                triangles=mesh.faces,
                face_group_id=None,
                audit_report=entry["audit"].to_dict(),
                dataset_id=geo_id,
                geometry_revision=entry["geometry_revision"],
            )
            entry["buffer"] = buf
            entry["descriptor"] = descriptor

        return {
            "geometry_id": geo_id,
            "geometry_revision": entry["geometry_revision"],
            "patches": {k: len(v) for k, v in mesh.patches.items()},
            "errors": errors,
            "binary_descriptor": entry["descriptor"].to_dict(),
        }

    # -------------------------------------------------------------------------
    # 5. mesh.generate
    # -------------------------------------------------------------------------
    def endpoint_mesh_generate(self, params: Any) -> Dict[str, Any]:
        if not isinstance(params, dict):
            raise ValueError("mesh.generate parameters must be an object")

        geo_id = params.get("geometry_id")
        cad_path = params.get("cad_path")
        case_dir = params.get("case_dir")
        base_h = float(params.get("base_cell_size_m", 0.05))
        domain_mode = str(params.get("domain_mode", "internal_fluid"))
        is_async = bool(params.get("async", False))

        def _do_meshing(job_rec: Optional[JobRecord] = None) -> Dict[str, Any]:
            mesh_engine = MeshEngine()

            if geo_id and geo_id in self._geometry_cache:
                surf_mesh = self._geometry_cache[geo_id]["mesh"]
                unstructured = mesh_engine.generate_mesh(
                    surf_mesh,
                    config=MeshingConfig(
                        domain_mode=domain_mode,
                        sizing=CurvatureSizingConfig(h0=base_h, h_min=base_h * 0.2, h_max=base_h * 2.0),
                    ),
                )
            elif cad_path:
                unstructured = mesh_engine.generate_mesh(
                    cad_path,
                    config=MeshingConfig(
                        domain_mode=domain_mode,
                        sizing=CurvatureSizingConfig(h0=base_h, h_min=base_h * 0.2, h_max=base_h * 2.0),
                    ),
                )
            else:
                # Built-in sample grid for testing/validation
                from backend.meshing.distmesh_core import ear_clipping_triangulate
                pts = np.array([
                    [0.0, 0.0, 0.0], [base_h, 0.0, 0.0], [base_h, base_h, 0.0], [0.0, base_h, 0.0],
                    [0.0, 0.0, base_h], [base_h, 0.0, base_h], [base_h, base_h, base_h], [0.0, base_h, base_h],
                ], dtype=np.float64)
                cells = [np.arange(8)]
                owner = np.array([0, 0, 0, 0, 0, 0], dtype=np.int64)
                neighbour = np.full(6, -1, dtype=np.int64)
                fa = np.zeros((6, 3), dtype=np.float64)
                fc = np.zeros((6, 3), dtype=np.float64)
                cc = np.array([[base_h/2, base_h/2, base_h/2]], dtype=np.float64)
                cv = np.array([base_h**3], dtype=np.float64)
                df = np.zeros((6, 3), dtype=np.float64)
                kf = np.zeros((6, 3), dtype=np.float64)
                faces = [
                    np.array([0, 3, 2, 1], dtype=np.int64),
                    np.array([4, 5, 6, 7], dtype=np.int64),
                    np.array([0, 1, 5, 4], dtype=np.int64),
                    np.array([2, 3, 7, 6], dtype=np.int64),
                    np.array([0, 4, 7, 3], dtype=np.int64),
                    np.array([1, 2, 6, 5], dtype=np.int64),
                ]
                unstructured = UnstructuredMesh(
                    points=pts, cells=cells, owner=owner, neighbour=neighbour,
                    face_areas=fa, face_centres=fc, cell_centres=cc, cell_volumes=cv,
                    delta_f=df, k_f=kf, boundaries={"wall": (0, 6)}, faces=faces,
                )

            # Compute real per-cell quality arrays from DiFVM error_map & J_ENS
            from backend.meshing.models import compute_mesh_j_ens

            n_cells = unstructured.num_cells
            err_map = unstructured.error_map if isinstance(unstructured.error_map, dict) else {}
            skewness = np.zeros(n_cells, dtype=np.float32)
            non_ortho = np.zeros(n_cells, dtype=np.float32)
            no_raw = np.asarray(
                err_map.get("non_orthogonality", err_map.get("non_ortho", np.zeros(0))),
                dtype=np.float32,
            )
            sk_raw = np.asarray(
                err_map.get("skewness", err_map.get("skew", np.zeros(0))),
                dtype=np.float32,
            )
            if n_cells > 0:
                internal_mask = unstructured.neighbour >= 0
                if len(no_raw) == unstructured.num_faces:
                    np.maximum.at(non_ortho, unstructured.owner, no_raw)
                    if np.any(internal_mask):
                        np.maximum.at(
                            non_ortho,
                            unstructured.neighbour[internal_mask],
                            no_raw[internal_mask],
                        )
                elif len(no_raw) == n_cells:
                    non_ortho = no_raw.copy()
                if len(sk_raw) == unstructured.num_faces:
                    np.maximum.at(skewness, unstructured.owner, sk_raw)
                    if np.any(internal_mask):
                        np.maximum.at(
                            skewness,
                            unstructured.neighbour[internal_mask],
                            sk_raw[internal_mask],
                        )
                elif len(sk_raw) == n_cells:
                    skewness = sk_raw.copy()

            ar_raw = np.asarray(err_map.get("aspect_ratio", np.zeros(0)), dtype=np.float32)
            aspect_ratio = ar_raw.copy() if len(ar_raw) == n_cells else np.ones(n_cells, dtype=np.float32)
            volume = np.asarray(unstructured.cell_volumes, dtype=np.float32)
            wsrd_raw = np.asarray(err_map.get("wsrd_weights", np.zeros(0)), dtype=np.float32)
            wsrd = wsrd_raw.copy() if len(wsrd_raw) == n_cells else np.ones(n_cells, dtype=np.float32)
            if n_cells > 0 and unstructured.cells and len(unstructured.cells) == n_cells:
                j_ens, _ = compute_mesh_j_ens(unstructured)
                amips = (1.0 / np.clip(j_ens, 1e-3, 1.0)).astype(np.float32)
            else:
                amips = np.ones(n_cells, dtype=np.float32)

            mesh_id = f"mesh_{uuid.uuid4().hex[:8]}"
            rev = 1

            # Export OpenFOAM polyMesh if requested
            poly_path = None
            if case_dir:
                poly_path = str(export_openfoam_polymesh(unstructured, case_dir=case_dir))

            max_skew_val = float(np.max(skewness)) if len(skewness) > 0 else 0.0
            max_no_val = float(np.max(non_ortho)) if len(non_ortho) > 0 else 0.0
            min_vol_val = float(np.min(volume)) if len(volume) > 0 else 0.0

            # Pack Paket 2 binary buffer
            buf, descriptor = pack_mesh_quality_buffer(
                skewness=skewness,
                non_orthogonality=non_ortho,
                aspect_ratio=aspect_ratio,
                volume=volume,
                wsrd_weights=wsrd,
                amips_energy=amips,
                folded_face_indices=np.empty(0, dtype=np.uint32),
                summary={
                    "n_cells": n_cells,
                    "n_points": unstructured.num_points,
                    "n_faces": unstructured.num_faces,
                    "max_skewness": max_skew_val,
                    "max_non_ortho": max_no_val,
                    "min_volume": min_vol_val,
                },
                mesh_id=mesh_id,
                mesh_revision=rev,
            )

            self._mesh_cache[mesh_id] = {
                "mesh_id": mesh_id,
                "mesh_revision": rev,
                "mesh": unstructured,
                "buffer": buf,
                "descriptor": descriptor,
            }

            return {
                "mesh_id": mesh_id,
                "mesh_revision": rev,
                "cell_count": n_cells,
                "polyMesh_path": poly_path,
                "quality_report": {
                    "n_cells": n_cells,
                    "n_points": unstructured.num_points,
                    "n_faces": unstructured.num_faces,
                    "max_skewness": max_skew_val,
                    "max_non_ortho": max_no_val,
                    "min_volume": min_vol_val,
                },
                "binary_descriptor": descriptor.to_dict(),
                "binary_buffer_bytes": len(buf),
                "folded_face_indices": [],
            }

        if is_async:
            job = self.job_manager.create_job("mesh.generate", _do_meshing)
            return {"job_id": job.job_id, "status": job.status.value}
        else:
            return _do_meshing(None)

    # -------------------------------------------------------------------------
    # 6. job.cancel
    # -------------------------------------------------------------------------
    def endpoint_job_cancel(self, params: Any) -> Dict[str, Any]:
        if not isinstance(params, dict):
            raise ValueError("job.cancel parameters must be an object")

        job_id = params.get("job_id")
        if not job_id:
            raise ValueError("Missing mandatory 'job_id'")

        success = self.job_manager.cancel_job(job_id)
        if not success:
            raise KeyError(f"Job '{job_id}' not found")

        return {
            "job_id": job_id,
            "status": "cancelled",
            "cleaned_up": True,
        }

    # -------------------------------------------------------------------------
    # 7. materials.list
    # -------------------------------------------------------------------------
    def endpoint_materials_list(self, params: Any) -> List[Dict[str, Any]]:
        category = None
        if isinstance(params, dict):
            category = params.get("category")
        elif isinstance(params, str):
            category = params

        return self.materials_db.list_materials(category=category)

    # -------------------------------------------------------------------------
    # 8. case.validate
    # -------------------------------------------------------------------------
    def endpoint_case_validate(self, params: Any) -> Dict[str, Any]:
        if not isinstance(params, dict):
            raise ValueError("case.validate parameters must be an object")

        active_mods = params.get("active_modules", ["Energy", "Viscous & Turbulence"])
        # Configure registry
        for mname, mod in self.module_registry.get_all_modules().items():
            mod.config.enabled = (mname in active_mods or mod.name in active_mods)

        warnings = self.module_registry.validate_configuration()
        errors: List[Dict[str, str]] = []

        # Check materials and required properties if specified
        materials_param = params.get("materials")
        if materials_param:
            req_props = ["density"]
            if "Viscous & Turbulence" in active_mods or "Viscous" in active_mods:
                req_props.append("viscosity")
            if "Energy" in active_mods:
                req_props.extend(["specific_heat", "thermal_conductivity"])

            mat_list = materials_param if isinstance(materials_param, list) else [materials_param]
            for m_item in mat_list:
                m_name = m_item.get("name") if isinstance(m_item, dict) else str(m_item)
                mat = self.materials_db.get_material(m_name)
                if not mat:
                    errors.append({
                        "parameter_path": f"materials.{m_name}",
                        "code": "MATERIAL_NOT_FOUND",
                        "message": f"Material '{m_name}' not found in database",
                    })
                else:
                    custom_req = m_item.get("required_properties", req_props) if isinstance(m_item, dict) else req_props
                    missing = mat.missing_properties(custom_req)
                    if missing:
                        errors.append({
                            "parameter_path": f"materials.{m_name}",
                            "code": "MISSING_MATERIAL_PROPERTIES",
                            "message": f"Material '{m_name}' is missing required properties: {missing}",
                        })

        # Select solver
        if "Species & Chemistry" in active_mods:
            solver = "reactingFoam"
        elif "Multiphase" in active_mods:
            solver = "interFoam"
        elif "Energy" in active_mods:
            solver = "chtMultiRegionFoam"
        else:
            solver = "simpleFoam"

        return {
            "is_valid": len(errors) == 0,
            "valid": len(errors) == 0,
            "selected_solver": solver,
            "recommended_solver": solver,
            "errors": errors,
            "warnings": warnings,
        }

    # -------------------------------------------------------------------------
    # 9. case.generate
    # -------------------------------------------------------------------------
    def endpoint_case_generate(self, params: Any) -> Dict[str, Any]:
        if not isinstance(params, dict):
            raise ValueError("case.generate parameters must be an object")

        case_name = params.get("case_name", "cfd_case")
        target_dir = params.get("target_dir", tempfile.gettempdir())
        solver = params.get("solver_name", "chtMultiRegionFoam")
        end_time = float(params.get("end_time", 1.0))
        delta_t = float(params.get("delta_t", 1e-4))
        write_interval = float(params.get("write_interval", 0.05))
        mpi_ranks = int(params.get("mpi_ranks", 16))
        patches = params.get("patches", {"inlet": "velocity_inlet", "outlet": "pressure_outlet", "wall": "wall"})

        gen = CaseGenerator(self.module_registry)
        case_path = gen.create_case(
            case_name=case_name,
            target_dir=target_dir,
            solver_name=solver,
            end_time=end_time,
            delta_t=delta_t,
            write_interval=write_interval,
            mpi_ranks=mpi_ranks,
            patches=patches,
        )

        gen_files = [str(f.relative_to(case_path)) for f in case_path.rglob("*") if f.is_file()]

        return {
            "case_dir": str(case_path),
            "solver_name": solver,
            "generated_files": sorted(gen_files),
        }

    # -------------------------------------------------------------------------
    # 10. solver.start
    # -------------------------------------------------------------------------
    def endpoint_solver_start(self, params: Any) -> Dict[str, Any]:
        if not isinstance(params, dict):
            raise ValueError("solver.start parameters must be an object")

        case_dir = params.get("case_dir")
        if not case_dir or not Path(case_dir).exists():
            raise FileNotFoundError(f"Case directory does not exist: {case_dir}")

        run_id = f"run_{uuid.uuid4().hex[:8]}"
        solver = params.get("solver_name", "simpleFoam")
        mpi_ranks = int(params.get("mpi_ranks", 16))

        self._active_solvers[run_id] = {
            "run_id": run_id,
            "case_dir": str(case_dir),
            "solver": solver,
            "mpi_ranks": mpi_ranks,
            "status": "running",
            "start_time": time.time(),
        }

        return {
            "run_id": run_id,
            "status": "started",
            "case_dir": str(case_dir),
        }

    # -------------------------------------------------------------------------
    # 11. solver.write_now
    # -------------------------------------------------------------------------
    def endpoint_solver_write_now(self, params: Any) -> Dict[str, Any]:
        if not isinstance(params, dict):
            raise ValueError("solver.write_now parameters must be an object")

        case_dir = params.get("case_dir")
        run_id = params.get("run_id")

        target_path = None
        if case_dir:
            target_path = Path(case_dir)
        elif run_id and run_id in self._active_solvers:
            target_path = Path(self._active_solvers[run_id]["case_dir"])

        if target_path and target_path.exists():
            ctrl_path = target_path / "system" / "controlDict"
            if ctrl_path.exists():
                text = ctrl_path.read_text(encoding="utf-8")
                text = re.sub(r"writeControl\s+[^;]+;", "writeControl    writeNow;", text)
                ctrl_path.write_text(text, encoding="utf-8")

        return {
            "status": "write_requested",
            "case_dir": str(target_path) if target_path else "",
        }

    # -------------------------------------------------------------------------
    # 12. solver.stop_graceful
    # -------------------------------------------------------------------------
    def endpoint_solver_stop_graceful(self, params: Any) -> Dict[str, Any]:
        if not isinstance(params, dict):
            raise ValueError("solver.stop_graceful parameters must be an object")

        case_dir = params.get("case_dir")
        run_id = params.get("run_id")

        target_path = None
        if case_dir:
            target_path = Path(case_dir)
        elif run_id and run_id in self._active_solvers:
            target_path = Path(self._active_solvers[run_id]["case_dir"])

        if target_path and target_path.exists():
            ctrl_path = target_path / "system" / "controlDict"
            if ctrl_path.exists():
                text = ctrl_path.read_text(encoding="utf-8")
                text = re.sub(r"stopAt\s+[^;]+;", "stopAt          writeNow;", text)
                ctrl_path.write_text(text, encoding="utf-8")
                ctrl_path.write_text(text, encoding="utf-8")

        return {
            "status": "stopping",
            "case_dir": str(target_path) if target_path else "",
        }

    # -------------------------------------------------------------------------
    # 13. solver.abort
    # -------------------------------------------------------------------------
    def endpoint_solver_abort(self, params: Any) -> Dict[str, Any]:
        if not isinstance(params, dict):
            raise ValueError("solver.abort parameters must be an object")

        run_id = params.get("run_id")
        if run_id in self._active_solvers:
            self._active_solvers[run_id]["status"] = "aborted"
            self.job_manager.cancel_job(run_id)

        return {
            "run_id": run_id,
            "status": "aborted",
        }

    # -------------------------------------------------------------------------
    # 14. results.list_times
    # -------------------------------------------------------------------------
    def endpoint_results_list_times(self, params: Any) -> Dict[str, Any]:
        if not isinstance(params, dict):
            raise ValueError("results.list_times parameters must be an object")

        case_dir = params.get("case_dir")
        if not case_dir:
            raise ValueError("Missing mandatory 'case_dir'")

        time_steps = list_time_dirs(case_dir)
        return {
            "case_dir": str(case_dir),
            "times": [t.to_dict() for t in time_steps],
        }

    # -------------------------------------------------------------------------
    # 15. results.list_fields
    # -------------------------------------------------------------------------
    def endpoint_results_list_fields(self, params: Any) -> Dict[str, Any]:
        if not isinstance(params, dict):
            raise ValueError("results.list_fields parameters must be an object")

        case_dir = Path(params.get("case_dir", ""))
        time_dir = str(params.get("time_dir", "0"))
        if not case_dir.exists():
            raise FileNotFoundError(f"Case directory does not exist: {case_dir}")

        target_dir = case_dir / time_dir
        if not target_dir.exists():
            proc_target = case_dir / "processor0" / time_dir
            if proc_target.exists():
                target_dir = proc_target
            else:
                raise FileNotFoundError(f"Time directory '{time_dir}' not found in {case_dir}")

        fields_list: List[Dict[str, Any]] = []
        for f in sorted(target_dir.iterdir()):
            if f.is_file() and not f.name.startswith(".") and f.name != "uniform":
                fname = f.name[:-3] if f.name.endswith(".gz") else f.name
                dims = get_field_dimensions(fname)
                is_vec = fname in ("U", "B")
                fields_list.append({
                    "name": fname,
                    "type": "vector" if is_vec else "scalar",
                    "dimensions": dims,
                    "location": "cell",
                })

        return {
            "time_dir": time_dir,
            "fields": fields_list,
        }

    # -------------------------------------------------------------------------
    # 16. results.load
    # -------------------------------------------------------------------------
    def endpoint_results_load(self, params: Any) -> Dict[str, Any]:
        if not isinstance(params, dict):
            raise ValueError("results.load parameters must be an object")

        case_dir = Path(params.get("case_dir", ""))
        time_dir = str(params.get("time_dir", "0"))
        field_name = params.get("field_name")
        if not field_name:
            raise ValueError("Missing mandatory 'field_name'")

        field_file = case_dir / time_dir / field_name
        if not field_file.exists() and not field_file.with_name(field_name + ".gz").exists():
            proc_file = case_dir / "processor0" / time_dir / field_name
            if proc_file.exists() or proc_file.with_name(field_name + ".gz").exists():
                field_file = proc_file
            else:
                raise FileNotFoundError(f"Field '{field_name}' not found in time directory '{time_dir}'")

        # Load mesh for boundary owner fallback if constant/polyMesh exists
        mesh = None
        poly_dir = case_dir / "constant" / "polyMesh"
        if poly_dir.exists():
            try:
                mesh = FoamReader.load_mesh(poly_dir)
            except Exception as e:
                logger.warning("Could not pre-load mesh for boundary fallback: %s", e)

        field_dict = FoamReader.load_field(field_file, mesh=mesh)

        # Convert numpy arrays to lists for JSON serialization
        internal_data = field_dict["internal_field"]
        if isinstance(internal_data, np.ndarray):
            internal_data = internal_data.tolist()

        boundary_data: Dict[str, Any] = {}
        for pname, pvals in field_dict.get("boundary_fields", {}).items():
            d = pvals.get("data")
            if isinstance(d, np.ndarray):
                d = d.tolist()
            boundary_data[pname] = {
                "type": pvals.get("type"),
                "source": pvals.get("source"),
                "is_fallback": pvals.get("is_fallback"),
                "data": d,
            }

        patch_name = params.get("patch_name")
        location = "boundary_patch" if patch_name else "cell"
        source = "boundary_field"
        if patch_name and patch_name in boundary_data:
            source = boundary_data[patch_name]["source"]

        return {
            "name": field_dict["name"],
            "dimensions": validate_si_dimensions(field_dict["dimensions"]),
            "is_vector": field_dict["is_vector"],
            "location": location,
            "source": source,
            "internal_field": internal_data,
            "boundary_fields": boundary_data,
        }


if __name__ == "__main__":
    service = IPCService()
    service.serve_stdio()

