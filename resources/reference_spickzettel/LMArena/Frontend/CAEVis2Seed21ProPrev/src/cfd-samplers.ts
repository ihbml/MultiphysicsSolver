// Mock CFD field sampler (for line probe, colormap preview).
// Generates plausible Cp, y+, u+, T, Mach profiles for a loaded transonic airfoil.

// Cp distribution for a NACA0012-style airfoil at α≈2°, transonic
export function chordwiseCp(xc: number, suction: boolean): number {
  // suction side has shock at x/c≈0.55
  if (suction) {
    if (xc < 0.05) return -0.3 - 14 * xc;
    if (xc < 0.55) {
      const t = (xc - 0.05) / 0.5;
      return -0.9 - 0.3 * Math.sin(t * Math.PI) - 0.1 * t;
    }
    // shock
    const shock = Math.exp(-Math.pow(((xc - 0.55) / 0.02), 2)) * 0.5;
    const t2 = (xc - 0.55) / 0.45;
    return -0.5 + 0.55 * t2 - shock;
  } else {
    // pressure side
    if (xc < 0.1) return 0.4 - 4 * xc;
    return 0.05 - 0.3 * Math.sin(xc * Math.PI * 1.1);
  }
}

// y+ at wall for a turbulent BL over a flat plate / airfoil — decays along chord
export function chordwiseYPlus(xc: number): number {
  return 45 * Math.pow(xc + 0.02, -0.13);
}

// Wall temperature (Kelvin) — mild recovery
export function chordwiseTw(xc: number, suction: boolean): number {
  const T_inf = 288.15;
  const recovery = 0.89;
  const cpS = chordwiseCp(xc, suction);
  return T_inf + recovery * (1 - cpS) * 5.0;
}

// u+(y+) law of the wall
export function uPlus(yPlus: number): number {
  const kappa = 0.41;
  const E = 9.793;
  if (yPlus < 11.0) return yPlus; // viscous sublayer
  return (1 / kappa) * Math.log(E * yPlus);
}

// Spalding unified formula (placeholder for full profile)
export function spaldingUPlus(yPlus: number): number {
  // We use uPlus() analytical viscous + log law above for plots
  return uPlus(yPlus);
}

// Generate u+ samples from RANS CFD (slightly perturbed log law)
export function ransUPlus(yPlus: number): number {
  const kappa = 0.41;
  const E = 9.793;
  if (yPlus < 5) return yPlus * (1 + 0.02 * yPlus);
  const u_log = (1 / kappa) * Math.log(E * yPlus);
  // Add wake component for large y+
  const wake = 0.8 * Math.pow(Math.min(yPlus / 500, 1), 2);
  return u_log + wake;
}

// For diff view (ΔCp across A/B runs)
export function deltaCp(xc: number, suction: boolean): number {
  // Optimized run B reduces shock strength and improves pressure recovery
  if (suction && xc > 0.4 && xc < 0.7) {
    return 0.15 * Math.exp(-Math.pow(((xc - 0.55) / 0.06), 2)); // ΔCp positive = less suction peak
  }
  if (xc > 0.8) return -0.04 * (xc - 0.8) / 0.2;
  return 0;
}
