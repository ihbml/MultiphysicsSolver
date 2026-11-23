export interface EngineMetadata {
  name: string;
  standard: string;
  compiler: string;
  totalExecutionTimeSec: number;
  timestamp: string;
}

export interface MeshLevelInfo {
  level: number;
  n: number;
  h: number;
  cellCount: number;
  faceCount: number;
  vertexCount: number;
  minVolume: number;
  maxVolume: number;
  avgVolume: number;
  maxNonOrthoAngleDeg: number;
  avgNonOrthoAngleDeg: number;
  maxSkewness: number;
  avgSkewness: number;
  alphaSkew: number;
}

export interface MMSLevelResult {
  gridLevel: number;
  nCells: number;
  h: number;
  L2_error: number;
  Linf_error: number;
  domainAvgPhi: number;
  probeCenterPhi: number;
  linearIters: number;
  deferredIters: number;
  solveTimeSec: number;
}

export interface MMSModeConvergence {
  modeId: number;
  modeName: string;
  p_obs_21_L2: number;
  p_obs_32_L2: number;
  p_obs_21_Linf: number;
  p_obs_32_Linf: number;
  GCI_21: number;
  GCI_32: number;
  asymptoticRangeRatio: number;
  f1: number;
  f2: number;
  f3: number;
  f_exact: number;
  levels: MMSLevelResult[];
}

export interface ParameterCheck {
  paramIndex: number;
  theta: number;
  g_adj: number;
  g_cs: number;
  relError: number | string;
}

export interface TaylorPoint {
  epsilon: number;
  R1: number;
  slope: number;
}

export interface AdjointVerification {
  h_cs: number | string;
  maxRelError: number | string;
  passedComplexStepGate: boolean;
  averageTaylorSlope: number;
  passedTaylorGate: boolean;
  parameterChecks: ParameterCheck[];
  taylorRemainderTest: TaylorPoint[];
}

export interface DefectItem {
  testCase: string;
  defectType: string;
  faceClassification: string;
  caught: boolean;
  hasNaN: boolean;
  location: [number, number, number];
  severityMetric: number;
  description: string;
}

export interface PathologySuite {
  totalTests: number;
  defectsCaught: number;
  detectionRate: number;
  zeroNaNs: boolean;
  zeroSegfaults: boolean;
  defects: DefectItem[];
}

export interface VisualCell {
  id: number;
  xc: [number, number, number];
  volume: number;
  phiExact: number;
  phiCorrected: number;
  phiNaive: number;
  errCorrected: number;
  errNaive: number;
}

export interface VisualFace {
  id: number;
  owner: number;
  neighbor: number;
  xf: [number, number, number];
  Sf: [number, number, number];
  area: number;
  df: [number, number, number];
  nonOrthoAngleDeg: number;
  skewnessMetric: number;
  alpha_f: number;
  beta_f: number;
  m_skew: [number, number, number];
  k_corr: [number, number, number];
  classification: string;
}

export interface Sample3DVisual {
  gridDim: [number, number, number];
  numSampleCells: number;
  cells: VisualCell[];
  sampleFaces: VisualFace[];
}

export interface VerificationData {
  engineMetadata: EngineMetadata;
  meshFamily: MeshLevelInfo[];
  mmsConvergence: MMSModeConvergence[];
  adjointVerification: AdjointVerification;
  pathologySuite: PathologySuite;
  sample3DVisual: Sample3DVisual;
}
