export interface CellData {
  t: number; // 0 hex8, 1 prism6
  n: number[];
  j: number; // J_ENS
  v: number; // volume
  g: number; // gauss closure
  p: number; // patch
  l: number; // layer (-1 core)
}
export interface VInfo {
  active: boolean;
  h: number;
  dmed: number;
  n: number[];
}
export interface SurfTri {
  n: number[];
  p: number;
  w: boolean;
}
export interface FaFace {
  tri: number;
  owner: number;
  area: number;
  c: number[];
  n: number[];
  W: number[][];
  k1: number;
  k2: number;
  H: number;
}
export interface FaEdge {
  f1: number;
  f2: number;
  le: number[];
  len: number;
  dgeo: number;
}
export interface FaMesh {
  faces: FaFace[];
  edges: FaEdge[];
}
export interface Report {
  nVerts: number;
  nCells: number;
  nHex: number;
  nPrism: number;
  minVol: number;
  minJens: number;
  maxGauss: number;
  boundaryFaces: number;
  interiorFaces: number;
  badFaces: number;
  hangingNodes: number;
  conformal: boolean;
  N: number;
  dMedMin: number;
  dMedMax: number;
  Hmin: number;
  Hmax: number;
  nRetracted: number;
  nDetRetract?: number;
  strombergClasses: number;
  bgHexes: number;
  bgMixed: number;
  maxRV: number;
  faFaces: number;
  faEdges: number;
  faArea: number;
  faOwnerBijective: boolean;
}
export interface MeshData {
  name: string;
  params: { dy1: number; growth: number; Nmax: number; medialFrac: number; minDetJ: number; visDeg: number; N: number };
  vertices: number[][];
  vinfo: VInfo[];
  cells: CellData[];
  surfTris: SurfTri[];
  patchNames: string[];
  faMesh?: FaMesh;
  report: Report;
}
export interface VerificationCase {
  name: string;
  nCells: number;
  nHex: number;
  nPrism: number;
  minVol: number;
  minVolPositive: boolean;
  minJens: number;
  minJensAboveQuarter: boolean;
  maxGauss: number;
  gaussBelow1e14: boolean;
  hangingNodes: number;
  conformal: boolean;
  N: number;
  dMedMin: number;
  Hmax: number;
  nRetracted: number;
  nDetRetract?: number;
  strombergClasses: number;
  bgMixed: number;
  maxRV: number;
  faFaces: number;
  faEdges: number;
  faOwnerBijective: boolean;
}
export interface Verification {
  unitTestsFailed: number;
  cases: VerificationCase[];
}
