/**
 * Writers for the generated discretisations.
 *   - legacy VTK (mesh + particles) for ParaView
 *   - OpenFOAM constant/polyMesh (one-cell-thick extrusion, empty front/back)
 *     which is exactly the directory layout DiFVM ingests as a drop-in
 *   - DiFVM graph arrays (owner / neighbour / Sf scatter-gather indices)
 *   - SPHinXsys-style boundary-surface table for the Eulerian SPH particles
 */
import { FvmMesh } from "./mesh";
import { PATCH_ORDER } from "./sdf";
import { SphData } from "./sph";

export function meshToVtk(m: FvmMesh): string {
  const L: string[] = [];
  const np = m.points.length / 2;
  L.push("# vtk DataFile Version 3.0");
  L.push("FVM/SPH Mesher - unstructured triangular FV mesh");
  L.push("ASCII");
  L.push("DATASET UNSTRUCTURED_GRID");
  L.push(`POINTS ${np} float`);
  for (let i = 0; i < np; i++)
    L.push(`${m.points[2 * i].toFixed(6)} ${m.points[2 * i + 1].toFixed(6)} 0`);
  L.push(`CELLS ${m.nCells} ${4 * m.nCells}`);
  for (let c = 0; c < m.nCells; c++)
    L.push(`3 ${m.tris[3 * c]} ${m.tris[3 * c + 1]} ${m.tris[3 * c + 2]}`);
  L.push(`CELL_TYPES ${m.nCells}`);
  for (let c = 0; c < m.nCells; c++) L.push("5");
  L.push(`CELL_DATA ${m.nCells}`);
  L.push("SCALARS quality float 1");
  L.push("LOOKUP_TABLE default");
  for (let c = 0; c < m.nCells; c++) L.push(m.quality[c].toFixed(5));
  L.push("SCALARS volume float 1");
  L.push("LOOKUP_TABLE default");
  for (let c = 0; c < m.nCells; c++) L.push(m.cv[c].toExponential(5));
  L.push("SCALARS rcm_order int 1");
  L.push("LOOKUP_TABLE default");
  const inv = new Int32Array(m.nCells);
  for (let i = 0; i < m.nCells; i++) inv[m.rcm[i]] = i;
  for (let c = 0; c < m.nCells; c++) L.push(String(inv[c]));
  return L.join("\n");
}

export function sphToVtk(s: SphData): string {
  const L: string[] = [];
  L.push("# vtk DataFile Version 3.0");
  L.push("FVM/SPH Mesher - relaxed Eulerian SPH particles");
  L.push("ASCII");
  L.push("DATASET POLYDATA");
  L.push(`POINTS ${s.n} float`);
  for (let i = 0; i < s.n; i++)
    L.push(`${s.x[i].toFixed(6)} ${s.y[i].toFixed(6)} 0`);
  L.push(`VERTICES ${s.n} ${2 * s.n}`);
  for (let i = 0; i < s.n; i++) L.push(`1 ${i}`);
  L.push(`POINT_DATA ${s.n}`);
  const scal = (name: string, get: (i: number) => number) => {
    L.push(`SCALARS ${name} float 1`);
    L.push("LOOKUP_TABLE default");
    for (let i = 0; i < s.n; i++) L.push(get(i).toExponential(5));
  };
  scal("Volume", (i) => s.V[i]);
  scal("SmoothingLength", (i) => s.h[i]);
  scal("DivPosition", (i) => s.divR[i]);
  scal("ZeroOrderResidual", (i) => s.eps0[i]);
  scal("BoundaryFlag", (i) => s.isBnd[i]);
  scal("BoundarySurface", (i) => s.asMag[i]);
  L.push("VECTORS BoundaryNormal float");
  for (let i = 0; i < s.n; i++)
    L.push(`${s.asNx[i].toFixed(6)} ${s.asNy[i].toFixed(6)} 0`);
  return L.join("\n");
}

export function sphToCsv(s: SphData): string {
  const L = [
    "id,x,y,volume,h,level,div_r,eps0_zero_order,eps1_first_order,n_neigh,is_boundary,A_iS,n_x,n_y,patch",
  ];
  for (let i = 0; i < s.n; i++)
    L.push(
      [
        i,
        s.x[i].toFixed(6),
        s.y[i].toFixed(6),
        s.V[i].toExponential(6),
        s.h[i].toExponential(6),
        s.level[i],
        s.divR[i].toFixed(5),
        s.eps0[i].toExponential(4),
        s.eps1[i].toExponential(4),
        s.nNeigh[i],
        s.isBnd[i],
        s.asMag[i].toExponential(6),
        s.asNx[i].toFixed(5),
        s.asNy[i].toFixed(5),
        s.patch[i] >= 0 ? PATCH_ORDER[s.patch[i]] : "interior",
      ].join(","),
    );
  return L.join("\n");
}

/** DiFVM-style static graph: gather over (owner, neighbour), scatter-add to cells */
export function graphToJson(m: FvmMesh): string {
  const internal: number[] = [];
  for (let f = 0; f < m.nFaces; f++) if (m.neigh[f] >= 0) internal.push(f);
  const src: number[] = [];
  const dst: number[] = [];
  for (const f of internal) {
    src.push(m.owner[f], m.neigh[f]);
    dst.push(m.neigh[f], m.owner[f]);
  }
  const patches: Record<string, number[]> = {};
  for (let f = 0; f < m.nFaces; f++) {
    if (m.neigh[f] >= 0) continue;
    const k = PATCH_ORDER[m.fPatch[f]] ?? "wall";
    (patches[k] ??= []).push(f);
  }
  const r6 = (a: ArrayLike<number>) =>
    Array.from(a, (v) => Number(v.toFixed(8)));
  const obj = {
    _format: "difvm-graph/1.0",
    _note:
      "FV operators = gather(owner,neighbour) -> flux -> segment_sum(dst) on this static graph",
    nCells: m.nCells,
    nFaces: m.nFaces,
    nInternalFaces: internal.length,
    cellCentres: r6(m.cc),
    cellVolumes: r6(m.cv),
    owner: Array.from(m.owner),
    neighbour: Array.from(m.neigh),
    faceCentres: r6(m.fc),
    faceAreas: r6(m.fa),
    faceNormals: r6(m.fn),
    faceWeights: r6(m.weight),
    nonOrthoAngleDeg: r6(m.nonOrtho),
    nonOrthoCorrection: r6(m.nonOrthoCorr),
    edgeSrc: src,
    edgeDst: dst,
    rcmPermutation: Array.from(m.rcm),
    boundaryPatches: Object.fromEntries(
      Object.entries(patches).map(([k, v]) => [k, { nFaces: v.length, faces: v }]),
    ),
  };
  return JSON.stringify(obj);
}

export const JAX_STUB = `"""
DiFVM-style graph operators for the exported mesh (Du, Li, Xu & Wang, 2026).
Every finite-volume operator is a gather / flux / scatter-add triple on the
static mesh connectivity graph -- the same primitive a GNN message-passing
layer uses, hence fully vectorised and differentiable under JAX.
"""
import json, jax, jax.numpy as jnp
from jax.ops import segment_sum

g = json.load(open("difvm_graph.json"))
owner = jnp.asarray(g["owner"]);  neigh = jnp.asarray(g["neighbour"])
Sf    = jnp.asarray(g["faceAreas"])[:, None] * jnp.asarray(
            g["faceNormals"]).reshape(-1, 2)
V     = jnp.asarray(g["cellVolumes"])
w     = jnp.asarray(g["faceWeights"])
nint  = g["nInternalFaces"]
o, n, Sfi, wi = owner[:nint], neigh[:nint], Sf[:nint], w[:nint]

def face_interp(phi):                       # gather + linear face value
    return wi * phi[o] + (1.0 - wi) * phi[n]

def divergence(flux_f, nc):                 # scatter-add (message passing)
    out  = segment_sum(flux_f, o, nc)
    out -= segment_sum(flux_f, n, nc)
    return out / V

def gradient(phi, nc):                      # Green-Gauss cell gradient
    phif = face_interp(phi)[:, None] * Sfi
    return (segment_sum(phif, o, nc) - segment_sum(phif, n, nc)) / V[:, None]

def laplacian(phi, nc, gamma=1.0):          # orthogonal part + deferred corr.
    d   = jnp.asarray(g["cellCentres"]).reshape(-1, 2)
    dON = d[n] - d[o]
    Ef  = (jnp.sum(Sfi * Sfi, 1) / jnp.sum(Sfi * dON, 1))[:, None] * dON
    f   = gamma * jnp.linalg.norm(Ef, axis=1) * (phi[n] - phi[o]) / \\
          jnp.linalg.norm(dON, axis=1)
    return (segment_sum(f, o, nc) - segment_sum(f, n, nc)) / V

# every function above is jit-able and reverse-mode differentiable:
#   dJ_dphi = jax.grad(lambda p: jnp.sum(laplacian(p, nc) ** 2))(phi0)
`;

/** one-cell-thick extrusion -> OpenFOAM constant/polyMesh (DiFVM drop-in) */
export function meshToFoam(m: FvmMesh, thickness = 0.05): string {
  const np = m.points.length / 2;
  const head = (cls: string, obj: string) =>
    `FoamFile\n{\n    version     2.0;\n    format      ascii;\n    class       ${cls};\n    location    "constant/polyMesh";\n    object      ${obj};\n}\n`;

  type F = { pts: number[]; own: number; nei: number; patch: string };
  const internal: F[] = [];
  const bnd: Record<string, F[]> = {};
  for (let f = 0; f < m.nFaces; f++) {
    const i = m.faceNodes[2 * f];
    const j = m.faceNodes[2 * f + 1];
    const O = m.owner[f];
    const N = m.neigh[f];
    if (N >= 0) {
      if (O < N) internal.push({ pts: [i, j, j + np, i + np], own: O, nei: N, patch: "" });
      else internal.push({ pts: [j, i, i + np, j + np], own: N, nei: O, patch: "" });
    } else {
      const k = PATCH_ORDER[m.fPatch[f]] ?? "wall";
      (bnd[k] ??= []).push({ pts: [i, j, j + np, i + np], own: O, nei: -1, patch: k });
    }
  }
  internal.sort((a, b) => a.own - b.own || a.nei - b.nei);
  const back: F[] = [];
  const front: F[] = [];
  for (let c = 0; c < m.nCells; c++) {
    const a = m.tris[3 * c];
    const b = m.tris[3 * c + 1];
    const d = m.tris[3 * c + 2];
    back.push({ pts: [a, d, b], own: c, nei: -1, patch: "back" });
    front.push({ pts: [a + np, b + np, d + np], own: c, nei: -1, patch: "front" });
  }
  const patchOrder = [...Object.keys(bnd), "back", "front"];
  const patchFaces: Record<string, F[]> = { ...bnd, back, front };

  const faces: F[] = [...internal];
  const boundaryEntries: string[] = [];
  for (const name of patchOrder) {
    const list = patchFaces[name];
    if (!list || !list.length) continue;
    const start = faces.length;
    faces.push(...list);
    const type =
      name === "back" || name === "front"
        ? "empty"
        : name === "wall" || name === "body"
          ? "wall"
          : "patch";
    boundaryEntries.push(
      `    ${name}\n    {\n        type            ${type};\n        nFaces          ${list.length};\n        startFace       ${start};\n    }`,
    );
  }

  const out: string[] = [];
  out.push(`/* ================= constant/polyMesh/points ================= */`);
  out.push(head("vectorField", "points"));
  out.push(`${2 * np}`, "(");
  for (let i = 0; i < np; i++)
    out.push(`(${m.points[2 * i].toFixed(8)} ${m.points[2 * i + 1].toFixed(8)} 0)`);
  for (let i = 0; i < np; i++)
    out.push(
      `(${m.points[2 * i].toFixed(8)} ${m.points[2 * i + 1].toFixed(8)} ${thickness})`,
    );
  out.push(")", "");
  out.push(`/* ================= constant/polyMesh/faces ================== */`);
  out.push(head("faceList", "faces"));
  out.push(`${faces.length}`, "(");
  for (const f of faces) out.push(`${f.pts.length}(${f.pts.join(" ")})`);
  out.push(")", "");
  out.push(`/* ================= constant/polyMesh/owner ================== */`);
  out.push(head("labelList", "owner"));
  out.push(`${faces.length}`, "(");
  for (const f of faces) out.push(String(f.own));
  out.push(")", "");
  out.push(`/* ================ constant/polyMesh/neighbour =============== */`);
  out.push(head("labelList", "neighbour"));
  out.push(`${internal.length}`, "(");
  for (const f of internal) out.push(String(f.nei));
  out.push(")", "");
  out.push(`/* ================ constant/polyMesh/boundary ================ */`);
  out.push(head("polyBoundaryMesh", "boundary"));
  out.push(`${boundaryEntries.length}`, "(");
  out.push(boundaryEntries.join("\n"));
  out.push(")", "");
  return out.join("\n");
}

/** Eulerian-SPH boundary storage in the form used by SPHinXsys */
export function sphBoundaryTable(s: SphData): string {
  const L = [
    "# Eulerian SPH boundary algorithm (Wang, Haidn & Hu 2024)",
    "# for every particle inside the boundaries (div r < 0.75 d):",
    "#   dW/dr * V_j  slot  <-  -|sum_p A_ip|      (flux magnitude of the boundary surface)",
    "#   e_ij         slot  <-  -sum_p A_ip / |sum_p A_ip|   (outward unit normal)",
    "# id, x, y, div_r, |A_iS|, n_x, n_y, patch, bc_input",
  ];
  const bcInput: Record<string, string> = {
    inlet: "U = U_inf, p = zero-gradient",
    outlet: "p = p_inf, U = zero-gradient",
    wall: "U_wall = 0 (non-slip mirror state)",
    body: "U_wall = 0 (non-slip mirror state)",
    farfield: "non-reflecting Riemann invariants",
    symmetry: "slip / reflecting",
  };
  for (let i = 0; i < s.n; i++) {
    if (!s.isBnd[i]) continue;
    const p = s.patch[i] >= 0 ? PATCH_ORDER[s.patch[i]] : "wall";
    L.push(
      [
        i,
        s.x[i].toFixed(6),
        s.y[i].toFixed(6),
        s.divR[i].toFixed(4),
        s.asMag[i].toExponential(6),
        s.asNx[i].toFixed(5),
        s.asNy[i].toFixed(5),
        p,
        bcInput[p],
      ].join(", "),
    );
  }
  return L.join("\n");
}

export function download(name: string, text: string) {
  const blob = new Blob([text], { type: "text/plain;charset=utf-8" });
  const url = URL.createObjectURL(blob);
  const a = document.createElement("a");
  a.href = url;
  a.download = name;
  a.click();
  setTimeout(() => URL.revokeObjectURL(url), 2000);
}
