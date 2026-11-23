// Built-in field variables with SI dimensions — mirrors Rust FieldCatalog
import type { FieldVar, FacePatch } from './types';
import {
  PRESSURE, VELOCITY, TEMPERATURE, DENSITY, DYN_VISC, THERM_COND,
  DIMLESS, MACH, YPLUS, NUSSELT, TAU_WALL, KIN_VISC,
} from './si-units';

export const FIELD_VARS: FieldVar[] = [
  { name: 'p',        label: 'Static Pressure',       dim: PRESSURE,   description: 'Absolute static pressure p' },
  { name: 'p_total',  label: 'Total Pressure',        dim: PRESSURE,   description: 'Pitot stagnation pressure p₀ = p + ½ρ|U|²' },
  { name: 'Cp',       label: 'Pressure Coefficient',  dim: DIMLESS,    description: 'Pressure coefficient Cₚ = (p - p∞) / q∞' },
  { name: 'U',        label: 'Velocity Vector',       dim: VELOCITY,   description: 'Reynolds-averaged velocity u_i' },
  { name: 'T',        label: 'Static Temperature',    dim: TEMPERATURE,description: 'Static static temperature T_s' },
  { name: 'rho',      label: 'Density',               dim: DENSITY,    description: 'Mass density ρ (ideal gas)' },
  { name: 'mu',       label: 'Dynamic Viscosity',     dim: DYN_VISC,   description: 'Molecular dynamic viscosity μ (Sutherland)' },
  { name: 'nu',       label: 'Kinematic Viscosity',   dim: KIN_VISC,   description: 'Kinematic viscosity ν = μ/ρ' },
  { name: 'kappa',    label: 'Thermal Conductivity',  dim: THERM_COND, description: 'Molecular thermal conductivity k' },
  { name: 'Mach',     label: 'Mach Number',           dim: MACH,       description: 'Local Mach number Ma = |U|/c, c=√(γRT)' },
  { name: 'yPlus',    label: 'Wall y+',               dim: YPLUS,      description: 'Dimensionless wall distance y⁺' },
  { name: 'tau_w',    label: 'Wall Shear Stress',     dim: TAU_WALL,   description: 'Viscous wall shear stress τ_w' },
  { name: 'Nu',       label: 'Nusselt Number',        dim: NUSSELT,    description: 'Nusselt number Nu = h·L/k' },
  { name: 'Tw',       label: 'Wall Temperature',      dim: TEMPERATURE,description: 'Wall surface temperature T_wall' },
];

// Reproduce some realistic face patches for a backward-facing step + nozzle cascade
export function buildMockPatches(): FacePatch[] {
  return [
    {
      id: 1, centroid: [-0.5, 0, 0], normal: [-1, 0, 0], normalConeHalfAngle: 0.01,
      bbox: { min: [-0.5, -0.5, -0.25], max: [-0.5, 0.5, 0.25] },
      cylRadius: 0.55, stepTag: 'AP242_INLET_FACE', area: 0.5,
    },
    {
      id: 2, centroid: [3, 0, 0], normal: [1, 0, 0], normalConeHalfAngle: 0.01,
      bbox: { min: [3, -0.5, -0.25], max: [3, 1.5, 0.25] },
      cylRadius: 1.55, stepTag: 'AP242_OUTLET_FACE', area: 1.0,
    },
    {
      id: 3, centroid: [1.25, 1.5, 0], normal: [0, 1, 0], normalConeHalfAngle: 0.005,
      bbox: { min: [-0.5, 1.5, -0.25], max: [3, 1.5, 0.25] },
      cylRadius: 1.96, stepTag: 'AP242_FAR_TOP', area: 1.75,
    },
    {
      id: 4, centroid: [1.25, -0.5, 0], normal: [0, -1, 0], normalConeHalfAngle: 0.005,
      bbox: { min: [-0.5, -0.5, -0.25], max: [3, -0.5, 0.25] },
      cylRadius: 0.72, stepTag: 'AP242_WALL_BOTTOM', area: 1.75,
    },
    {
      id: 5, centroid: [0, 0.05, 0], normal: [0, 1, 0], normalConeHalfAngle: 0.002,
      bbox: { min: [-0.5, 0, -0.25], max: [0.5, 0.1, 0.25] },
      cylRadius: 0.11, stepTag: 'AP242_STEP_FACE', area: 0.1,
    },
    {
      id: 6, centroid: [0.5, 0.0, 0.0], normal: [0, 0, 1], normalConeHalfAngle: 0.01,
      bbox: { min: [-0.5, -0.5, 0.25], max: [3, 1.5, 0.25] },
      cylRadius: 0, stepTag: 'AP242_SYMM_FRONT', area: 3.5,
    },
    {
      id: 7, centroid: [0.5, 0.0, -0.0], normal: [0, 0, -1], normalConeHalfAngle: 0.01,
      bbox: { min: [-0.5, -0.5, -0.25], max: [3, 1.5, -0.25] },
      cylRadius: 0, stepTag: 'AP242_SYMM_BACK', area: 3.5,
    },
    // Airfoil (NACA0012-ish) patches
    {
      id: 10, centroid: [1.5, 0.02, 0], normal: [0, 1, 0], normalConeHalfAngle: 0.04,
      bbox: { min: [1.0, 0.0, -0.25], max: [2.0, 0.06, 0.25] },
      cylRadius: 0.05, stepTag: 'AP242_BLADE_SUCTION', area: 0.42,
    },
    {
      id: 11, centroid: [1.5, -0.02, 0], normal: [0, -1, 0], normalConeHalfAngle: 0.04,
      bbox: { min: [1.0, -0.06, -0.25], max: [2.0, 0.0, 0.25] },
      cylRadius: 0.05, stepTag: 'AP242_BLADE_PRESSURE', area: 0.42,
    },
    // Hub / shroud cylindrical
    {
      id: 20, centroid: [0, 0.02, 0], normal: [0, 1, 0], normalConeHalfAngle: 0.0,
      bbox: { min: [-0.5, 0.018, -0.25], max: [3, 0.022, 0.25] },
      cylRadius: 0.020, stepTag: 'AP242_HUB', area: 0.0,
    },
  ];
}
