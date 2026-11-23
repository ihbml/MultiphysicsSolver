import { buildCsr, gmres, ksAggregate, type Csr, type Tri } from "./engine";

/* ------------------------------------------------------------------ FOSM ---
 * First-Order Second-Moment robust optimisation without Monte Carlo.
 * Random field xi ~ N(0, Sigma), Sigma_ij = sigma^2 exp(-r/l_corr).
 * Phi = mu_g + kappa sigma_g, sigma_g^2 = d^T Sigma d, d = dg/dxi.
 * Exact sigma sensitivity uses one second-order adjoint pair.
 */
export type FosmParams = {
  nx: number;
  ny: number;
  sigmaXi: number;
  corrLength: number;
  kappa: number;
  rhoKs: number;
  simpP: number;
  sourceQ: number;
};

export type FosmRun = {
  nx: number;
  ny: number;
  mean: number;
  sigma: number;
  robust: number;
  kappa: number;
  peakT: number;
  sigmaXi: number;
  corrLength: number;
  covariance: number[][];
  dGdXi: number[];
  dSigmaDgamma: number[];
  dMuDgamma: number[];
  dRobustDgamma: number[];
  design: number[];
  temperature: number[];
  solves: number;
  elapsedMs: number;
};

type CondSystem = { A: Csr; b: Float64Array; dir: Uint8Array; k: Float64Array; faceCond: number[]; mesh: { nx: number; ny: number; h: number; size: number; faces: { c1: number; c2: number; metric: number; area: number }[] } };

function conductionSystem(nx: number, ny: number, x: number[], xi: number[] | null, sigmaXi: number, simpP: number, sourceQ: number): CondSystem {
  const h = 1;
  const size = nx * ny;
  const faces: { c1: number; c2: number; metric: number; area: number }[] = [];
  const idx = (i: number, j: number) => j * nx + i;
  for (let j = 0; j < ny; j++) {
    for (let i = 0; i < nx; i++) {
      if (i + 1 < nx) faces.push({ c1: idx(i, j), c2: idx(i + 1, j), metric: h, area: h * h });
      if (j + 1 < ny) faces.push({ c1: idx(i, j), c2: idx(i, j + 1), metric: h, area: h * h });
    }
  }
  const k = new Float64Array(size);
  for (let e = 0; e < size; e++) {
    const xc = Math.min(1, Math.max(0, x[e]));
    k[e] = 0.05 + Math.pow(xc, simpP) * (1 - 0.05);
    if (xi) k[e] *= Math.exp(sigmaXi * xi[e]);
  }
  const faceCond = faces.map((f) => 0.5 * (k[f.c1] + k[f.c2]) * (f.area / f.metric));
  const dir = new Uint8Array(size);
  for (let j = 0; j < ny; j++) dir[j * nx] = 1;
  const trips: Tri[] = [];
  faces.forEach((f, i) => {
    const c = faceCond[i];
    if (!dir[f.c1]) trips.push({ r: f.c1, c: f.c1, v: c }, { r: f.c1, c: f.c2, v: -c });
    if (!dir[f.c2]) trips.push({ r: f.c2, c: f.c2, v: c }, { r: f.c2, c: f.c1, v: -c });
  });
  for (let e = 0; e < size; e++) if (dir[e]) trips.push({ r: e, c: e, v: 1 });
  const b = new Float64Array(size);
  for (let e = 0; e < size; e++) b[e] = dir[e] ? 0 : sourceQ * h * h;
  return { A: buildCsr(size, trips), b, dir, k, faceCond, mesh: { nx, ny, h, size, faces } };
}

export function runFosm(params: FosmParams): FosmRun {
  const t0 = Date.now();
  const { nx, ny, sigmaXi, corrLength, kappa, rhoKs, simpP, sourceQ } = params;
  const n = nx * ny;
  let seed = 777;
  const rand = () => {
    seed = (1103515245 * seed + 12345) % 2147483648;
    return seed / 2147483648;
  };
  const design = Array.from({ length: n }, () => 0.25 + 0.6 * rand());
  const base = conductionSystem(nx, ny, design, null, sigmaXi, simpP, sourceQ);
  const A = base.A;
  const T = gmres(A, base.b, 1e-11, 50, 6000).x;
  const ks = ksAggregate(Array.from(T), rhoKs);
  const mean = ks.value;
  const lambda = gmres(A, Float64Array.from(ks.weights), 1e-11, 50, 6000).x;
  for (let e = 0; e < n; e++) if (base.dir[e]) lambda[e] = 0;
  const cov: number[][] = Array.from({ length: n }, () => new Array(n).fill(0));
  for (let i = 0; i < n; i++) {
    const xi = (i % nx + 0.5) * base.mesh.h;
    const yi = (Math.floor(i / nx) + 0.5) * base.mesh.h;
    for (let j = 0; j < n; j++) {
      const xj = (j % nx + 0.5) * base.mesh.h;
      const yj = (Math.floor(j / nx) + 0.5) * base.mesh.h;
      const dist = Math.hypot(xi - xj, yi - yj);
      cov[i][j] = sigmaXi * sigmaXi * Math.exp(-dist / corrLength);
    }
  }
  const dGdXi = new Array(n).fill(0);
  base.mesh.faces.forEach((f, i) => {
    const l1 = base.dir[f.c1] ? 0 : lambda[f.c1];
    const l2 = base.dir[f.c2] ? 0 : lambda[f.c2];
    const q = (l1 - l2) * (T[f.c1] - T[f.c2]);
    dGdXi[f.c1] -= 0.5 * sigmaXi * base.k[f.c1] * f.metric * q;
    dGdXi[f.c2] -= 0.5 * sigmaXi * base.k[f.c2] * f.metric * q;
  });
  let variance = 0;
  for (let i = 0; i < n; i++) for (let j = 0; j < n; j++) variance += dGdXi[i] * cov[i][j] * dGdXi[j];
  const sigma = Math.sqrt(Math.max(variance, 0));
  const v = new Float64Array(n);
  for (let i = 0; i < n; i++) v[i] = sigma > 1e-300 ? dGdXi[i] / sigma : 0;
  // Sigma v via the dense kernel (n <= 64).
  const sigV = new Float64Array(n);
  for (let i = 0; i < n; i++) {
    let acc = 0;
    for (let j = 0; j < n; j++) acc += cov[i][j] * v[j];
    sigV[i] = acc;
  }
  const t = new Float64Array(n);
  const s = new Float64Array(n);
  base.mesh.faces.forEach((f, i) => {
    const metric = f.area / f.metric;
    const wsum = 0.5 * sigmaXi * metric * (sigV[f.c1] * base.k[f.c1] + sigV[f.c2] * base.k[f.c2]);
    const dT = T[f.c1] - T[f.c2];
    const l1 = base.dir[f.c1] ? 0 : lambda[f.c1];
    const l2 = base.dir[f.c2] ? 0 : lambda[f.c2];
    if (!base.dir[f.c1]) {
      t[f.c1] += wsum * dT;
      s[f.c1] += wsum * (l1 - l2);
    }
    if (!base.dir[f.c2]) {
      t[f.c2] -= wsum * dT;
      s[f.c2] -= wsum * (l1 - l2);
    }
  });
  for (let e = 0; e < n; e++) {
    if (base.dir[e]) {
      t[e] = 0;
      s[e] = 0;
    }
  }
  const muV = gmres(A, t, 1e-11, 50, 6000).x;
  const etaV = gmres(A, s, 1e-11, 50, 6000).x;
  let wMu = 0;
  for (let e = 0; e < n; e++) wMu += ks.weights[e] * muV[e];
  const hKs = new Float64Array(n);
  for (let e = 0; e < n; e++) hKs[e] = ks.weights[e] * (muV[e] - wMu);
  const zeta = gmres(A, hKs, 1e-11, 50, 6000).x;
  const accum = (u: Float64Array, w: Float64Array, mask: boolean) => {
    const acc = new Float64Array(n);
    base.mesh.faces.forEach((f) => {
      const u1 = mask && base.dir[f.c1] ? 0 : u[f.c1];
      const u2 = mask && base.dir[f.c2] ? 0 : u[f.c2];
      const q = (u1 - u2) * (w[f.c1] - w[f.c2]) * (0.5 * f.area / f.metric);
      acc[f.c1] += q;
      acc[f.c2] += q;
    });
    return acc;
  };
  const acc1 = accum(etaV, T, false);
  const acc2 = accum(lambda, muV, true);
  const acc3 = accum(zeta, T, false);
  const accMu = accum(lambda, T, true);
  const dSigmaDgamma = new Array(n).fill(0);
  const dMuDgamma = new Array(n).fill(0);
  const dRobustDgamma = new Array(n).fill(0);
  for (let e = 0; e < n; e++) {
    const xc = Math.min(1, Math.max(0, design[e]));
    const dk = simpP * Math.pow(xc, simpP - 1) * (1 - 0.05);
    const explicit = base.k[e] > 1e-300 ? (sigV[e] * dGdXi[e] * dk) / base.k[e] : 0;
    const dSig = sigma > 1e-300 ? (acc1[e] + acc2[e] + acc3[e]) * dk + explicit : 0;
    dSigmaDgamma[e] = dSig;
    dMuDgamma[e] = -accMu[e] * dk;
    dRobustDgamma[e] = dMuDgamma[e] + kappa * dSig;
  }
  let worstPeak = 0;
  for (let e = 0; e < n; e++) worstPeak = Math.max(worstPeak, T[e]);
  return {
    nx,
    ny,
    mean,
    sigma,
    robust: mean + kappa * sigma,
    kappa,
    peakT: worstPeak,
    sigmaXi,
    corrLength,
    covariance: cov.map((r) => r.map((z) => Math.round(z * 1e6) / 1e6)),
    dGdXi: dGdXi.map((z) => Math.round(z * 1e7) / 1e7),
    dSigmaDgamma: dSigmaDgamma.map((z) => Math.round(z * 1e7) / 1e7),
    dMuDgamma: dMuDgamma.map((z) => Math.round(z * 1e7) / 1e7),
    dRobustDgamma: dRobustDgamma.map((z) => Math.round(z * 1e7) / 1e7),
    design: design.map((z) => Math.round(z * 1000) / 1000),
    temperature: Array.from(T, (z) => Math.round(z * 1e4) / 1e4),
    solves: 5,
    elapsedMs: Date.now() - t0,
  };
}

/* ---------------------------------------------------------------- Revolve ---
 * Griewank & Walther optimal binomial checkpointing. The planner emits concrete
 * actions for reversing N steps with S stored states using a DP-optimal split.
 */
export type RevolveAction = { kind: "advance" | "checkpoint" | "restore" | "reverse"; step: number; slot: number };

export type RevolvePlan = {
  nsteps: number;
  slots: number;
  actions: RevolveAction[];
  peakSlots: number;
  checkpoints: number;
  restores: number;
  reverses: number;
  predictedForwardExecutions: number;
  zeroMemoryExecutions: number;
  binomial: number;
  dpTable: { n: number; s: number; cost: number }[];
  timeline: { label: string; kind: string; step: number; slot: number }[];
};

function binomial(n: number, k: number): number {
  if (k < 0 || k > n) return 0;
  let r = 1;
  for (let i = 1; i <= k; i++) r = (r * (n - k + i)) / i;
  return r;
}

export function planRevolve(nsteps: number, slots: number): RevolvePlan {
  const N = Math.max(1, Math.min(160, Math.floor(nsteps)));
  const S = Math.max(0, Math.min(24, Math.floor(slots)));
  const memo = new Map<string, number>();
  const cost = (n: number, s: number): number => {
    if (n <= 0) return 0;
    if (s < 0) s = 0;
    if (n <= s + 1) return n;
    if (s === 0) return n + (n * (n - 1)) / 2;
    const key = `${n}:${s}`;
    const hit = memo.get(key);
    if (hit !== undefined) return hit;
    let best = Infinity;
    for (let i = 1; i < n; i++) best = Math.min(best, i + cost(i, s) + cost(n - i, s - 1));
    memo.set(key, best);
    return best;
  };
  const actions: RevolveAction[] = [];
  let peak = 0;
  const used: number[] = [];
  let slotCounter = 0;
  let checkpoints = 0;
  let restores = 0;
  const gen = (n: number, base: number) => {
    if (n <= 0) return;
    const free = S - used.length;
    if (n <= free + 1) {
      for (let t = 1; t <= n - 1; t++) {
        const slot = slotCounter++;
        used.push(slot);
        peak = Math.max(peak, used.length);
        actions.push({ kind: "checkpoint", step: base + t, slot });
        checkpoints++;
      }
      for (let k = n; k >= 1; k--) actions.push({ kind: "reverse", step: base + k, slot: -1 });
      for (let t = n - 1; t >= 1; t--) used.pop();
      return;
    }
    if (free <= 0) {
      for (let k = n; k >= 1; k--) actions.push({ kind: "reverse", step: base + k, slot: -1 });
      return;
    }
    let bestI = 1;
    let bestCost = Infinity;
    for (let i = 1; i < n; i++) {
      const c = i + cost(i, free) + cost(n - i, free - 1);
      if (c < bestCost) {
        bestCost = c;
        bestI = i;
      }
    }
    const slot = slotCounter++;
    used.push(slot);
    peak = Math.max(peak, used.length);
    actions.push({ kind: "checkpoint", step: base + bestI, slot });
    checkpoints++;
    gen(n - bestI, base + bestI);
    used.pop();
    actions.push({ kind: "restore", step: base, slot: -1 });
    restores++;
    gen(bestI, base);
  };
  gen(N, 0);
  const dpTable: { n: number; s: number; cost: number }[] = [];
  for (let s = 0; s <= S; s++) dpTable.push({ n: N, s, cost: cost(N, s) });
  const timeline = actions.map((a) => ({
    label: a.kind === "reverse" ? `adjoint t=${a.step}` : `${a.kind} t=${a.step}`,
    kind: a.kind,
    step: a.step,
    slot: a.slot,
  }));
  return {
    nsteps: N,
    slots: S,
    actions,
    peakSlots: peak,
    checkpoints,
    restores,
    reverses: actions.filter((a) => a.kind === "reverse").length,
    predictedForwardExecutions: Math.round(cost(N, S)),
    zeroMemoryExecutions: N + (N * (N - 1)) / 2,
    binomial: binomial(N, Math.min(S, N)),
    dpTable,
    timeline,
  };
}

/* ------------------------------------------------------------------ Hybrid ---
 * Two-stage global (MIGA + PSO) -> local (Nelder-Mead + L-BFGS) shape
 * optimisation of the coupled thermo-fluid model. Shape parameters control a
 * bank of smooth obstacles; the gradient is obtained by the exact chain rule
 * through the shape map and the physics adjoint.
 */
import { DEFAULT_PHYSICS, ThermoFluidModel } from "./engine";

export type HybridParams = {
  nx: number;
  ny: number;
  blobs: number;
  generations: number;
  islands: number;
  popPerIsland: number;
  psoSwarm: number;
  psoIterations: number;
  usePso: boolean;
};

export type HybridRun = {
  nx: number;
  ny: number;
  best: number;
  globalBest: number;
  globalHistory: number[];
  localHistory: number[];
  theta: number[];
  density: number[];
  globalEvaluations: number;
  localEvaluations: number;
  nmEvaluations: number;
  lbfgsEvaluations: number;
  islands: number;
  generations: number;
  usedPso: boolean;
  elapsedMs: number;
  improved: boolean;
  history: { iteration: number; value: number; stage: string }[];
};

function rngFactory(seed: number) {
  let s = seed >>> 0;
  return () => {
    s = (s * 1664525 + 1013904223) >>> 0;
    return s / 4294967296;
  };
}

export function runHybrid(params: HybridParams): HybridRun {
  const t0 = Date.now();
  const { nx, ny, blobs } = params;
  const prm = { ...DEFAULT_PHYSICS, nx, ny, sourceQ: 3e-3, wHydraulic: 0.02, kFluid: 0.04, tCap: 4 };
  const model = new ThermoFluidModel(prm);
  const dim = blobs * 3;
  const lo: number[] = [];
  const hi: number[] = [];
  for (let k = 0; k < blobs; k++) {
    lo.push(0.5, 0.5, 1.2);
    hi.push(nx - 0.5, ny - 0.5, 0.45 * Math.min(nx, ny));
  }
  const blobDensity = (theta: number[]) => {
    const gamma = new Float64Array(nx * ny);
    const jac = Array.from({ length: dim }, () => new Float64Array(nx * ny));
    for (let e = 0; e < nx * ny; e++) {
      const px = ((e % nx) + 0.5) * 1;
      const py = (Math.floor(e / nx) + 0.5) * 1;
      let cut = 0;
      let gammaVal = 1;
      for (let k = 0; k < blobs; k++) {
        const cx = theta[3 * k];
        const cy = theta[3 * k + 1];
        const r = theta[3 * k + 2];
        const d = Math.hypot(px - cx, py - cy);
        cut += 1 / (1 + Math.exp((d - r) / 0.7));
      }
      gammaVal = Math.min(1, Math.max(0.12, 1 - cut));
      gamma[e] = gammaVal;
      const active = gammaVal > 0.12 + 1e-9 && gammaVal < 1 - 1e-9;
      if (!active) continue;
      for (let k = 0; k < blobs; k++) {
        const cx = theta[3 * k];
        const cy = theta[3 * k + 1];
        const r = theta[3 * k + 2];
        const dx = px - cx;
        const dy = py - cy;
        const d = Math.max(Math.hypot(dx, dy), 1e-9);
        const sig = 1 / (1 + Math.exp((d - r) / 0.7));
        const dsd = (-sig * (1 - sig)) / 0.7;
        jac[3 * k][e] -= dsd * (dx / d);
        jac[3 * k + 1][e] -= dsd * (dy / d);
        jac[3 * k + 2][e] -= dsd * -1;
      }
    }
    return { gamma, jac };
  };
  let evals = 0;
  const fitness = (theta: number[]): number => {
    const { gamma } = blobDensity(theta);
    const res = model.evaluate(gamma);
    evals++;
    return res.objective + 8 * Math.max(0, res.constraints[1]) + 2 * Math.max(0, res.constraints[0]);
  };
  const gradient = (theta: number[]): number[] => {
    const { gamma, jac } = blobDensity(theta);
    const res = model.evaluate(gamma);
    evals++;
    const g = new Array(dim).fill(0);
    for (let k = 0; k < dim; k++) {
      let acc = 0;
      for (let e = 0; e < nx * ny; e++) acc += res.dObj[e] * jac[k][e];
      g[k] = acc;
    }
    return g;
  };
  const clamp = (t: number[]) => t.map((v, i) => Math.min(hi[i], Math.max(lo[i], v)));
  const rand = rngFactory(20240517);
  const history: { iteration: number; value: number; stage: string }[] = [];
  const globalHistory: number[] = [];

  // ---- MIGA: ring migration, SBX crossover, polynomial mutation with NSGA-II crowding.
  const islands: { x: number[]; f: number; crowd: number; rank: number }[][] = [];
  for (let isl = 0; isl < params.islands; isl++) {
    const pop: { x: number[]; f: number; crowd: number; rank: number }[] = [];
    for (let i = 0; i < params.popPerIsland; i++) {
      const x = clamp(Array.from({ length: dim }, (_, d) => lo[d] + rand() * (hi[d] - lo[d])));
      pop.push({ x, f: fitness(x), crowd: 0, rank: 0 });
    }
    islands.push(pop);
  }
  const crowding = (pop: { f: number; crowd: number }[]) => {
    pop.forEach((p) => (p.crowd = 0));
    const sorted = [...pop].sort((a, b) => a.f - b.f);
    if (sorted.length < 3) return;
    sorted[0].crowd = Infinity;
    sorted[sorted.length - 1].crowd = Infinity;
    const span = Math.max(sorted[sorted.length - 1].f - sorted[0].f, 1e-12);
    for (let i = 1; i < sorted.length - 1; i++) sorted[i].crowd = (sorted[i + 1].f - sorted[i - 1].f) / span;
  };
  let globalBest = Infinity;
  let bestTheta = clamp(Array.from({ length: dim }, (_, d) => (lo[d] + hi[d]) / 2));
  for (let gen = 0; gen < params.generations; gen++) {
    islands.forEach((pop) => {
      crowding(pop);
      pop.sort((a, b) => a.f - b.f);
      for (const p of pop) if (p.f < globalBest) {
        globalBest = p.f;
        bestTheta = p.x.slice();
      }
      const next = pop.slice(0, 2).map((p) => ({ ...p, x: p.x.slice() }));
      while (next.length < params.popPerIsland) {
        const pick = () => pop[Math.floor(rand() * pop.length)];
        const a = pick();
        const b = pick();
        const c1: number[] = [];
        for (let d = 0; d < dim; d++) {
          const u = rand();
          const beta = u <= 0.5 ? Math.pow(2 * u, 1 / 16) : Math.pow(1 / (2 * (1 - u)), 1 / 16);
          c1.push(0.5 * ((1 + beta) * a.x[d] + (1 - beta) * b.x[d]));
        }
        for (let d = 0; d < dim; d++) {
          if (rand() < 0.15) {
            const delta = rand() < 0.5 ? Math.pow(2 * rand(), 1 / 21) - 1 : 1 - Math.pow(2 * (1 - rand()), 1 / 21);
            c1[d] += delta * (hi[d] - lo[d]) * 0.2;
          }
        }
        const x = clamp(c1);
        next.push({ x, f: fitness(x), crowd: 0, rank: 0 });
      }
      pop.splice(0, pop.length, ...next);
    });
    if (params.islands > 1 && (gen + 1) % 2 === 0) {
      const migrants = islands.map((pop) => [...pop].sort((a, b) => a.f - b.f)[0].x);
      islands.forEach((pop, isl) => {
        const src = (isl + params.islands - 1) % params.islands;
        pop.sort((a, b) => a.f - b.f);
        pop[pop.length - 1].x = migrants[src].slice();
        pop[pop.length - 1].f = fitness(migrants[src]);
      });
    }
    globalHistory.push(globalBest);
    history.push({ iteration: history.length + 1, value: globalBest, stage: "MIGA" });
  }
  // ---- PSO refinement of the global basin
  if (params.usePso) {
    const swarm = Array.from({ length: params.psoSwarm }, () => clamp(Array.from({ length: dim }, (_, d) => lo[d] + rand() * (hi[d] - lo[d]))));
    const vel = swarm.map(() => Array.from({ length: dim }, () => (rand() - 0.5) * 0.15 * (hi[0] - lo[0])));
    const pbest = swarm.map((x) => x.slice());
    const pbestF = swarm.map((x) => fitness(x));
    let gbest = pbest[pbestF.indexOf(Math.min(...pbestF))].slice();
    let gbestF = Math.min(...pbestF);
    for (let it = 0; it < params.psoIterations; it++) {
      const w = 0.72 * (1 - 0.5 * (it / Math.max(1, params.psoIterations)));
      for (let i = 0; i < swarm.length; i++) {
        for (let d = 0; d < dim; d++) {
          const r1 = rand();
          const r2 = rand();
          vel[i][d] = w * vel[i][d] + 1.5 * r1 * (pbest[i][d] - swarm[i][d]) + 1.6 * r2 * (gbest[d] - swarm[i][d]);
          const vmax = 0.3 * (hi[d] - lo[d]);
          vel[i][d] = Math.min(vmax, Math.max(-vmax, vel[i][d]));
          swarm[i][d] = Math.min(hi[d], Math.max(lo[d], swarm[i][d] + vel[i][d]));
        }
        const f = fitness(swarm[i]);
        if (f < pbestF[i]) {
          pbestF[i] = f;
          pbest[i] = swarm[i].slice();
          if (f < gbestF) {
            gbestF = f;
            gbest = swarm[i].slice();
          }
        }
      }
      history.push({ iteration: history.length + 1, value: Math.min(gbestF, globalBest), stage: "PSO" });
    }
    if (gbestF < globalBest) {
      globalBest = gbestF;
      bestTheta = gbest.slice();
    }
  }
  // ---- Nelder-Mead downhill simplex
  const nmStart = evals;
  const simplex: number[][] = [bestTheta.slice()];
  const fv: number[] = [fitness(bestTheta)];
  for (let i = 0; i < dim; i++) {
    const p = bestTheta.slice();
    p[i] = Math.min(hi[i], p[i] + 0.08 * (hi[i] - lo[i]));
    simplex.push(p);
    fv.push(fitness(p));
  }
  for (let it = 0; it < 60; it++) {
    const order = simplex.map((_, i) => i).sort((a, b) => fv[a] - fv[b]);
    const s = order.map((i) => simplex[i]);
    const f = order.map((i) => fv[i]);
    simplex.splice(0, simplex.length, ...s);
    fv.splice(0, fv.length, ...f);
    const centroid = new Array(dim).fill(0);
    for (let i = 0; i < dim; i++) for (let d = 0; d < dim; d++) centroid[d] += simplex[i][d] / dim;
    const at = (t: number) => clamp(centroid.map((c, d) => c + t * (simplex[dim][d] - c)));
    const xr = at(1);
    const fr = fitness(xr);
    if (fr < fv[0]) {
      const xe = at(2);
      const fe = fitness(xe);
      simplex[dim] = fe < fr ? xe : xr;
      fv[dim] = fe < fr ? fe : fr;
    } else if (fr < fv[dim - 1]) {
      simplex[dim] = xr;
      fv[dim] = fr;
    } else {
      const xc = at(-0.5);
      const fc = fitness(xc);
      if (fc < fv[dim]) {
        simplex[dim] = xc;
        fv[dim] = fc;
      } else {
        for (let i = 1; i <= dim; i++) {
          simplex[i] = clamp(simplex[0].map((v, d) => v + 0.5 * (simplex[i][d] - v)));
          fv[i] = fitness(simplex[i]);
        }
      }
    }
    const bestIdx = fv.indexOf(Math.min(...fv));
    if (bestIdx >= 0) bestTheta = simplex[bestIdx].slice();
    history.push({ iteration: history.length + 1, value: Math.min(...fv), stage: "Nelder-Mead" });
  }
  const nmEvaluations = evals - nmStart;
  // ---- L-BFGS polish with the analytic shape gradient
  const lbStart = evals;
  let x = bestTheta.slice();
  let fx = fitness(x);
  let g = gradient(x);
  const sHist: number[][] = [];
  const yHist: number[][] = [];
  const rhoHist: number[] = [];
  for (let it = 0; it < 30; it++) {
    const q = g.slice();
    const alpha = new Array(sHist.length).fill(0);
    for (let i = sHist.length - 1; i >= 0; i--) {
      alpha[i] = rhoHist[i] * dot(sHist[i], q);
      for (let d = 0; d < dim; d++) q[d] -= alpha[i] * yHist[i][d];
    }
    let scale = 1;
    if (sHist.length) {
      const sy = dot(sHist[sHist.length - 1], yHist[yHist.length - 1]);
      const yy = dot(yHist[yHist.length - 1], yHist[yHist.length - 1]);
      scale = sy / Math.max(yy, 1e-12);
    }
    const r = q.map((v) => v * scale);
    for (let i = 0; i < sHist.length; i++) {
      const beta = rhoHist[i] * dot(yHist[i], r);
      for (let d = 0; d < dim; d++) r[d] += (alpha[i] - beta) * sHist[i][d];
    }
    let dir = r.map((v) => -v);
    let slope = dot(g, dir);
    if (slope >= -1e-16) {
      dir = g.map((v) => -v);
      slope = -dot(g, g);
    }
    let step = 1;
    let xNew = x.slice();
    let fNew = fx;
    let accepted = false;
    for (let ls = 0; ls < 25; ls++) {
      xNew = clamp(x.map((v, d) => v + step * dir[d]));
      fNew = fitness(xNew);
      if (fNew <= fx + 1e-4 * step * slope) {
        accepted = true;
        break;
      }
      step *= 0.5;
    }
    if (!accepted) break;
    const gNew = gradient(xNew);
    const s = xNew.map((v, d) => v - x[d]);
    const yd = gNew.map((v, d) => v - g[d]);
    const sy = dot(s, yd);
    if (sy > 1e-12) {
      sHist.push(s);
      yHist.push(yd);
      rhoHist.push(1 / sy);
      if (sHist.length > 8) {
        sHist.shift();
        yHist.shift();
        rhoHist.shift();
      }
    }
    const change = Math.max(...xNew.map((v, d) => Math.abs(v - x[d])));
    x = xNew;
    fx = fNew;
    g = gNew;
    bestTheta = x.slice();
    history.push({ iteration: history.length + 1, value: fx, stage: "L-BFGS" });
    if (change < 1e-6) break;
  }
  const lbfgsEvaluations = evals - lbStart;
  const { gamma } = blobDensity(bestTheta);
  return {
    nx,
    ny,
    best: Math.min(fx, globalBest),
    globalBest,
    globalHistory,
    localHistory: history.filter((h) => h.stage !== "MIGA" && h.stage !== "PSO").map((h) => h.value),
    theta: bestTheta.map((v) => Math.round(v * 1000) / 1000),
    density: Array.from(gamma, (v) => Math.round(v * 1000) / 1000),
    globalEvaluations: params.islands * params.popPerIsland * (params.generations + 1) + (params.usePso ? params.psoSwarm * (params.psoIterations + 1) : 0),
    localEvaluations: nmEvaluations + lbfgsEvaluations,
    nmEvaluations,
    lbfgsEvaluations,
    islands: params.islands,
    generations: params.generations,
    usedPso: params.usePso,
    elapsedMs: Date.now() - t0,
    improved: Math.min(fx, globalBest) < globalBest * 0.98,
    history,
  };
}

function dot(a: number[], b: number[]): number {
  let s = 0;
  for (let i = 0; i < a.length; i++) s += a[i] * b[i];
  return s;
}
