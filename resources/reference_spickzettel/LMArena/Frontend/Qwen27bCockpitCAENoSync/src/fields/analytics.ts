// ─────────────────────────────────────────────────────────────────────────────
// Analytic multiphysics field model for the step-channel + cylinder domain.
// The CPU mirror of the GLSL chunk in src/gl/shaders.ts (and of the WGSL in
// rust/src/render/shaders.wgsl) — used for streamline integration, LUT value
// normalization, and Compare-panel statistics.
//
// Physics: incompressible U∞ = 8 m/s channel flow, backward-facing step
// recirculation bubble, cylinder potential-flow disturbance + wake, k/ω
// turbulence scalars, wall-shear stress with separation lines.
// ─────────────────────────────────────────────────────────────────────────────

export interface DomainParams {
  L: number; // streamwise length (m)
  H: number; // far top height (m)
  hw: number; // half-width (z ∈ [-hw, hw])
  stepX: number;
  h1: number; // low top (x < stepX)
  cylX: number;
  cylY: number;
  cylR: number;
}

export const DEFAULT_DOMAIN: DomainParams = {
  L: 10, H: 2, hw: 1, stepX: 4, h1: 1.4, cylX: 6, cylY: 0.5, cylR: 0.5,
};

export const RHO = 1.204; // kg/m³
export const MU = 1.82e-5; // Pa·s

export function localTop(x: number, d: DomainParams): number {
  const s = smoothstep(d.stepX - 0.04, d.stepX + 0.04, x);
  return d.h1 + (d.H - d.h1) * s;
}

function smoothstep(a: number, b: number, x: number): number {
  const t = Math.min(1, Math.max(0, (x - a) / (b - a)));
  return t * t * (3 - 2 * t);
}
const clamp = (v: number, a: number, b: number) => Math.min(b, Math.max(a, v));

/** Flow development factor 0.35 (inlet ramp) → 1.0 (fully developed). */
export function devFactor(t: number): number {
  return 0.35 + 0.65 * smoothstep(0, 600, t);
}

/** Velocity [m/s] at p. dev ∈ (0,1] = development factor. */
export function velField(p: [number, number, number], dev: number, d: DomainParams): [number, number, number] {
  const [x, y, z] = p;
  const h = localTop(x, d);
  const hstep = smoothstep(d.stepX - 0.04, d.stepX + 0.04, x);
  const U0 = 8 * dev;
  const yn = clamp(y / h, -0.2, 1.15);
  let u = U0 * (1 - yn * yn) * (1 - 0.16 * hstep);
  let v = 0.05 * U0 * yn * (1 - yn) * 4 * Math.sin(Math.PI * clamp(yn, 0, 1));

  // Recirculation bubble behind the step (counter-clockwise)
  const cx = d.stepX + 1.4, cy = (d.h1 + d.H) * 0.47;
  const rdx = x - cx, rdy = y - cy;
  const r2 = rdx * rdx + rdy * rdy + 1e-4;
  const G = (2.4 * dev * Math.exp(-r2 / 0.36)) / r2;
  u += -G * rdy;
  v += G * rdx;

  // Cylinder potential-flow disturbance
  const qx = x - d.cylX, qy = y - d.cylY;
  const R2 = qx * qx + qy * qy;
  const Rmin = d.cylR * 1.04;
  if (R2 > Rmin * Rmin) {
    const f = (d.cylR * d.cylR) / (R2 * R2);
    u += U0 * (1 - f * (qx * qx - qy * qy)) - U0;
    v += -U0 * 2 * f * qx * qy;
  }

  // Wake deflection
  const wx = (x - d.cylX) * 2.2;
  const wy = (y - d.cylY) * 3;
  const w = Math.exp(-wx * wx) * smoothstep(d.cylX, d.cylX + 1, x) * Math.exp(-wy * wy);
  v += 1.6 * w * dev;

  // Turbulent wiggle (visual texture only, dimlesss small scale)
  u += 0.3 * dev * Math.sin(6 * y + 3 + 2 * z);
  v += 0.22 * dev * Math.sin(4 * x + 5 * z);
  let wz = 0.4 * dev * Math.sin(3 * x + 2 * y);
  wz *= 1 - z * z; // vanishes on symmetry planes
  u = clamp(u, -14, 14);
  v = clamp(v, -10, 10);
  return [u, v, wz];
}

/** Gauge pressure [Pa] (relative to 101325 Pa freestream). */
export function pGaugeField(p: [number, number, number], dev: number, d: DomainParams): number {
  const [x, y] = p;
  const u = velField(p, dev, d);
  const U2 = u[0] * u[0] + u[1] * u[1];
  const U0 = 8 * dev;
  let pg = 0.5 * RHO * (U0 * U0 * 0.9 - U2);
  pg += 220 * dev * smoothstep(d.stepX, d.stepX + 2.2, x); // pressure recovery
  const rdx = x - (d.stepX + 1.4), rdy = y - (d.h1 + d.H) * 0.47;
  pg -= 160 * dev * Math.exp(-((rdx * rdx + rdy * rdy) / 0.5)); // bubble dip
  const qx = x - d.cylX, qy = y - d.cylY;
  pg += 140 * dev * Math.exp(-(qx * qx + qy * qy) / 0.3); // stagnation bump
  return pg;
}

/** Temperature [K] — mild buoyancy-like gradient with wake heating. */
export function TField(p: [number, number, number], dev: number, d: DomainParams): number {
  const u = velField(p, dev, d);
  const U2 = u[0] * u[0] + u[1] * u[1] + u[2] * u[2];
  return 298 + 9 * dev * (1 - Math.min(1, Math.sqrt(U2) / (9 * dev + 1e-6)));
}

/** TKE k [m²/s²]. */
export function kField(p: [number, number, number], dev: number, d: DomainParams): number {
  const [x, y] = p;
  const rdx = x - (d.stepX + 1.4), rdy = y - (d.h1 + d.H) * 0.47;
  const qx = x - d.cylX, qy = y - d.cylY;
  const e1 = Math.exp(-((rdx * rdx + rdy * rdy) / 1.6));
  const e2 = Math.exp(-((qx * qx + qy * qy) / 0.5));
  return 0.05 * dev * (0.5 + 2.4 * e1 + 1.8 * e2 + 0.35 * y * x * 0.1);
}

/** Specific dissipation ω [1/s]. */
export function omegaField(p: [number, number, number], dev: number, d: DomainParams): number {
  const [x, y] = p;
  return 25 * dev * (0.3 + 1.5 * Math.exp(-((x - d.cylX) ** 2 + (y - d.cylY) ** 2) / 0.7) + 0.5 * smoothstep(d.stepX, d.stepX + 2, x)) * (0.4 + 0.6 * Math.abs(Math.sin(x * 0.7 + y)));
}

export interface Shear { mag: number; dir: [number, number, number] }

/**
 * Wall-shear stress τw [Pa] with transport direction — drives Surface-LIC
 * streak orientation and separation lines (mag → 0).
 */
export function tauW(p: [number, number, number], dev: number, d: DomainParams, patch: number): Shear {
  const [x, y] = p;
  const U0 = 8 * dev;
  if (patch === 3) {
    // bottom wall — reverses inside the recirculation zone (separation line!)
    const h = localTop(x, d);
    const uw = U0 * 0.82 * (1 - (0.03 / h) ** 2);
    const flip = x > d.stepX + 0.35 && x < d.stepX + 3.4 ? -1 : 1;
    const mag = 16 * dev * Math.pow(Math.max(0, uw / U0), 1.6) * (flip < 0 ? 0.55 : 1);
    return { mag: mag * (0.9 + 0.1 * Math.sin(x * 2.1)), dir: [flip, 0, 0] };
  }
  if (patch === 2) {
    const uw = U0 * 0.62 * (1 - 0.3 * smoothstep(d.stepX, d.stepX + 2, x));
    const mag = 15 * dev * Math.pow(uw / U0, 1.6);
    return { mag, dir: [1, 0, 0] };
  }
  if (patch === 4) {
    // step face — immediate separation, near-zero shear
    const mag = 1.1 * dev * (1 - clamp((y - d.h1) / 0.4, 0, 1));
    return { mag, dir: [0, 1, 0] };
  }
  if (patch === 5) {
    const qx = x - d.cylX, qy = y - d.cylY;
    const R = Math.sqrt(qx * qx + qy * qy) + 1e-9;
    const th = Math.abs(Math.atan2(qy, qx));
    const shoulder = Math.exp(-((th - 0.75) ** 2) / 0.5); // peak ±43°
    const sep = Math.max(0.03, 1 - Math.exp(-((th - 1.42) ** 2) / 0.015)); // →0 at ±81.5°
    const mag = 17 * dev * (0.25 + 1.15 * shoulder) * sep;
    return { mag, dir: [-qy / R, qx / R, 0] };
  }
  return { mag: 0, dir: [1, 0, 0] };
}

export function inDomain(p: [number, number, number], d: DomainParams): boolean {
  const [x, y, z] = p;
  if (x < 0 || x > d.L || y < 0 || y > localTop(x, d) || Math.abs(z) > d.hw) return false;
  const qx = x - d.cylX, qy = y - d.cylY;
  return qx * qx + qy * qy > d.cylR * d.cylR;
}

export interface FieldDef {
  id: string;
  label: string;
  unit: string;
  range: [number, number];
  wallOnly?: boolean;
  kind: "scalar" | "vector";
}

export const FIELD_DEFS: FieldDef[] = [
  { id: "velocity", label: "|U|  velocity magnitude", unit: "m/s", range: [0, 11.5], kind: "vector" },
  { id: "p", label: "p_rgh  pressure (gauge)", unit: "Pa", range: [-320, 640], kind: "scalar" },
  { id: "T", label: "T  temperature", unit: "K", range: [297.5, 307.5], kind: "scalar" },
  { id: "k", label: "k  turbulent KE", unit: "m²/s²", range: [0.02, 0.24], kind: "scalar" },
  { id: "omega", label: "ω  spec. dissipation", unit: "1/s", range: [4, 62], kind: "scalar" },
  { id: "tau_w", label: "τ_w  wall shear", unit: "Pa", range: [0, 17], kind: "vector", wallOnly: true },
];

export function fieldValue(id: string, p: [number, number, number], dev: number, d: DomainParams, patch = -1): number {
  switch (id) {
    case "velocity": {
      const u = velField(p, dev, d);
      return Math.sqrt(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
    }
    case "p": return pGaugeField(p, dev, d);
    case "T": return TField(p, dev, d);
    case "k": return kField(p, dev, d);
    case "omega": return omegaField(p, dev, d);
    case "tau_w": return tauW(p, dev, d, patch).mag;
    default: return 0;
  }
}

/** RK4 streamline integration. Returns polyline points (terminates outside domain). */
export function integrateStreamline(
  seed: [number, number, number],
  dev: number,
  d: DomainParams,
  steps = 88,
  dt = 0.006
): [number, number, number][] {
  const pts: [number, number, number][] = [];
  let p: [number, number, number] = [...seed];
  if (!inDomain(p, d)) return pts;
  const f = (q: [number, number, number]) => velField(q, dev, d);
  pts.push(p);
  for (let i = 0; i < steps; i++) {
    const k1 = f(p);
    const p2: [number, number, number] = [p[0] + (k1[0] * dt) / 2, p[1] + (k1[1] * dt) / 2, p[2] + (k1[2] * dt) / 2];
    const k2 = f(p2);
    const p3: [number, number, number] = [p[0] + (k2[0] * dt) / 2, p[1] + (k2[1] * dt) / 2, p[2] + (k2[2] * dt) / 2];
    const k3 = f(p3);
    const p4: [number, number, number] = [p[0] + k3[0] * dt, p[1] + k3[1] * dt, p[2] + k3[2] * dt];
    const k4 = f(p4);
    const scale = Math.min(1.3, Math.max(0.5, Math.hypot(k4[0], k4[1], k4[2]) / 6));
    p = [p[0] + (dt / 6) * (k1[0] + 2 * k2[0] + 2 * k3[0] + k4[0]) * scale,
         p[1] + (dt / 6) * (k1[1] + 2 * k2[1] + 2 * k3[1] + k4[1]) * scale,
         p[2] + (dt / 6) * (k1[2] + 2 * k2[2] + 2 * k3[2] + k4[2]) * scale];
    if (!inDomain(p, d)) break;
    pts.push(p);
  }
  return pts;
}

/** Seed a family of streamlines for the current domain. */
export function seedStreamlines(d: DomainParams): [number, number, number][] {
  const seeds: [number, number, number][] = [];
  for (let i = 0; i < 9; i++) {
    const x = 0.5 + i * (d.L - 1) / 8;
    const h = localTop(x, d);
    for (const fy of [0.22, 0.72]) {
      for (const z of [-0.55, 0, 0.55]) {
        seeds.push([x, h * fy, z]);
      }
    }
  }
  return seeds;
}

// Compare-panel statistics (synthetic but consistent with the analytic model).
export function fieldStats(id: string, t: number) {
  const dev = devFactor(t);
  const base: Record<string, { min: number; max: number; mean: number; rms: number }> = {
    velocity: { min: 0, max: 11.1 * dev, mean: 5.2 * dev, rms: 1.4 * dev },
    p: { min: -318 * dev, max: 642 * dev, mean: 42 * dev, rms: 190 * dev },
    T: { min: 297.6, max: 307.4 * dev + 297.6, mean: 301.2 * dev, rms: 2.1 * dev },
    k: { min: 0.02, max: 0.235 * dev, mean: 0.085 * dev, rms: 0.03 * dev },
    omega: { min: 4, max: 61 * dev, mean: 22 * dev, rms: 12 * dev },
    tau_w: { min: 0, max: 16.8 * dev, mean: 6.2 * dev, rms: 2.4 * dev },
  };
  return base[id] ?? base.velocity;
}
