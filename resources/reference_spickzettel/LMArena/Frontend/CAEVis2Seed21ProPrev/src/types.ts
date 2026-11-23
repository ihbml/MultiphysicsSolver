// CFD Cockpit shared types (mirrors Rust structs)

export type Vec3 = [number, number, number];

export interface FacePatch {
  id: number;
  centroid: Vec3;
  normal: Vec3;
  normalConeHalfAngle: number; // radians
  bbox: { min: Vec3; max: Vec3 };
  cylRadius: number; // cyl_radius_yz about centroid
  stepTag: string;
  area: number;
}

export type BcType = 'wall' | 'inlet' | 'outlet' | 'farfield' | 'symmetry' | 'mrf';

export interface SpatialRule {
  id: string;
  label: string;
  expr: string;
  humanReadable: string;
  matchedFaceIds: number[];
  bcType: BcType;
  color: string;
  conflicting: boolean;
}

export interface Gizmo {
  id: string;
  kind: 'mrf' | 'spray' | 'mic' | 'porous' | 'farfield';
  position: Vec3;
  axis: Vec3;
  // MRF
  radius?: number;
  height?: number;
  rpm?: number;
  tipMach?: number;
  // Spray
  coneHalfAngle?: number;
  d32?: number;
  // Mic
  distanceSphere?: number;
}

// 7-element SI dimension vector [kg, m, s, K, mol, A, cd]
export type DimVec = [number, number, number, number, number, number, number];

export interface FieldVar {
  name: string;
  label: string;
  dim: DimVec;
  description: string;
}

export interface LineProbe {
  pA: Vec3;
  pB: Vec3;
  sampleCount: number;
}

export interface RunData {
  name: string;
  cd: number;
  p0Loss: number;
  tMax: number;
  field: Float32Array; // scalar field on grid
}

export interface WorkspaceId {
  id:
    | 'bc-authoring'
    | 'gizmos'
    | 'line-probe'
    | 'ab-compare'
    | 'field-calc';
  label: string;
  icon: string;
}
