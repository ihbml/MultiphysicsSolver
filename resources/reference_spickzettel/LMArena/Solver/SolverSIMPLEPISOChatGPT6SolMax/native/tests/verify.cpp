#include "adjoint.hpp"
#include "chemistry.hpp"
#include "generated/chain_abc.hpp"
#include <cmath>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>
#include <string>

using namespace continuum;

static void require(bool ok, const std::string& message) {
  if (!ok) throw std::runtime_error("FAIL: " + message);
}

struct LineMesh {
  int n;
  std::vector<int> owner, neighbor, offsets, faces;
  std::vector<int8_t> signs;
  std::vector<uint8_t> boundary;
  std::vector<double> cx, cy, cz, volume, fx, fy, fz, sx, sy, sz;
  std::vector<double> pbc, mbc, hbc, skewx, skewy, skewz, kx, ky, kz, omega;

  explicit LineMesh(int count, bool flux_boundaries = false) : n(count) {
    int nf = n + 1;
    cx.resize(n); cy.assign(n, 0); cz.assign(n, 0); volume.assign(n, 1);
    owner.resize(nf); neighbor.resize(nf); boundary.assign(nf, 0);
    fx.resize(nf); fy.assign(nf, 0); fz.assign(nf, 0);
    sx.assign(nf, 1); sy.assign(nf, 0); sz.assign(nf, 0);
    pbc.assign(nf, 0); mbc.assign(nf, 0); hbc.assign(nf, 0);
    for (int i = 0; i < n; ++i) cx[i] = i + 0.5;
    for (int f = 0; f < n - 1; ++f) { owner[f] = f; neighbor[f] = f + 1; fx[f] = f + 1; }
    int left = n - 1, right = n;
    owner[left] = 0; neighbor[left] = -1; fx[left] = 0; sx[left] = -1;
    owner[right] = n - 1; neighbor[right] = -1; fx[right] = n;
    boundary[left] = boundary[right] = static_cast<uint8_t>(flux_boundaries ? Boundary::flux : Boundary::pressure);
    pbc[left] = 1; pbc[right] = 0;
    if (flux_boundaries) { mbc[left] = -1; mbc[right] = 1; }
    offsets.push_back(0);
    for (int i = 0; i < n; ++i) {
      for (int f = 0; f < nf; ++f) {
        if (owner[f] == i) { faces.push_back(f); signs.push_back(1); }
        else if (neighbor[f] == i) { faces.push_back(f); signs.push_back(-1); }
      }
      offsets.push_back(static_cast<int>(faces.size()));
    }
    // One corrected interior face and one corrected halo/boundary face.
    skewx.assign(2, 0); skewy.assign(2, 0); skewz.assign(2, 0);
    kx.assign(2, 0); ky.assign(2, 0); kz.assign(2, 0); omega.assign(2, 0.8);
  }

  MeshView mesh() {
    MeshView m;
    m.owned_cells = m.total_cells = n; m.face_count = n + 1;
    m.split = {0, n - 2, n - 1, n, n + 1};
    m.owner = readonly(view(owner)); m.neighbor = readonly(view(neighbor));
    m.cx = readonly(view(cx)); m.cy = readonly(view(cy)); m.cz = readonly(view(cz));
    m.volume = readonly(view(volume));
    m.fx = readonly(view(fx)); m.fy = readonly(view(fy)); m.fz = readonly(view(fz));
    m.sx = readonly(view(sx)); m.sy = readonly(view(sy)); m.sz = readonly(view(sz));
    m.boundary = readonly(view(boundary));
    m.pressure_bc = readonly(view(pbc)); m.mass_bc = readonly(view(mbc));
    m.enthalpy_bc = readonly(view(hbc));
    m.cell_faces = {readonly(view(offsets)), readonly(view(faces)), readonly(view(signs))};
    m.correction = {view(skewx), view(skewy), view(skewz), view(kx), view(ky), view(kz), view(omega)};
    return m;
  }
};

struct FlowStorage {
  std::vector<double> ux, uy, uz, p, h, d, mass, rho, cp, gamma, fx, fy, fz, mu, kappa;
  FlowStorage(int n, int nf)
      : ux(n, 0), uy(n, 0), uz(n, 0), p(n, 0), h(n, 1), d(n, 0.4), mass(nf, 0),
        rho(n, 1), cp(n, 1), gamma(n, 0.95), fx(n, 0), fy(n, 0), fz(n, 0),
        mu(nf, 0.05), kappa(nf, 0.02) {}
  FlowView flow() {
    return {view(ux), view(uy), view(uz), view(p), view(h), view(d), view(mass),
            readonly(view(rho)), readonly(view(cp)), readonly(view(gamma)),
            readonly(view(fx)), readonly(view(fy)), readonly(view(fz)),
            readonly(view(mu)), readonly(view(kappa))};
  }
};

static double fd(const MeshView& m, const adjoint::Design<double>& original,
                 adjoint::Parameter type, int index, double h) {
  auto evaluate = [&](double direction) {
    auto q = original;
    if (type == adjoint::Parameter::gamma) q.gamma[index] += direction;
    if (type == adjoint::Parameter::centroid_x) q.center[index].x += direction;
    if (type == adjoint::Parameter::normal_x) q.area[index].x += direction;
    auto u = adjoint::solve_primal(m, q);
    return adjoint::objective(m, q, u);
  };
  return (evaluate(h) - evaluate(-h)) / (2 * h);
}

int main(int argc, char** argv) {
  try {
    LineMesh skew(2);
    skew.cy[1] = 0.3; skew.fx[0] = 0.98; skew.fy[0] = 0.12; skew.sy[0] = 0.17;
    auto ms = skew.mesh(); validate(ms); populate_corrections(ms);
    FlowStorage fs(2, 3);
    fs.rho.assign(2, 1.3); fs.d.assign(2, 0.1);
    fs.ux.assign(2, 0.5); fs.uy.assign(2, -0.2);
    for (int i = 0; i < 2; ++i) fs.p[i] = 2 * skew.cx[i] - 0.7 * skew.cy[i];
    std::vector<Vec3<double>> exact_gradient(2, {2, -0.7, 0.4});
    rhie_chow_faces(ms, fs.flow(), exact_gradient);
    double beta = geometry(ms, 0).beta;
    double rhie_error = std::abs(fs.mass[0] - 1.3 * (0.5 - 0.2 * 0.17));
    require(rhie_error < 1e-13, "linear Rhie-Chow exactness on skew/nonorthogonal face");
    require(std::abs(beta - 0.5) > 1e-3, "plane intersection beta is not midpoint");

    LineMesh checker(4);
    auto check_mesh = checker.mesh(); validate(check_mesh); populate_corrections(check_mesh);
    FlowStorage check_flow(4, 5);
    check_flow.d.assign(4, 0.1);
    check_flow.p = {1, -1, 1, -1};
    rhie_chow_faces(check_mesh, check_flow.flow(), gradients(check_mesh, readonly(view(check_flow.p))));
    double checker_response = 0;
    for (int f = 0; f < 3; ++f) checker_response = std::max(checker_response, std::abs(check_flow.mass[f]));
    require(checker_response > 0.05, "Rhie-Chow detects alternating collocated pressure mode");

    LineMesh closed(4, true);
    auto mc = closed.mesh(); validate(mc); populate_corrections(mc);
    FlowStorage fc(4, 5);
    fc.mass = {0.2, -0.4, 0.7, -1, 1};
    fc.d.assign(4, 0.7);
    auto projected = correct_pressure(mc, fc.flow(), 1.0, 2);
    double mass_error = projected.max_divergence;
    require(projected.solve.converged && mass_error < 1e-11, "CSR pressure projection conserves every owned cell");
    require(std::abs(std::accumulate(fc.mass.begin() + 3, fc.mass.end(), 0.0)) < 1e-14,
            "closed boundary flux conservation");
    bool anchored = false;
    auto pressure_csr = pressure_matrix(mc, fc.flow(), anchored);
    std::vector<double> probe{0.1, -0.2, 0.45, -0.13};
    auto from_csr = pressure_csr.multiply(probe);
    auto from_gather = pressure_apply_matrix_free(mc, fc.flow(), readonly(view(probe)), anchored);
    double matrix_free_error = 0;
    for (int i = 0; i < 4; ++i) matrix_free_error = std::max(matrix_free_error, std::abs(from_csr[i] - from_gather[i]));
    require(matrix_free_error < 1e-14, "matrix-free CellFaceCSR operator equals assembled CSR");
    LineMesh compact(32);
    auto compact_mesh = compact.mesh(); validate(compact_mesh); populate_corrections(compact_mesh);
    require(compact_mesh.correction.sx.size == 2 &&
            static_cast<double>(compact_mesh.correction.sx.size) / compact_mesh.face_count < 0.061,
            "compact corrected quadrants occupy approximately six percent of faces");

    // Manufactured SPD Poisson system exercises FP32 AMG under three FP64 outers.
    const int n = 128;
    std::vector<std::map<int, double>> rows(n);
    std::vector<double> rhs(n);
    for (int i = 0; i < n; ++i) {
      rows[i][i] = 2;
      if (i > 0) rows[i][i - 1] = -1;
      if (i + 1 < n) rows[i][i + 1] = -1;
      rhs[i] = std::sin((i + 1) * 0.23) + 0.01;
    }
    auto a = linear::from_rows(rows);
    linear::AMG amg(a);
    auto ir = linear::iterative_refinement(a, rhs, amg, 1e-10, 60);
    auto bc = linear::mixed_bicgstab(a, rhs, amg, 1e-10, 120);
    auto gm = linear::mixed_fgmres(a, rhs, amg, 1e-10, 120);
    require(amg.levels() >= 2 && ir.converged && bc.converged && gm.converged, "AMG/Krylov convergence");
    require(ir.eta_round < 1e-10 && bc.eta_round < 1e-10 && gm.eta_round < 1e-10,
            "FP64 true residual backward error");

    using Kin = chemistry::ChainABC;
    double y[3]{0.8, 0.1, 0.1}, w[3]{}, j[Kin::nnz]{};
    Kin::eval(1100, 1, y, w, j);
    double jac_error = 0;
    for (int row = 0; row < 3; ++row)
      for (int k = Kin::rowptr[row]; k < Kin::rowptr[row + 1]; ++k) {
        const int col = Kin::col[k];
        double yp[3]{y[0], y[1], y[2]}, ym[3]{y[0], y[1], y[2]}, wp[3]{}, wm[3]{}, dummy[Kin::nnz]{};
        yp[col] += 1e-4; ym[col] -= 1e-4;
        Kin::eval(1100, 1, yp, wp, dummy);
        Kin::eval(1100, 1, ym, wm, dummy);
        jac_error = std::max(jac_error, std::abs(j[k] - (wp[row] - wm[row]) / 2e-4) /
                                           std::max(1.0, std::abs(j[k])));
      }
    require(jac_error < 1e-9, "AoT analytical sparse chemistry Jacobian");
    const int nc = 32;
    std::vector<double> ya(nc, 1), yb(nc, 0), yc(nc, 0), temp(nc, 300), density(nc, 1);
    for (int i = 0; i < 4; ++i) temp[i] = 1100;
    CAbiSpan<double> species[3]{view(ya), view(yb), view(yc)};
    chemistry::BatchView<Kin> batch{nc, species, readonly(view(temp)), readonly(view(density))};
    auto chem = chemistry::operator_split_bdf2<Kin>(batch, 0.025, 1e-5);
    double chem_mass = 0;
    for (int i = 0; i < nc; ++i) chem_mass = std::max(chem_mass, std::abs(ya[i] + yb[i] + yc[i] - 1));
    require(chem.active == 4 && chem.idle == 28 && !chem.failed && chem_mass < 1e-10,
            "chemistry compaction, positivity and mass closure");
    require(ya[0] < 0.01 && yb[0] > 0 && yc[0] > 0 && ya[4] == 1,
            "stiff active cells advance; idle cells remain untouched");
    std::vector<double> large_a(512, 1), large_b(512, 0), large_c(512, 0);
    std::vector<double> large_t(512, 300), large_rho(512, 1);
    for (int i = 0; i < 16; ++i) large_t[i] = 1100;
    CAbiSpan<double> large_species[3]{view(large_a), view(large_b), view(large_c)};
    chemistry::BatchView<Kin> large_batch{512, large_species, readonly(view(large_t)), readonly(view(large_rho))};
    auto stolen = chemistry::operator_split_bdf2<Kin>(large_batch, 0.025, 1e-5);
    require(stolen.active == 16 && stolen.idle == 496 && !stolen.failed && large_a[0] < 0.01 &&
            large_a[511] == 1, "parallel-sized dynamic chemistry work queue");

    LineMesh design_mesh(3);
    auto md = design_mesh.mesh(); validate(md); populate_corrections(md);
    auto design = adjoint::cast_design<double>(md);
    design.gamma = {0.86, 0.73, 0.91};
    design.area[3].x = 1.07;
    design.center[3].x = 3.06;
    auto derivatives = adjoint::differentiate(md, design);
    double primal_norm = linear::norm(adjoint::residual(md, design, derivatives.state));
    double adjoint_error = 0;
    auto compare = [&](double exact, adjoint::Parameter kind, int idx) {
      double numerical = fd(md, design, kind, idx, 4e-5);
      double diff = std::abs(exact - numerical);
      adjoint_error = std::max(adjoint_error, diff / std::max(1.0, std::abs(numerical)));
      if (!(diff < 1e-8)) std::cerr << "adjoint kind=" << static_cast<int>(kind) << " idx=" << idx
                                    << " adj=" << std::setprecision(16) << exact << " fd=" << numerical
                                    << " abs=" << diff << '\n';
      require(diff < 1e-8 && diff / std::max(1.0, std::abs(numerical)) < 1e-8,
              "IFT adjoint vs centered primal re-solves");
    };
    compare(derivatives.gamma[1], adjoint::Parameter::gamma, 1);
    compare(derivatives.centroid[3].x, adjoint::Parameter::centroid_x, 3);
    compare(derivatives.normal[3].x, adjoint::Parameter::normal_x, 3);
    compare(derivatives.centroid[2].x, adjoint::Parameter::centroid_x, 2);
    compare(derivatives.normal[2].x, adjoint::Parameter::normal_x, 2);
    require(primal_norm < 1e-11, "coupled state equations solved");
    adjoint::CadDirection direction;
    direction.gamma.assign(3, 0); direction.gamma[1] = 0.2;
    direction.centroid.assign(4, {}); direction.normal.assign(4, {});
    direction.centroid[3].x = 0.3; direction.normal[3].x = -0.1;
    double cad_gradient = adjoint::cad_chain(derivatives, direction);
    auto cad_evaluate = [&](double step) {
      auto perturbed = design;
      perturbed.gamma[1] += 0.2 * step;
      perturbed.center[3].x += 0.3 * step;
      perturbed.area[3].x -= 0.1 * step;
      return adjoint::objective(md, perturbed, adjoint::solve_primal(md, perturbed));
    };
    double cad_error = std::abs(cad_gradient - (cad_evaluate(4e-5) - cad_evaluate(-4e-5)) / 8e-5);
    require(cad_error < 1e-8, "CAD chain rule vs simultaneous geometry/porosity perturbation");

    // Complete PISO momentum -> RC -> pressure x2 -> enthalpy pass.
    FlowStorage transient(4, 5);
    transient.mass = {-0.4, 0.2, 0.1, -1, 1};
    transient.fx.assign(4, 0.28);
    transient.h = {1, 1.4, 1.8, 2.2};
    auto piso = advance(mc, transient.flow(), {Coupling::piso, 0.04, 1, 2});
    require(piso.pressure_solves == 2 && piso.continuity.back() < 1e-10 &&
            std::all_of(transient.h.begin(), transient.h.end(), [](double x) { return std::isfinite(x); }),
            "segregated PISO/enthalpy FVM step");

    std::ostringstream json;
    json << std::setprecision(15) << "{\n"
         << "  \"native\": \"g++ -std=c++20 -O2\",\n"
         << "  \"passed\": true,\n"
         << "  \"face\": {\"beta\": " << beta << ", \"linearError\": " << rhie_error
         << ", \"checkerboardResponse\": " << checker_response
         << ", \"massError\": " << mass_error << ", \"pisoMassError\": " << piso.continuity.back()
         << ", \"matrixFreeError\": " << matrix_free_error << "},\n"
         << "  \"linear\": {\"levels\": " << amg.levels() << ", \"irIterations\": " << ir.iterations
         << ", \"bicgIterations\": " << bc.iterations << ", \"fgmresIterations\": " << gm.iterations
         << ", \"etaRound\": " << ir.eta_round << ", \"fp32Floor\": " << ir.fp32_floor
         << ", \"history\": [";
    for (size_t i = 0; i < ir.residual_history.size(); ++i) {
      if (i) json << ", ";
      json << ir.residual_history[i];
    }
    json << "]},\n"
         << "  \"chemistry\": {\"active\": " << chem.active << ", \"idle\": " << chem.idle
         << ", \"substeps\": " << chem.max_substeps << ", \"massError\": " << chem_mass
         << ", \"jacobianError\": " << jac_error << ", \"hotA\": " << ya[0] << "},\n"
         << "  \"adjoint\": {\"primalResidual\": " << primal_norm << ", \"fdError\": " << adjoint_error
         << ", \"gammaGradient\": " << derivatives.gamma[1] << ", \"centroidGradient\": "
         << derivatives.centroid[3].x << ", \"normalGradient\": " << derivatives.normal[3].x
         << ", \"cadGradient\": " << cad_gradient << ", \"cadError\": " << cad_error << "}\n}";
    if (argc > 1 && std::string(argv[1]) == "--json") std::cout << json.str() << '\n';
    else {
      std::cout << "PASS  discrete mass conservation: " << mass_error << '\n'
                << "PASS  Rhie-Chow linear exactness and checkerboard damping: " << rhie_error << '\n'
                << "PASS  FP32 AMG / FP64 IR-BiCGStab-FGMRES: " << amg.levels() << " levels\n"
                << "PASS  AoT chemistry Jacobian and BDF2: " << jac_error << '\n'
                << "PASS  IFT adjoint vs central finite differences: " << adjoint_error << '\n'
                << "PASS  PISO + enthalpy continuity: " << piso.continuity.back() << '\n';
    }
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}