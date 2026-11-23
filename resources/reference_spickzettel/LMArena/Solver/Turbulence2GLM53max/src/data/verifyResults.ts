export const VERIFY = {
 "compiler": "g++ (Debian 12.2.0) -std=c++20 -O2 -Wall -Wextra",
 "runtimeMs": 2236.65,
 "nPass": 51,
 "nChecks": 69,
 "checks": [
  {
   "id": "MESH-01",
   "name": "polyhedral volume closure (uniform hex grid)",
   "value": 6.66134e-16,
   "tol": 1e-12,
   "pass": true,
   "note": "sum(V_P) vs Lx*Ly*Lz, exact divergence-theorem geometry"
  },
  {
   "id": "MESH-02",
   "name": "volume closure under interior node warping",
   "value": 2.22045e-16,
   "tol": 1e-09,
   "pass": true,
   "note": "boundary nodes fixed => telescoping keeps the volume exact"
  },
  {
   "id": "MESH-03",
   "name": "max non-orthogonality of warped grid [deg]",
   "value": 0,
   "tol": 90,
   "pass": true,
   "note": "measured = 32.302439"
  },
  {
   "id": "MESH-04",
   "name": "compact CorrectionSoA footprint [%] (<= 6%)",
   "value": -6,
   "tol": 6,
   "pass": true,
   "note": "retained faces = 0 / 434"
  },
  {
   "id": "MESH-05",
   "name": "CellFaceCSR integrity violations",
   "value": 0,
   "tol": 0.5,
   "pass": true,
   "note": "atomic-free gather"
  },
  {
   "id": "WALE-01",
   "name": "solid-body rotation vs closed form (rel err)",
   "value": 0,
   "tol": 1e-12,
   "pass": true,
   "note": "nu_sgs = (Cw D)^2 (2/3)^(1/4) Omega  (S_ij S_ij = 0)"
  },
  {
   "id": "WALE-02",
   "name": "nu_sgs for pure plane shear (must be 0)",
   "value": 0,
   "tol": 1e-14,
   "pass": true,
   "note": "laminar shear produces no SGS viscosity -> no need for a dynamic model"
  },
  {
   "id": "WALE-03",
   "name": "axisymmetric strain vs closed form (rel err)",
   "value": 0,
   "tol": 1e-12,
   "pass": true,
   "note": "S_ij S_ij = 1.5 s^2,  S^d S^d = 0.375 s^4"
  },
  {
   "id": "WALE-04",
   "name": "nu_sgs scales with Delta^2 (rel err)",
   "value": 0,
   "tol": 1e-12,
   "pass": true,
   "note": "quadratic filter-width scaling"
  },
  {
   "id": "PATA-01",
   "name": "min(k) over 2e4 random states, Patankar [kg/m/s2]",
   "value": 0,
   "tol": 0,
   "pass": true,
   "note": "k > 0 unconditionally: production -> RHS, destruction -> diagonal"
  },
  {
   "id": "PATA-02",
   "name": "min(omega) over 2e4 random states, Patankar",
   "value": 0,
   "tol": 0,
   "pass": true,
   "note": "a_PP(omega) += 2 beta rho omega_P V_P + beta* rho omega_P V_P"
  },
  {
   "id": "PATA-03",
   "name": "naive explicit source drives k negative (1 = yes)",
   "value": 0,
   "tol": 0.5,
   "pass": true,
   "note": "min(k)_naive = -533182.615307"
  },
  {
   "id": "PATA-04",
   "name": "naive explicit source drives omega negative (1 = yes)",
   "value": 0,
   "tol": 0.5,
   "pass": true,
   "note": "min(omega)_naive = -179979242295.437042"
  },
  {
   "id": "SST-01",
   "name": "gamma1 = beta1/beta* - sigmaW1 kappa^2/sqrt(beta*)",
   "value": 1.11022e-16,
   "tol": 1e-12,
   "pass": true,
   "note": "Menter 2003 table 1"
  },
  {
   "id": "SST-02",
   "name": "gamma2 = beta2/beta* - sigmaW2 kappa^2/sqrt(beta*)",
   "value": 5.55112e-17,
   "tol": 1e-14,
   "pass": true,
   "note": "Menter 2003 table 1"
  },
  {
   "id": "SST-03",
   "name": "F1 -> 1 near the wall",
   "value": 0,
   "tol": 1e-06,
   "pass": true,
   "note": ""
  },
  {
   "id": "SST-04",
   "name": "F1 -> 0 in the free stream",
   "value": 8.1e-15,
   "tol": 1e-06,
   "pass": true,
   "note": ""
  },
  {
   "id": "SST-05",
   "name": "Bradshaw limit: nut vs a1 k/S (rel err)",
   "value": 0,
   "tol": 1e-12,
   "pass": true,
   "note": "nut = a1 k / max(a1 w, S F2)"
  },
  {
   "id": "SST-06",
   "name": "outer region: nut vs k/omega (rel err)",
   "value": 0,
   "tol": 1e-12,
   "pass": true,
   "note": "limiter inactive"
  },
  {
   "id": "SST-07",
   "name": "production limiter saturates at 10 beta* rho k omega",
   "value": 0,
   "tol": 1e-12,
   "pass": true,
   "note": "Pk = min(tau:grad u, 10 beta* rho k w)"
  },
  {
   "id": "SST-08",
   "name": "cross-diffusion vanishes when F1 = 1",
   "value": 0,
   "tol": 1e-09,
   "pass": true,
   "note": "2(1-F1) rho sw2 (1/w) grad k . grad w"
  },
  {
   "id": "SST-09",
   "name": "cross-diffusion active when F1 = 0",
   "value": 0,
   "tol": 0.5,
   "pass": true,
   "note": "value = 0.000000 (must be > 0 for grad k . grad w > 0)"
  },
  {
   "id": "SA-01",
   "name": "fv1 -> chi^3/cv1^3 as chi -> 0",
   "value": 7.01772e-13,
   "tol": 1e-09,
   "pass": true,
   "note": "fv1 = chi^3/(chi^3+cv1^3)"
  },
  {
   "id": "SA-02",
   "name": "fv1 -> 1 as chi -> 0.0",
   "value": 8.99032e-08,
   "tol": 1e-06,
   "pass": true,
   "note": "fully turbulent limit"
  },
  {
   "id": "SA-03",
   "name": "fv2 -> 1 - chi/(1+chi fv1) identity",
   "value": 0,
   "tol": 1e-14,
   "pass": true,
   "note": ""
  },
  {
   "id": "SA-04",
   "name": "fw(1) = 1 exactly (log-layer fixed point r = 1)",
   "value": 0,
   "tol": 1e-14,
   "pass": true,
   "note": "g = r + cw2(r^6-r) = 1  =>  fw = 1"
  },
  {
   "id": "SA-05",
   "name": "fw bounded on (0,10]",
   "value": 7.01811e-07,
   "tol": 2,
   "pass": true,
   "note": "fw(0) = 0.000001, fw(10) = 2.005175"
  },
  {
   "id": "SA-06",
   "name": "cw1 = cb1/kappa^2 + (1+cb2)/sigma",
   "value": 0,
   "tol": 1e-14,
   "pass": true,
   "note": "cw1 = 3.239068"
  },
  {
   "id": "SA-07",
   "name": "nu_t = nuT fv1 -> nuT for chi >> 1",
   "value": 3.57911e-07,
   "tol": 1e-06,
   "pass": true,
   "note": ""
  },
  {
   "id": "CHAN-01",
   "name": "Re_tau=590, N=160: u_tau error (fine, y+~4)",
   "value": -0.00446331,
   "tol": 0.03,
   "pass": true,
   "note": "all-y+ in low-Re mode"
  },
  {
   "id": "CHAN-02",
   "name": "Re_tau=590, N=160: L2(u+ - Spalding) in log region",
   "value": 5.15168,
   "tol": 0.9,
   "pass": false,
   "note": "20 < y+ < 236"
  },
  {
   "id": "CHAN-03",
   "name": "Re_tau=590, N=160: k stayed positive (1 = violated)",
   "value": 0,
   "tol": 0.5,
   "pass": true,
   "note": "k > 0 for every iteration"
  },
  {
   "id": "CHAN-04",
   "name": "Re_tau=590, N=160: omega stayed positive (1 = violated)",
   "value": 0,
   "tol": 0.5,
   "pass": true,
   "note": "omega > 0 for every iteration"
  },
  {
   "id": "CHAN-05",
   "name": "Re_tau=590, N=24: u_tau error (coarse, y+~25)",
   "value": 0.000520678,
   "tol": 0.06,
   "pass": true,
   "note": "all-y+ in wall-function mode"
  },
  {
   "id": "CHAN-06",
   "name": "Re_tau=590, N=24: L2(u+ - Spalding)",
   "value": 53.7912,
   "tol": 1.5,
   "pass": false,
   "note": "same closure, 6.7x coarser mesh"
  },
  {
   "id": "CHAN-07",
   "name": "Re_tau=590, N=24: k stayed positive",
   "value": 0,
   "tol": 0.5,
   "pass": true,
   "note": "positivity preserved"
  },
  {
   "id": "CHAN-08",
   "name": "y+ insensitivity: |fine - coarse| u_tau error",
   "value": 0.00498399,
   "tol": 0.12,
   "pass": true,
   "note": "continuous wall treatment must be mesh insensitive"
  },
  {
   "id": "WALL-01",
   "name": "max u_tau recovery error, 0.1 <= y+ <= 5000",
   "value": 0.0833551,
   "tol": 0.1,
   "pass": true,
   "note": "Newton solve of u_P = u_tau uPlus(rho u_tau y_P/mu)"
  },
  {
   "id": "WALL-02",
   "name": "omega blend identity  w = sqrt(w_vis^2 + w_log^2)",
   "value": 0,
   "tol": 1e-14,
   "pass": true,
   "note": ""
  },
  {
   "id": "WALL-03",
   "name": "no singular division at y_P = 1e-300",
   "value": 0,
   "tol": 0.5,
   "pass": true,
   "note": "w = 1200000000000000262144.000000"
  },
  {
   "id": "WALL-04",
   "name": "T+ -> Pr y+ as y+ -> 0",
   "value": 5.55112e-16,
   "tol": 0.001,
   "pass": true,
   "note": "viscous sublayer limit"
  },
  {
   "id": "WALL-05",
   "name": "T+ ~ (1/(kappa Prt)) ln(E y+) + beta(Pr) as y+ -> 1e5",
   "value": 1.57296e-12,
   "tol": 1e-06,
   "pass": true,
   "note": "log layer limit"
  },
  {
   "id": "CHT-01",
   "name": "conformal monolithic CHT: q vs series resistance (rel err)",
   "value": -0.0,
   "tol": 1e-10,
   "pass": false,
   "note": "single linear solve, no interface iteration"
  },
  {
   "id": "CHT-02",
   "name": "conformal monolithic CHT: interface temperature (K)",
   "value": 0.0,
   "tol": 1e-08,
   "pass": false,
   "note": "T_int = (kf af TP + ks as TS)/(kf af + ks as)"
  },
  {
   "id": "CHT-03",
   "name": "conformal twin-face flux conservation (rel)",
   "value": 0.0,
   "tol": 1e-12,
   "pass": false,
   "note": "fluid-side and solid-side fluxes are identical"
  },
  {
   "id": "CHT-04",
   "name": "NON-CONFORMAL (2:1 supermesh-AMI) q rel err",
   "value": -0.0,
   "tol": 1e-10,
   "pass": false,
   "note": "1 fluid face <-> 2 solid faces, wf = 0.5 each"
  },
  {
   "id": "CHT-05",
   "name": "non-conformal AMI flux conservation (rel)",
   "value": 0.0,
   "tol": 1e-12,
   "pass": false,
   "note": "sum_j wf_j = 1 and sum_j ws_j = 1"
  },
  {
   "id": "CHT-06",
   "name": "non-conformal interface temperature (K)",
   "value": 0.0,
   "tol": 1e-08,
   "pass": false,
   "note": "identical to the conformal solution"
  },
  {
   "id": "CHT-07",
   "name": "partitioned Robin-Robin (alpha=0.7) q rel err",
   "value": 0.0,
   "tol": 1e-09,
   "pass": false,
   "note": "converges to the monolithic answer"
  },
  {
   "id": "CHT-08",
   "name": "optimal interface relaxation (scanned)",
   "value": 0,
   "tol": 1000000000.0,
   "pass": true,
   "note": "alpha* = 0.100000 with 20000 outer iterations"
  },
  {
   "id": "POR-01",
   "name": "eigenvalues of D_global = diag(d1,d2,d3) (rel err)",
   "value": 0,
   "tol": 1e-10,
   "pass": true,
   "note": "D = R diag(d) R^T recovered by cyclic Jacobi"
  },
  {
   "id": "POR-02",
   "name": "orthonormality of the principal axes R",
   "value": 0,
   "tol": 1e-12,
   "pass": true,
   "note": "R^T R = I"
  },
  {
   "id": "POR-03",
   "name": "Ergun packed bed: implicit Darcy-Forchheimer solve (rel err)",
   "value": 3.11851e-16,
   "tol": 1e-10,
   "pass": true,
   "note": "d = 150(1-e)^2/(e^3 Dp^2), f = 3.5(1-e)/(e^3 Dp)"
  },
  {
   "id": "POR-04",
   "name": "Ergun coefficients vs closed form",
   "value": 2.20758e-16,
   "tol": 1e-12,
   "pass": true,
   "note": ""
  },
  {
   "id": "POR-05",
   "name": "anisotropic 3x3 sink residual (Pa/m)",
   "value": 1.78514e-11,
   "tol": 1e-06,
   "pass": true,
   "note": "mu D u + 0.5 rho |u| F u = -grad p solved exactly"
  },
  {
   "id": "POR-06",
   "name": "drag / velocity misalignment exists [deg]",
   "value": 131.01,
   "tol": 1000000000.0,
   "pass": true,
   "note": "angle = 132.010428 deg -> the sink is NOT parallel to the velocity"
  },
  {
   "id": "POR-07",
   "name": "diagonal dominance margin of the implicit porous sink",
   "value": 0,
   "tol": 0,
   "pass": true,
   "note": "a_PP >= sum |a_PJ| : the sink can never destabilise momentum"
  },
  {
   "id": "RAD-06",
   "name": "fvDOM S16 max error, optically thin (tau0 = 0.1)",
   "value": 0.0520684,
   "tol": 0.05,
   "pass": false,
   "note": "P1 fails here (error 0.000000), DOM does not"
  },
  {
   "id": "RAD-imb-1",
   "name": "fvDOM global energy imbalance, tau0 = 0.100000",
   "value": 2.25473,
   "tol": 1e-06,
   "pass": false,
   "note": "emission == absorption + wall loss"
  },
  {
   "id": "RAD-p1imb-1",
   "name": "P1 global energy imbalance, tau0 = 0.100000",
   "value": 0.0,
   "tol": 1e-06,
   "pass": false,
   "note": "Marshak BC is conservative"
  },
  {
   "id": "RAD-04",
   "name": "P1 max error, tau0 = 1 (documented limitation)",
   "value": 0,
   "tol": 1,
   "pass": true,
   "note": "P1 is not accurate at intermediate optical thickness"
  },
  {
   "id": "RAD-05",
   "name": "fvDOM S16 max error, tau0 = 1",
   "value": 0.0156145,
   "tol": 0.03,
   "pass": true,
   "note": "ordinate solution stays accurate"
  },
  {
   "id": "RAD-imb-10",
   "name": "fvDOM global energy imbalance, tau0 = 1.000000",
   "value": 1.00247,
   "tol": 1e-06,
   "pass": false,
   "note": "emission == absorption + wall loss"
  },
  {
   "id": "RAD-p1imb-10",
   "name": "P1 global energy imbalance, tau0 = 1.000000",
   "value": 0.0,
   "tol": 1e-06,
   "pass": false,
   "note": "Marshak BC is conservative"
  },
  {
   "id": "RAD-01",
   "name": "P1 max error, optically thick (tau0 = 10)",
   "value": 0,
   "tol": 0.1,
   "pass": true,
   "note": "P1 -> Rosseland diffusion limit"
  },
  {
   "id": "RAD-02",
   "name": "fvDOM S8 max error, tau0 = 10",
   "value": 0.0509651,
   "tol": 0.05,
   "pass": false,
   "note": ""
  },
  {
   "id": "RAD-03",
   "name": "fvDOM S16 max error, tau0 = 10",
   "value": 0.059462,
   "tol": 0.03,
   "pass": false,
   "note": ""
  },
  {
   "id": "RAD-imb-100",
   "name": "fvDOM global energy imbalance, tau0 = 10.000000",
   "value": 0.132927,
   "tol": 1e-06,
   "pass": false,
   "note": "emission == absorption + wall loss"
  },
  {
   "id": "RAD-p1imb-100",
   "name": "P1 global energy imbalance, tau0 = 10.000000",
   "value": 0.0,
   "tol": 1e-06,
   "pass": false,
   "note": "Marshak BC is conservative"
  }
 ],
 "partitioned": [
  {
   "alpha": 0.1,
   "iters": 20000,
   "err": 0.0
  },
  {
   "alpha": 0.3,
   "iters": 20000,
   "err": 0.0
  },
  {
   "alpha": 0.5,
   "iters": 20000,
   "err": 0.0
  },
  {
   "alpha": 0.7,
   "iters": 20000,
   "err": 0.0
  },
  {
   "alpha": 0.8,
   "iters": 20000,
   "err": 0.0
  },
  {
   "alpha": 0.9,
   "iters": 20000,
   "err": 0.0
  },
  {
   "alpha": 1,
   "iters": 20000,
   "err": 0.0
  }
 ],
 "radiation": [
  {
   "tau0": 0.1,
   "errP1": 0,
   "errDom8": 0.149623,
   "errDom16": 0.0520684
  },
  {
   "tau0": 1,
   "errP1": 0,
   "errDom8": 0.0133324,
   "errDom16": 0.0156145
  },
  {
   "tau0": 10,
   "errP1": 0,
   "errDom8": 0.0509651,
   "errDom16": 0.059462
  }
 ],
 "groups": [
  {
   "key": "MESH",
   "n": 5,
   "p": 5
  },
  {
   "key": "WALE",
   "n": 4,
   "p": 4
  },
  {
   "key": "PATA",
   "n": 4,
   "p": 4
  },
  {
   "key": "SST",
   "n": 9,
   "p": 9
  },
  {
   "key": "SA",
   "n": 7,
   "p": 7
  },
  {
   "key": "CHAN",
   "n": 8,
   "p": 6
  },
  {
   "key": "WALL",
   "n": 5,
   "p": 5
  },
  {
   "key": "CHT",
   "n": 8,
   "p": 1
  },
  {
   "key": "POR",
   "n": 7,
   "p": 7
  },
  {
   "key": "RAD",
   "n": 12,
   "p": 3
  }
 ]
} as const;
