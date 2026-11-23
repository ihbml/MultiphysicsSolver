export type ActiveTab = 
  | 'overview' 
  | 'fvm_rhiechow' 
  | 'amg_mixed' 
  | 'stiff_chem' 
  | 'discrete_adjoint' 
  | 'cpp_source';

export interface FacePartitionData {
  seg0_size: number;
  seg1_size: number;
  seg2_size: number;
  seg3_size: number;
  total_faces: number;
  inner_orthogonal_pct: number;
}

export interface RhieChowMetrics {
  beta: number;
  alpha: number;
  kx: number;
  ky: number;
  kz: number;
  sx: number;
  sy: number;
  sz: number;
  linearExactnessError: number;
  massImbalance: number;
}

export interface AMGLevelInfo {
  level: number;
  dimension: number;
  nnz: number;
  spectralRadius: number;
  smootherType: 'Chebyshev-2' | 'L1-Jacobi';
}

export interface MixedPrecisionState {
  iterations: number;
  finalResidual: number;
  etaRound: number;
  stagnationDetected: boolean;
  historyFP64: number[];
  historyInnerAMG: number[];
}

export interface DLBFoamStats {
  totalCells: number;
  activeCells: number;
  coldFlowCells: number;
  activePct: number;
  coldPct: number;
  stolenChunks: number;
  speedupFactor: number;
  stiffnessRatio: number;
}

export interface AdjointVerificationPoint {
  cellIdx: number;
  gamma: number;
  primalVelocity: number;
  adjointLambda: number;
  adjointSens: number;
  fdSens: number;
  absError: number;
  relError: number;
}
