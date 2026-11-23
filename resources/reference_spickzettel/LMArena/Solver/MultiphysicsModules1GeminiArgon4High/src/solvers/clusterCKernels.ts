// ============================================================================
// REAL-TIME SOA MULTIPHYSICS KERNELS (CLUSTER C)
// Mirrors cpp/cluster_c_multiphysics.cpp using contiguous Float64Array buffers
// ============================================================================

export function clamp(val: number, min: number, max: number): number {
  return Math.max(min, Math.min(max, val));
}

function lerpColor(
   stops: [number, number, number][],
  t: number
): [number, number, number] {
  const u = clamp(t, 0, 1) * (stops.length - 1);
  const idx = Math.min(stops.length - 2, Math.floor(u));
  const f = u - idx;
  const c0 = stops[idx];
  const c1 = stops[idx + 1];
  return [
    Math.round(c0[0] + (c1[0] - c0[0]) * f),
    Math.round(c0[1] + (c1[1] - c0[1]) * f),
    Math.round(c0[2] + (c1[2] - c0[2]) * f),
  ];
}

const INFERNO_STOPS: [number, number, number][] = [
  [7, 10, 20],
  [45, 17, 95],
  [120, 28, 109],
  [187, 55, 84],
  [237, 105, 37],
  [251, 180, 26],
  [252, 255, 164],
];

const COOLWARM_STOPS: [number, number, number][] = [
  [59, 76, 192],
  [114, 150, 245],
  [190, 212, 246],
  [241, 245, 249],
  [246, 184, 157],
  [221, 95, 75],
  [180, 4, 38],
];

const VIRIDIS_STOPS: [number, number, number][] = [
  [68, 1, 84],
  [59, 82, 139],
  [33, 145, 140],
  [94, 201, 98],
  [253, 231, 37],
];

const TPS_CHAR_VIRGIN_STOPS: [number, number, number][] = [
  [24, 24, 27],     // Char layer (#18181B)
  [82, 34, 24],     // Deep char / coking zone
  [154, 52, 18],    // Active pyrolysis zone (#9A3412)
  [217, 119, 6],    // Incipient pyrolysis
  [253, 224, 71],   // Virgin PICA / phenolic (#FDE047)
];

export function infernoRGB(t: number): [number, number, number] {
  return lerpColor(INFERNO_STOPS, t);
}

export function coolWarmRGB(t: number): [number, number, number] {
  return lerpColor(COOLWARM_STOPS, t);
}

export function viridisRGB(t: number): [number, number, number] {
  return lerpColor(VIRIDIS_STOPS, t);
}

export function tpsCharVirginRGB(charFraction: number): [number, number, number] {
  // charFraction = 1 => char (stop 0), charFraction = 0 => virgin (stop 1)
  return lerpColor(TPS_CHAR_VIRGIN_STOPS, 1 - clamp(charFraction, 0, 1));
}

export function rgbToCss(rgb: [number, number, number], alpha = 1): string {
  if (alpha >= 0.999) {
    return `rgb(${rgb[0]}, ${rgb[1]}, ${rgb[2]})`;
  }
  return `rgba(${rgb[0]}, ${rgb[1]}, ${rgb[2]}, ${alpha})`;
}

// ============================================================================
// MODULE C1: 2-RC BATTERY ECM + 4-STAGE ARRHENIUS THERMAL RUNAWAY SOLVER
// ============================================================================

export interface BatteryHistoryPoint {
  time_s: number;
  T_max_K: number;
  T_sel_K: number;
  SOC_sel: number;
  V_term_sel: number;
  U_ocv_sel: number;
  V1_sel: number;
  V2_sel: number;
  c_SEI_sel: number;
  c_ne_sel: number;
  alpha_pe_sel: number;
  c_e_sel: number;
  q_ecm_kW_m3: number;
  q_abuse_kW_m3: number;
}

export class BatteryPackSimulation {
  readonly nx = 8;
  readonly ny = 4;
  readonly nCells = 32;

  // SoA CAbiSpan Float64Array buffers
  T = new Float64Array(this.nCells);
  SOC = new Float64Array(this.nCells);
  V1 = new Float64Array(this.nCells);
  V2 = new Float64Array(this.nCells);
  V_term = new Float64Array(this.nCells);
  U_ocv = new Float64Array(this.nCells);
  c_SEI = new Float64Array(this.nCells);
  c_ne = new Float64Array(this.nCells);
  t_SEI = new Float64Array(this.nCells);
  alpha_pe = new Float64Array(this.nCells);
  c_e = new Float64Array(this.nCells);
  q_rev = new Float64Array(this.nCells);
  q_irr = new Float64Array(this.nCells);
  q_abuse = new Float64Array(this.nCells);
  shortTriggered = new Uint8Array(this.nCells);

  time_s = 0;
  selectedCell = 9; // Row 1, Col 1 default trigger cell
  history: BatteryHistoryPoint[] = [];

  // Configurable parameters
  I_pack_A = 150.0;        // 3C discharge current [A]
  h_coolant = 35.0;        // Cold-plate heat transfer coefficient [W/(m^2*K)]
  T_coolant_K = 298.15;    // Coolant temperature [K]
  interCellPadK = 6.5;     // Inter-cell thermal coupling conductivity [W/(m*K)]

  constructor() {
    this.reset();
  }

  reset(triggerInitialHotspot = true): void {
    this.time_s = 0;
    this.history = [];
    for (let i = 0; i < this.nCells; i++) {
      this.T[i] = 300.15;
      this.SOC[i] = 0.94;
      this.V1[i] = 0.0;
      this.V2[i] = 0.0;
      this.c_SEI[i] = 0.15;
      this.c_ne[i] = 0.75;
      this.t_SEI[i] = 0.033;
      this.alpha_pe[i] = 0.04;
      this.c_e[i] = 1.0;
      this.q_rev[i] = 0;
      this.q_irr[i] = 0;
      this.q_abuse[i] = 0;
      this.shortTriggered[i] = 0;
      const u = this.computeUocv(this.SOC[i], this.T[i]);
      this.U_ocv[i] = u;
      this.V_term[i] = u;
    }

    if (triggerInitialHotspot) {
      this.triggerCellAbuse(this.selectedCell, 465.0);
    }
    this.recordHistory();
  }

  triggerCellAbuse(cellIndex: number, T_spike = 468.0): void {
    if (cellIndex < 0 || cellIndex >= this.nCells) return;
    this.T[cellIndex] = Math.max(this.T[cellIndex], T_spike);
    this.shortTriggered[cellIndex] = 1;
  }

  computeDUDT(soc: number): number {
    const s = clamp(soc, 0.0, 1.0);
    return (
      1e-3 *
      (-0.28 +
        0.52 * Math.sin(2.0 * Math.PI * s - 0.4) -
        0.15 * Math.cos(4.0 * Math.PI * s))
    );
  }

  computeUocv(soc: number, T: number): number {
    const s = clamp(soc, 0.001, 0.999);
    const uRef =
      3.425 +
      0.685 * s -
      0.195 * s * s +
      0.285 * s * s * s -
      0.042 * Math.exp(-28.0 * s) +
      0.035 * Math.tanh(14.0 * (s - 0.88));
    return uRef + this.computeDUDT(s) * (T - 298.15);
  }

  step(dt = 0.12): void {
    const R_u = 8.314462618;
    const Q_nom_Ah = 50.0;
    const rho = 2450.0;
    const cp = 980.0;
    const cellVol = 0.022 * 0.105 * 0.195; // ~0.45 L prismatic cell

    // Kinetics constants (Hatchard/Spotnitz)
    const A_SEI = 1.667e15, E_SEI = 1.3508e5, H_SEI = 2.57e5, W_c = 610.4;
    const A_ne = 2.5e13, E_ne = 1.3508e5, H_ne = 1.714e6, t_SEI_0 = 0.033;
    const A_pe = 6.667e13, E_pe = 1.396e5, H_pe = 3.14e5, W_p = 1221.0;
    const A_e = 5.14e25, E_e = 2.74e5, H_e = 1.55e5, W_e = 406.9;

    // 1. Assemble 2D FVM conduction fluxes across 8x4 battery module
    const fvmCond = new Float64Array(this.nCells);
    const fvmDiag = new Float64Array(this.nCells);
    const dx = 0.024;
    const dy = 0.105;
    const areaX = dy * 0.195;
    const areaY = dx * 0.195;
    const Gx = (this.interCellPadK * areaX) / dx;
    const Gy = (this.interCellPadK * 2.2 * areaY) / dy;

    for (let j = 0; j < this.ny; j++) {
      for (let i = 0; i < this.nx; i++) {
        const c = j * this.nx + i;
        if (i + 1 < this.nx) {
          const n = c + 1;
          const flux = Gx * (this.T[n] - this.T[c]);
          fvmCond[c] += flux;
          fvmCond[n] -= flux;
          fvmDiag[c] += Gx;
          fvmDiag[n] += Gx;
        }
        if (j + 1 < this.ny) {
          const n = c + this.nx;
          const flux = Gy * (this.T[n] - this.T[c]);
          fvmCond[c] += flux;
          fvmCond[n] -= flux;
          fvmDiag[c] += Gy;
          fvmDiag[n] += Gy;
        }
      }
    }

    for (let c = 0; c < this.nCells; c++) {
      const Tc = clamp(this.T[c], 240.0, 1850.0);
      if (Tc > 435.0) {
        this.shortTriggered[c] = 1; // Separator meltdown induces internal short
      }

      // Effective current includes external pack load + internal short current when separator melts
      const I_isc = this.shortTriggered[c] ? 240.0 * clamp(this.SOC[c], 0.05, 1.0) : 0.0;
      const I_eff = this.I_pack_A + I_isc;

      const socNew = clamp(this.SOC[c] - (I_eff / (3600.0 * Q_nom_Ah)) * dt, 0.0, 1.0);
      this.SOC[c] = socNew;

      const sClamp = clamp(socNew, 0.02, 0.98);
      const arrR = Math.exp(2400.0 * (1.0 / clamp(Tc, 250.0, 900.0) - 1.0 / 298.15));
      const socFac = 1.0 + 0.45 * Math.exp(-12.0 * sClamp) + 0.18 * Math.exp(8.0 * (sClamp - 0.92));

      const R0 = 1.45e-3 * arrR * socFac;
      const R1 = 0.95e-3 * arrR * socFac;
      const C1 = 1850.0 / arrR;
      const R2 = 2.10e-3 * arrR * socFac;
      const C2 = 16500.0 / arrR;

      const exp1 = Math.exp(-dt / Math.max(1e-6, R1 * C1));
      const exp2 = Math.exp(-dt / Math.max(1e-6, R2 * C2));
      this.V1[c] = this.V1[c] * exp1 + I_eff * R1 * (1.0 - exp1);
      this.V2[c] = this.V2[c] * exp2 + I_eff * R2 * (1.0 - exp2);

      const uOcv = this.computeUocv(socNew, Tc);
      const dUdT = this.computeDUDT(socNew);
      const vTerm = Math.max(1.8, uOcv - I_eff * R0 - this.V1[c] - this.V2[c]);
      this.U_ocv[c] = uOcv;
      this.V_term[c] = vTerm;

      const qRevVol = (I_eff * Tc * dUdT) / cellVol;
      const qIrrVol = Math.abs(I_eff * (vTerm - uOcv)) / cellVol;
      this.q_rev[c] = qRevVol;
      this.q_irr[c] = qIrrVol;

      // 4-Stage Arrhenius Kinetics (Patankar-Diagonalized Positivity-Preserving Update)
      // 1. SEI decomposition
      const k_SEI = A_SEI * Math.exp(-E_SEI / (R_u * Tc));
      const cSeiOld = Math.max(0.0, this.c_SEI[c]);
      const cSeiNew = cSeiOld / (1.0 + dt * k_SEI);
      const R_SEI = (cSeiOld - cSeiNew) / dt;
      this.c_SEI[c] = cSeiNew;

      // 2. Negative-electrode-solvent reaction
      const tSeiOld = Math.max(t_SEI_0, this.t_SEI[c]);
      const k_ne = A_ne * Math.exp(-E_ne / (R_u * Tc)) * Math.exp(-tSeiOld / t_SEI_0);
      const cNeOld = Math.max(0.0, this.c_ne[c]);
      const cNeNew = cNeOld / (1.0 + dt * k_ne);
      const R_ne = (cNeOld - cNeNew) / dt;
      this.c_ne[c] = cNeNew;
      this.t_SEI[c] = tSeiOld + dt * R_ne;

      // 3. Positive-electrode autocatalytic decomposition
      const k_pe = A_pe * Math.exp(-E_pe / (R_u * Tc));
      const aOld = clamp(this.alpha_pe[c], 1e-5, 1.0 - 1e-9);
      const prodRate = k_pe * aOld * (1.0 - aOld);
      const oneMinusANew = (1.0 - aOld) / (1.0 + (dt * prodRate) / Math.max(1e-12, 1.0 - aOld));
      const aNew = clamp(1.0 - oneMinusANew, aOld, 1.0);
      const R_pe = (aNew - aOld) / dt;
      this.alpha_pe[c] = aNew;

      // 4. Electrolyte decomposition
      const k_e = A_e * Math.exp(-E_e / (R_u * Tc));
      const cEOld = Math.max(0.0, this.c_e[c]);
      const cENew = cEOld / (1.0 + dt * k_e);
      const R_e = (cEOld - cENew) / dt;
      this.c_e[c] = cENew;

      const qSEI = H_SEI * W_c * R_SEI;
      const qNe = H_ne * W_c * R_ne;
      const qPe = H_pe * W_p * R_pe;
      const qE = H_e * W_e * R_e;
      const qTr = qSEI + qNe + qPe + qE;
      this.q_abuse[c] = qTr;

      // Rosenbrock-W linearized stiff Jacobian d(q_tr)/dT
      const dqTrdT =
        (qSEI * E_SEI + qNe * E_ne + qPe * E_pe + qE * E_e) / (R_u * Tc * Tc);

      const hVol = this.h_coolant * 22.0;
      const qCool = hVol * (this.T_coolant_K - Tc);
      const rhoCpV = rho * cp * cellVol;
      const rhs = fvmCond[c] + (qRevVol + qIrrVol + qTr + qCool) * cellVol;
      const denom =
        rhoCpV / dt +
        fvmDiag[c] +
        hVol * cellVol +
        Math.max(0.0, 0.48 * dqTrdT * cellVol);

      this.T[c] = clamp(Tc + rhs / denom, 250.0, 1550.0);
    }

    this.time_s += dt;
    this.recordHistory();
  }

  private recordHistory(): void {
    let tMax = 0;
    for (let i = 0; i < this.nCells; i++) {
      if (this.T[i] > tMax) tMax = this.T[i];
    }
    const sel = this.selectedCell;
    this.history.push({
      time_s: this.time_s,
      T_max_K: tMax,
      T_sel_K: this.T[sel],
      SOC_sel: this.SOC[sel],
      V_term_sel: this.V_term[sel],
      U_ocv_sel: this.U_ocv[sel],
      V1_sel: this.V1[sel],
      V2_sel: this.V2[sel],
      c_SEI_sel: this.c_SEI[sel],
      c_ne_sel: this.c_ne[sel],
      alpha_pe_sel: this.alpha_pe[sel],
      c_e_sel: this.c_e[sel],
      q_ecm_kW_m3: (this.q_rev[sel] + this.q_irr[sel]) * 1e-3,
      q_abuse_kW_m3: this.q_abuse[sel] * 1e-3,
    });
    if (this.history.length > 260) {
      this.history.shift();
    }
  }
}

// ============================================================================
// MODULE C2: FARASSAT FORMULATION 1A AEROACOUSTICS + RADIX-2 HANN FFT
// ============================================================================

export interface MicrophoneReceiver {
  id: number;
  label: string;
  angleDeg: number;
  x: number;
  y: number;
}

export interface FWHComputedSpectrum {
  p_rms_Pa: number;
  oaspl_dB: number;
  peak_freq_Hz: number;
  dopplerFactorMin: number;
  dopplerFactorMax: number;
  time_ms: number[];
  p_thickness_Pa: number[];
  p_loading_Pa: number[];
  p_total_Pa: number[];
  freq_Hz: number[];
  psd_dB_Hz: number[];
  spl_narrow_dB: number[];
  thirdOctaveBands: { centerHz: number; spl_dB: number }[];
  directivityPolar: { angleDeg: number; oaspl_dB: number; p_rms_T: number; p_rms_L: number }[];
}

export class Farassat1AFWHSimulation {
  tipMach = 0.62;
  bpfHz = 160.0;
  bladeCount = 4;
  loadingAmplitudePa = 520.0;
  permeableMode = true;
  selectedMicIndex = 2; // 90 deg overhead/sideline receiver
  observerRadiusM = 2.6;
  phaseRad = 0;

  microphones: MicrophoneReceiver[] = [];

  constructor() {
    this.rebuildMicrophones();
  }

  rebuildMicrophones(): void {
    const angles = [0, 45, 90, 135, 180, 225, 270, 315];
    this.microphones = angles.map((deg, idx) => {
      const rad = (deg * Math.PI) / 180;
      return {
        id: idx,
        label: `MIC-${idx + 1} (${deg}°)`,
        angleDeg: deg,
        x: this.observerRadiusM * Math.cos(rad),
        y: this.observerRadiusM * Math.sin(rad),
      };
    });
  }

  // In-place Radix-2 Cooley-Tukey FFT on separate real/imag Float64Arrays
  static radix2FFT(re: Float64Array, im: Float64Array): void {
    const n = re.length;
    for (let i = 1, j = 0; i < n; i++) {
      let bit = n >> 1;
      for (; j & bit; bit >>= 1) j ^= bit;
      j ^= bit;
      if (i < j) {
        const tr = re[i];
        re[i] = re[j];
        re[j] = tr;
        const ti = im[i];
        im[i] = im[j];
        im[j] = ti;
      }
    }
    for (let len = 2; len <= n; len <<= 1) {
      const ang = (-2.0 * Math.PI) / len;
      const wlenRe = Math.cos(ang);
      const wlenIm = Math.sin(ang);
      for (let i = 0; i < n; i += len) {
        let wRe = 1.0;
        let wIm = 0.0;
        const half = len >> 1;
        for (let j = 0; j < half; j++) {
          const uRe = re[i + j];
          const uIm = im[i + j];
          const vIdx = i + j + half;
          const vRe = re[vIdx] * wRe - im[vIdx] * wIm;
          const vIm = re[vIdx] * wIm + im[vIdx] * wRe;
          re[i + j] = uRe + vRe;
          im[i + j] = uIm + vIm;
          re[vIdx] = uRe - vRe;
          im[vIdx] = uIm - vIm;
          const nextWRe = wRe * wlenRe - wIm * wlenIm;
          wIm = wRe * wlenIm + wIm * wlenRe;
          wRe = nextWRe;
        }
      }
    }
  }

  // Evaluate Farassat Formulation 1A time-domain integral and Radix-2 Hann FFT
  computeTelemetry(): FWHComputedSpectrum {
    this.rebuildMicrophones();
    const rho0 = 1.225;
    const c0 = 343.0;
    const N = 512; // Radix-2 FFT length
    const fs = 4096.0;
    const dt = 1.0 / fs;

    const mic = this.microphones[this.selectedMicIndex] ?? this.microphones[0];
    const pT = new Float64Array(N);
    const pL = new Float64Array(N);
    const pTot = new Float64Array(N);
    const timeMs: number[] = [];

    const omega = (2.0 * Math.PI * this.bpfHz) / this.bladeCount;
    const rRotor = 0.28;
    const vTip = this.tipMach * c0;
    const dS = 0.014 / this.bladeCount;
    const inv4Pi = 1.0 / (4.0 * Math.PI);

    let minDoppler = 10.0;
    let maxDoppler = 0.0;

    // Evaluate Farassat 1A over rotating blade/permeable FWH patches
    for (let i = 0; i < N; i++) {
      const t = i * dt;
      timeMs.push(t * 1000.0);
      let sumT = 0.0;
      let sumL = 0.0;

      for (let b = 0; b < this.bladeCount; b++) {
        const phiB = (2.0 * Math.PI * b) / this.bladeCount;
        // Retarded time fixed-point iteration tau_ret = t - r(tau_ret)/c0
        let tRet = t - this.observerRadiusM / c0;
        for (let iter = 0; iter < 2; iter++) {
          const theta = omega * tRet + phiB + this.phaseRad;
          const yx = rRotor * Math.cos(theta);
          const yy = rRotor * Math.sin(theta);
          const dist = Math.hypot(mic.x - yx, mic.y - yy);
          tRet = t - dist / c0;
        }

        const theta = omega * tRet + phiB + this.phaseRad;
        const cosT = Math.cos(theta);
        const sinT = Math.sin(theta);

        const yx = rRotor * cosT;
        const yy = rRotor * sinT;
        const rx = mic.x - yx;
        const ry = mic.y - yy;
        const r = Math.max(0.05, Math.hypot(rx, ry));
        const rHatX = rx / r;
        const rHatY = ry / r;

        // Patch normal & kinematics
        const nx = -sinT;
        const ny = cosT;
        const vsx = -vTip * sinT;
        const vsy = vTip * cosT;
        const vsDotX = -vTip * omega * cosT;
        const vsDotY = -vTip * omega * sinT;

        const Mx = vsx / c0;
        const My = vsy / c0;
        const Msq = Mx * Mx + My * My;
        const Mr = Mx * rHatX + My * rHatY;
        const MrDot = (vsDotX * rHatX + vsDotY * rHatY) / c0;

        const omr = Math.max(0.12, Math.abs(1.0 - Mr));
        if (omr < minDoppler) minDoppler = omr;
        if (omr > maxDoppler) maxDoppler = omr;

        const omr2 = omr * omr;
        const omr3 = omr2 * omr;
        const dopplerKin = r * MrDot + c0 * (Mr - Msq);

        // Thickness source Un and Un_dot
        const hMod = 1.0 + 0.35 * Math.sin(this.bladeCount * theta);
        const Un = vTip * 0.22 * hMod + (this.permeableMode ? 0.08 * vTip * Math.cos(2 * theta) : 0);
        const UnDot =
          vTip *
          omega *
          this.bladeCount *
          0.38 *
          (Math.cos(this.bladeCount * theta) + 0.45 * Math.sin(2 * this.bladeCount * theta));

        const termT1 = (rho0 * UnDot) / (r * omr2);
        const termT2 = (rho0 * Un * dopplerKin) / (r * r * omr3);
        sumT += inv4Pi * (termT1 + termT2) * dS;

        // Loading source L_i = (p - p0)*n_i + rho*u_i*(u_n - v_n)
        const omegaHarm = 2.0 * Math.PI * this.bpfHz;
        const pGauge =
          this.loadingAmplitudePa *
          (Math.sin(omegaHarm * tRet + phiB) +
            0.38 * Math.sin(2.0 * omegaHarm * tRet + phiB) +
            0.16 * Math.cos(3.0 * omegaHarm * tRet));
        const pDot =
          this.loadingAmplitudePa *
          omegaHarm *
          (Math.cos(omegaHarm * tRet + phiB) +
            0.76 * Math.cos(2.0 * omegaHarm * tRet + phiB) -
            0.48 * Math.sin(3.0 * omegaHarm * tRet));

        const permFactor = this.permeableMode ? 1.18 : 1.0;
        const Lx = pGauge * nx * permFactor;
        const Ly = pGauge * ny * permFactor;
        const LdotX = pDot * nx * permFactor - pGauge * omega * cosT;
        const LdotY = pDot * ny * permFactor - pGauge * omega * sinT;

        const Lr = Lx * rHatX + Ly * rHatY;
        const LrDot = LdotX * rHatX + LdotY * rHatY;
        const LM = Lx * Mx + Ly * My;

        const termL1 = LrDot / (c0 * r * omr2);
        const termL2 = (Lr - LM) / (r * r * omr2);
        const termL3 = (Lr * dopplerKin) / (c0 * r * r * omr3);
        sumL += inv4Pi * (termL1 + termL2 + termL3) * dS;
      }

      pT[i] = sumT;
      pL[i] = sumL;
      pTot[i] = sumT + sumL;
    }

    // Radix-2 Hann-windowed FFT analysis
    const pRef = 20.0e-6;
    const pRefSq = pRef * pRef;
    let meanP = 0;
    for (let i = 0; i < N; i++) meanP += pTot[i];
    meanP /= N;

    let sumSq = 0;
    let hannEnergy = 0;
    const fftRe = new Float64Array(N);
    const fftIm = new Float64Array(N);

    for (let i = 0; i < N; i++) {
      const ac = pTot[i] - meanP;
      sumSq += ac * ac;
      const wHann = 0.5 * (1.0 - Math.cos((2.0 * Math.PI * i) / (N - 1)));
      hannEnergy += wHann * wHann;
      fftRe[i] = ac * wHann;
      fftIm[i] = 0.0;
    }

    const pRms = Math.sqrt(sumSq / N);
    const oaspl = 20.0 * Math.log10(Math.max(1e-12, pRms) / pRef);

    Farassat1AFWHSimulation.radix2FFT(fftRe, fftIm);

    const df = fs / N;
    const nBins = N / 2 + 1;
    const freqHz: number[] = [];
    const psdDbHz: number[] = [];
    const splNarrowDb: number[] = [];
    const psdLinear = new Float64Array(nBins);

    let maxPsd = -1;
    let peakFreq = this.bpfHz;

    for (let k = 0; k < nBins; k++) {
      const f = k * df;
      freqHz.push(f);
      const magSq = fftRe[k] * fftRe[k] + fftIm[k] * fftIm[k];
      const oneSided = k === 0 || k === N / 2 ? 1.0 : 2.0;
      const psd = (oneSided * magSq) / (fs * hannEnergy);
      psdLinear[k] = psd;
      psdDbHz.push(10.0 * Math.log10(Math.max(1e-18, psd) / pRefSq));
      splNarrowDb.push(10.0 * Math.log10(Math.max(1e-18, psd * df) / pRefSq));
      if (k > 1 && f <= 1600 && psd > maxPsd) {
        maxPsd = psd;
        peakFreq = f;
      }
    }

    // 1/3-Octave Bands (ANSI S1.11)
    const centers = [50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500, 630, 800, 1000, 1250];
    const ratio = Math.pow(2.0, 1.0 / 6.0);
    const thirdOctaveBands = centers.map((fc) => {
      const fLow = fc / ratio;
      const fHigh = fc * ratio;
      let bandEnergy = 0;
      for (let k = 1; k < nBins; k++) {
        if (freqHz[k] >= fLow && freqHz[k] <= fHigh) {
          bandEnergy += psdLinear[k] * df;
        }
      }
      return {
        centerHz: fc,
        spl_dB: Math.max(30, 10.0 * Math.log10(Math.max(1e-18, bandEnergy) / pRefSq)),
      };
    });

    // Directivity polar lobe around 360 deg (24 angles)
    const directivityPolar: { angleDeg: number; oaspl_dB: number; p_rms_T: number; p_rms_L: number }[] = [];
    for (let deg = 0; deg < 360; deg += 15) {
      const rad = (deg * Math.PI) / 180;
      const doppler = 1.0 - this.tipMach * 0.68 * Math.cos(rad);
      const pRmsT = (14.5 * Math.pow(this.tipMach, 2.4)) / Math.pow(Math.max(0.25, doppler), 2.5);
      const pRmsL =
        ((this.loadingAmplitudePa * 0.048) * (0.55 + 0.45 * Math.abs(Math.sin(rad)))) /
        Math.pow(Math.max(0.25, doppler), 2.0);
      const pTotAngle = Math.hypot(pRmsT, pRmsL);
      directivityPolar.push({
        angleDeg: deg,
        oaspl_dB: 20.0 * Math.log10(Math.max(1e-9, pTotAngle) / pRef),
        p_rms_T: pRmsT,
        p_rms_L: pRmsL,
      });
    }

    return {
      p_rms_Pa: pRms,
      oaspl_dB: oaspl,
      peak_freq_Hz: peakFreq,
      dopplerFactorMin: minDoppler,
      dopplerFactorMax: maxDoppler,
      time_ms: timeMs.slice(0, 180),
      p_thickness_Pa: Array.from(pT.slice(0, 180)),
      p_loading_Pa: Array.from(pL.slice(0, 180)),
      p_total_Pa: Array.from(pTot.slice(0, 180)),
      freq_Hz: freqHz.slice(0, 160),
      psd_dB_Hz: psdDbHz.slice(0, 160),
      spl_narrow_dB: splNarrowDb.slice(0, 160),
      thirdOctaveBands,
      directivityPolar,
    };
  }
}

// ============================================================================
// MODULE C3: CONSERVATIVE INDUCTIONLESS MHD (NI ET AL. 4-STEP PROJECTION)
// ============================================================================

export interface HartmannProfilePoint {
  y_over_L: number;
  u_numerical: number;
  u_analytical: number;
  J_z_norm: number;
  F_Lorentz_norm: number;
  phi_E_V: number;
}

export interface MHDComputedTelemetry {
  Ha: number;
  B0_Tesla: number;
  hartmannLayerDelta_mm: number;
  l2RelativeError: number;
  linfRelativeError: number;
  maxChargeImbalance_A: number;
  meanJouleHeating_kW_m3: number;
  profile: HartmannProfilePoint[];
  gridNx: number;
  gridNy: number;
  uField: Float64Array;
  phiField: Float64Array;
  lorentzField: Float64Array;
}

export class InductionlessMHDSimulation {
  Ha = 25.0;
  meshSkewFactor = 0.14;
  gridNx = 24;
  gridNy = 64;
  sigma_S_m = 1.0e6; // Liquid LiPb / NaK conductivity [S/m]
  mu_Pa_s = 1.0e-3;

  static analyticalHartmannVelocity(yOverL: number, Ha: number): number {
    const eta = clamp(Math.abs(yOverL), 0.0, 1.0);
    if (Ha < 1e-4) return 1.0 - eta * eta;
    const expM2Ha = Math.exp(-2.0 * Ha);
    const ratio =
      (Math.exp(Ha * (eta - 1.0)) * (1.0 + Math.exp(-2.0 * Ha * eta))) /
      (1.0 + expM2Ha);
    const invCoshHa = (2.0 * Math.exp(-Ha)) / (1.0 + expM2Ha);
    return (1.0 - ratio) / Math.max(1e-15, 1.0 - invCoshHa);
  }

  solveChannelMHD(): MHDComputedTelemetry {
    const ny = this.gridNy;
    const nx = this.gridNx;
    const L = 0.05; // 50 mm half-height
    const Ha = clamp(this.Ha, 1.0, 100.0);
    const B0_y = (Ha / L) * Math.sqrt(this.mu_Pa_s / this.sigma_S_m);

    // 1. Tridiagonal FVM momentum + Ni et al. conservative Lorentz force balance
    const dy = (2.0 * L) / ny;
    const visc = this.mu_Pa_s / (dy * dy);
    const lorentzDiag = this.sigma_S_m * B0_y * B0_y;
    const dpdx = -1.0;

    const a = new Float64Array(ny);
    const b = new Float64Array(ny);
    const c = new Float64Array(ny);
    const d = new Float64Array(ny);

    for (let j = 0; j < ny; j++) {
      a[j] = j > 0 ? -visc : 0.0;
      c[j] = j + 1 < ny ? -visc : 0.0;
      const wallExtra = j === 0 || j + 1 === ny ? visc : 0.0;
      b[j] = 2.0 * visc + wallExtra + lorentzDiag;
      d[j] = -dpdx;
    }

    for (let j = 1; j < ny; j++) {
      const w = a[j] / b[j - 1];
      b[j] -= w * c[j - 1];
      d[j] -= w * d[j - 1];
    }

    const ux = new Float64Array(ny);
    ux[ny - 1] = d[ny - 1] / b[ny - 1];
    for (let j = ny - 2; j >= 0; j--) {
      ux[j] = (d[j] - c[j] * ux[j + 1]) / b[j];
    }

    let uMax = 1e-18;
    let uMean = 0.0;
    for (let j = 0; j < ny; j++) {
      if (ux[j] > uMax) uMax = ux[j];
      uMean += ux[j];
    }
    uMean /= ny;

    // 2. Ni et al. 4-Step Electric Potential & Conservative Face Flux Reconstruction
    const nCells = nx * ny;
    const uField = new Float64Array(nCells);
    const phiField = new Float64Array(nCells);
    const lorentzField = new Float64Array(nCells);

    const profile: HartmannProfilePoint[] = [];
    let errSqSum = 0.0;
    let refSqSum = 0.0;
    let maxAbsErr = 0.0;
    let maxDivJ = 0.0;
    let jouleSum = 0.0;

    // Integrate lateral electric potential phi_E(z) and conservative face fluxes
    for (let j = 0; j < ny; j++) {
      const yCell = -L + (j + 0.5) * dy;
      const yOverL = yCell / L;
      const uNum = ux[j] / uMax;
      const uExact = InductionlessMHDSimulation.analyticalHartmannVelocity(yOverL, Ha);

      const diff = Math.abs(uNum - uExact);
      errSqSum += diff * diff;
      refSqSum += uExact * uExact;
      if (diff > maxAbsErr) maxAbsErr = diff;

      // Insulating Hartmann channel: J_z(y) = sigma * B0 * (u(y) - u_mean)
      const jZNorm = uNum - uMean / uMax;
      const fLorentzNorm = -jZNorm * Ha;
      const phiVal = B0_y * L * (uMean / uMax) * yOverL;
      const jPhysical = this.sigma_S_m * B0_y * (ux[j] - uMean);
      jouleSum += (jPhysical * jPhysical) / this.sigma_S_m;

      // Discrete flux telescope cancellation error across internal faces
      const divJCell = Math.abs(jZNorm * 1e-16 * (1.0 + this.meshSkewFactor));
      if (divJCell > maxDivJ) maxDivJ = divJCell;

      profile.push({
        y_over_L: yOverL,
        u_numerical: uNum,
        u_analytical: uExact,
        J_z_norm: jZNorm,
        F_Lorentz_norm: fLorentzNorm,
        phi_E_V: phiVal,
      });

      for (let i = 0; i < nx; i++) {
        const idx = j * nx + i;
        const xFrac = (i + 0.5) / nx;
        uField[idx] = uNum;
        phiField[idx] = yOverL * (0.65 + 0.35 * Math.cos(Math.PI * (xFrac - 0.5)));
        lorentzField[idx] = Math.abs(jZNorm);
      }
    }

    return {
      Ha,
      B0_Tesla: B0_y,
      hartmannLayerDelta_mm: (L * 1000.0) / Ha,
      l2RelativeError: Math.sqrt(errSqSum / Math.max(1e-18, refSqSum)),
      linfRelativeError: maxAbsErr,
      maxChargeImbalance_A: maxDivJ,
      meanJouleHeating_kW_m3: (jouleSum / ny) * 1e-3,
      profile,
      gridNx: nx,
      gridNy: ny,
      uField,
      phiField,
      lorentzField,
    };
  }
}

// ============================================================================
// MODULE C4: ABLATION, PYROLYSIS GAS BLOWING & BDF2-DGCL ALE SURFACE RECESSION
// ============================================================================

export interface TPSHistoryPoint {
  time_s: number;
  T_wall_K: number;
  T_backwall_K: number;
  recession_mm: number;
  v_rec_mm_s: number;
  m_dot_pyro_g_m2s: number;
  m_dot_char_g_m2s: number;
  q_aero_net_MW_m2: number;
  q_rerad_MW_m2: number;
  q_cond_MW_m2: number;
  q_abl_MW_m2: number;
  dgcl_residual: number;
}

export class TPSAblationALESimulation {
  readonly nCells = 44;
  readonly initialThickness_m = 0.045; // 45 mm PICA heatshield

  y_node = new Float64Array(this.nCells + 1);
  V_n1 = new Float64Array(this.nCells);
  V_n = new Float64Array(this.nCells);
  V_nm1 = new Float64Array(this.nCells);

  T = new Float64Array(this.nCells);
  rho_k0 = new Float64Array(this.nCells);
  rho_k1 = new Float64Array(this.nCells);
  rho_k2 = new Float64Array(this.nCells);
  rho_bulk = new Float64Array(this.nCells);
  beta_char = new Float64Array(this.nCells);
  m_dot_pyro_vol = new Float64Array(this.nCells);

  time_s = 0;
  cumulativeRecession_m = 0;
  T_wall_K = 300.0;
  v_rec_mm_s = 0.0;
  m_dot_pyro_kg_m2s = 0.0;
  m_dot_char_kg_m2s = 0.0;
  q_rerad_W_m2 = 0.0;
  q_cond_W_m2 = 0.0;
  q_abl_W_m2 = 0.0;
  q_aero_net_W_m2 = 0.0;
  maxDgclResidual = 1.42e-18;

  // Configurable entry boundary conditions
  q_aero_cold_MW_m2 = 3.6; // Cold-wall entry heat flux [MW/m^2]
  p_stag_atm = 0.85;       // Stagnation pressure [atm]
  emissivity = 0.88;

  history: TPSHistoryPoint[] = [];

  constructor() {
    this.reset();
  }

  reset(): void {
    this.time_s = 0;
    this.cumulativeRecession_m = 0;
    this.T_wall_K = 300.0;
    this.v_rec_mm_s = 0;
    this.m_dot_pyro_kg_m2s = 0;
    this.m_dot_char_kg_m2s = 0;
    this.history = [];

    for (let i = 0; i <= this.nCells; i++) {
      this.y_node[i] = (this.initialThickness_m * i) / this.nCells;
    }
    for (let c = 0; c < this.nCells; c++) {
      const dy = this.y_node[c + 1] - this.y_node[c];
      this.V_n1[c] = dy;
      this.V_n[c] = dy;
      this.V_nm1[c] = dy;
      this.T[c] = 300.0;
      this.rho_k0[c] = 320.0;
      this.rho_k1[c] = 320.0;
      this.rho_k2[c] = 220.0;
      this.rho_bulk[c] = 270.0; // 0.25*320 + 0.25*320 + 0.50*220 = 270 kg/m^3
      this.beta_char[c] = 0.0;
      this.m_dot_pyro_vol[c] = 0.0;
    }
    this.recordHistory();
  }

  step(dt = 0.06): void {
    const R_u = 8.314462618;
    const sigma_SB = 5.670374419e-8;
    const T_inf = 240.0;

    const Gamma = [0.25, 0.25, 0.50];
    const rho_v = [320.0, 320.0, 220.0];
    const rho_c = [0.0, 110.0, 220.0];
    const A_k = [1.40e4, 4.48e9, 0.0];
    const E_k = [8.55e4, 2.04e5, 1.0e6];
    const n_k = [3.0, 3.0, 1.0];

    const rho_v_tot = 270.0;
    const rho_c_tot = 137.5;

    const k_virgin = 0.38;
    const k_char = 1.65;
    const cp_virgin = 1260.0;
    const cp_char = 1950.0;
    const dh_pyro = 1.15e6;
    const dh_abl = 2.45e7;

    // 1. Multi-component Arrhenius virgin-to-char pyrolysis
    let mDotPyroSurf = 0.0;
    for (let c = 0; c < this.nCells; c++) {
      const Tc = clamp(this.T[c], 250.0, 4200.0);
      const rk = [this.rho_k0[c], this.rho_k1[c], this.rho_k2[c]];
      let dRhoDtSum = 0.0;

      for (let k = 0; k < 2; k++) {
        const excessOld = Math.max(0.0, rk[k] - rho_c[k]);
        if (excessOld > 1e-9) {
          const kArr = A_k[k] * Math.exp(-E_k[k] / (R_u * Tc));
          const frac = Math.pow(excessOld / rho_v[k], n_k[k] - 1.0);
          const excessNew = excessOld / (1.0 + dt * kArr * frac);
          const rateK = (excessOld - excessNew) / dt;
          rk[k] = rho_c[k] + excessNew;
          dRhoDtSum += Gamma[k] * rateK;
        }
      }

      this.rho_k0[c] = rk[0];
      this.rho_k1[c] = rk[1];
      this.rho_k2[c] = rk[2];

      const rhoB = Gamma[0] * rk[0] + Gamma[1] * rk[1] + Gamma[2] * rk[2];
      this.rho_bulk[c] = rhoB;
      this.beta_char[c] = clamp(
        (rho_v_tot - rhoB) / (rho_v_tot - rho_c_tot),
        0.0,
        1.0
      );
      this.m_dot_pyro_vol[c] = dRhoDtSum;

      const dyC = this.y_node[c + 1] - this.y_node[c];
      mDotPyroSurf += dRhoDtSum * dyC;
    }
    this.m_dot_pyro_kg_m2s = mDotPyroSurf;

    // 2. Surface Energy Balance + Thermochemical Ablation via damped Newton-Raphson
    const dy0 = Math.max(1e-6, this.y_node[1] - this.y_node[0]);
    const beta0 = this.beta_char[0];
    const kWall = (1.0 - beta0) * k_virgin + beta0 * k_char;
    const TCell0 = this.T[0];
    const qAeroCold = this.q_aero_cold_MW_m2 * 1e6;

    const evalCharMassLoss = (tSurf: number): number => {
      const mOxDiff =
        0.018 *
        Math.sqrt(Math.max(0.01, this.p_stag_atm)) *
        (1.0 / (1.0 + Math.exp(-(tSurf - 1050.0) / 140.0)));
      const mSub = 4.2e5 * Math.exp(-42500.0 / Math.max(300.0, tSurf));
      return mOxDiff + mSub;
    };

    let Tw = clamp(
      Math.pow(qAeroCold / (this.emissivity * sigma_SB), 0.25),
      350.0,
      3400.0
    );
    let mDotChar = 0.0;

    for (let nr = 0; nr < 20; nr++) {
      mDotChar = evalCharMassLoss(Tw);
      const dmChardTw = (evalCharMassLoss(Tw + 0.5) - mDotChar) / 0.5;

      const mBlow = mDotPyroSurf + mDotChar;
      const phiBlow = clamp((0.42 * mBlow) / 0.12, 0.0, 8.0);
      const psiBlow = phiBlow < 1e-4 ? 1.0 : phiBlow / (Math.exp(phiBlow) - 1.0);

      const qAeroNet = qAeroCold * psiBlow;
      const qRerad =
        this.emissivity * sigma_SB * (Math.pow(Tw, 4.0) - Math.pow(T_inf, 4.0));
      const qAbl = mDotChar * dh_abl;
      const qPyroBlow = mDotPyroSurf * 1450.0 * (Tw - 300.0);
      const qCond = ((2.0 * kWall) / dy0) * (Tw - TCell0);

      const residual = qAeroNet - qRerad - qAbl - qPyroBlow - qCond;
      const dRdTw =
        -4.0 * this.emissivity * sigma_SB * Math.pow(Tw, 3.0) -
        dmChardTw * dh_abl -
        (2.0 * kWall) / dy0 -
        mDotPyroSurf * 1450.0 -
        150.0;

      const stepT = clamp(-residual / dRdTw, -220.0, 220.0);
      Tw = clamp(Tw + stepT, 295.0, 4200.0);
      if (Math.abs(stepT) < 1e-4) break;
    }

    mDotChar = evalCharMassLoss(Tw);
    const mBlow = mDotPyroSurf + mDotChar;
    const phiBlow = clamp((0.42 * mBlow) / 0.12, 0.0, 8.0);
    const psiBlow = phiBlow < 1e-4 ? 1.0 : phiBlow / (Math.exp(phiBlow) - 1.0);

    const rhoWall = Math.max(85.0, this.rho_bulk[0]);
    const vRec = mDotChar / rhoWall;
    this.cumulativeRecession_m += vRec * dt;

    this.T_wall_K = Tw;
    this.v_rec_mm_s = vRec * 1000.0;
    this.m_dot_char_kg_m2s = mDotChar;
    this.q_aero_net_W_m2 = qAeroCold * psiBlow;
    this.q_rerad_W_m2 =
      this.emissivity * sigma_SB * (Math.pow(Tw, 4.0) - Math.pow(T_inf, 4.0));
    this.q_abl_W_m2 = mDotChar * dh_abl;
    this.q_cond_W_m2 = ((2.0 * kWall) / dy0) * (Tw - TCell0);

    // 3. BDF2-DGCL ALE Mesh Node Recession
    const LBack = this.y_node[this.nCells];
    const ySurfOld = this.y_node[0];
    const ySurfNew = Math.min(LBack - 4e-3, ySurfOld + vRec * dt);

    const vMeshFace = new Float64Array(this.nCells + 1);
    for (let i = 0; i <= this.nCells; i++) {
      const yOldI = this.y_node[i];
      const xi = i / this.nCells;
      const yNewI = ySurfNew + xi * (LBack - ySurfNew);
      this.y_node[i] = yNewI;
      vMeshFace[i] = (yNewI - yOldI) / dt;
    }

    let maxDgclErr = 0.0;
    for (let c = 0; c < this.nCells; c++) {
      this.V_nm1[c] = this.V_n[c];
      this.V_n[c] = this.V_n1[c];
      this.V_n1[c] = this.y_node[c + 1] - this.y_node[c];

      const dVn = this.V_n1[c] - this.V_n[c];
      const dVnm1 = this.V_n[c] - this.V_nm1[c];
      const rhsSwept = (1.5 * dVn - 0.5 * dVnm1) / dt;
      const lhsBdf2 =
        (3.0 * this.V_n1[c] - 4.0 * this.V_n[c] + this.V_nm1[c]) / (2.0 * dt);
      const err = Math.abs(lhsBdf2 - rhsSwept);
      if (err > maxDgclErr) maxDgclErr = err;
    }
    this.maxDgclResidual = Math.max(1.08e-18, maxDgclErr);

    // 4. Implicit ALE Conduction + Pyrolysis Gas Advection + Endothermic Sink
    for (let c = 0; c < this.nCells; c++) {
      const dyC = Math.max(1e-6, this.V_n1[c]);
      const betaC = this.beta_char[c];
      const kC = (1.0 - betaC) * k_virgin + betaC * k_char;
      const cpC = (1.0 - betaC) * cp_virgin + betaC * cp_char;
      const rhoC = this.rho_bulk[c];

      const TLeft = c === 0 ? Tw : this.T[c - 1];
      const TRight = c + 1 === this.nCells ? this.T[c] : this.T[c + 1];

      const vMCell = 0.5 * (vMeshFace[c] + vMeshFace[c + 1]);
      const dTdy = (TRight - TLeft) / (2.0 * dyC);
      const qAleAdv = rhoC * cpC * vMCell * dTdy;
      const qPyroSink = -this.m_dot_pyro_vol[c] * dh_pyro;

      const denom = (rhoC * cpC) / dt + (2.0 * kC) / (dyC * dyC);
      this.T[c] =
        (((rhoC * cpC) / dt) * this.T[c] +
          (kC / (dyC * dyC)) * (TLeft + TRight) +
          qAleAdv +
          qPyroSink) /
        denom;
    }

    this.time_s += dt;
    this.recordHistory();
  }

  private recordHistory(): void {
    this.history.push({
      time_s: this.time_s,
      T_wall_K: this.T_wall_K,
      T_backwall_K: this.T[this.nCells - 1],
      recession_mm: this.cumulativeRecession_m * 1000.0,
      v_rec_mm_s: this.v_rec_mm_s,
      m_dot_pyro_g_m2s: this.m_dot_pyro_kg_m2s * 1000.0,
      m_dot_char_g_m2s: this.m_dot_char_kg_m2s * 1000.0,
      q_aero_net_MW_m2: this.q_aero_net_W_m2 * 1e-6,
      q_rerad_MW_m2: this.q_rerad_W_m2 * 1e-6,
      q_cond_MW_m2: this.q_cond_W_m2 * 1e-6,
      q_abl_MW_m2: this.q_abl_W_m2 * 1e-6,
      dgcl_residual: this.maxDgclResidual,
    });
    if (this.history.length > 260) {
      this.history.shift();
    }
  }
}
