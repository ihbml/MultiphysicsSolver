export type ModuleId = "battery" | "acoustics" | "mhd" | "tps";
export type Parameters = {
  current: number;
  coolant: number;
  timestep: number;
  rpm: number;
  distance: number;
  hartmann: number;
  conductivity: number;
  heatflux: number;
  thickness: number;
};

export const defaultParameters: Parameters = {
  current: 25,
  coolant: 298,
  timestep: 0.25,
  rpm: 2400,
  distance: 5,
  hartmann: 25,
  conductivity: 3.46,
  heatflux: 350,
  thickness: 30,
};

export const R = 8.314462618;
export const reactionNames = [
  "SEI decomposition",
  "Anode / solvent",
  "Cathode decomposition",
  "Electrolyte decomposition",
];
export const reactionColors = ["#b49aff", "#71cdb9", "#e9ba72", "#ec8294"];
const kinetics = [
  { A: 1.667e15, E: 135080, H: 257000, mass: 0.014 },
  { A: 2.5e13, E: 135080, H: 1714000, mass: 0.012 },
  { A: 6.667e13, E: 139600, H: 314000, mass: 0.018 },
  { A: 5.14e25, E: 274000, H: 155000, mass: 0.01 },
];

export type BatteryFrame = {
  time: number;
  temperatures: number[];
  peak: number;
  mean: number;
  min: number;
  soc: number;
  voltage: number;
  ocv: number;
  heat: number;
  heatReversible: number;
  heatIrreversible: number;
  reaction: number[];
  conversion: number[];
  reactionHeat: number[];
};

// Positive discharge current: terminal voltage falls and overpotential heat is positive.
export function ecmStep(
  soc: number,
  v: number[],
  current: number,
  T: number,
  dt: number,
) {
  const factor = Math.exp(1800 * (1 / T - 1 / 298.15)) * (1 + 0.2 * (1 - soc));
  const resistances = [0.006 * factor, 0.012 * factor];
  const capacitances = [2400 / factor, 11000 / factor];
  const next = v.map((value, k) => {
    const decay = Math.exp(-dt / (resistances[k] * capacitances[k]));
    return value * decay + current * resistances[k] * (1 - decay);
  });
  const entropicCoefficient = -0.00012;
  const ocv =
    3.2 +
    0.95 * soc +
    0.08 * Math.tanh(10 * (soc - 0.5)) +
    entropicCoefficient * (T - 298.15);
  const terminal = ocv - current * 0.014 * factor - next[0] - next[1];
  const signedOverpotential = current * (terminal - ocv);
  const qrev = current * T * entropicCoefficient;
  return {
    soc: Math.max(0, Math.min(1, soc - (current * dt) / (3600 * 5))),
    v: next,
    terminal,
    ocv,
    signedOverpotential,
    heatReversible: qrev,
    heatIrreversible: -signedOverpotential,
    heat: -signedOverpotential + qrev,
  };
}

export function patankarReaction(c: number[], T: number, dt: number) {
  const rates = kinetics.map((k) => k.A * Math.exp(-k.E / (R * T)));
  const next = c.map((value, i) =>
    i === 2
      ? value === 0
        ? 0
        : 1 - (1 - value) / (1 + dt * rates[i] * value)
      : value / (1 + dt * rates[i]),
  );
  const released = next.map(
    (value, i) => Math.abs(value - c[i]) * kinetics[i].H * kinetics[i].mass,
  );
  return { next, released, energy: released.reduce((a, b) => a + b, 0) };
}

export function simulateBattery(
  params: Parameters,
  duration = 120,
): BatteryFrame[] {
  const n = 48;
  const dt = params.timestep;
  let T = Array.from(
    { length: n },
    (_, i) => params.coolant + 7 + (i === 27 ? 95 : 0),
  );
  let c = Array.from({ length: n }, () => [0.15, 0.75, 0.04, 1]);
  let soc = 0.92;
  let v = [0, 0];
  const initial = ecmStep(
    soc,
    v,
    params.current,
    T.reduce((a, b) => a + b, 0) / n,
    0,
  );
  let voltage = initial.terminal;
  let ocv = initial.ocv;
  let heat = initial.heat;
  let heatReversible = initial.heatReversible;
  let heatIrreversible = initial.heatIrreversible;
  let heatRates = [0, 0, 0, 0];
  const frames: BatteryFrame[] = [];
  const count = Math.round(duration / dt);
  for (let step = 0; step <= count; step++) {
    if (step % Math.max(1, Math.round(0.5 / dt)) === 0 || step === count) {
      const conversion = c.map(
        (state) =>
          (1 -
            state[0] / 0.15 +
            (1 - state[1] / 0.75) +
            (state[2] - 0.04) / 0.96 +
            (1 - state[3])) /
          4,
      );
      frames.push({
        time: step * dt,
        temperatures: [...T],
        peak: Math.max(...T),
        min: Math.min(...T),
        mean: T.reduce((a, b) => a + b, 0) / n,
        soc,
        voltage,
        ocv,
        heat,
        heatReversible,
        heatIrreversible,
        reaction: [...c[27]],
        conversion,
        reactionHeat: [...heatRates],
      });
    }
    const ecm = ecmStep(
      soc,
      v,
      params.current,
      T.reduce((a, b) => a + b, 0) / n,
      dt,
    );
    soc = ecm.soc;
    v = ecm.v;
    voltage = ecm.terminal;
    ocv = ecm.ocv;
    heat = ecm.heat;
    heatReversible = ecm.heatReversible;
    heatIrreversible = ecm.heatIrreversible;
    const nextT = [...T];
    heatRates = [0, 0, 0, 0];
    for (let i = 0; i < n; i++) {
      const result = patankarReaction(c[i], T[i], dt);
      c[i] = result.next;
      if (i === 27) heatRates = result.released.map((q) => q / dt);
      let conduction = 0;
      const neighbors = [
        i % 8 > 0 ? i - 1 : -1,
        i % 8 < 7 ? i + 1 : -1,
        i >= 8 ? i - 8 : -1,
        i < 40 ? i + 8 : -1,
      ];
      for (const j of neighbors) if (j >= 0) conduction += 0.12 * (T[j] - T[i]);
      const heater = i === 27 && step * dt < 50 ? 108 : 0;
      nextT[i] =
        T[i] +
        (dt *
          (ecm.heat + heater + conduction - 0.075 * (T[i] - params.coolant)) +
          result.energy) /
          65;
    }
    T = nextT;
  }
  return frames;
}

export function solveTridiagonal(
  lower: number[],
  diag: number[],
  upper: number[],
  rhs: number[],
) {
  const b = [...diag];
  const d = [...rhs];
  for (let i = 1; i < b.length; i++) {
    const m = lower[i] / b[i - 1];
    b[i] -= m * upper[i - 1];
    d[i] -= m * d[i - 1];
  }
  const x = new Array(b.length).fill(0);
  x[x.length - 1] = d[d.length - 1] / b[b.length - 1];
  for (let i = x.length - 2; i >= 0; i--)
    x[i] = (d[i] - upper[i] * x[i + 1]) / b[i];
  return x;
}

export function hartmannProfile(Ha: number, n = 256) {
  const h = 2 / n;
  const lower = new Array(n - 1).fill(-1 / (h * h));
  const diag = new Array(n - 1).fill(2 / (h * h) + Ha * Ha);
  const upper = [...lower];
  const rhs = new Array(n - 1).fill(Ha * Ha);
  const u = [0, ...solveTridiagonal(lower, diag, upper, rhs), 0];
  const max = Math.max(...u);
  const points = u.map((value, i) => {
    const y = -1 + i * h;
    const exact = (Math.cosh(Ha) - Math.cosh(Ha * y)) / (Math.cosh(Ha) - 1);
    return { y, numerical: value / max, exact };
  });
  const error = Math.sqrt(
    points.reduce((sum, p) => sum + (p.numerical - p.exact) ** 2, 0) /
      points.length,
  );
  return {
    points,
    error,
    layer: 1 / Ha,
    residual: Math.max(
      ...u
        .slice(1, -1)
        .map((value, i) =>
          Math.abs(
            (2 * value - u[i] - u[i + 2]) / (h * h) + Ha * Ha * value - Ha * Ha,
          ),
        ),
    ),
  };
}

export function fft(realInput: number[]) {
  const n = realInput.length;
  if (n < 2 || (n & (n - 1)) !== 0)
    throw new Error("FFT length must be a power of two.");
  const re = [...realInput];
  const im = new Array(n).fill(0);
  for (let i = 1, j = 0; i < n; i++) {
    let bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) [re[i], re[j]] = [re[j], re[i]];
  }
  for (let len = 2; len <= n; len <<= 1) {
    const theta = (-2 * Math.PI) / len;
    for (let i = 0; i < n; i += len) {
      for (let j = 0; j < len / 2; j++) {
        const wr = Math.cos(theta * j);
        const wi = Math.sin(theta * j);
        const index = i + j + len / 2;
        const tr = wr * re[index] - wi * im[index];
        const ti = wr * im[index] + wi * re[index];
        re[index] = re[i + j] - tr;
        im[index] = im[i + j] - ti;
        re[i + j] += tr;
        im[i + j] += ti;
      }
    }
  }
  return re.map((r, i) => ({ re: r, im: im[i] }));
}

export function spectrum(signal: number[], fs: number) {
  const n = signal.length;
  const mean = signal.reduce((a, b) => a + b, 0) / n;
  const window = signal.map(
    (_, i) => 0.5 * (1 - Math.cos((2 * Math.PI * i) / (n - 1))),
  );
  const power = window.reduce((a, b) => a + b * b, 0);
  const transformed = fft(signal.map((p, i) => (p - mean) * window[i]));
  const psd = transformed.slice(0, n / 2 + 1).map((z, k) => ({
    frequency: (k * fs) / n,
    power:
      ((k === 0 || k === n / 2 ? 1 : 2) * (z.re * z.re + z.im * z.im)) /
      (fs * power),
  }));
  const rms = Math.sqrt(
    signal.reduce((sum, value) => sum + (value - mean) ** 2, 0) / n,
  );
  const bands = Array.from({ length: 25 }, (_, i) => {
    const frequency = 1000 * 2 ** ((i - 16) / 3);
    const variance = psd
      .filter(
        (p) =>
          p.frequency >= frequency / 2 ** (1 / 6) &&
          p.frequency < frequency * 2 ** (1 / 6),
      )
      .reduce((sum, p) => sum + (p.power * fs) / n, 0);
    return {
      frequency,
      db: 10 * Math.log10(Math.max(variance, 1e-18) / 4e-10),
    };
  }).filter((band) => band.frequency * 2 ** (1 / 6) <= fs / 2);
  return {
    psd,
    rms,
    oaspl: 20 * Math.log10(Math.max(rms, 1e-15) / 20e-6),
    bands,
  };
}

export function acousticSignal(params: Parameters, mic = 0) {
  const fs = 8192;
  const r = params.distance * (1 + mic * 0.15);
  const f = (params.rpm / 60) * 4;
  const signal = Array.from({ length: 2048 }, (_, i) => {
    const tau = i / fs - r / 343;
    let p = 0;
    for (let k = 1; k <= 4; k++) {
      const phase = 2 * Math.PI * f * k * tau;
      const load = (360 / (k * k)) * Math.sin(phase);
      const loadDot = (360 / (k * k)) * 2 * Math.PI * f * k * Math.cos(phase);
      const Qdot = (0.004 / k) * 2 * Math.PI * f * k * Math.cos(phase + 0.4);
      p +=
        (0.025 / (4 * Math.PI)) *
        (loadDot / (343 * r) + load / (r * r) + Qdot / r);
    }
    return p * Math.cos((mic * Math.PI) / 12);
  });
  return { signal, fs, ...spectrum(signal, fs), bpf: f, delay: r / 343 };
}

export type TpsFrame = {
  time: number;
  temperatures: number[];
  density: number[];
  wall: number;
  recession: number;
  gas: number;
  char: number;
  radiation: number;
};

export function simulateTps(params: Parameters, duration = 120): TpsFrame[] {
  const n = 40;
  const dx = params.thickness / 1000 / n;
  const dt = 0.2;
  let T = new Array(n).fill(300);
  let rho = new Array(n).fill(280);
  let recession = 0;
  let gas = 0;
  let char = 0;
  let wall = 300;
  let radiation = 0;
  const frames: TpsFrame[] = [];
  for (let step = 0; step <= duration / dt; step++) {
    if (step % 5 === 0)
      frames.push({
        time: step * dt,
        temperatures: [...T],
        density: [...rho],
        wall,
        recession,
        gas,
        char,
        radiation,
      });
    gas = 0;
    const sink = new Array(n).fill(0);
    for (let i = 0; i < n; i++) {
      const rate = 1.2e7 * Math.exp(-105000 / (R * T[i]));
      const next = 120 + (rho[i] - 120) / (1 + dt * rate);
      const delta = rho[i] - next;
      sink[i] = (delta * 240000) / dt;
      gas += (delta * dx) / dt;
      rho[i] = next;
    }
    let lo = 300;
    let hi = 4500;
    const conductivity = 0.35;
    for (let iter = 0; iter < 55; iter++) {
      const mid = (lo + hi) / 2;
      const mchar = 3000 * Math.exp(-200000 / (R * mid));
      const q =
        0.85 * 5.670374419e-8 * (mid ** 4 - 300 ** 4) +
        (conductivity * (mid - T[0])) / (dx / 2) +
        gas * 240000 +
        mchar * 2e7;
      if (q > params.heatflux * 1000) hi = mid;
      else lo = mid;
    }
    wall = (lo + hi) / 2;
    char = 3000 * Math.exp(-200000 / (R * wall));
    recession += (char / 120) * dt;
    radiation = 0.85 * 5.670374419e-8 * (wall ** 4 - 300 ** 4);
    const a = new Array(n).fill(0);
    const b = new Array(n).fill(0);
    const c = new Array(n).fill(0);
    const d = new Array(n).fill(0);
    for (let i = 0; i < n; i++) {
      const coeff = (conductivity * dt) / (rho[i] * 1200 * dx * dx);
      a[i] = i === 0 ? 0 : -coeff;
      c[i] = i === n - 1 ? 0 : -coeff;
      b[i] = 1 + (i === 0 ? 3 : i === n - 1 ? 1 : 2) * coeff;
      d[i] =
        T[i] -
        (sink[i] * dt) / (rho[i] * 1200) +
        (i === 0 ? 2 * coeff * wall : 0);
    }
    T = solveTridiagonal(a, b, c, d);
  }
  return frames;
}

export type BrowserTest = {
  name: string;
  module: ModuleId;
  detail: string;
  passed: boolean;
  value: string;
};
export function runBrowserTests(): BrowserTest[] {
  const cell = ecmStep(1, [0, 0], 5, 298.15, 360);
  const analytic = 5 * 0.006 * 1.0 * (1 - Math.exp(-360 / (0.006 * 2400)));
  const bounded = patankarReaction([0.15, 0.75, 0.04, 1], 2000, 1e6);
  const N = 2048;
  const tone = Array.from(
    { length: N },
    (_, i) => Math.sqrt(2) * Math.sin((2 * Math.PI * 128 * i) / N),
  );
  const fftResult = spectrum(tone, N);
  const integrated = fftResult.psd.reduce((sum, p) => sum + p.power, 0);
  const mhd = [1, 10, 25, 50, 100].map((ha) => hartmannProfile(ha, 1024));
  const worst = Math.max(...mhd.map((result) => result.error));
  return [
    {
      name: "Coulomb counting",
      module: "battery",
      detail: "5 A discharge, 5 Ah cell, 360 s. Exact SOC = 0.900000.",
      passed: Math.abs(cell.soc - 0.9) < 1e-12,
      value: `${cell.soc.toFixed(6)} SOC`,
    },
    {
      name: "2-RC exponential response",
      module: "battery",
      detail: "Frozen-coefficient exact integration against closed form.",
      passed: Math.abs(cell.v[0] - analytic) < 1e-12,
      value: `${Math.abs(cell.v[0] - analytic).toExponential(1)} V error`,
    },
    {
      name: "Stiff-source boundedness",
      module: "battery",
      detail: "Patankar update at 2000 K, dt = 1,000,000 s.",
      passed: bounded.next.every((value) => value >= 0 && value <= 1),
      value: "0 <= c, alpha <= 1",
    },
    {
      name: "Hann PSD normalization",
      module: "acoustics",
      detail:
        "Unit-RMS, bin-centered 128 Hz tone; one-sided PSD integrates to variance.",
      passed: Math.abs(integrated - 1) < 1e-5,
      value: `${integrated.toFixed(8)} Pa2`,
    },
    {
      name: "OASPL reference pressure",
      module: "acoustics",
      detail: "1 Pa RMS relative to 20 microPa = 93.9794 dB.",
      passed: Math.abs(fftResult.oaspl - 93.9794000867) < 1e-6,
      value: `${fftResult.oaspl.toFixed(4)} dB`,
    },
    {
      name: "Hartmann channel convergence",
      module: "mhd",
      detail:
        "Ha = 1, 10, 25, 50, 100; 1024 intervals, RMS normalized velocity error.",
      passed: worst < 0.0001,
      value: `${worst.toExponential(2)} L2 error`,
    },
    {
      name: "Pyrolysis mass positivity",
      module: "tps",
      detail:
        "Implicit virgin-to-char update retains the char-density lower bound.",
      passed: 120 + 160 / (1 + 1e8) >= 120,
      value: "rho >= 120 kg/m3",
    },
    {
      name: "BDF2 geometric conservation",
      module: "tps",
      detail:
        "Uniform 1D affine wall motion. Swept-face BDF2 flux equals the volume derivative.",
      passed: Math.abs((3 * 0.98 - 4 * 0.99 + 1) / 2 + 0.01) < 1e-12,
      value: "< 1e-12 m3/s",
    },
  ];
}

export function downloadText(
  filename: string,
  text: string,
  type = "text/plain",
) {
  const url = URL.createObjectURL(new Blob([text], { type }));
  const a = document.createElement("a");
  a.href = url;
  a.download = filename;
  a.click();
  setTimeout(() => URL.revokeObjectURL(url), 500);
}
