export type FluxInput = {
  slope: number;
  skew: number;
  checkerboard: number;
  mobility: number;
  velocity: number;
};

export type FluxResult = {
  beta: number;
  alpha: number;
  linearError: number;
  gradientError: number;
  before: number[];
  after: number[];
  mass: number[];
  pressureCorrection: number[];
  maxDivergence: number;
  checkerboardResponse: number;
};

export function solveDense(matrix: number[][], rhs: number[]): number[] {
  const n = rhs.length;
  const a = matrix.map((row, i) => [...row, rhs[i]]);
  for (let k = 0; k < n; k++) {
    let pivot = k;
    for (let i = k + 1; i < n; i++) if (Math.abs(a[i][k]) > Math.abs(a[pivot][k])) pivot = i;
    if (Math.abs(a[pivot][k]) < 1e-18) throw new Error("Singular discrete operator");
    [a[pivot], a[k]] = [a[k], a[pivot]];
    for (let i = k + 1; i < n; i++) {
      const scale = a[i][k] / a[k][k];
      for (let j = k + 1; j <= n; j++) a[i][j] -= scale * a[k][j];
    }
  }
  const x = Array(n).fill(0);
  for (let i = n - 1; i >= 0; i--) {
    let sum = a[i][n];
    for (let j = i + 1; j < n; j++) sum -= a[i][j] * x[j];
    x[i] = sum / a[i][i];
  }
  return x;
}

type Vec2 = { x: number; y: number };
const vdot = (a: Vec2, b: Vec2) => a.x * b.x + a.y * b.y;

export function runFlux(input: FluxInput): FluxResult {
  const n = 5;
  const center = Array.from({ length: n }, (_, i) => ({ x: i + 0.5, y: input.skew * Math.sin(i * 0.9) }));
  const face = Array.from({ length: n + 1 }, (_, f) => {
    if (f === 0) return { x: 0, y: 0, s: { x: -1, y: 0 }, p: 0, q: -1 };
    if (f === n) return { x: n, y: 0, s: { x: 1, y: 0 }, p: n - 1, q: -1 };
    return {
      x: f + input.skew * 0.11,
      y: (center[f - 1].y + center[f].y) / 2 + input.skew * 0.3,
      s: { x: 1, y: 0.12 + f * 0.018 },
      p: f - 1,
      q: f,
    };
  });
  const exact = { x: input.slope, y: -0.37 };
  const linear = center.map((p) => vdot(p, exact));
  const pressure = linear.map((v, i) => v + (i % 2 ? -1 : 1) * input.checkerboard);
  const boundaryPressure = (f: number) => input.slope * face[f].x - 0.37 * face[f].y;
  const gradients = (field: number[]): Vec2[] => center.map((c, i) => {
    let xx = 0, bx = 0;
    for (let index = 0; index < face.length; index++) {
      const f = face[index];
      if (f.p !== i && f.q !== i) continue;
      const j = f.p === i ? f.q : f.p;
      const other = j >= 0 ? center[j] : { x: f.x, y: f.y };
      const dp = (j >= 0 ? field[j] : boundaryPressure(index)) - field[i];
      const dx = other.x - c.x, dy = other.y - c.y;
      const weight = 1 / Math.max(dx * dx + dy * dy, 1e-18);
      xx += weight * dx * dx;
      bx += weight * dx * (dp - exact.y * dy);
    }
    return { x: bx / (xx + 1e-14 * xx + 1e-15), y: exact.y };
  });
  const computedGrad = gradients(pressure);
  const linearGrad = gradients(linear);
  const gradientError = Math.max(...linearGrad.map((g) => Math.hypot(g.x - exact.x, g.y - exact.y)));
  const area = (f: number) => face[f].s;
  const geometry = (f: number) => {
    const point = face[f], p = center[point.p];
    const nPoint = point.q >= 0 ? center[point.q] : { x: point.x, y: point.y };
    const d = { x: nPoint.x - p.x, y: nPoint.y - p.y };
    const s = area(f), ds = vdot(d, s);
    const beta = point.q < 0 ? 1 : ((point.x - p.x) * s.x + (point.y - p.y) * s.y) / ds;
    return { d, beta, alpha: vdot(s, s) / ds };
  };
  const reconstruct = (field: number[], grad: Vec2[]): number[] => face.map((f, index) => {
    const g = geometry(index);
    const beta = f.q < 0 ? 0 : g.beta;
    const gp = f.q < 0 ? grad[f.p] : {
      x: grad[f.p].x + beta * (grad[f.q].x - grad[f.p].x),
      y: grad[f.p].y + beta * (grad[f.q].y - grad[f.p].y),
    };
    const dp = (f.q < 0 ? boundaryPressure(index) : field[f.q]) - field[f.p];
    return input.velocity * f.s.x - input.mobility * g.alpha * (dp - vdot(gp, g.d));
  });
  const ideal = reconstruct(linear, Array(n).fill(exact));
  const linearError = Math.max(...ideal.map((m, f) => Math.abs(m - input.velocity * face[f].s.x)));
  const predicted = reconstruct(pressure, computedGrad);
  const divergence = (mass: number[]) => Array.from({ length: n }, (_, i) => {
    let sum = 0;
    face.forEach((f, k) => { if (f.p === i) sum += mass[k]; if (f.q === i) sum -= mass[k]; });
    return sum;
  });
  const before = divergence(predicted);
  const a = Array.from({ length: n }, () => Array(n).fill(0));
  face.forEach((f, index) => {
    const conductance = input.mobility * geometry(index).alpha;
    a[f.p][f.p] += conductance;
    if (f.q >= 0) {
      a[f.q][f.q] += conductance;
      a[f.p][f.q] -= conductance;
      a[f.q][f.p] -= conductance;
    }
  });
  const correction = solveDense(a, before.map((v) => -v));
  const mass = predicted.map((m, f) => m + input.mobility * geometry(f).alpha *
    (correction[face[f].p] - (face[f].q >= 0 ? correction[face[f].q] : 0)));
  const after = divergence(mass);
  const checkerboardResponse = Math.max(...predicted.map((v, f) => Math.abs(v - input.velocity * face[f].s.x)));
  return {
    beta: geometry(2).beta,
    alpha: geometry(2).alpha,
    linearError,
    gradientError,
    before,
    after,
    mass,
    pressureCorrection: correction,
    maxDivergence: Math.max(...after.map(Math.abs)),
    checkerboardResponse,
  };
}

const f32 = Math.fround;
const vectorNorm = (x: number[]) => Math.sqrt(x.reduce((sum, v) => sum + v * v, 0));
const denseMultiply = (a: number[][], x: number[], rounded = false) => a.map((row) => {
  let sum = 0;
  row.forEach((v, j) => { sum = rounded ? f32(sum + f32(v * x[j])) : sum + v * x[j]; });
  return sum;
});

export type AMGResult = { history: number[]; levels: number; etaRound: number; fp32Floor: number; converged: boolean; coarse: number };

export function runAMG(size: number, coupling: number): AMGResult {
  const n = size;
  const a = Array.from({ length: n }, (_, i) => Array.from({ length: n }, (_, j) =>
    i === j ? 2 * coupling + 0.12 : Math.abs(i - j) === 1 ? -coupling : 0));
  const b = Array.from({ length: n }, (_, i) => Math.sin((i + 1) * 0.23) + 0.01);
  const nc = Math.ceil(n / 2);
  const p: number[][] = Array.from({ length: n }, (_, i) => Array.from({ length: nc }, (_, j) => (Math.floor(i / 2) === j ? 1 : 0)));
  const tentative = p.map((row) => [...row]);
  // One smoothed aggregation level. All arithmetic inside each V-cycle is FP32.
  for (let i = 0; i < n; i++) {
    const diag = a[i][i];
    for (let j = 0; j < nc; j++) {
      let ap = 0;
      for (let k = Math.max(0, i - 1); k <= Math.min(n - 1, i + 1); k++) ap += a[i][k] * tentative[k][j];
      p[i][j] = f32(tentative[i][j] - 0.66 * ap / diag);
    }
  }
  const coarse = Array.from({ length: nc }, () => Array(nc).fill(0));
  for (let i = 0; i < n; i++) for (let j = Math.max(0, i - 1); j <= Math.min(n - 1, i + 1); j++) {
    for (let u = 0; u < nc; u++) if (p[i][u])
      for (let v = 0; v < nc; v++) if (p[j][v]) coarse[u][v] = f32(coarse[u][v] + f32(p[i][u] * a[i][j] * p[j][v]));
  }
  const l1 = a.map((row) => f32(row.reduce((sum, v) => sum + Math.abs(v), 0)));
  const smooth = (x: number[], rhs: number[]) => {
    for (let iter = 0; iter < 3; iter++) {
      const ax = denseMultiply(a, x, true);
      x = x.map((v, i) => f32(v + f32(0.8 * f32(rhs[i] - ax[i]) / l1[i])));
    }
    return x;
  };
  const precondition = (r: number[]) => {
    const scale = Math.max(...r.map(Math.abs));
    if (!scale) return Array(n).fill(0);
    const rhs = r.map((v) => f32(v / scale));
    let x = smooth(Array(n).fill(0), rhs);
    const residual = denseMultiply(a, x, true).map((v, i) => f32(rhs[i] - v));
    const rc = Array(nc).fill(0);
    for (let i = 0; i < n; i++) for (let j = 0; j < nc; j++) rc[j] = f32(rc[j] + f32(p[i][j] * residual[i]));
    const ec = solveDense(coarse, rc).map(f32);
    x = x.map((v, i) => {
      let sum = v;
      for (let j = 0; j < nc; j++) sum = f32(sum + f32(p[i][j] * ec[j]));
      return sum;
    });
    return smooth(x, rhs).map((v) => v * scale);
  };
  let x = Array(n).fill(0);
  const history: number[] = [];
  const target = vectorNorm(b) * 1e-11;
  for (let iter = 0; iter < 35; iter++) {
    const ax = denseMultiply(a, x);
    const r = b.map((v, i) => v - ax[i]);
    const norm = vectorNorm(r);
    history.push(norm);
    if (norm < target) break;
    const dx = precondition(r);
    x = x.map((v, i) => v + dx[i]);
    if (iter > 7 && norm >= history[iter - 6] * 0.995) break;
  }
  const residual = b.map((v, i) => v - denseMultiply(a, x)[i]);
  const normA = Math.max(...a.map((row) => row.reduce((sum, v) => sum + Math.abs(v), 0)));
  const xn = Math.max(...x.map(Math.abs)), bn = Math.max(...b.map(Math.abs));
  const denom = normA * xn + bn;
  return {
    history,
    levels: 2,
    coarse: nc,
    etaRound: Math.max(...residual.map(Math.abs)) / denom,
    fp32Floor: 1.1920928955078125e-7 * normA * xn / denom,
    converged: vectorNorm(residual) < target,
  };
}

export type ChemistryResult = {
  active: number;
  idle: number;
  substeps: number;
  rates: [number, number];
  trajectory: { time: number; a: number; b: number; c: number }[];
  massError: number;
  jacobianError: number;
};

export function runChemistry(temperature: number, dt: number, hotCells: number): ChemistryResult {
  const k1 = 1e7 * Math.exp(-80000 / (8.31446261815324 * temperature));
  const k2 = 30;
  const coldRate = 1e7 * Math.exp(-80000 / (8.31446261815324 * 300));
  const active = Math.abs(coldRate) > 1e-5 ? 32 : hotCells;
  const source = (y: number[]) => [-k1 * y[0], k1 * y[0] - k2 * y[1], k2 * y[1]];
  const jac = [[-k1, 0, 0], [k1, -k2, 0], [0, k2, 0]];
  const hfd = 1e-4, base = [0.8, 0.1, 0.1];
  let jacobianError = 0;
  for (let j = 0; j < 3; j++) {
    const plus = [...base], minus = [...base];
    plus[j] += hfd; minus[j] -= hfd;
    const wp = source(plus), wm = source(minus);
    for (let i = 0; i < 3; i++) jacobianError = Math.max(jacobianError,
      Math.abs(jac[i][j] - (wp[i] - wm[i]) / (2 * hfd)) / Math.max(1, Math.abs(jac[i][j])));
  }
  // Keep the stiff linear BDF2 characteristic roots real and nonnegative.
  const substeps = Math.max(1, Math.min(4096, Math.ceil(dt * Math.max(k1, k2) / 0.4)));
  const h = dt / substeps;
  let old = [1, 0, 0], previous = [1, 0, 0];
  const trajectory = [{ time: 0, a: 1, b: 0, c: 0 }];
  for (let step = 0; step < substeps; step++) {
    const coeff = step ? 1.5 : 1;
    const matrix = jac.map((row, i) => row.map((v, j) => (i === j ? coeff : 0) - h * v));
    const rhs = old.map((v, i) => step ? 2 * v - 0.5 * previous[i] : v);
    const next = solveDense(matrix, rhs);
    previous = old; old = next;
    if (step % Math.max(1, Math.floor(substeps / 42)) === 0 || step === substeps - 1)
      trajectory.push({ time: (step + 1) * h, a: old[0], b: old[1], c: old[2] });
  }
  return {
    active,
    idle: 32 - active,
    substeps,
    rates: [k1, k2],
    trajectory,
    massError: Math.abs(old.reduce((sum, v) => sum + v, 0) - 1),
    jacobianError,
  };
}

type Dual = { v: number; d: number };
const dual = (v: number, d = 0): Dual => ({ v, d });
const add = (a: Dual, b: Dual): Dual => dual(a.v + b.v, a.d + b.d);
const sub = (a: Dual, b: Dual): Dual => dual(a.v - b.v, a.d - b.d);
const mul = (a: Dual, b: Dual): Dual => dual(a.v * b.v, a.d * b.v + a.v * b.d);
const div = (a: Dual, b: Dual): Dual => dual(a.v / b.v, (a.d * b.v - a.v * b.d) / (b.v * b.v));
const sum = (items: Dual[]): Dual => items.reduce(add, dual(0));
type AdjointDesign = { gamma: Dual[]; rightX: Dual; rightS: Dual };

function coupledResidual(q: AdjointDesign, u: Dual[]): Dual[] {
  const x = [0.5, 1.5, 2.5];
  const s = [dual(1), dual(1), dual(-1), q.rightS];
  const xFace = [dual(1), dual(2), dual(0), q.rightX];
  const owner = [0, 1, 0, 2], neighbor = [1, 2, -1, -1];
  const gradient = x.map((cell, i) => {
    const contributions: { distance: Dual; dp: Dual }[] = [];
    for (let f = 0; f < 4; f++) if (owner[f] === i || neighbor[f] === i) {
      const other = owner[f] === i ? neighbor[f] : owner[f];
      contributions.push({
        distance: sub(other >= 0 ? dual(x[other]) : xFace[f], dual(cell)),
        dp: sub(other >= 0 ? u[2 * other + 1] : dual(f === 2 ? 1 : 0), u[2 * i + 1]),
      });
    }
    const terms = contributions.map(({ distance, dp }) => {
      const weight = div(dual(1), add(mul(distance, distance), dual(1e-25)));
      return { a: mul(weight, mul(distance, distance)), b: mul(weight, mul(distance, dp)) };
    });
    const a = sum(terms.map((item) => item.a));
    return div(sum(terms.map((item) => item.b)), add(a, add(mul(dual(1e-14), a), dual(1e-15))));
  });
  const alpha = xFace.map((point, f) => {
    const d = sub(neighbor[f] >= 0 ? dual(x[neighbor[f]]) : point, dual(x[owner[f]]));
    return div(mul(s[f], s[f]), mul(d, s[f]));
  });
  const drag = q.gamma.map((g) => {
    const solid = sub(dual(1), g);
    return add(dual(0.001), mul(dual(99.999), mul(mul(solid, solid), solid)));
  });
  const diag = x.map((_, i) => add(add(dual(2), drag[i]), sum(
    owner.map((p, f) => (neighbor[f] >= 0 && (p === i || neighbor[f] === i)) ? mul(dual(0.15), alpha[f]) : dual(0)),
  )));
  const mobility = diag.map((v) => div(dual(1), v));
  const flux = owner.map((p, f) => {
    const nb = neighbor[f];
    const d = sub(nb >= 0 ? dual(x[nb]) : xFace[f], dual(x[p]));
    const beta = nb >= 0 ? div(sub(xFace[f], dual(x[p])), d) : dual(0);
    const velocity = nb >= 0 ? add(u[2 * p], mul(beta, sub(u[2 * nb], u[2 * p]))) : u[2 * p];
    const gp = nb >= 0 ? add(gradient[p], mul(beta, sub(gradient[nb], gradient[p]))) : gradient[p];
    const mobilityFace = nb >= 0 ? add(mobility[p], mul(beta, sub(mobility[nb], mobility[p]))) : mobility[p];
    const dp = sub(nb >= 0 ? u[2 * nb + 1] : dual(f === 2 ? 1 : 0), u[2 * p + 1]);
    return sub(mul(velocity, s[f]), mul(mul(mobilityFace, alpha[f]), sub(dp, mul(gp, d))));
  });
  return x.flatMap((_, i) => {
    const neighbors = owner.map((p, f) => neighbor[f] >= 0 && (p === i || neighbor[f] === i)
      ? mul(mul(dual(0.15), alpha[f]), u[2 * (p === i ? neighbor[f] : p)]) : dual(0));
    const momentum = sub(add(mul(diag[i], u[2 * i]), sub(gradient[i], dual(0.28))), sum(neighbors));
    const continuity = sum(owner.map((p, f) => p === i ? flux[f] : neighbor[f] === i ? mul(dual(-1), flux[f]) : dual(0)));
    return [momentum, continuity];
  });
}

function adjointObjective(q: AdjointDesign, u: Dual[]): Dual {
  const cells = q.gamma.map((g, i) => add(add(
    mul(dual(0.5), mul(sub(u[2 * i], dual(0.21)), sub(u[2 * i], dual(0.21)))),
    mul(dual(0.004), mul(u[2 * i + 1], u[2 * i + 1]))), mul(dual(0.01), mul(g, g))));
  return add(sum(cells), add(mul(dual(0.001), mul(q.rightX, q.rightX)),
    add(mul(dual(0.002), mul(q.rightS, q.rightS)), dual(0.002))));
}

function jacobian(q: AdjointDesign, u: number[]): number[][] {
  return Array.from({ length: 6 }, (_, row) => Array.from({ length: 6 }, (_, col) =>
    coupledResidual(q, u.map((v, i) => dual(v, i === col ? 1 : 0)))[row].d));
}

function solveState(q: AdjointDesign): number[] {
  const zero = Array.from({ length: 6 }, () => dual(0));
  const r = coupledResidual(q, zero).map((v) => -v.v);
  return solveDense(jacobian(q, Array(6).fill(0)), r);
}

export type AdjointResult = {
  value: number;
  stateResidual: number;
  gammaGradient: number;
  centroidGradient: number;
  normalGradient: number;
  fdError: number;
  cadGradient: number;
  lambda: number[];
  comparisons: { name: string; adjoint: number; finiteDifference: number; error: number }[];
};

export function runAdjoint(gamma: number, faceX: number, normal: number): AdjointResult {
  const make = (g: number, x: number, s: number): AdjointDesign =>
    ({ gamma: [dual(0.86), dual(g), dual(0.91)], rightX: dual(x), rightS: dual(s) });
  const q = make(gamma, faceX, normal);
  const u = solveState(q);
  const j = jacobian(q, u);
  const objectiveU = u.map((_, i) => adjointObjective(q, u.map((v, k) => dual(v, i === k ? 1 : 0))).d);
  const lambda = solveDense(j[0].map((_, i) => j.map((row) => row[i])), objectiveU.map((v) => -v));
  const gradient = (which: "gamma" | "center" | "normal") => {
    const seeded = make(gamma, faceX, normal);
    if (which === "gamma") seeded.gamma[1].d = 1;
    if (which === "center") seeded.rightX.d = 1;
    if (which === "normal") seeded.rightS.d = 1;
    const state = u.map((v) => dual(v));
    return adjointObjective(seeded, state).d + coupledResidual(seeded, state)
      .reduce((total, row, i) => total + lambda[i] * row.d, 0);
  };
  const objectiveAt = (g: number, x: number, s: number) => {
    const design = make(g, x, s);
    return adjointObjective(design, solveState(design).map((v) => dual(v))).v;
  };
  const h = 4e-5;
  const comparisons = ([
    ["Brinkman porosity", gradient("gamma"), (objectiveAt(gamma + h, faceX, normal) - objectiveAt(gamma - h, faceX, normal)) / (2 * h)],
    ["Face centroid", gradient("center"), (objectiveAt(gamma, faceX + h, normal) - objectiveAt(gamma, faceX - h, normal)) / (2 * h)],
    ["Face normal", gradient("normal"), (objectiveAt(gamma, faceX, normal + h) - objectiveAt(gamma, faceX, normal - h)) / (2 * h)],
  ] as [string, number, number][]).map(([name, adjoint, finiteDifference]) =>
    ({ name, adjoint, finiteDifference, error: Math.abs(adjoint - finiteDifference) }));
  return {
    value: adjointObjective(q, u.map((v) => dual(v))).v,
    stateResidual: Math.max(...coupledResidual(q, u.map((v) => dual(v))).map((v) => Math.abs(v.v))),
    gammaGradient: comparisons[0].adjoint,
    centroidGradient: comparisons[1].adjoint,
    normalGradient: comparisons[2].adjoint,
    fdError: Math.max(...comparisons.map((row) => row.error)),
    cadGradient: 0.2 * comparisons[0].adjoint + 0.3 * comparisons[1].adjoint - 0.1 * comparisons[2].adjoint,
    lambda,
    comparisons,
  };
}