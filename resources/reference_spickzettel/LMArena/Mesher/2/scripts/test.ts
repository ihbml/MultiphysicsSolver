import { getGeometry } from "../src/lib/sdf";
import { MeshJob, buildFvm } from "../src/lib/mesh";
import { KERNELS, NeighborGrid, SphJob } from "../src/lib/sph";
import { buildInterp, classifyCells, classifyParticles, CELL_INTERFACE, PART_BUFFER, Scheme } from "../src/lib/coupling";
import { meshToFoam, graphToJson, sphToCsv } from "../src/lib/exporters";

for (const id of ["cylinder", "airfoil", "nozzle", "step", "bubble"]) {
  const geo = getGeometry(id);
  const t0 = Date.now();
  const job = new MeshJob(geo, { h0: 0.05, grading: 0.4, hmaxRatio: 3, iters: 45 });
  let done = false;
  while (!done) done = job.step();
  const m = buildFvm(job);
  const s = m.stats;
  console.log(
    `${id.padEnd(9)} cells=${String(s.nCells).padStart(5)} faces=${String(s.nFaces).padStart(5)} ` +
      `bnd=${String(s.nBoundary).padStart(4)} qmin=${s.qMin.toFixed(3)} qmean=${s.qMean.toFixed(3)} ` +
      `noMax=${s.nonOrthoMax.toFixed(1)}° closure=${s.closureResidual.toExponential(1)} ` +
      `perim=${s.boundaryArea.toFixed(3)}/${s.exactPerimeter.toFixed(3)} bw ${s.bandwidthBefore}→${s.bandwidthAfter} ` +
      `[${Date.now() - t0} ms]`,
  );
}

console.log("\n--- SPH relaxation (cylinder) ---");
const geo = getGeometry("cylinder");
const sp = {
  dp: 0.06,
  hFactor: 1.3,
  iters: 0,
  kernel: "wendland" as const,
  refineRatio: 1,
  grading: 0.6,
  volumeReinit: true,
  lattice: "square" as const,
};
for (const iters of [0, 30, 80]) {
  const t0 = Date.now();
  const job = new SphJob(geo, { ...sp, iters });
  let done = iters === 0;
  while (!done) done = job.step();
  if (iters > 0) job.reinitVolumes();
  const d = job.finalize().diag;
  console.log(
    `iters=${String(iters).padStart(3)} n=${d.n} bnd=${d.nBoundary} nb=${d.neighMean.toFixed(1)} ` +
      `eps0bulk=${d.eps0Bulk.toExponential(2)} eps0max=${d.eps0Max.toExponential(2)} ` +
      `M-I=${d.eps1Mean.toFixed(3)} divR=${d.divRMean.toFixed(3)} ` +
      `sum|A_iS|=${d.surfaceSum.toFixed(3)} (exact ${d.exactPerimeter.toFixed(3)}) ` +
      `ghost=${Math.round(d.ghostPairs)} [${Date.now() - t0} ms]`,
  );
  console.log("   patches:", JSON.stringify(d.patchCount));
}

console.log("\n--- coupling operators ---");
const mjob = new MeshJob(geo, { h0: 0.08, grading: 0.6, hmaxRatio: 3, iters: 20 });
let d1 = false;
while (!d1) d1 = mjob.step();
const mesh = buildFvm(mjob);
const core = { ...geo.core, r: 0.7, buffer: 3 };
const pjob = new SphJob(geo, { ...sp, iters: 40 }, (x, y) => Math.hypot(x - core.x, y - core.y) < core.r);
let d2 = false;
while (!d2) d2 = pjob.step();
const sph = pjob.finalize();
const band = core.buffer * sp.dp;
const cellClass = classifyCells(mesh.cc, mesh.nCells, { ...core, r: core.r - band }, band);
const partClass = classifyParticles(sph.x, sph.y, sph.n, core, band);
const ccx = new Float64Array(mesh.nCells);
const ccy = new Float64Array(mesh.nCells);
for (let c = 0; c < mesh.nCells; c++) {
  ccx[c] = mesh.cc[2 * c];
  ccy[c] = mesh.cc[2 * c + 1];
}
const cellTargets: number[] = [];
for (let c = 0; c < mesh.nCells; c++) if (cellClass[c] === CELL_INTERFACE) cellTargets.push(c);
const partTargets: number[] = [];
for (let i = 0; i < sph.n; i++) if (partClass[i] === PART_BUFFER) partTargets.push(i);
const kern = KERNELS.wendland;
const meanCell = Math.sqrt(mesh.stats.meanVol);
const pGrid = new NeighborGrid(sph.x, sph.y, sph.n, kern.kappa * sp.hFactor * sp.dp, geo.bbox);
const cGrid = new NeighborGrid(ccx, ccy, mesh.nCells, meanCell, geo.bbox);
console.log(`core particles=${sph.n} interface cells=${cellTargets.length} buffer particles=${partTargets.length}`);
for (const sc of ["shepard", "mls", "opmis"] as Scheme[]) {
  const a = buildInterp(ccx, ccy, cellTargets, kern.kappa * sp.hFactor * sp.dp, { x: sph.x, y: sph.y, n: sph.n }, kern, sc, pGrid);
  const b = buildInterp(sph.x, sph.y, partTargets, Math.max(kern.kappa * sp.hFactor * sp.dp, 2.6 * meanCell), { x: ccx, y: ccy, n: mesh.nCells }, kern, sc, cGrid);
  console.log(
    `${sc.padEnd(8)} p→m: m=${a.stats.stencilMean.toFixed(1)} C0=${a.stats.c0Max.toExponential(1)} C1=${a.stats.c1Mean.toExponential(1)} wmin=${a.stats.wMin.toFixed(3)} lin=${a.stats.linearErr.toExponential(1)} over=${a.stats.shockOvershoot.toFixed(1)}% empty=${a.stats.nEmpty} | ` +
      `m→p: m=${b.stats.stencilMean.toFixed(1)} C1=${b.stats.c1Mean.toExponential(1)} wmin=${b.stats.wMin.toFixed(3)} over=${b.stats.shockOvershoot.toFixed(1)}% empty=${b.stats.nEmpty}`,
  );
}

const foam = meshToFoam(mesh);
const gj = graphToJson(mesh);
const csv = sphToCsv(sph);
console.log(`\nexports: polyMesh ${(foam.length / 1024).toFixed(0)} kB, graph ${(gj.length / 1024).toFixed(0)} kB, csv ${(csv.length / 1024).toFixed(0)} kB`);
console.log("foam head:\n" + foam.split("\n").slice(0, 4).join("\n"));
