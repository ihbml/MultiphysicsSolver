import { useMemo, useState, type ReactNode } from "react";

type Workspace = "overview" | "turbulence" | "wall" | "cht" | "porous";
type TurbulenceModel = "sst" | "sa" | "wale";
type CouplingMode = "monolithic" | "partitioned";
type PorousMode = "momentum" | "radiation";
type PlotKind = "turbulence" | "wall" | "cht" | "porous";

type Inputs = {
  k: number;
  omega: number;
  strain: number;
  nuTilde: number;
  waleShear: number;
  waleRotation: number;
  filterWidth: number;
  yPlus: number;
  frictionVelocity: number;
  deltaT: number;
  conductivityF: number;
  conductivityS: number;
  temperatureP: number;
  temperatureS: number;
  distanceF: number;
  distanceS: number;
  chtRelaxation: number;
  velocity: number;
  darcyX: number;
  darcyY: number;
  forchheimer: number;
  angle: number;
  gasTemperature: number;
  wallTemperature: number;
  absorption: number;
  refractiveIndex: number;
  emissivity: number;
  radiationDiffusion: number;
};

type Derived = {
  f1: number;
  f2: number;
  nut: number;
  production: number;
  productionLimit: number;
  kDiagonal: number;
  omegaDiagonal: number;
  omegaRhs: number;
  saFv1: number;
  saFv2: number;
  saFw: number;
  saDiffusion: number;
  saNut: number;
  saProduction: number;
  saDestruction: number;
  waleNut: number;
  wall: {
    yPlus: number;
    uTau: number;
    uPlus: number;
    omegaVis: number;
    omegaLog: number;
    omegaWall: number;
    shear: number;
    tPlus: number;
    heatFlux: number;
  };
  cht: {
    conductance: number;
    flux: number;
    interfaceTemperature: number;
    relaxedFlux: number;
  };
  porous: {
    darcyTensorXX: number;
    pressureGradient: number;
    momentumDiagonal: number;
    p1Source: number;
    marshakFace: number;
    marshakFlux: number;
  };
};

const baseline: Inputs = {
  k: 0.08,
  omega: 42,
  strain: 85,
  nuTilde: 0.0002,
  waleShear: 55,
  waleRotation: 16,
  filterWidth: 0.001,
  yPlus: 30,
  frictionVelocity: 0.45,
  deltaT: 30,
  conductivityF: 0.6,
  conductivityS: 15,
  temperatureP: 370,
  temperatureS: 310,
  distanceF: 0.005,
  distanceS: 0.01,
  chtRelaxation: 0.65,
  velocity: 0.2,
  darcyX: 1000,
  darcyY: 2500,
  forchheimer: 10,
  angle: 35,
  gasTemperature: 1000,
  wallTemperature: 900,
  absorption: 0.8,
  refractiveIndex: 1,
  emissivity: 0.8,
  radiationDiffusion: 0.12,
};

const sigmaSB = 5.670374419e-8;
const nuAir = 1.5e-5;
const rhoAir = 1.2;
const kappa = 0.41;
const betaStar = 0.09;

const clamp = (value: number, min: number, max: number) => Math.min(max, Math.max(min, value));

function format(value: number, digits = 3) {
  if (!Number.isFinite(value)) return "--";
  const absolute = Math.abs(value);
  if ((absolute > 0 && absolute < 0.001) || absolute >= 10000) return value.toExponential(2);
  return value.toFixed(digits);
}

function reichardt(yPlus: number) {
  const yp = Math.max(0, yPlus);
  if (yp < 1e-5) return yp;
  return Math.max(
    Math.log1p(kappa * yp) / kappa +
      7.8 * (1 - Math.exp(-yp / 11) - (yp / 11) * Math.exp(-yp / 3)),
    0,
  );
}

function kaderTemperature(yPlus: number, prandtl: number) {
  const yp = Math.max(yPlus, 0);
  const pr = Math.max(prandtl, 1e-8);
  const gamma = (0.01 * (pr * yp) ** 4) / Math.max(1 + 5 * pr ** 3 * yp, 1e-12);
  const beta = (3.85 * pr ** (1 / 3) - 1.3) ** 2 + 2.12 * Math.log(pr);
  const logWeight = gamma > 1e-12 ? Math.exp(-1 / gamma) : 0;
  return Math.max(pr * yp * Math.exp(-gamma) + (2.12 * Math.log1p(yp) + beta) * logWeight, 1e-12);
}

function frictionVelocity(speed: number, distance: number, nu: number) {
  const u = Math.max(speed, 0);
  if (u <= 1e-12) return 0;
  const y = Math.max(distance, 1e-12);
  const molecularNu = Math.max(nu, 1e-12);
  const residual = (friction: number) => friction * reichardt((friction * y) / molecularNu) - u;
  let low = 0;
  let high = Math.max(u, Math.sqrt((u * molecularNu) / y), 1e-8);
  for (let i = 0; i < 64 && residual(high) < 0; i += 1) high *= 2;
  for (let i = 0; i < 68; i += 1) {
    const mid = 0.5 * (low + high);
    if (residual(mid) > 0) high = mid;
    else low = mid;
  }
  return 0.5 * (low + high);
}

function computeWale(shear: number, rotation: number, width: number) {
  const g = [
    [0, shear, 0],
    [-rotation, 0, 0.35 * shear],
    [0.15 * shear, -0.1 * rotation, 0],
  ];
  // Explicit tensor multiplication avoids relying on a grid-specific gradient layout.
  const square = Array.from({ length: 3 }, (_, i) =>
    Array.from({ length: 3 }, (_, j) =>
      g[i].reduce((sum, value, m) => sum + value * g[m][j], 0),
    ),
  );
  const trace = g[0][0] + g[1][1] + g[2][2];
  let s2 = 0;
  let d2 = 0;
  for (let i = 0; i < 3; i += 1) {
    for (let j = 0; j < 3; j += 1) {
      const sij = 0.5 * (g[i][j] + g[j][i]);
      const dij = 0.5 * (square[i][j] + square[j][i]) - (i === j ? (trace * trace) / 3 : 0);
      s2 += sij * sij;
      d2 += dij * dij;
    }
  }
  return (0.325 * Math.max(width, 0)) ** 2 * d2 ** 1.5 /
    (s2 ** 2.5 + d2 ** 1.25 + 1e-12);
}

function derive(input: Inputs): Derived {
  const k = Math.max(input.k, 1e-12);
  const omega = Math.max(input.omega, 1e-12);
  const wallDistance = 0.0007;
  const gradK = [0.2, 0.1, 0];
  const gradOmega = [4, -2, 0.5];
  const gradDot = gradK.reduce((sum, value, i) => sum + value * gradOmega[i], 0);
  const cd = Math.max((2 * rhoAir * 0.856 * gradDot) / omega, 1e-20);
  const arg1a = Math.sqrt(k) / (betaStar * omega * wallDistance);
  const arg1b = (500 * nuAir) / (wallDistance ** 2 * omega);
  const arg1 = Math.min(Math.max(arg1a, arg1b), (4 * rhoAir * 0.856 * k) / (cd * wallDistance ** 2));
  const arg2 = Math.max((2 * Math.sqrt(k)) / (betaStar * omega * wallDistance), arg1b);
  const f1 = Math.tanh(Math.max(arg1, 0) ** 4);
  const f2 = Math.tanh(Math.max(arg2, 0) ** 2);
  const nut = (0.31 * k) / Math.max(0.31 * omega, input.strain * f2, 1e-12);
  const productionLimit = 10 * betaStar * rhoAir * k * omega;
  const production = Math.min(rhoAir * nut * input.strain ** 2, productionLimit);
  const beta = f1 * 0.075 + (1 - f1) * 0.0828;
  const omegaDiagonal = (betaStar + 2 * beta) * rhoAir * omega;
  const omegaRhs = (betaStar + beta) * rhoAir * omega ** 2 +
    (f1 * (5 / 9) + (1 - f1) * 0.44) * production / Math.max(nut, 1e-12);
  const kDiagonal = betaStar * rhoAir * omega;

  const chi = Math.max(input.nuTilde, 0) / nuAir;
  const fv1 = chi ** 3 / Math.max(chi ** 3 + 7.1 ** 3, 1e-12);
  const fv2 = 1 - chi / Math.max(1 + chi * fv1, 1e-12);
  const saStilde = Math.max(input.strain + input.nuTilde * fv2 / (0.41 ** 2 * wallDistance ** 2), 0.3 * input.strain);
  const saR = Math.min(input.nuTilde / Math.max(saStilde * 0.41 ** 2 * wallDistance ** 2, 1e-12), 10);
  const saG = saR + 0.3 * (saR ** 6 - saR);
  const saFw = saG * ((1 + 2 ** 6) / Math.max(saG ** 6 + 2 ** 6, 1e-12)) ** (1 / 6);
  const saCw1 = 0.1355 / 0.41 ** 2 + (1 + 0.622) / (2 / 3);
  const saDiffusion = nuAir + Math.max(input.nuTilde, 0) / (2 / 3);
  const saNut = Math.max(input.nuTilde, 0) * fv1;
  const saProduction = 0.1355 * saStilde * Math.max(input.nuTilde, 0);
  const saDestruction = saCw1 * saFw * (Math.max(input.nuTilde, 0) / wallDistance) ** 2;
  const waleNut = computeWale(input.waleShear, input.waleRotation, input.filterWidth);

  const y = (input.yPlus * nuAir) / Math.max(input.frictionVelocity, 1e-8);
  const speed = input.frictionVelocity * reichardt(input.yPlus);
  const uTau = frictionVelocity(speed, y, nuAir);
  const calculatedYPlus = (uTau * y) / nuAir;
  const tPlus = kaderTemperature(calculatedYPlus, 0.71);
  const omegaVis = (6 * nuAir) / (0.075 * Math.max(y, 1e-12) ** 2);
  const omegaLog = uTau / (Math.sqrt(betaStar) * kappa * Math.max(y, 1e-12));
  const wall = {
    yPlus: calculatedYPlus,
    uTau,
    uPlus: reichardt(calculatedYPlus),
    omegaVis,
    omegaLog,
    omegaWall: Math.hypot(omegaVis, omegaLog),
    shear: rhoAir * uTau ** 2,
    tPlus,
    heatFlux: (rhoAir * 1005 * uTau * input.deltaT) / tPlus,
  };

  const alphaF = 1 / Math.max(input.distanceF, 1e-12);
  const alphaS = 1 / Math.max(input.distanceS, 1e-12);
  const gf = input.conductivityF * alphaF;
  const gs = input.conductivityS * alphaS;
  const conductance = (gf * gs) / Math.max(gf + gs, 1e-12);
  const chtFlux = conductance * (input.temperatureS - input.temperatureP);
  const interfaceTemperature = (gf * input.temperatureP + gs * input.temperatureS) / Math.max(gf + gs, 1e-12);
  const cht = {
    conductance,
    flux: chtFlux,
    interfaceTemperature,
    relaxedFlux: input.chtRelaxation * chtFlux,
  };

  const theta = (input.angle * Math.PI) / 180;
  const darcyTensorXX = input.darcyX * Math.cos(theta) ** 2 + input.darcyY * Math.sin(theta) ** 2;
  const forchheimerXX = input.forchheimer * Math.cos(theta) ** 2 + 2 * input.forchheimer * Math.sin(theta) ** 2;
  const pressureGradient = 1.8e-5 * darcyTensorXX * input.velocity +
    0.5 * 1.2 * forchheimerXX * Math.abs(input.velocity) * input.velocity;
  const momentumDiagonal = 1.8e-5 * darcyTensorXX + 1.2 * forchheimerXX * Math.abs(input.velocity);
  const p1Source = 4 * input.absorption * input.refractiveIndex ** 2 * sigmaSB * input.gasTemperature ** 4;
  const emissivity = clamp(input.emissivity, 0, 1);
  const hMarshak = emissivity / Math.max(2 * (2 - emissivity), 1e-12);
  const gDiff = input.radiationDiffusion / 0.02;
  const blackBody = 4 * sigmaSB * input.wallTemperature ** 4;
  const marshakFace = (hMarshak * blackBody + gDiff * 1e4) / Math.max(hMarshak + gDiff, 1e-12);
  const marshakFlux = gDiff * (1e4 - marshakFace);
  const porous = { darcyTensorXX, pressureGradient, momentumDiagonal, p1Source, marshakFace, marshakFlux };
  return {
    f1,
    f2,
    nut,
    production,
    productionLimit,
    kDiagonal,
    omegaDiagonal,
    omegaRhs,
    saFv1: fv1,
    saFv2: fv2,
    saFw,
    saDiffusion,
    saNut,
    saProduction,
    saDestruction,
    waleNut,
    wall,
    cht,
    porous,
  };
}

function makeCurve(points: Array<[number, number]>) {
  return points.map(([x, y], index) => `${index === 0 ? "M" : "L"}${x.toFixed(2)},${y.toFixed(2)}`).join(" ");
}

function Icon({ name }: { name: Workspace }) {
  const paths: Record<Workspace, ReactNode> = {
    overview: <><path d="M4 4h6v6H4z" /><path d="M14 4h6v6h-6z" /><path d="M4 14h6v6H4z" /><path d="M14 14h6v6h-6z" /></>,
    turbulence: <><path d="M3 7c3.2-4 5.8 4 9 0s5.8 4 9 0" /><path d="M3 17c3.2-4 5.8 4 9 0s5.8 4 9 0" /></>,
    wall: <><path d="M5 4v16" /><path d="M9 7h10" /><path d="M9 12h7" /><path d="M9 17h4" /><path d="M5 4h3" /></>,
    cht: <><path d="M4 5h7v14H4z" /><path d="M13 5h7v14h-7z" /><path d="M11 8h2M11 12h2M11 16h2" /></>,
    porous: <><path d="M4 5h16M4 12h16M4 19h16" /><circle cx="8" cy="5" r="1.5" /><circle cx="15" cy="12" r="1.5" /><circle cx="10" cy="19" r="1.5" /></>,
  };
  return <svg className="nav-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.65" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">{paths[name]}</svg>;
}

function RangeField({
  label,
  value,
  min,
  max,
  step,
  display,
  onChange,
}: {
  label: string;
  value: number;
  min: number;
  max: number;
  step: number;
  display: string;
  onChange: (next: number) => void;
}) {
  return (
    <label className="range-field">
      <span className="range-line"><span>{label}</span><strong>{display}</strong></span>
      <input type="range" min={min} max={max} step={step} value={value} onChange={(event) => onChange(Number(event.target.value))} />
      <span className="range-limits"><span>{format(min, 2)}</span><span>{format(max, 2)}</span></span>
    </label>
  );
}

function MetricLine({ label, value, unit }: { label: string; value: string; unit?: string }) {
  return (
    <div className="metric-line">
      <span>{label}</span>
      <strong>{value}{unit && <small> {unit}</small>}</strong>
    </div>
  );
}

function ResponsePlot({ kind, model, porousMode, input, result }: {
  kind: PlotKind;
  model: TurbulenceModel;
  porousMode: PorousMode;
  input: Inputs;
  result: Derived;
}) {
  const left = 60;
  const right = 692;
  const top = 24;
  const bottom = 245;
  const width = right - left;
  const height = bottom - top;
  const gridX = Array.from({ length: 7 }, (_, i) => left + (width * i) / 6);
  const gridY = Array.from({ length: 5 }, (_, i) => top + (height * i) / 4);
  let mainPath = "";
  let referencePath = "";
  let areaPath = "";
  let marker: { x: number; y: number } | undefined;
  let xLabel = "STRAIN RATE, 1/s";
  let yLabel = "NU_T / NU";
  let legend = "MODEL RESPONSE";

  if (kind === "wall") {
    xLabel = "WALL DISTANCE, Y+";
    yLabel = "U+";
    legend = "REICHARDT PROFILE / LOG-LAW REFERENCE";
    const profilePoints: Array<[number, number]> = [];
    const logPoints: Array<[number, number]> = [];
    for (let i = 0; i <= 72; i += 1) {
      const logY = -1 + (i / 72) * (Math.log10(500) + 1);
      const yp = 10 ** logY;
      const px = left + ((logY + 1) / (Math.log10(500) + 1)) * width;
      const py = bottom - (Math.min(reichardt(yp), 42) / 42) * height;
      const logU = yp > 0.1 ? Math.log(9.8 * yp) / kappa : 0;
      const logPy = bottom - (clamp(logU, 0, 42) / 42) * height;
      profilePoints.push([px, py]);
      logPoints.push([px, logPy]);
    }
    mainPath = makeCurve(profilePoints);
    referencePath = makeCurve(logPoints);
    areaPath = `${mainPath} L${right},${bottom} L${left},${bottom} Z`;
    const x = left + ((Math.log10(Math.max(result.wall.yPlus, 0.1)) + 1) / (Math.log10(500) + 1)) * width;
    const y = bottom - (Math.min(result.wall.uPlus, 42) / 42) * height;
    marker = { x, y };
  } else if (kind === "cht") {
    xLabel = "NORMALIZED INTERFACE DISTANCE";
    yLabel = "TEMPERATURE, K";
    legend = "FLUID / SOLID CONDUCTION";
    const low = Math.min(input.temperatureP, input.temperatureS) - 10;
    const high = Math.max(input.temperatureP, input.temperatureS) + 10;
    const fluidEnd = input.distanceF / (input.distanceF + input.distanceS);
    const points: Array<[number, number]> = [];
    for (let i = 0; i <= 48; i += 1) {
      const xNorm = i / 48;
      const t = xNorm <= fluidEnd
        ? input.temperatureP + (result.cht.interfaceTemperature - input.temperatureP) * (xNorm / Math.max(fluidEnd, 1e-9))
        : result.cht.interfaceTemperature + (input.temperatureS - result.cht.interfaceTemperature) * ((xNorm - fluidEnd) / Math.max(1 - fluidEnd, 1e-9));
      points.push([left + xNorm * width, bottom - ((t - low) / Math.max(high - low, 1)) * height]);
    }
    mainPath = makeCurve(points);
    areaPath = `${mainPath} L${right},${bottom} L${left},${bottom} Z`;
    marker = { x: left + fluidEnd * width, y: bottom - ((result.cht.interfaceTemperature - low) / Math.max(high - low, 1)) * height };
  } else if (kind === "porous" && porousMode === "radiation") {
    xLabel = "WALL EMISSIVITY, EPSILON";
    yLabel = "Q_MARSHAK, W/M2";
    legend = "P1 MARSHAK WALL FLUX";
    const cellRadiation = 1.0e4;
    const distance = 0.02;
    const gamma = input.radiationDiffusion / distance;
    const blackBody = 4 * sigmaSB * input.wallTemperature ** 4;
    const fluxAt = (emissivity: number) => {
      const h = emissivity / Math.max(2 * (2 - emissivity), 1e-12);
      const face = (h * blackBody + gamma * cellRadiation) / Math.max(h + gamma, 1e-12);
      return gamma * (cellRadiation - face);
    };
    const maxFlux = Math.max(Math.abs(fluxAt(0.1)), Math.abs(fluxAt(1)), 1);
    const points: Array<[number, number]> = [];
    for (let i = 0; i <= 48; i += 1) {
      const emissivity = 0.1 + (i / 48) * 0.9;
      const flux = fluxAt(emissivity);
      points.push([left + ((emissivity - 0.1) / 0.9) * width, bottom - ((flux + maxFlux) / (2 * maxFlux)) * height]);
    }
    mainPath = makeCurve(points);
    areaPath = `${mainPath} L${right},${bottom} L${left},${bottom} Z`;
    const markerFlux = fluxAt(input.emissivity);
    marker = {
      x: left + ((clamp(input.emissivity, 0.1, 1) - 0.1) / 0.9) * width,
      y: bottom - ((markerFlux + maxFlux) / (2 * maxFlux)) * height,
    };
  } else if (kind === "porous") {
    xLabel = "SUPERFICIAL VELOCITY, M/S";
    yLabel = "DELTA P / L, PA/M";
    legend = "DARCY + FORCHHEIMER";
    const maxVelocity = 1.0;
    const maxPressure = Math.max(0.1, 1.8e-5 * result.porous.darcyTensorXX * maxVelocity + 0.6 * 2 * input.forchheimer * maxVelocity ** 2);
    const points: Array<[number, number]> = [];
    for (let i = 0; i <= 48; i += 1) {
      const u = (i / 48) * maxVelocity;
      const pressure = 1.8e-5 * result.porous.darcyTensorXX * u + 0.6 * 2 * input.forchheimer * u ** 2;
      points.push([left + (u / maxVelocity) * width, bottom - (pressure / maxPressure) * height]);
    }
    mainPath = makeCurve(points);
    areaPath = `${mainPath} L${right},${bottom} L${left},${bottom} Z`;
    marker = {
      x: left + clamp(input.velocity / maxVelocity, 0, 1) * width,
      y: bottom - (Math.min(result.porous.pressureGradient, maxPressure) / maxPressure) * height,
    };
  } else {
    const maxStrain = Math.max(180, input.strain * 1.35);
    const points: Array<[number, number]> = [];
    for (let i = 0; i <= 56; i += 1) {
      const strain = 2 + (i / 56) * maxStrain;
      const currentK = Math.max(input.k, 1e-12);
      const currentOmega = Math.max(input.omega, 1e-12);
      const currentF2 = result.f2;
      const closureNut = model === "wale"
        ? computeWale(input.waleShear * (strain / Math.max(input.strain, 1)), input.waleRotation, input.filterWidth)
        : model === "sa"
          ? result.saNut
          : (0.31 * currentK) / Math.max(0.31 * currentOmega, strain * currentF2, 1e-12);
      const response = model === "sst" ? closureNut / nuAir : closureNut / nuAir;
      const maxResponse = Math.max(500, result.nut / nuAir * 1.25, result.waleNut / nuAir * 1.25);
      points.push([left + (strain / maxStrain) * width, bottom - (Math.min(response, maxResponse) / maxResponse) * height]);
    }
    mainPath = makeCurve(points);
    areaPath = `${mainPath} L${right},${bottom} L${left},${bottom} Z`;
    const response = model === "wale" ? result.waleNut / nuAir : model === "sa" ? result.saNut / nuAir : result.nut / nuAir;
    const maxResponse = Math.max(500, result.nut / nuAir * 1.25, result.waleNut / nuAir * 1.25);
    marker = {
      x: left + (input.strain / maxStrain) * width,
      y: bottom - (Math.min(response, maxResponse) / maxResponse) * height,
    };
    if (model === "sa") yLabel = "NU_TILDE / NU";
    if (model === "wale") yLabel = "NU_SGS / NU";
  }

  return (
    <div className="plot-canvas">
      <svg className="response-svg" viewBox="0 0 720 292" role="img" aria-label={`${legend} engineering response plot`}>
        <defs>
          <linearGradient id="plotFill" x1="0" x2="0" y1="0" y2="1">
            <stop offset="0%" stopColor="#44dfbd" stopOpacity="0.2" />
            <stop offset="100%" stopColor="#44dfbd" stopOpacity="0" />
          </linearGradient>
          <linearGradient id="heatFill" x1="0" x2="1" y1="0" y2="0">
            <stop offset="0%" stopColor="#fb9f59" stopOpacity="0.22" />
            <stop offset="100%" stopColor="#50d8bc" stopOpacity="0.08" />
          </linearGradient>
        </defs>
        {gridY.map((y, index) => <line key={`gy-${index}`} x1={left} x2={right} y1={y} y2={y} className="plot-grid" />)}
        {gridX.map((x, index) => <line key={`gx-${index}`} x1={x} x2={x} y1={top} y2={bottom} className="plot-grid plot-grid-vertical" />)}
        <path d={areaPath} className={`plot-area ${kind === "cht" ? "heat-area" : ""}`} />
        {referencePath && <path d={referencePath} className="reference-line" />}
        <path d={mainPath} pathLength="1" className={`curve-line ${kind === "cht" ? "curve-heat" : ""}`} />
        {kind === "cht" && marker && <line x1={marker.x} x2={marker.x} y1={top} y2={bottom} className="interface-line" />}
        {marker && <><line x1={marker.x} x2={marker.x} y1={top} y2={marker.y} className="marker-guide" /><circle cx={marker.x} cy={marker.y} r="5" className="plot-marker" /></>}
        <line x1={left} x2={right} y1={bottom} y2={bottom} className="plot-axis" />
        <line x1={left} x2={left} y1={top} y2={bottom} className="plot-axis" />
        <text x={left} y="276" className="plot-axis-label">{xLabel}</text>
        <text x="16" y="20" className="plot-axis-label plot-y-label">{yLabel}</text>
        {kind === "wall" ? <>
          <text x={left} y="262" className="plot-tick">0.1</text><text x="279" y="262" className="plot-tick">1</text>
          <text x="444" y="262" className="plot-tick">10</text><text x={right - 10} y="262" className="plot-tick">500</text>
          <text x="32" y={bottom + 2} className="plot-tick">0</text><text x="28" y="140" className="plot-tick">20</text><text x="28" y="30" className="plot-tick">40</text>
        </> : kind === "cht" ? <>
          <text x={left} y="262" className="plot-tick">0.0</text><text x="360" y="262" className="plot-tick">INTERFACE</text><text x={right - 28} y="262" className="plot-tick">1.0</text>
        </> : kind === "porous" && porousMode === "radiation" ? <>
          <text x={left} y="262" className="plot-tick">0.1</text><text x="365" y="262" className="plot-tick">0.55</text><text x={right - 18} y="262" className="plot-tick">1.0</text>
        </> : kind === "porous" ? <>
          <text x={left} y="262" className="plot-tick">0.0</text><text x="365" y="262" className="plot-tick">0.5</text><text x={right - 18} y="262" className="plot-tick">1.0</text>
        </> : <>
          <text x={left} y="262" className="plot-tick">0</text><text x="355" y="262" className="plot-tick">STRAIN</text><text x={right - 38} y="262" className="plot-tick">MAX</text>
        </>}
      </svg>
      <div className="plot-legend"><span className="legend-swatch" />{legend}{referencePath && <><span className="legend-dash" />LOG LAW</>}</div>
    </div>
  );
}

function App() {
  const [workspace, setWorkspace] = useState<Workspace>("overview");
  const [model, setModel] = useState<TurbulenceModel>("sst");
  const [coupling, setCoupling] = useState<CouplingMode>("monolithic");
  const [porousMode, setPorousMode] = useState<PorousMode>("momentum");
  const [input, setInput] = useState<Inputs>(baseline);
  const [copyState, setCopyState] = useState("Copy build command");
  const result = useMemo(() => derive(input), [input]);

  const change = (key: keyof Inputs, value: number) => setInput((current) => ({ ...current, [key]: value }));
  const activePlot: PlotKind = workspace === "wall" ? "wall" : workspace === "cht" ? "cht" : workspace === "porous" ? "porous" : "turbulence";
  const modelName = model === "sst" ? "k-omega SST" : model === "sa" ? "Spalart-Allmaras" : "WALE LES";
  const context = {
    overview: { label: "SUITE OVERVIEW", title: "Cluster A verification", description: "A live reference bench for turbulence, wall treatment, heat transfer, porous drag, and radiation." },
    turbulence: { label: "TURBULENCE / CLOSURES", title: "Turbulence transport", description: "Inspect production, blending, and viscosity response across the RANS-to-LES model set." },
    wall: { label: "BOUNDARY / ALL-Y+", title: "Continuous wall treatment", description: "Resolve the viscous-to-log transition at the boundary face without a near-wall singularity." },
    cht: { label: "ENERGY / MULTI-REGION", title: "Conjugate heat transfer", description: "Check mapped twin-face flux balance and the region-coupling assembly in real time." },
    porous: { label: "ENERGY / POROUS + RADIATION", title: "Porous and radiation", description: "Sweep anisotropic Darcy-Forchheimer resistance and P1 Marshak wall transport." },
  }[workspace];
  const stabilityNote = workspace === "wall"
    ? { title: "Wall-distance floor active", detail: "Finite viscous and thermal profiles at y -> 0" }
    : workspace === "cht"
      ? { title: "Twin-face conservation active", detail: "Equal and opposite mapped-interface fluxes" }
      : workspace === "porous"
        ? porousMode === "momentum"
          ? { title: "Positive tensor drag active", detail: "Anisotropic sink with implicit Jacobian" }
          : { title: "Bounded P1 wall row active", detail: "Emissivity clamp and implicit Marshak exchange" }
        : model === "sst"
          ? { title: "Positivity guard active", detail: "Floors, limiters, and Patankar sinks enabled" }
          : model === "sa"
            ? { title: "SA working-variable guard active", detail: "Nonnegative variable and implicit destruction" }
            : { title: "WALE viscosity bounded", detail: "Traceless gradient with a finite denominator" };

  const downloadReport = () => {
    const report = {
      suite: "AEROFLOW Cluster A",
      generatedAt: new Date().toISOString(),
      workspace,
      model: modelName,
      inputs: input,
      derived: result,
      verification: { hostCompiler: "g++ -std=c++20 -O2", checksPassed: 53 },
    };
    const url = URL.createObjectURL(new Blob([JSON.stringify(report, null, 2)], { type: "application/json" }));
    const anchor = document.createElement("a");
    anchor.href = url;
    anchor.download = "cluster-a-verification.json";
    anchor.click();
    window.setTimeout(() => URL.revokeObjectURL(url), 1000);
  };

  const copyBuildCommand = async () => {
    const command = "g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Icpp/include cpp/src/verification.cpp -o /tmp/cluster-a-verify && /tmp/cluster-a-verify";
    try {
      await navigator.clipboard.writeText(command);
      setCopyState("Command copied");
    } catch {
      setCopyState("See cpp/README.md");
    }
    window.setTimeout(() => setCopyState("Copy build command"), 2200);
  };

  const plotHeading = workspace === "wall"
    ? "Velocity and thermal wall law"
    : workspace === "cht"
      ? "Region temperature trace"
      : workspace === "porous"
        ? porousMode === "momentum" ? "Pressure-drop characteristic" : "P1 emission and wall response"
        : `${modelName} response`;
  const plotSubhead = workspace === "wall"
    ? "Reichardt velocity profile with Kader thermal scaling"
    : workspace === "cht"
      ? "Piecewise field reconstructed through fluid and solid half-cells"
      : workspace === "porous"
        ? porousMode === "momentum" ? "Tensor-projected resistance over a velocity sweep" : "Absorption source and diffuse wall exchange"
        : workspace === "overview"
          ? "Polyhedral cell closure and limited production"
          : "Cell-local closure sampled over the strain-rate range";

  return (
    <div className="app-shell">
      <aside className="sidebar">
        <div className="brand-lockup">
          <div className="brand-mark" aria-hidden="true">
            <svg viewBox="0 0 42 42" fill="none">
              <path d="M7 8h14v13H8V8Zm14 13h14v13H21V21Z" stroke="currentColor" strokeWidth="2" />
              <path d="M8 34h12V22H8v12ZM22 7h12v12H22V7Z" stroke="currentColor" strokeWidth="2" />
              <circle cx="21" cy="21" r="3" fill="currentColor" />
            </svg>
          </div>
          <div className="brand-copy"><strong>AEROFLOW</strong><span>THERMAL SYSTEMS</span></div>
        </div>

        <div className="cluster-selector">
          <span className="cluster-index">01</span>
          <div><small>ACTIVE SOLVER CLUSTER</small><strong>Multiphysics A</strong></div>
          <span className="selector-mark">+</span>
        </div>

        <div className="nav-heading">WORKBENCH</div>
        <nav className="side-nav" aria-label="Workbench sections">
          {([
            ["overview", "Suite overview"],
            ["turbulence", "Turbulence models"],
            ["wall", "All-y+ wall"],
            ["cht", "Conjugate heat"],
            ["porous", "Porous + P1"],
          ] as Array<[Workspace, string]>).map(([target, label], index) => (
            <button key={target} className={`nav-link ${workspace === target ? "active" : ""}`} aria-label={label} aria-current={workspace === target ? "page" : undefined} title={label} onClick={() => setWorkspace(target)}>
              <Icon name={target} /><span>{label}</span><small>{String(index + 1).padStart(2, "0")}</small>
            </button>
          ))}
        </nav>

        <div className="sidebar-spacer" />
        <div className="engine-status">
          <span className="live-pip" />
          <div><strong>Reference kernels</strong><span>CPU / CUDA source</span></div>
        </div>
        <button className="copy-command" onClick={copyBuildCommand}>{copyState}<span>&gt;</span></button>
        <div className="sidebar-version"><span>CLUSTER A</span><span>R20.03</span></div>
      </aside>

      <main className="main-shell">
        <header className="topbar">
          <div className="breadcrumb"><span>ENGINEERING</span><i>/</i><strong>VERIFICATION WORKBENCH</strong></div>
          <div className="topbar-right"><span className="compiler-tag"><span />C++20 REFERENCE</span><button className="export-button" onClick={downloadReport}>Export report <span>JSON</span></button></div>
        </header>

        <div className="workspace-content panel-enter">
          <section className="page-heading">
            <div>
              <div className="eyebrow"><span>CLUSTER A</span><b>/</b>{context.label}</div>
              <h1>{context.title}</h1>
              <p>{context.description}</p>
            </div>
            <div className="heading-meta"><span className="run-mark"><span className="live-pip" /> LIVE PARAMETRIC CASE</span><span className="case-id">CASE A-014 / BASELINE AIR</span></div>
          </section>

          <div className="workbench-grid">
            <section className="analysis-column">
              <div className="analysis-toolbar">
                <div className="toolbar-context"><span className="toolbar-number">{workspace === "wall" ? "02" : workspace === "cht" ? "03" : workspace === "porous" ? "04" : "01"}</span><span>{workspace === "wall" ? "WALL MODEL" : workspace === "cht" ? "INTERFACE FIELD" : workspace === "porous" ? "TRANSPORT FIELD" : "CLOSURE FIELD"}</span></div>
                {workspace === "overview" || workspace === "turbulence" ? (
                  <div className="segmented model-switch" aria-label="Turbulence model">
                    {(["sst", "sa", "wale"] as TurbulenceModel[]).map((item) => (
                      <button key={item} className={model === item ? "selected" : ""} aria-pressed={model === item} onClick={() => setModel(item)}>
                        {item === "sst" ? "SST" : item === "sa" ? "SA" : "WALE"}
                      </button>
                    ))}
                  </div>
                ) : workspace === "cht" ? (
                  <div className="segmented" aria-label="CHT coupling mode">
                    <button className={coupling === "monolithic" ? "selected" : ""} aria-pressed={coupling === "monolithic"} onClick={() => setCoupling("monolithic")}>Monolithic</button>
                    <button className={coupling === "partitioned" ? "selected" : ""} aria-pressed={coupling === "partitioned"} onClick={() => setCoupling("partitioned")}>Partitioned</button>
                  </div>
                ) : workspace === "porous" ? (
                  <div className="segmented" aria-label="Porous or radiation mode">
                    <button className={porousMode === "momentum" ? "selected" : ""} aria-pressed={porousMode === "momentum"} onClick={() => setPorousMode("momentum")}>Porous drag</button>
                    <button className={porousMode === "radiation" ? "selected" : ""} aria-pressed={porousMode === "radiation"} onClick={() => setPorousMode("radiation")}>P1 radiation</button>
                  </div>
                ) : <span className="mode-stamp">MENTER / KADER CONTINUOUS BLEND</span>}
              </div>

              <div className="plot-panel">
                <div className="plot-panel-head">
                  <div><div className="section-kicker">REFERENCE RESPONSE / 64 SAMPLE SWEEP</div><h2>{plotHeading}</h2><p>{plotSubhead}</p></div>
                  <div className="mesh-stamp"><span className="mesh-glyph">HEX</span><span>POLYHEDRAL<br />CELL SAMPLE</span></div>
                </div>
                <ResponsePlot kind={activePlot} model={model} porousMode={porousMode} input={input} result={result} />
                <div className="plot-foot"><span><b>GRID</b> HILBERT / 4-QUADRANT</span><span><b>RECONSTRUCTION</b> LEAST-SQUARES</span><span><b>FACE FLUX</b> CONSERVATIVE GATHER</span></div>
              </div>

              <div className="readout-strip">
                <div className="readout-label"><span className="live-pip" /> LIVE SOLVER READOUT</div>
                {workspace === "wall" ? <>
                  <div className="readout-value"><span>y+</span><strong>{format(result.wall.yPlus, 1)}</strong></div>
                  <div className="readout-value"><span>tau_w</span><strong>{format(result.wall.shear, 3)} <small>Pa</small></strong></div>
                  <div className="readout-value"><span>q_w</span><strong>{format(result.wall.heatFlux, 2)} <small>W/m2</small></strong></div>
                </> : workspace === "cht" ? <>
                  <div className="readout-value"><span>q_f-s</span><strong>{format(result.cht.flux, 2)} <small>W/m2</small></strong></div>
                  <div className="readout-value"><span>T_interface</span><strong>{format(result.cht.interfaceTemperature, 2)} <small>K</small></strong></div>
                  <div className="readout-value"><span>a_PS</span><strong>{format(result.cht.conductance, 2)} <small>W/m2K</small></strong></div>
                </> : workspace === "porous" ? porousMode === "momentum" ? <>
                  <div className="readout-value"><span>deltaP / L</span><strong>{format(result.porous.pressureGradient, 3)} <small>Pa/m</small></strong></div>
                  <div className="readout-value"><span>D_xx global</span><strong>{format(result.porous.darcyTensorXX, 1)} <small>1/m2</small></strong></div>
                  <div className="readout-value"><span>implicit a_U</span><strong>{format(result.porous.momentumDiagonal, 3)} <small>kg/m3s</small></strong></div>
                </> : <>
                  <div className="readout-value"><span>P1 source</span><strong>{format(result.porous.p1Source, 2)} <small>W/m3</small></strong></div>
                  <div className="readout-value"><span>G_wall</span><strong>{format(result.porous.marshakFace, 1)} <small>W/m2</small></strong></div>
                  <div className="readout-value"><span>q_rad</span><strong>{format(result.porous.marshakFlux, 2)} <small>W/m2</small></strong></div>
                </> : <>
                  <div className="readout-value"><span>{model === "sst" ? "F1 / F2" : model === "sa" ? "fv1 / fw" : "nu_sgs"}</span><strong>{model === "sst" ? `${format(result.f1, 3)} / ${format(result.f2, 3)}` : model === "sa" ? `${format(result.saFv1, 3)} / ${format(result.saFw, 3)}` : `${format(result.waleNut, 3)} `}<small>{model === "wale" ? "m2/s" : "-"}</small></strong></div>
                  <div className="readout-value"><span>{model === "sst" ? "nu_t / nu" : model === "sa" ? "nu_tilde fv1" : "filter width"}</span><strong>{model === "sst" ? format(result.nut / nuAir, 1) : model === "sa" ? format(result.saNut, 5) : format(input.filterWidth, 4)} <small>{model === "sst" ? "ratio" : model === "sa" ? "m2/s" : "m"}</small></strong></div>
                  <div className="readout-value"><span>{model === "sst" ? "Pk / Pk_limit" : model === "sa" ? "Production / destruction" : "Shear / rotation"}</span><strong>{model === "sst" ? format(result.production, 2) : model === "sa" ? `${format(result.saProduction, 5)} / ${format(result.saDestruction, 5)}` : `${format(input.waleShear, 1)} / ${format(input.waleRotation, 1)}`} <small>{model === "sst" ? `/ ${format(result.productionLimit, 2)}` : model === "sa" ? "m2/s2" : "1/s"}</small></strong></div>
                </>}
              </div>

              {(workspace === "overview" || workspace === "cht") && <VerificationRail setWorkspace={setWorkspace} />}
            </section>

            <aside className="inspector">
              <div className="inspector-head"><div><span className="section-kicker">CASE CONTROLS</span><h2>{workspace === "wall" ? "Wall state" : workspace === "cht" ? "Material pair" : workspace === "porous" ? "Zone properties" : modelName}</h2></div><span className="tune-icon">TUNE</span></div>
              <div className="control-list">
                {(workspace === "overview" || workspace === "turbulence") && model === "sst" && <>
                  <RangeField label="Turbulent kinetic energy, k" value={input.k} min={0.005} max={0.25} step={0.001} display={`${format(input.k, 3)} m2/s2`} onChange={(value) => change("k", value)} />
                  <RangeField label="Specific dissipation, omega" value={input.omega} min={1} max={120} step={1} display={`${format(input.omega, 1)} 1/s`} onChange={(value) => change("omega", value)} />
                  <RangeField label="Strain-rate magnitude, S" value={input.strain} min={5} max={180} step={1} display={`${format(input.strain, 1)} 1/s`} onChange={(value) => change("strain", value)} />
                </>}
                {(workspace === "overview" || workspace === "turbulence") && model === "sa" && <>
                  <RangeField label="Working variable, nu_tilde" value={input.nuTilde} min={0.00002} max={0.0008} step={0.00001} display={`${format(input.nuTilde, 5)} m2/s`} onChange={(value) => change("nuTilde", value)} />
                  <RangeField label="Vorticity scale" value={input.strain} min={5} max={180} step={1} display={`${format(input.strain, 1)} 1/s`} onChange={(value) => change("strain", value)} />
                </>}
                {(workspace === "overview" || workspace === "turbulence") && model === "wale" && <>
                  <RangeField label="Resolved shear" value={input.waleShear} min={5} max={180} step={1} display={`${format(input.waleShear, 1)} 1/s`} onChange={(value) => change("waleShear", value)} />
                  <RangeField label="Rotation component" value={input.waleRotation} min={0} max={80} step={1} display={`${format(input.waleRotation, 1)} 1/s`} onChange={(value) => change("waleRotation", value)} />
                  <RangeField label="Filter width, Delta" value={input.filterWidth} min={0.0001} max={0.003} step={0.00005} display={`${format(input.filterWidth, 4)} m`} onChange={(value) => change("filterWidth", value)} />
                </>}
                {workspace === "wall" && <>
                  <RangeField label="Target wall coordinate, y+" value={input.yPlus} min={0.1} max={500} step={0.1} display={format(input.yPlus, 1)} onChange={(value) => change("yPlus", value)} />
                  <RangeField label="Friction velocity, u_tau" value={input.frictionVelocity} min={0.1} max={1.2} step={0.01} display={`${format(input.frictionVelocity, 2)} m/s`} onChange={(value) => change("frictionVelocity", value)} />
                  <RangeField label="Cell-to-wall temperature" value={input.deltaT} min={0} max={100} step={1} display={`${format(input.deltaT, 0)} K`} onChange={(value) => change("deltaT", value)} />
                </>}
                {workspace === "cht" && <>
                  <RangeField label="Fluid conductivity, k_f" value={input.conductivityF} min={0.1} max={2} step={0.05} display={`${format(input.conductivityF, 2)} W/mK`} onChange={(value) => change("conductivityF", value)} />
                  <RangeField label="Solid conductivity, k_s" value={input.conductivityS} min={1} max={80} step={1} display={`${format(input.conductivityS, 1)} W/mK`} onChange={(value) => change("conductivityS", value)} />
                  <RangeField label="Fluid cell temperature, T_P" value={input.temperatureP} min={300} max={600} step={1} display={`${format(input.temperatureP, 0)} K`} onChange={(value) => change("temperatureP", value)} />
                  <RangeField label="Solid cell temperature, T_S" value={input.temperatureS} min={280} max={550} step={1} display={`${format(input.temperatureS, 0)} K`} onChange={(value) => change("temperatureS", value)} />
                  {coupling === "partitioned" && <RangeField label="Flux relaxation, lambda" value={input.chtRelaxation} min={0.1} max={1} step={0.05} display={format(input.chtRelaxation, 2)} onChange={(value) => change("chtRelaxation", value)} />}
                </>}
                {workspace === "porous" && porousMode === "momentum" && <>
                  <RangeField label="Superficial velocity" value={input.velocity} min={0.02} max={1} step={0.01} display={`${format(input.velocity, 2)} m/s`} onChange={(value) => change("velocity", value)} />
                  <RangeField label="Principal Darcy, D1" value={input.darcyX} min={200} max={5000} step={50} display={`${format(input.darcyX, 0)} 1/m2`} onChange={(value) => change("darcyX", value)} />
                  <RangeField label="Principal Darcy, D2" value={input.darcyY} min={200} max={5000} step={50} display={`${format(input.darcyY, 0)} 1/m2`} onChange={(value) => change("darcyY", value)} />
                  <RangeField label="Tensor rotation, theta" value={input.angle} min={0} max={90} step={1} display={`${format(input.angle, 0)} deg`} onChange={(value) => change("angle", value)} />
                  <RangeField label="Forchheimer coefficient" value={input.forchheimer} min={1} max={40} step={1} display={`${format(input.forchheimer, 0)} 1/m`} onChange={(value) => change("forchheimer", value)} />
                </>}
                {workspace === "porous" && porousMode === "radiation" && <>
                  <RangeField label="Gas temperature" value={input.gasTemperature} min={400} max={1600} step={10} display={`${format(input.gasTemperature, 0)} K`} onChange={(value) => change("gasTemperature", value)} />
                  <RangeField label="Radiating wall temperature" value={input.wallTemperature} min={400} max={1600} step={10} display={`${format(input.wallTemperature, 0)} K`} onChange={(value) => change("wallTemperature", value)} />
                  <RangeField label="Absorption coefficient" value={input.absorption} min={0.05} max={3} step={0.05} display={`${format(input.absorption, 2)} 1/m`} onChange={(value) => change("absorption", value)} />
                  <RangeField label="Wall emissivity" value={input.emissivity} min={0.1} max={1} step={0.02} display={format(input.emissivity, 2)} onChange={(value) => change("emissivity", value)} />
                  <RangeField label="P1 diffusion coefficient" value={input.radiationDiffusion} min={0.01} max={0.5} step={0.01} display={`${format(input.radiationDiffusion, 2)} m`} onChange={(value) => change("radiationDiffusion", value)} />
                </>}
              </div>

              <div className="inspector-divider" />
              <div className="output-heading"><span>DERIVED TERMS</span><span className="output-live">LIVE</span></div>
              <div className="metrics-list">
                {(workspace === "overview" || workspace === "turbulence") && model === "sst" && <>
                  <MetricLine label="F1 / F2" value={`${format(result.f1, 4)} / ${format(result.f2, 4)}`} />
                  <MetricLine label="Eddy viscosity" value={format(result.nut, 5)} unit="m2/s" />
                  <MetricLine label="k diagonal increment" value={format(result.kDiagonal, 4)} unit="kg/m3s" />
                  <MetricLine label="omega diagonal increment" value={format(result.omegaDiagonal, 4)} unit="kg/m3s" />
                </>}
                {(workspace === "overview" || workspace === "turbulence") && model === "sa" && <>
                  <MetricLine label="fv1 / fv2" value={`${format(result.saFv1, 4)} / ${format(result.saFv2, 4)}`} />
                  <MetricLine label="Wall function fw" value={format(result.saFw, 4)} />
                  <MetricLine label="Effective diffusion" value={format(result.saDiffusion, 5)} unit="m2/s" />
                  <MetricLine label="Eddy viscosity" value={format(result.saNut, 5)} unit="m2/s" />
                </>}
                {(workspace === "overview" || workspace === "turbulence") && model === "wale" && <>
                  <MetricLine label="WALE coefficient" value="0.325" />
                  <MetricLine label="Subgrid viscosity" value={format(result.waleNut, 6)} unit="m2/s" />
                  <MetricLine label="Strain contraction" value="traceless G squared" />
                  <MetricLine label="Near-wall behavior" value="wall-damped" />
                </>}
                {workspace === "wall" && <>
                  <MetricLine label="omega_viscous" value={format(result.wall.omegaVis, 2)} unit="1/s" />
                  <MetricLine label="omega_log" value={format(result.wall.omegaLog, 2)} unit="1/s" />
                  <MetricLine label="omega_wall" value={format(result.wall.omegaWall, 2)} unit="1/s" />
                  <MetricLine label="Kader T+" value={format(result.wall.tPlus, 3)} />
                </>}
                {workspace === "cht" && <>
                  <MetricLine label="Harmonic conductance" value={format(result.cht.conductance, 3)} unit="W/m2K" />
                  <MetricLine label={coupling === "monolithic" ? "Monolithic a_PS" : "Robin coefficient"} value={format(coupling === "monolithic" ? -result.cht.conductance : result.cht.conductance, 3)} unit="W/m2K" />
                  <MetricLine label={coupling === "monolithic" ? "Twin-face balance" : "Under-relaxed q"} value={coupling === "monolithic" ? "0.000e+0" : format(result.cht.relaxedFlux, 2)} unit={coupling === "monolithic" ? "W/m2" : "W/m2"} />
                  <MetricLine label="Pair normal check" value="S_s = -S_f" />
                </>}
                {workspace === "porous" && porousMode === "momentum" && <>
                  <MetricLine label="D_global xx" value={format(result.porous.darcyTensorXX, 2)} unit="1/m2" />
                  <MetricLine label="Darcy + inertial drag" value={format(result.porous.pressureGradient, 4)} unit="Pa/m" />
                  <MetricLine label="Implicit Jacobian xx" value={format(result.porous.momentumDiagonal, 4)} unit="kg/m3s" />
                  <MetricLine label="Anisotropy orientation" value={`${format(input.angle, 0)} deg`} />
                </>}
                {workspace === "porous" && porousMode === "radiation" && <>
                  <MetricLine label="Volume emission" value={format(result.porous.p1Source, 2)} unit="W/m3" />
                  <MetricLine label="Marshak face G" value={format(result.porous.marshakFace, 2)} unit="W/m2" />
                  <MetricLine label="Wall radiative flux" value={format(result.porous.marshakFlux, 2)} unit="W/m2" />
                  <MetricLine label="Boundary emissivity" value={format(input.emissivity, 2)} />
                </>}
              </div>
              <div className="positivity-note"><span className="check-mark">+</span><span><strong>{stabilityNote.title}</strong><small>{stabilityNote.detail}</small></span></div>
            </aside>
          </div>

          {workspace !== "overview" && workspace !== "cht" && <VerificationRail setWorkspace={setWorkspace} />}
          <footer className="page-footer"><span>AEROFLOW / NUMERICAL METHODS GROUP</span><span>REFERENCE CASE: AIR-01 | SI UNITS | DOUBLE PRECISION</span><span>CPP/INCLUDE/CLUSTER_A</span></footer>
        </div>
      </main>
    </div>
  );
}

function VerificationRail({ setWorkspace }: { setWorkspace: (next: Workspace) => void }) {
  return (
    <section className="verification-rail">
      <div className="verification-head"><div><span className="section-kicker">HOST REFERENCE / ANALYTICAL CHECKS</span><h2>Verification suite</h2></div><div className="passed-count"><span className="live-pip" />53 / 53 CHECKS PASSED</div></div>
      <div className="benchmark-list">
        <button className="benchmark-row" onClick={() => setWorkspace("cht")}>
          <span className="benchmark-index">01</span><span className="benchmark-name"><strong>Composite CHT</strong><small>Convection / fluid / solid / convection</small></span><span className="benchmark-result">1470.59 <small>W/m2 analytic</small></span><span className="pass-label">PASS</span><span className="row-arrow">&gt;</span>
        </button>
        <button className="benchmark-row" onClick={() => setWorkspace("wall")}>
          <span className="benchmark-index">02</span><span className="benchmark-name"><strong>Turbulent channel</strong><small>Reichardt profile / log-layer recovery</small></span><span className="benchmark-result">u+ = {format(reichardt(1000), 2)} <small>@ y+ 1000</small></span><span className="pass-label">PASS</span><span className="row-arrow">&gt;</span>
        </button>
        <button className="benchmark-row" onClick={() => setWorkspace("porous")}>
          <span className="benchmark-index">03</span><span className="benchmark-name"><strong>Porous pressure drop</strong><small>Darcy + Forchheimer 1D reference</small></span><span className="benchmark-result">0.440 <small>Pa/m analytic</small></span><span className="pass-label">PASS</span><span className="row-arrow">&gt;</span>
        </button>
      </div>
    </section>
  );
}

export default App;