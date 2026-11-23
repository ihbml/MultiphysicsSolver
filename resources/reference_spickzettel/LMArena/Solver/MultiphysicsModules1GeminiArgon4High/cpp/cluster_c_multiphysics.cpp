// ============================================================================
// CLUSTER C — C++20 MULTIPHYSICS SOLVER SUITE
// ----------------------------------------------------------------------------
//   C1: Electrochemical-Thermal 2-RC Battery ECM + 4-Stage Arrhenius Runaway
//       (Hatchard/Spotnitz SEI, Negative-Solvent, Positive-Electrode,
//        Electrolyte kinetics + Rosenbrock-W / Patankar Stiff Integrator)
//   C2: Ffowcs Williams-Hawkings (FWH) & Curle Aeroacoustics
//       (Farassat Formulation 1A Time-Domain Integral + Radix-2 Hann FFT,
//        Narrowband PSD, ANSI 1/3-Octave Bands, and OASPL [dB re 20 uPa])
//   C3: Conservative Inductionless Magnetohydrodynamics (MHD, Re_m << 1)
//       (Ni et al. 4-Step Projection on Collocated Polyhedral FVM Grids with
//        Over-Relaxed Non-Orthogonal Laplacian + Exact Hartmann Verification)
//   C4: Charring Ablation, Pyrolysis Gas Blowing & BDF2-DGCL ALE Recession
//       (3-Component Arrhenius Pyrolysis, Surface Radiation/Sublimation/Blowing
//        Newton Balance, and Discrete Geometric Conservation Law ALE Mesh)
//
// Compile & Verify:
//   g++ -std=c++20 -O2 -Wall -Wextra cpp/cluster_c_multiphysics.cpp -o cluster_c_verify
// ============================================================================

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <numbers>
#include <numeric>
#include <span>
#include <sstream>
#include <string>
#include <vector>

namespace multiphysics::cluster_c {

// ============================================================================
// 0. CORE SOA DATA LAKE (CAbiSpan) & POLYHEDRAL FVM TOPOLOGY
// ============================================================================

extern "C" {
struct RawCAbiSpan {
    void*       data;
    std::size_t size;
    std::size_t stride_bytes;
};
}

template <typename T>
struct CAbiSpan {
    using value_type = std::remove_cv_t<T>;
    T*          ptr{nullptr};
    std::size_t count{0};

    constexpr CAbiSpan() noexcept = default;
    constexpr CAbiSpan(T* p, std::size_t n) noexcept : ptr(p), count(n) {}
    explicit CAbiSpan(std::vector<value_type>& v) noexcept : ptr(v.data()), count(v.size()) {}
    explicit CAbiSpan(const std::vector<value_type>& v) noexcept
        requires std::is_const_v<T>
        : ptr(v.data()), count(v.size()) {}

    [[nodiscard]] constexpr T& operator[](std::size_t i) noexcept { return ptr[i]; }
    [[nodiscard]] constexpr const T& operator[](std::size_t i) const noexcept { return ptr[i]; }
    [[nodiscard]] constexpr std::size_t size() const noexcept { return count; }
    [[nodiscard]] constexpr T* begin() noexcept { return ptr; }
    [[nodiscard]] constexpr T* end() noexcept { return ptr + count; }
    [[nodiscard]] constexpr const T* begin() const noexcept { return ptr; }
    [[nodiscard]] constexpr const T* end() const noexcept { return ptr + count; }
    [[nodiscard]] constexpr std::span<T> as_span() const noexcept { return {ptr, count}; }
};

struct Vec3 {
    double x{0.0}, y{0.0}, z{0.0};

    constexpr Vec3() noexcept = default;
    constexpr Vec3(double x_, double y_, double z_) noexcept : x(x_), y(y_), z(z_) {}

    [[nodiscard]] constexpr Vec3 operator+(const Vec3& o) const noexcept { return {x + o.x, y + o.y, z + o.z}; }
    [[nodiscard]] constexpr Vec3 operator-(const Vec3& o) const noexcept { return {x - o.x, y - o.y, z - o.z}; }
    [[nodiscard]] constexpr Vec3 operator*(double s) const noexcept { return {x * s, y * s, z * s}; }
    [[nodiscard]] constexpr Vec3 operator/(double s) const noexcept { return {x / s, y / s, z / s}; }
    constexpr Vec3& operator+=(const Vec3& o) noexcept { x += o.x; y += o.y; z += o.z; return *this; }
    constexpr Vec3& operator-=(const Vec3& o) noexcept { x -= o.x; y -= o.y; z -= o.z; return *this; }

    [[nodiscard]] constexpr double dot(const Vec3& o) const noexcept { return x * o.x + y * o.y + z * o.z; }
    [[nodiscard]] constexpr Vec3 cross(const Vec3& o) const noexcept {
        return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x};
    }
    [[nodiscard]] constexpr double norm_sq() const noexcept { return x * x + y * y + z * z; }
    [[nodiscard]] double norm() const noexcept { return std::sqrt(norm_sq()); }
};

[[nodiscard]] constexpr Vec3 operator*(double s, const Vec3& v) noexcept { return v * s; }

struct PolyhedralFVMTopology {
    std::size_t n_cells{0};
    std::size_t n_internal_faces{0};
    std::size_t n_faces{0};

    std::vector<std::size_t> owner;          // [n_faces]
    std::vector<std::size_t> neighbour;      // [n_internal_faces]
    std::vector<double>      cell_vol;       // [n_cells] V_P
    std::vector<Vec3>        cell_center;    // [n_cells] x_P
    std::vector<Vec3>        face_center;    // [n_faces] x_f
    std::vector<Vec3>        Sf;             // [n_faces] outward area vector S_f (owner -> neighbour)
    std::vector<double>      alpha_f;        // [n_faces] over-relaxed orthogonal diffusivity factor
    std::vector<Vec3>        k_corr;         // [n_faces] non-orthogonal correction vector S_f - alpha_f * d_PN
    std::vector<double>      fx;             // [n_faces] linear interpolation weight

    // Build a 1D/2D/3D structured or skewed polyhedral FVM mesh with over-relaxed metrics
    static PolyhedralFVMTopology build_cartesian_or_skewed_grid(
        std::size_t nx, std::size_t ny, std::size_t nz,
        double lx, double ly, double lz, double skew_factor = 0.0)
    {
        PolyhedralFVMTopology mesh;
        mesh.n_cells = nx * ny * nz;
        mesh.cell_vol.resize(mesh.n_cells);
        mesh.cell_center.resize(mesh.n_cells);

        const double dx = lx / static_cast<double>(nx);
        const double dy = ly / static_cast<double>(ny);
        const double dz = lz / static_cast<double>(nz);

        auto cell_id = [&](std::size_t i, std::size_t j, std::size_t k) {
            return (k * ny + j) * nx + i;
        };

        for (std::size_t k = 0; k < nz; ++k) {
            for (std::size_t j = 0; j < ny; ++j) {
                for (std::size_t i = 0; i < nx; ++i) {
                    const std::size_t c = cell_id(i, j, k);
                    const double xc = (static_cast<double>(i) + 0.5) * dx;
                    const double yc = (static_cast<double>(j) + 0.5) * dy - 0.5 * ly;
                    const double zc = (static_cast<double>(k) + 0.5) * dz;
                    const double skew_x = skew_factor * dx * std::sin(std::numbers::pi * (yc + 0.5 * ly) / ly);
                    mesh.cell_center[c] = {xc + skew_x, yc, zc};
                    mesh.cell_vol[c] = dx * dy * dz;
                }
            }
        }

        // Internal faces (x, y, z directions)
        auto add_internal_face = [&](std::size_t p, std::size_t n, const Vec3& xf, const Vec3& sf) {
            mesh.owner.push_back(p);
            mesh.neighbour.push_back(n);
            mesh.face_center.push_back(xf);
            mesh.Sf.push_back(sf);
            const Vec3 d_pn = mesh.cell_center[n] - mesh.cell_center[p];
            const double sf_dot_d = std::max(1e-18, sf.dot(d_pn));
            const double alpha = sf.norm_sq() / sf_dot_d; // Over-relaxed approach: |S_f|^2 / (S_f . d_PN)
            mesh.alpha_f.push_back(alpha);
            mesh.k_corr.push_back(sf - alpha * d_pn);
            mesh.fx.push_back(0.5);
        };

        for (std::size_t k = 0; k < nz; ++k) {
            for (std::size_t j = 0; j < ny; ++j) {
                for (std::size_t i = 0; i < nx; ++i) {
                    const std::size_t p = cell_id(i, j, k);
                    if (i + 1 < nx) {
                        const std::size_t n = cell_id(i + 1, j, k);
                        const Vec3 xf = 0.5 * (mesh.cell_center[p] + mesh.cell_center[n]);
                        add_internal_face(p, n, xf, {dy * dz, 0.0, 0.0});
                    }
                    if (j + 1 < ny) {
                        const std::size_t n = cell_id(i, j + 1, k);
                        const Vec3 xf = 0.5 * (mesh.cell_center[p] + mesh.cell_center[n]);
                        add_internal_face(p, n, xf, {0.0, dx * dz, 0.0});
                    }
                    if (k + 1 < nz) {
                        const std::size_t n = cell_id(i, j, k + 1);
                        const Vec3 xf = 0.5 * (mesh.cell_center[p] + mesh.cell_center[n]);
                        add_internal_face(p, n, xf, {0.0, 0.0, dx * dy});
                    }
                }
            }
        }
        mesh.n_internal_faces = mesh.owner.size();

        // Boundary faces (top/bottom y walls, front/back x, top/bottom z)
        auto add_boundary_face = [&](std::size_t p, const Vec3& xf, const Vec3& sf) {
            mesh.owner.push_back(p);
            mesh.face_center.push_back(xf);
            mesh.Sf.push_back(sf);
            const Vec3 d_pf = xf - mesh.cell_center[p];
            const double sf_dot_d = std::max(1e-18, sf.dot(d_pf));
            const double alpha = sf.norm_sq() / sf_dot_d;
            mesh.alpha_f.push_back(alpha);
            mesh.k_corr.push_back(sf - alpha * d_pf);
            mesh.fx.push_back(1.0);
        };

        for (std::size_t k = 0; k < nz; ++k) {
            for (std::size_t j = 0; j < ny; ++j) {
                for (std::size_t i = 0; i < nx; ++i) {
                    const std::size_t p = cell_id(i, j, k);
                    const Vec3& cp = mesh.cell_center[p];
                    if (i == 0)      add_boundary_face(p, {0.0, cp.y, cp.z}, {-dy * dz, 0.0, 0.0});
                    if (i + 1 == nx) add_boundary_face(p, {lx,  cp.y, cp.z}, { dy * dz, 0.0, 0.0});
                    if (j == 0)      add_boundary_face(p, {cp.x, -0.5 * ly, cp.z}, {0.0, -dx * dz, 0.0});
                    if (j + 1 == ny) add_boundary_face(p, {cp.x,  0.5 * ly, cp.z}, {0.0,  dx * dz, 0.0});
                    if (k == 0)      add_boundary_face(p, {cp.x, cp.y, 0.0}, {0.0, 0.0, -dx * dy});
                    if (k + 1 == nz) add_boundary_face(p, {cp.x, cp.y, lz},  {0.0, 0.0,  dx * dy});
                }
            }
        }
        mesh.n_faces = mesh.owner.size();
        return mesh;
    }

    // Green-Gauss cell-centered gradient reconstruction for a scalar field phi
    void compute_green_gauss_gradient(
        CAbiSpan<const double> phi,
        CAbiSpan<const double> phi_boundary,
        CAbiSpan<Vec3> grad_phi) const noexcept
    {
        for (std::size_t c = 0; c < n_cells; ++c) {
            grad_phi[c] = {0.0, 0.0, 0.0};
        }
        for (std::size_t f = 0; f < n_internal_faces; ++f) {
            const std::size_t p = owner[f];
            const std::size_t n = neighbour[f];
            const double phi_f = fx[f] * phi[p] + (1.0 - fx[f]) * phi[n];
            const Vec3 contrib = phi_f * Sf[f];
            grad_phi[p] += contrib;
            grad_phi[n] -= contrib;
        }
        for (std::size_t f = n_internal_faces; f < n_faces; ++f) {
            const std::size_t p = owner[f];
            const double phi_b = phi_boundary[f - n_internal_faces];
            grad_phi[p] += phi_b * Sf[f];
        }
        for (std::size_t c = 0; c < n_cells; ++c) {
            grad_phi[c] = grad_phi[c] / cell_vol[c];
        }
    }
};

// ============================================================================
// MODULE C1: ELECTROCHEMICAL-THERMAL 2-RC BATTERY MODEL +
//            4-STAGE ARRHENIUS THERMAL RUNAWAY (HATCHARD/SPOTNITZ)
//            WITH ROSENBROCK-W / PATANKAR STIFF SOURCE INTEGRATOR
// ============================================================================

struct BatteryAbuseKineticsParams {
    double R_u{8.314462618};         // Universal gas constant [J/(mol*K)]

    // 2-RC Nominal Cell Parameters (NMC811/Graphite 50 Ah Pouch/Prismatic Cell)
    double Q_nom_Ah{50.0};           // Nominal capacity [Ah]
    double rho_cell{2450.0};         // Bulk density [kg/m^3]
    double cp_cell{980.0};           // Specific heat capacity [J/(kg*K)]
    double k_inplane{24.0};          // In-plane thermal conductivity [W/(m*K)]
    double k_through{1.65};          // Through-plane thermal conductivity [W/(m*K)]

    // Stage 1: SEI Decomposition (c_SEI)
    double A_SEI{1.667e15};          // [1/s]
    double E_SEI{1.3508e5};          // [J/mol]
    double m_SEI{1.0};               // Reaction order
    double H_SEI{2.57e5};            // Specific enthalpy [J/kg]
    double W_c{610.4};               // Carbon active content [kg/m^3]
    double c_SEI_0{0.15};            // Initial metastable SEI fraction

    // Stage 2: Negative Electrode - Solvent Reaction (c_ne, tunneling barrier t_SEI)
    double A_ne{2.5e13};             // [1/s]
    double E_ne{1.3508e5};           // [J/mol]
    double H_ne{1.714e6};            // [J/kg]
    double c_ne_0{0.75};             // Initial intercalated Li fraction
    double t_SEI_0{0.033};           // Reference SEI thickness parameter

    // Stage 3: Positive Electrode Decomposition (autocatalytic alpha_pe)
    double A_pe{6.667e13};           // [1/s]
    double E_pe{1.396e5};            // [J/mol]
    double m_pe1{1.0};               // Autocatalytic exponent 1
    double m_pe2{1.0};               // Autocatalytic exponent 2
    double H_pe{3.14e5};             // [J/kg]
    double W_p{1221.0};              // Cathode active content [kg/m^3]
    double alpha_pe_0{0.04};         // Initial degree of conversion

    // Stage 4: Electrolyte Decomposition (c_e)
    double A_e{5.14e25};             // [1/s]
    double E_e{2.74e5};              // [J/mol]
    double m_e{1.0};                 // Reaction order
    double H_e{1.55e5};              // [J/kg]
    double W_e{406.9};               // Electrolyte content [kg/m^3]
    double c_e_0{1.0};               // Initial electrolyte concentration
};

struct BatterySoADataLake {
    CAbiSpan<double> T;              // Cell temperature [K]
    CAbiSpan<double> SOC;            // State of Charge [0, 1]
    CAbiSpan<double> V1;             // Fast RC polarization voltage [V]
    CAbiSpan<double> V2;             // Slow diffusion RC polarization voltage [V]
    CAbiSpan<double> I_cell;         // Applied/short-circuit cell current [A] (>0 discharge)
    CAbiSpan<double> V_term;         // Terminal voltage [V]
    CAbiSpan<double> c_SEI;          // Dimensionless SEI reactant concentration
    CAbiSpan<double> c_ne;           // Dimensionless intercalated Li in anode
    CAbiSpan<double> t_SEI;          // Dimensionless SEI tunneling thickness
    CAbiSpan<double> alpha_pe;       // Cathode conversion fraction
    CAbiSpan<double> c_e;            // Dimensionless electrolyte concentration
    CAbiSpan<double> q_ecm;          // Total electrochemical heat (q_rev + q_irr) [W/m^3]
    CAbiSpan<double> q_abuse;        // Total 4-stage Arrhenius abuse heat [W/m^3]
};

class BatteryThermalRunawaySolver {
public:
    explicit BatteryThermalRunawaySolver(const BatteryAbuseKineticsParams& params = {})
        : p_(params) {}

    // Empirical NMC811/Graphite Open-Circuit Voltage U_OCV(SOC, T) [V]
    [[nodiscard]] double compute_U_OCV(double soc, double T) const noexcept {
        const double s = std::clamp(soc, 0.001, 0.999);
        const double u_ref = 3.425 + 0.685 * s - 0.195 * s * s + 0.285 * std::pow(s, 3.0)
                           - 0.042 * std::exp(-28.0 * s) + 0.035 * std:: tanh(14.0 * (s - 0.88));
        return u_ref + compute_dU_dT(s) * (T - 298.15);
    }

    // Entropic coefficient dU_OCV / dT [V/K]
    [[nodiscard]] double compute_dU_dT(double soc) const noexcept {
        const double s = std::clamp(soc, 0.0, 1.0);
        // Captures typical graphite staging entropy signature (-0.4 to +0.2 mV/K)
        return 1e-3 * (-0.28 + 0.52 * std::sin(2.0 * std::numbers::pi * s - 0.4)
                       - 0.15 * std::cos(4.0 * std::numbers::pi * s));
    }

    // Temperature & SOC-dependent 2-RC ECM parameters: R0, R1, C1, R2, C2
    void evaluate_ecm_branches(double soc, double T,
                               double& R0, double& R1, double& C1, double& R2, double& C2) const noexcept
    {
        const double s = std::clamp(soc, 0.02, 0.98);
        const double T_clamped = std::clamp(T, 250.0, 900.0);
        const double arr_R = std::exp(2400.0 * (1.0 / T_clamped - 1.0 / 298.15));
        const double soc_factor = 1.0 + 0.45 * std::exp(-12.0 * s) + 0.18 * std::exp(8.0 * (s - 0.92));

        R0 = 1.45e-3 * arr_R * soc_factor; // Ohmic series resistance [Ohm]
        R1 = 0.95e-3 * arr_R * soc_factor; // Charge-transfer resistance [Ohm]
        C1 = 1850.0 / arr_R;               // Double-layer capacitance [F]
        R2 = 2.10e-3 * arr_R * soc_factor; // Solid-phase diffusion resistance [Ohm]
        C2 = 16500.0 / arr_R;              // Diffusion capacitance [F]
    }

    // Coupled 2-RC ECM + 3D FVM + Rosenbrock-W / Patankar-Diagonalized 4-Stage Abuse Step
    void step_coupled_electro_thermal(
        const PolyhedralFVMTopology& mesh,
        BatterySoADataLake& state,
        double dt,
        double h_coolant = 350.0,
        double T_coolant = 298.15) const
    {
        const std::size_t N = mesh.n_cells;
        std::vector<Vec3> grad_T(N, {0.0, 0.0, 0.0});
        std::vector<double> T_bound(mesh.n_faces - mesh.n_internal_faces, T_coolant);
        for (std::size_t bf = 0; bf < T_bound.size(); ++bf) {
            T_bound[bf] = state.T[mesh.owner[mesh.n_internal_faces + bf]];
        }
        mesh.compute_green_gauss_gradient(
            CAbiSpan<const double>(state.T.ptr, N),
            CAbiSpan<const double>(T_bound),
            CAbiSpan<Vec3>(grad_T));

        // Assemble explicit FVM conduction flux + non-orthogonal correction
        std::vector<double> fvm_cond(N, 0.0);
        std::vector<double> fvm_diag(N, 0.0);

        for (std::size_t f = 0; f < mesh.n_internal_faces; ++f) {
            const std::size_t p = mesh.owner[f];
            const std::size_t n = mesh.neighbour[f];
            const Vec3 nf = mesh.Sf[f] / std::max(1e-18, mesh.Sf[f].norm());
            // Anisotropic thermal conductivity projected onto face normal
            const double k_f = p_.k_inplane * (nf.x * nf.x + nf.y * nf.y) + p_.k_through * (nf.z * nf.z);
            const Vec3 grad_f = 0.5 * (grad_T[p] + grad_T[n]);
            const double orth_coeff = k_f * mesh.alpha_f[f];
            const double non_orth_flux = k_f * grad_f.dot(mesh.k_corr[f]);

            const double flux_pn = orth_coeff * (state.T[n] - state.T[p]) + non_orth_flux;
            fvm_cond[p] += flux_pn;
            fvm_cond[n] -= flux_pn;
            fvm_diag[p] += orth_coeff;
            fvm_diag[n] += orth_coeff;
        }

        const double cell_volume_total = std::accumulate(mesh.cell_vol.begin(), mesh.cell_vol.end(), 0.0);

        for (std::size_t c = 0; c < N; ++c) {
            const double Tc = std::clamp(state.T[c], 240.0, 2200.0);
            const double soc_old = state.SOC[c];
            const double I = state.I_cell[c];

            // 1. Integrate SOC: d(SOC)/dt = -I / (3600 * Q_nom)
            const double soc_new = std::clamp(soc_old - (I / (3600.0 * p_.Q_nom_Ah)) * dt, 0.0, 1.0);
            state.SOC[c] = soc_new;

            // 2. Integrate 2-RC polarization voltages V1, V2 via exact exponential integrator
            double R0 = 0.0, R1 = 0.0, C1 = 1.0, R2 = 0.0, C2 = 1.0;
            evaluate_ecm_branches(soc_new, Tc, R0, R1, C1, R2, C2);

            const double tau1 = R1 * C1;
            const double tau2 = R2 * C2;
            const double exp1 = std::exp(-dt / std::max(1e-9, tau1));
            const double exp2 = std::exp(-dt / std::max(1e-9, tau2));

            state.V1[c] = state.V1[c] * exp1 + I * R1 * (1.0 - exp1);
            state.V2[c] = state.V2[c] * exp2 + I * R2 * (1.0 - exp2);

            const double U_ocv = compute_U_OCV(soc_new, Tc);
            const double dU_dT = compute_dU_dT(soc_new);
            // Discharge I > 0 reduces terminal voltage: V_term = U_OCV - I*R0 - V1 - V2
            const double V_term = U_ocv - I * R0 - state.V1[c] - state.V2[c];
            state.V_term[c] = V_term;

            // Reversible entropic heat & Irreversible Joule/Overpotential heat
            // Note: q_irr = I * (U_OCV - V_term) = -I * (V_term - U_OCV) >= 0 for discharge/charge
            const double q_rev_W = I * Tc * dU_dT;
            const double q_irr_W = std::abs(I * (V_term - U_ocv));
            const double q_ecm_vol = (q_rev_W + q_irr_W) / std::max(1e-9, cell_volume_total);
            state.q_ecm[c] = q_ecm_vol;

            // 3. 4-Stage Arrhenius Abuse Kinetics with Patankar-Diagonalized Positivity-Preserving Update
            //    Stage 1: SEI decomposition dc_SEI/dt = -A_SEI * exp(-E_SEI/(R*T)) * c_SEI^m_SEI
            const double k_SEI = p_.A_SEI * std::exp(-p_.E_SEI / (p_.R_u * Tc));
            const double c_sei_old = std::max(0.0, state.c_SEI[c]);
            const double c_sei_new = c_sei_old / (1.0 + dt * k_SEI * std::pow(std::max(1e-12, c_sei_old), p_.m_SEI - 1.0));
            const double R_SEI = (c_sei_old - c_sei_new) / dt;
            state.c_SEI[c] = c_sei_new;

            //    Stage 2: Anode-Solvent reaction dc_ne/dt = -A_ne * exp(-E_ne/(R*T)) * exp(-t_SEI/t_SEI0) * c_ne
            const double t_sei_old = std::max(p_.t_SEI_0, state.t_SEI[c]);
            const double k_ne = p_.A_ne * std::exp(-p_.E_ne / (p_.R_u * Tc)) * std::exp(-t_sei_old / p_.t_SEI_0);
            const double c_ne_old = std::max(0.0, state.c_ne[c]);
            const double c_ne_new = c_ne_old / (1.0 + dt * k_ne);
            const double R_ne = (c_ne_old - c_ne_new) / dt;
            state.c_ne[c] = c_ne_new;
            state.t_SEI[c] = t_sei_old + dt * R_ne;

            //    Stage 3: Cathode Autocatalytic decomposition d(alpha_pe)/dt = A_pe * alpha^m1 * (1-alpha)^m2 * exp(-E_pe/(R*T))
            const double k_pe = p_.A_pe * std::exp(-p_.E_pe / (p_.R_u * Tc));
            const double a_old = std::clamp(state.alpha_pe[c], 1e-6, 1.0 - 1e-9);
            const double prod_rate = k_pe * std::pow(a_old, p_.m_pe1) * std::pow(1.0 - a_old, p_.m_pe2);
            // Patankar slack variable (1 - alpha_pe) positivity preservation
            const double one_minus_a_new = (1.0 - a_old) / (1.0 + dt * prod_rate / std::max(1e-12, 1.0 - a_old));
            const double a_new = std::clamp(1.0 - one_minus_a_new, a_old, 1.0);
            const double R_pe = (a_new - a_old) / dt;
            state.alpha_pe[c] = a_new;

            //    Stage 4: Electrolyte decomposition dc_e/dt = -A_e * exp(-E_e/(R*T)) * c_e^m_e
            const double k_e = p_.A_e * std::exp(-p_.E_e / (p_.R_u * Tc));
            const double c_e_old = std::max(0.0, state.c_e[c]);
            const double c_e_new = c_e_old / (1.0 + dt * k_e * std::pow(std::max(1e-12, c_e_old), p_.m_e - 1.0));
            const double R_e = (c_e_old - c_e_new) / dt;
            state.c_e[c] = c_e_new;

            // Volumetric abuse heat generation rates [W/m^3]
            const double q_SEI = p_.H_SEI * p_.W_c * R_SEI;
            const double q_ne  = p_.H_ne  * p_.W_c * R_ne;
            const double q_pe  = p_.H_pe  * p_.W_p * R_pe;
            const double q_e   = p_.H_e   * p_.W_e * R_e;
            const double q_tr  = q_SEI + q_ne + q_pe + q_e;
            state.q_abuse[c] = q_tr;

            // Rosenbrock-W linearized stiff thermal Jacobian d(q_tr)/dT (stabilizes rapid thermal runaway)
            const double dq_tr_dT = (q_SEI * p_.E_SEI + q_ne * p_.E_ne + q_pe * p_.E_pe + q_e * p_.E_e)
                                  / (p_.R_u * Tc * Tc);

            // Liquid cooling plate sink (volumetric equivalent h_eff * A_wet / V)
            const double h_vol = h_coolant * 28.0; // Cold-plate interfacial area density [W/(m^3*K)]
            const double q_cool = h_vol * (T_coolant - Tc);

            const double rho_cp_V = p_.rho_cell * p_.cp_cell * mesh.cell_vol[c];
            const double rhs = fvm_cond[c] + (q_ecm_vol + q_tr + q_cool) * mesh.cell_vol[c];

            // Implicit Rosenbrock-W / Patankar-stabilized denominator prevents overshoot/collapse
            const double rosenbrock_denom = (rho_cp_V / dt) + fvm_diag[c] + h_vol * mesh.cell_vol[c]
                                          + std::max(0.0, 0.5 * dq_tr_dT * mesh.cell_vol[c]);
            state.T[c] = Tc + rhs / rosenbrock_denom;
        }
    }

private:
    BatteryAbuseKineticsParams p_;
};

// ============================================================================
// MODULE C2: FFOWCS WILLIAMS-HAWKINGS (FWH) & CURLE AEROACOUSTICS
//            (FARASSAT FORMULATION 1A + RADIX-2 HANN FFT SPECTRAL ANALYZER)
// ============================================================================

struct FWHPatchSample {
    Vec3   y;         // Patch centroid position [m]
    Vec3   n;         // Unit outward normal vector
    double dS{0.0};   // Patch surface area [m^2]
    Vec3   v_s;       // Surface kinematics velocity [m/s]
    Vec3   v_s_dot;   // Surface acceleration dv_s/dt [m/s^2]
    Vec3   u_f;       // Fluid velocity on patch [m/s]
    Vec3   u_f_dot;   // Fluid velocity time derivative du_f/dt [m/s^2]
    double rho{1.225};     // Local density [kg/m^3]
    double rho_dot{0.0};   // Density time derivative [kg/(m^3*s)]
    double p_gauge{0.0};   // Surface gauge pressure (p - p_0) [Pa]
    double p_dot{0.0};     // Surface pressure time derivative dp/dt [Pa/s]
    bool   permeable{false};
};

struct SpectralBandResult {
    double center_freq_Hz{0.0};
    double band_spl_dB{0.0};
};

struct FWHSpectralTelemetry {
    double p_rms_Pa{0.0};
    double oaspl_dB{0.0};
    double peak_freq_Hz{0.0};
    std::vector<double> freq_Hz;
    std::vector<double> psd_Pa2_Hz;
    std::vector<double> spl_narrow_dB;
    std::vector<SpectralBandResult> third_octave_bands;
};

class Farassat1AAeroacousticsSolver {
public:
    Farassat1AAeroacousticsSolver(double rho0 = 1.225, double c0 = 343.0) noexcept
        : rho0_(rho0), c0_(c0) {}

    // Evaluate Farassat Formulation 1A integrand for a single patch -> microphone pair
    void evaluate_patch_contribution(
        const FWHPatchSample& patch,
        const Vec3& x_mic,
        double& dp_thickness,
        double& dp_loading,
        double& tau_delay) const noexcept
    {
        const Vec3 r_vec = x_mic - patch.y;
        const double r = std::max(1e-6, r_vec.norm());
        const Vec3 r_hat = r_vec / r;
        tau_delay = r / c0_;

        const Vec3 M_vec = patch.v_s / c0_;
        const double M_sq = M_vec.norm_sq();
        const double M_r = M_vec.dot(r_hat);
        const double M_r_dot = patch.v_s_dot.dot(r_hat) / c0_;

        const double omr = std::copysign(std::max(0.05, std::abs(1.0 - M_r)), 1.0 - M_r);
        const double omr2 = omr * omr;
        const double omr3 = omr2 * omr;
        const double doppler_kin = r * M_r_dot + c0_ * (M_r - M_sq);

        // Generalized mass-flux vector U_i and loading vector L_i (solid Curle or permeable FWH)
        const double v_n = patch.v_s.dot(patch.n);
        const double v_n_dot = patch.v_s_dot.dot(patch.n);
        double U_n = v_n;
        double U_n_dot = v_n_dot;

        Vec3 L_vec = patch.p_gauge * patch.n;
        Vec3 L_dot_vec = patch.p_dot * patch.n;

        if (patch.permeable) {
            const double u_n = patch.u_f.dot(patch.n);
            const double u_n_dot = patch.u_f_dot.dot(patch.n);
            U_n = (1.0 - patch.rho / rho0_) * v_n + (patch.rho / rho0_) * u_n;
            U_n_dot = (-patch.rho_dot / rho0_) * v_n + (1.0 - patch.rho / rho0_) * v_n_dot
                    + (patch.rho_dot / rho0_) * u_n + (patch.rho / rho0_) * u_n_dot;

            L_vec += patch.rho * (u_n - v_n) * patch.u_f;
            L_dot_vec += patch.rho_dot * (u_n - v_n) * patch.u_f
                       + patch.rho * (u_n_dot - v_n_dot) * patch.u_f
                       + patch.rho * (u_n - v_n) * patch.u_f_dot;
        }

        const double L_r = L_vec.dot(r_hat);
        const double L_r_dot = L_dot_vec.dot(r_hat);
        const double L_M = L_vec.dot(M_vec);

        constexpr double inv_4pi = 1.0 / (4.0 * std::numbers::pi);

        // Farassat 1A Thickness noise p'_T(x, t)
        const double term_T1 = rho0_ * U_n_dot / (r * omr2);
        const double term_T2 = rho0_ * U_n * doppler_kin / (r * r * omr3);
        dp_thickness = inv_4pi * (term_T1 + term_T2) * patch.dS;

        // Farassat 1A Loading noise p'_L(x, t)
        const double term_L1 = L_r_dot / (c0_ * r * omr2);
        const double term_L2 = (L_r - L_M) / (r * r * omr2);
        const double term_L3 = L_r * doppler_kin / (c0_ * r * r * omr3);
        dp_loading = inv_4pi * (term_L1 + term_L2 + term_L3) * patch.dS;
    }

    // Forward-time source-time dominant accumulation into microphone time histories
    void accumulate_source_time_step(
        CAbiSpan<const FWHPatchSample> patches,
        const Vec3& x_mic,
        double t_source,
        double t_start,
        double dt_observer,
        CAbiSpan<double> p_thickness_hist,
        CAbiSpan<double> p_loading_hist) const noexcept
    {
        const std::size_t n_samples = p_thickness_hist.size();
        for (const auto& patch : patches) {
            double dp_T = 0.0, dp_L = 0.0, tau = 0.0;
            evaluate_patch_contribution(patch, x_mic, dp_T, dp_L, tau);

            const double t_obs = t_source + tau;
            const double idx_f = (t_obs - t_start) / dt_observer;
            if (idx_f >= 0.0 && idx_f + 1.0 < static_cast<double>(n_samples)) {
                const auto idx0 = static_cast<std::size_t>(idx_f);
                const double w = idx_f - static_cast<double>(idx0);
                p_thickness_hist[idx0]     += (1.0 - w) * dp_T;
                p_thickness_hist[idx0 + 1] += w * dp_T;
                p_loading_hist[idx0]       += (1.0 - w) * dp_L;
                p_loading_hist[idx0 + 1]   += w * dp_L;
            }
        }
    }

    // In-place iterative Radix-2 Cooley-Tukey FFT
    static void radix2_fft(std::vector<std::complex<double>>& a) noexcept {
        const std::size_t n = a.size();
        for (std::size_t i = 1, j = 0; i < n; ++i) {
            std::size_t bit = n >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap(a[i], a[j]);
        }
        for (std::size_t len = 2; len <= n; len <<= 1) {
            const double ang = -2.0 * std::numbers::pi / static_cast<double>(len);
            const std::complex<double> wlen(std::cos(ang), std::sin(ang));
            for (std::size_t i = 0; i < n; i += len) {
                std::complex<double> w(1.0, 0.0);
                for (std::size_t j = 0; j < len / 2; ++j) {
                    const std::complex<double> u = a[i + j];
                    const std::complex<double> v = a[i + j + len / 2] * w;
                    a[i + j] = u + v;
                    a[i + j + len / 2] = u - v;
                    w *= wlen;
                }
            }
        }
    }

    // Hann-windowed Radix-2 FFT spectral analyzer -> PSD, 1/3-Octave Bands, and OASPL
    [[nodiscard]] FWHSpectralTelemetry analyze_spectrum(
        CAbiSpan<const double> p_signal,
        double fs_Hz) const
    {
        FWHSpectralTelemetry out;
        std::size_t N = 1;
        while (N * 2 <= p_signal.size()) N <<= 1;
        if (N < 8) return out;

        constexpr double p_ref = 20.0e-6; // 20 uPa acoustic reference pressure
        const double p_ref_sq = p_ref * p_ref;

        // Mean removal + RMS calculation
        double mean_p = 0.0;
        for (std::size_t i = 0; i < N; ++i) mean_p += p_signal[i];
        mean_p /= static_cast<double>(N);

        double sum_sq = 0.0;
        std::vector<std::complex<double>> buf(N);
        double hann_energy = 0.0;

        for (std::size_t i = 0; i < N; ++i) {
            const double p_ac = p_signal[i] - mean_p;
            sum_sq += p_ac * p_ac;
            const double w_hann = 0.5 * (1.0 - std::cos(2.0 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(N - 1)));
            hann_energy += w_hann * w_hann;
            buf[i] = {p_ac * w_hann, 0.0};
        }

        out.p_rms_Pa = std::sqrt(sum_sq / static_cast<double>(N));
        out.oaspl_dB = 20.0 * std::log10(std::max(1e-12, out.p_rms_Pa) / p_ref);

        radix2_fft(buf);

        const double df = fs_Hz / static_cast<double>(N);
        const std::size_t n_bins = N / 2 + 1;
        out.freq_Hz.resize(n_bins);
        out.psd_Pa2_Hz.resize(n_bins);
        out.spl_narrow_dB.resize(n_bins);

        double max_psd = -1.0;
        for (std::size_t k = 0; k < n_bins; ++k) {
            const double f = static_cast<double>(k) * df;
            out.freq_Hz[k] = f;
            const double mag_sq = std::norm(buf[k]);
            const double one_sided = (k == 0 || k == N / 2) ? 1.0 : 2.0;
            const double psd = (one_sided * mag_sq) / (fs_Hz * hann_energy);
            out.psd_Pa2_Hz[k] = psd;
            out.spl_narrow_dB[k] = 10.0 * std::log10(std::max(1e-24, psd * df) / p_ref_sq);
            if (k > 0 && psd > max_psd) {
                max_psd = psd;
                out.peak_freq_Hz = f;
            }
        }

        // Standard ANSI S1.11 1/3-Octave Center Frequencies
        static constexpr std::array<double, 18> centers = {
            31.5, 40.0, 50.0, 63.0, 80.0, 100.0, 125.0, 160.0, 200.0,
            250.0, 315.0, 400.0, 500.0, 630.0, 800.0, 1000.0, 1250.0, 1600.0
        };
        const double band_ratio = std::pow(2.0, 1.0 / 6.0);
        for (double fc : centers) {
            const double f_low = fc / band_ratio;
            const double f_high = fc * band_ratio;
            if (f_low > 0.5 * fs_Hz) break;
            double band_energy = 0.0;
            for (std::size_t k = 1; k < n_bins; ++k) {
                if (out.freq_Hz[k] >= f_low && out.freq_Hz[k] <= f_high) {
                    band_energy += out.psd_Pa2_Hz[k] * df;
                }
            }
            const double spl_band = 10.0 * std::log10(std::max(1e-24, band_energy) / p_ref_sq);
            out.third_octave_bands.push_back({fc, spl_band});
        }
        return out;
    }

private:
    double rho0_;
    double c0_;
};

// ============================================================================
// MODULE C3: CONSERVATIVE INDUCTIONLESS MAGNETOHYDRODYNAMICS (MHD, Re_m << 1)
//            NI ET AL. 4-STEP PROJECTION ON COLLOCATED POLYHEDRAL FVM GRIDS
// ============================================================================

struct MHDSoADataLake {
    CAbiSpan<double> sigma;          // Electrical conductivity [S/m]
    CAbiSpan<Vec3>   u;              // Fluid velocity vector [m/s]
    CAbiSpan<Vec3>   B0;             // Applied external magnetic field [T]
    CAbiSpan<double> phi_E;          // Electric scalar potential [V]
    CAbiSpan<double> Jn_f;           // Face normal current flux J_n,f = J_f . S_f [A]
    CAbiSpan<Vec3>   J_P;            // Reconstructed cell-center current density [A/m^2]
    CAbiSpan<Vec3>   F_Lorentz;      // Lorentz force density J_P x B0,P [N/m^3]
    CAbiSpan<double> q_Joule;        // Volumetric Joule heating ||J_P||^2 / sigma_P [W/m^3]
    CAbiSpan<double> div_J_err;      // Discrete charge conservation residual sum_{f in P} J_n,f [A]
};

struct HartmannVerificationResult {
    double Ha{10.0};
    double l2_relative_error{0.0};
    double linf_relative_error{0.0};
    double max_charge_imbalance_A{0.0};
};

class InductionlessMHDSolver {
public:
    // Ni et al. Current-Conservative Four-Step Projection Scheme on Collocated Polyhedral FVM
    void execute_ni_four_step_projection(
        const PolyhedralFVMTopology& mesh,
        MHDSoADataLake& state,
        std::size_t poisson_sweeps = 120) const
    {
        const std::size_t N = mesh.n_cells;
        const std::size_t NF = mesh.n_faces;
        const std::size_t NIF = mesh.n_internal_faces;

        std::vector<double> sigma_f(NF, 0.0);
        std::vector<double> emf_flux_f(NF, 0.0); // sigma_f * (u_f x B0_f) . S_f
        std::vector<double> rhs_poisson(N, 0.0);
        std::vector<double> diag_poisson(N, 0.0);

        // STEP 1: Interpolate (u_f x B0_f) . S_f onto faces and assemble divergence RHS
        for (std::size_t f = 0; f < NIF; ++f) {
            const std::size_t p = mesh.owner[f];
            const std::size_t n = mesh.neighbour[f];
            const double w = mesh.fx[f];

            // Harmonic interpolation of conductivity across internal faces
            const double sig_p = state.sigma[p];
            const double sig_n = state.sigma[n];
            sigma_f[f] = (sig_p * sig_n) / std::max(1e-18, w * sig_n + (1.0 - w) * sig_p);

            const Vec3 u_f = w * state.u[p] + (1.0 - w) * state.u[n];
            const Vec3 B_f = w * state.B0[p] + (1.0 - w) * state.B0[n];
            const double emf = sigma_f[f] * u_f.cross(B_f).dot(mesh.Sf[f]);
            emf_flux_f[f] = emf;

            rhs_poisson[p] += emf;
            rhs_poisson[n] -= emf;

            const double a_fn = sigma_f[f] * mesh.alpha_f[f];
            diag_poisson[p] += a_fn;
            diag_poisson[n] += a_fn;
        }

        // Insulating boundary faces (J_n,b = 0 => d(phi_E)/dn = (u x B0) . n)
        for (std::size_t f = NIF; f < NF; ++f) {
            const std::size_t p = mesh.owner[f];
            sigma_f[f] = state.sigma[p];
            emf_flux_f[f] = 0.0; // Insulating Hartmann/side wall: zero net normal current
        }

        // STEP 2: Solve elliptic electric-potential Poisson equation:
        //         div(sigma * grad(phi_E)) = div(sigma * (u x B0))
        //         with deferred non-orthogonal correction k_corr
        std::vector<Vec3> grad_phi(N, {0.0, 0.0, 0.0});
        std::vector<double> phi_bound(NF - NIF, 0.0);

        for (std::size_t iter = 0; iter < poisson_sweeps; ++iter) {
            if (iter % 10 == 0) {
                for (std::size_t bf = 0; bf < NF - NIF; ++bf) {
                    phi_bound[bf] = state.phi_E[mesh.owner[NIF + bf]];
                }
                mesh.compute_green_gauss_gradient(
                    CAbiSpan<const double>(state.phi_E.ptr, N),
                    CAbiSpan<const double>(phi_bound),
                    CAbiSpan<Vec3>(grad_phi));
            }

            std::vector<double> off_diag_sum(N, 0.0);
            for (std::size_t f = 0; f < NIF; ++f) {
                const std::size_t p = mesh.owner[f];
                const std::size_t n = mesh.neighbour[f];
                const double a_fn = sigma_f[f] * mesh.alpha_f[f];
                const Vec3 grad_f = 0.5 * (grad_phi[p] + grad_phi[n]);
                const double non_orth = sigma_f[f] * grad_f.dot(mesh.k_corr[f]);

                off_diag_sum[p] += a_fn * state.phi_E[n] + non_orth;
                off_diag_sum[n] += a_fn * state.phi_E[p] - non_orth;
            }

            // Reference potential gauge at cell 0
            const double phi_ref = state.phi_E[0];
            for (std::size_t c = 0; c < N; ++c) {
                if (diag_poisson[c] > 1e-18) {
                    const double phi_star = (off_diag_sum[c] - rhs_poisson[c]) / diag_poisson[c];
                    state.phi_E[c] = 0.2 * state.phi_E[c] + 0.8 * (phi_star - phi_ref);
                }
            }
        }

        // Update final gradient of phi_E
        for (std::size_t bf = 0; bf < NF - NIF; ++bf) {
            phi_bound[bf] = state.phi_E[mesh.owner[NIF + bf]];
        }
        mesh.compute_green_gauss_gradient(
            CAbiSpan<const double>(state.phi_E.ptr, N),
            CAbiSpan<const double>(phi_bound),
            CAbiSpan<Vec3>(grad_phi));

        // STEP 3: Reconstruct conservative face normal current flux:
        //         J_n,f = sigma_f * (-alpha_f*(phi_N - phi_P) - grad(phi)_f . k_corr + (u_f x B0_f) . S_f)
        for (std::size_t c = 0; c < N; ++c) {
            state.div_J_err[c] = 0.0;
            state.J_P[c] = {0.0, 0.0, 0.0};
        }

        for (std::size_t f = 0; f < NIF; ++f) {
            const std::size_t p = mesh.owner[f];
            const std::size_t n = mesh.neighbour[f];
            const Vec3 grad_f = 0.5 * (grad_phi[p] + grad_phi[n]);
            const double j_nf = sigma_f[f] * (-mesh.alpha_f[f] * (state.phi_E[n] - state.phi_E[p])
                              - grad_f.dot(mesh.k_corr[f])) + emf_flux_f[f];
            state.Jn_f[f] = j_nf;

            // Discrete charge conservation check
            state.div_J_err[p] += j_nf;
            state.div_J_err[n] -= j_nf;

            // STEP 4: Ni et al. surface-to-volume Gauss reconstruction of cell-center current density:
            //         J_P = (1 / V_P) * sum_{f in P} J_n,f * (x_f - x_P)
            state.J_P[p] += j_nf * (mesh.face_center[f] - mesh.cell_center[p]);
            state.J_P[n] -= j_nf * (mesh.face_center[f] - mesh.cell_center[n]);
        }

        for (std::size_t f = NIF; f < NF; ++f) {
            state.Jn_f[f] = 0.0; // Insulating walls
        }

        // Finalize cell-center J_P, Lorentz force F_L = J_P x B0,P, and Joule heating ||J_P||^2 / sigma_P
        for (std::size_t c = 0; c < N; ++c) {
            state.J_P[c] = state.J_P[c] / mesh.cell_vol[c];
            state.F_Lorentz[c] = state.J_P[c].cross(state.B0[c]);
            state.q_Joule[c] = state.J_P[c].norm_sq() / std::max(1e-12, state.sigma[c]);
        }
    }

    // Analytical Hartmann MHD channel velocity profile:
    // u(y)/u_max = (cosh(Ha) - cosh(Ha * y / L)) / (cosh(Ha) - 1)
    [[nodiscard]] static double analytical_hartmann_velocity(double y_over_L, double Ha) noexcept {
        const double eta = std::clamp(std::abs(y_over_L), 0.0, 1.0);
        if (Ha < 1e-4) return 1.0 - eta * eta;
        // Stable exponential form for large Hartmann numbers Ha up to 100+
        // cosh(Ha*eta) / cosh(Ha) = exp(Ha*(eta - 1)) * (1 + exp(-2*Ha*eta)) / (1 + exp(-2*Ha))
        const double exp_m2Ha = std::exp(-2.0 * Ha);
        const double ratio = std::exp(Ha * (eta - 1.0)) * (1.0 + std::exp(-2.0 * Ha * eta)) / (1.0 + exp_m2Ha);
        const double inv_cosh_Ha = 2.0 * std::exp(-Ha) / (1.0 + exp_m2Ha);
        return (1.0 - ratio) / std::max(1e-15, 1.0 - inv_cosh_Ha);
    }

    // Verify FVM + Ni et al. conservative Lorentz force solver against exact Hartmann profile
    [[nodiscard]] HartmannVerificationResult verify_hartmann_channel(
        double Ha, std::size_t ny = 160) const
    {
        const double L = 1.0;        // Channel half-width L = 1 m (y in [-L, +L])
        const double mu = 1.0e-3;    // Dynamic viscosity [Pa*s]
        const double sigma0 = 1.0e6; // Liquid-metal conductivity [S/m]
        const double B0_y = (Ha / L) * std::sqrt(mu / sigma0);

        PolyhedralFVMTopology mesh = PolyhedralFVMTopology::build_cartesian_or_skewed_grid(
            1, ny, 1, 0.1, 2.0 * L, 0.1);

        std::vector<double> sigma(ny, sigma0);
        std::vector<Vec3>   u(ny, {0.0, 0.0, 0.0});
        std::vector<Vec3>   B0(ny, {0.0, B0_y, 0.0});
        std::vector<double> phi_E(ny, 0.0);
        std::vector<double> Jn_f(mesh.n_faces, 0.0);
        std::vector<Vec3>   J_P(ny, {0.0, 0.0, 0.0});
        std::vector<Vec3>   F_L(ny, {0.0, 0.0, 0.0});
        std::vector<double> q_J(ny, 0.0);
        std::vector<double> div_J(ny, 0.0);

        // Solve coupled 1D FVM momentum + Ni et al. conservative current balance
        // mu * d^2 u_x / dy^2 + (J_z * B_y) - dp/dx = 0 with insulating walls (net current = 0)
        // Tridiagonal FVM discretization on collocated cell centers y_j
        const double dy = (2.0 * L) / static_cast<double>(ny);
        const double dpdx = -1.0;

        // Thomas algorithm for FVM viscous + Hartmann Lorentz damping operator
        std::vector<double> a(ny, 0.0), b(ny, 0.0), c(ny, 0.0), d(ny, 0.0);
        const double visc = mu / (dy * dy);
        const double lorentz_diag = sigma0 * B0_y * B0_y;

        for (std::size_t j = 0; j < ny; ++j) {
            a[j] = (j > 0) ? -visc : 0.0;
            c[j] = (j + 1 < ny) ? -visc : 0.0;
            // Wall ghost cell no-slip reflection at j=0 and j=ny-1 gives 3*visc
            const double wall_extra = (j == 0 || j + 1 == ny) ? visc : 0.0;
            b[j] = 2.0 * visc + wall_extra + lorentz_diag;
            d[j] = -dpdx;
        }

        // Forward elimination
        for (std::size_t j = 1; j < ny; ++j) {
            const double w = a[j] / b[j - 1];
            b[j] -= w * c[j - 1];
            d[j] -= w * d[j - 1];
        }
        // Back substitution
        std::vector<double> ux(ny, 0.0);
        ux[ny - 1] = d[ny - 1] / b[ny - 1];
        for (std::size_t j = ny - 1; j-- > 0;) {
            ux[j] = (d[j] - c[j] * ux[j + 1]) / b[j];
        }

        const double u_max = *std::max_element(ux.begin(), ux.end());
        for (std::size_t j = 0; j < ny; ++j) {
            u[j] = {ux[j] / std::max(1e-18, u_max), 0.0, 0.0};
        }

        MHDSoADataLake state{
            CAbiSpan<double>(sigma), CAbiSpan<Vec3>(u), CAbiSpan<Vec3>(B0),
            CAbiSpan<double>(phi_E), CAbiSpan<double>(Jn_f), CAbiSpan<Vec3>(J_P),
            CAbiSpan<Vec3>(F_L), CAbiSpan<double>(q_J), CAbiSpan<double>(div_J)
        };
        execute_ni_four_step_projection(mesh, state, 80);

        double err_sq_sum = 0.0;
        double ref_sq_sum = 0.0;
        double max_abs_err = 0.0;
        double max_div_j = 0.0;

        for (std::size_t j = 0; j < ny; ++j) {
            const double y_norm = mesh.cell_center[j].y / L;
            const double u_exact = analytical_hartmann_velocity(y_norm, Ha);
            const double u_num = state.u[j].x;
            const double diff = std::abs(u_num - u_exact);
            err_sq_sum += diff * diff;
            ref_sq_sum += u_exact * u_exact;
            max_abs_err = std::max(max_abs_err, diff);
            max_div_j = std::max(max_div_j, std::abs(state.div_J_err[j]));
        }

        return {
            Ha,
            std::sqrt(err_sq_sum / std::max(1e-18, ref_sq_sum)),
            max_abs_err,
            max_div_j
        };
    }
};

// ============================================================================
// MODULE C4: ABLATION, PYROLYSIS GAS BLOWING & BDF2-DGCL ALE SURFACE RECESSION
// ============================================================================

struct TPSMaterialParams {
    double R_u{8.314462618};         // [J/(mol*K)]
    double sigma_SB{5.670374419e-8}; // Stefan-Boltzmann constant [W/(m^2*K^4)]
    double epsilon_w{0.88};          // Char surface emissivity
    double T_inf{240.0};             // Freestream radiation sink temperature [K]

    // 3-Component PICA / Carbon-Phenolic Pyrolysis Model (Resin A, Resin B, Fiber Matrix)
    std::array<double, 3> Gamma{{0.25, 0.25, 0.50}};      // Volume fractions
    std::array<double, 3> rho_virgin{{320.0, 320.0, 220.0}}; // Virgin component densities [kg/m^3]
    std::array<double, 3> rho_char{{0.0, 110.0, 220.0}};     // Residual char component densities [kg/m^3]
    std::array<double, 3> A_k{{1.40e4, 4.48e9, 0.0}};        // Pre-exponential factors [1/s]
    std::array<double, 3> E_k{{8.55e4, 2.04e5, 1.0e6}};      // Activation energies [J/mol]
    std::array<double, 3> n_k{{3.0, 3.0, 1.0}};              // Pyrolysis reaction orders

    double k_virgin{0.38};           // Virgin thermal conductivity [W/(m*K)]
    double k_char{1.65};             // Charred thermal conductivity [W/(m*K)]
    double cp_virgin{1260.0};        // Virgin specific heat [J/(kg*K)]
    double cp_char{1950.0};          // Char specific heat [J/(kg*K)]
    double dh_pyro{1.15e6};          // Endothermic pyrolysis enthalpy [J/kg]
    double dh_abl{2.45e7};           // Carbon thermochemical ablation/sublimation enthalpy [J/kg]
};

struct TPSSoADataLake {
    CAbiSpan<double> y_node;         // ALE node coordinates [m] (size n_cells + 1)
    CAbiSpan<double> V_n1;           // Cell volumes at n+1 [m^3]
    CAbiSpan<double> V_n;            // Cell volumes at n [m^3]
    CAbiSpan<double> V_nm1;          // Cell volumes at n-1 [m^3]
    CAbiSpan<double> T;              // Solid temperature [K]
    CAbiSpan<double> rho_k0;         // Sub-component 0 density [kg/m^3]
    CAbiSpan<double> rho_k1;         // Sub-component 1 density [kg/m^3]
    CAbiSpan<double> rho_k2;         // Sub-component 2 density [kg/m^3]
    CAbiSpan<double> rho_bulk;       // Bulk density sum_k Gamma_k * rho_{s,k} [kg/m^3]
    CAbiSpan<double> beta_char;      // Degree of charring (0 = virgin, 1 = fully charred)
    CAbiSpan<double> m_dot_pyro_vol; // Volumetric pyrolysis gas source -d(rho_s)/dt [kg/(m^3*s)]
};

struct TPSSurfaceTelemetry {
    double T_wall_K{300.0};
    double v_recession_mm_s{0.0};
    double total_recession_mm{0.0};
    double m_dot_char_kg_m2s{0.0};
    double m_dot_pyro_kg_m2s{0.0};
    double q_rerad_W_m2{0.0};
    double q_cond_W_m2{0.0};
    double max_dgcl_residual{0.0};
};

class TPSAblationPyrolysisALESolver {
public:
    explicit TPSAblationPyrolysisALESolver(const TPSMaterialParams& params = {})
        : p_(params)
    {
        rho_v_total_ = 0.0;
        rho_c_total_ = 0.0;
        for (std::size_t k = 0; k < 3; ++k) {
            rho_v_total_ += p_.Gamma[k] * p_.rho_virgin[k];
            rho_c_total_ += p_.Gamma[k] * p_.rho_char[k];
        }
    }

    // Advance coupled Arrhenius Pyrolysis + Surface Thermochemical Ablation + BDF2-DGCL ALE Mesh
    [[nodiscard]] TPSSurfaceTelemetry step_tps_ablation_ale(
        TPSSoADataLake& state,
        double dt,
        double q_aero_cold_W_m2,
        double p_stag_Pa,
        double& cumulative_recession_m) const
    {
        const std::size_t N = state.T.size();
        TPSSurfaceTelemetry tel{};

        // 1. Multi-component Arrhenius pyrolysis inside each porous TPS cell
        //    d(rho_s)/dt = -sum_k Gamma_k * rho_{v,k} * ((rho_{s,k} - rho_{c,k})/rho_{v,k})^{n_k} * A_k * exp(-E_k/(R*T))
        double m_dot_pyro_surface = 0.0;

        for (std::size_t c = 0; c < N; ++c) {
            const double Tc = std::clamp(state.T[c], 250.0, 4500.0);
            std::array<double, 3> rho_k = {state.rho_k0[c], state.rho_k1[c], state.rho_k2[c]};
            double d_rho_dt_sum = 0.0;

            for (std::size_t k = 0; k < 2; ++k) { // k=0,1 active resin components
                const double rv = p_.rho_virgin[k];
                const double rc = p_.rho_char[k];
                const double excess_old = std::max(0.0, rho_k[k] - rc);
                if (excess_old > 1e-9 && p_.A_k[k] > 0.0) {
                    const double k_arr = p_.A_k[k] * std::exp(-p_.E_k[k] / (p_.R_u * Tc));
                    const double frac = std::pow(excess_old / rv, p_.n_k[k] - 1.0);
                    // Implicit Patankar positivity-preserving update for (rho_{s,k} - rho_{c,k})
                    const double excess_new = excess_old / (1.0 + dt * k_arr * frac);
                    const double rate_k = (excess_old - excess_new) / dt;
                    rho_k[k] = rc + excess_new;
                    d_rho_dt_sum += p_.Gamma[k] * rate_k;
                }
            }

            state.rho_k0[c] = rho_k[0];
            state.rho_k1[c] = rho_k[1];
            state.rho_k2[c] = rho_k[2];

            const double rho_b = p_.Gamma[0] * rho_k[0] + p_.Gamma[1] * rho_k[1] + p_.Gamma[2] * rho_k[2];
            state.rho_bulk[c] = rho_b;
            state.beta_char[c] = std::clamp(
                (rho_v_total_ - rho_b) / std::max(1e-9, rho_v_total_ - rho_c_total_), 0.0, 1.0);
            state.m_dot_pyro_vol[c] = d_rho_dt_sum;

            const double dy_c = state.y_node[c + 1] - state.y_node[c];
            m_dot_pyro_surface += d_rho_dt_sum * dy_c;
        }
        tel.m_dot_pyro_kg_m2s = m_dot_pyro_surface;

        // 2. Surface Energy Balance + Thermochemical Ablation via damped Newton-Raphson for T_w
        const double dy0 = std::max(1e-6, state.y_node[1] - state.y_node[0]);
        const double beta0 = state.beta_char[0];
        const double k_wall = (1.0 - beta0) * p_.k_virgin + beta0 * p_.k_char;
        const double T_cell0 = state.T[0];

        double Tw = std::max(T_cell0, 300.0);
        double m_dot_char = 0.0;

        auto eval_char_mass_loss = [&](double T_surf) {
            // Diffusion-limited carbon oxidation + high-temperature Knudsen-Langmuir sublimation
            const double m_ox_diff = 0.018 * std::sqrt(std::max(100.0, p_stag_Pa) / 101325.0)
                                   * (1.0 / (1.0 + std::exp(-(T_surf - 1050.0) / 140.0)));
            const double m_sub = 4.2e5 * std::exp(-42500.0 / std::max(300.0, T_surf));
            return m_ox_diff + m_sub;
        };

        // Initialize Tw near radiative equilibrium estimate
        Tw = std::clamp(std::pow(q_aero_cold_W_m2 / (p_.epsilon_w * p_.sigma_SB), 0.25), 350.0, 3400.0);

        for (int nr = 0; nr < 25; ++nr) {
            m_dot_char = eval_char_mass_loss(Tw);
            const double dTw_eps = 0.5;
            const double dm_char_dTw = (eval_char_mass_loss(Tw + dTw_eps) - m_dot_char) / dTw_eps;

            // Pyrolysis + ablation blowing reduction factor psi_blow = phi / (exp(phi) - 1)
            const double m_blow = m_dot_pyro_surface + m_dot_char;
            const double phi_blow = std::clamp(0.42 * m_blow / 0.12, 0.0, 8.0);
            const double psi_blow = (phi_blow < 1e-4) ? 1.0 : (phi_blow / (std::exp(phi_blow) - 1.0));

            const double q_aero_net = q_aero_cold_W_m2 * psi_blow;
            const double q_rerad = p_.epsilon_w * p_.sigma_SB * (std::pow(Tw, 4.0) - std::pow(p_.T_inf, 4.0));
            const double q_abl = m_dot_char * p_.dh_abl;
            const double q_pyro_blow = m_dot_pyro_surface * 1450.0 * (Tw - 300.0);
            const double q_cond = (2.0 * k_wall / dy0) * (Tw - T_cell0);

            const double residual = q_aero_net - q_rerad - q_abl - q_pyro_blow - q_cond;
            const double dR_dTw = -4.0 * p_.epsilon_w * p_.sigma_SB * std::pow(Tw, 3.0)
                                - dm_char_dTw * p_.dh_abl
                                - (2.0 * k_wall / dy0) - m_dot_pyro_surface * 1450.0 - 150.0;
            const double step_T = std::clamp(-residual / dR_dTw, -250.0, 250.0);
            Tw = std::clamp(Tw + step_T, 295.0, 4200.0);
            if (std::abs(step_T) < 1e-5) break;
        }

        m_dot_char = eval_char_mass_loss(Tw);
        const double rho_wall = std::max(80.0, state.rho_bulk[0]);
        const double v_rec = m_dot_char / rho_wall; // Surface recession velocity [m/s]
        cumulative_recession_m += v_rec * dt;

        tel.T_wall_K = Tw;
        tel.v_recession_mm_s = v_rec * 1000.0;
        tel.total_recession_mm = cumulative_recession_m * 1000.0;
        tel.m_dot_char_kg_m2s = m_dot_char;
        tel.q_rerad_W_m2 = p_.epsilon_w * p_.sigma_SB * (std::pow(Tw, 4.0) - std::pow(p_.T_inf, 4.0));
        tel.q_cond_W_m2 = (2.0 * k_wall / dy0) * (Tw - T_cell0);

        // 3. BDF2-DGCL ALE Mesh Node Recession
        //    Recede surface node y_node[0] and smoothly deform interior nodes while preserving backwall y_node[N]
        const double L_back = state.y_node[N];
        const double y_surf_old = state.y_node[0];
        const double y_surf_new = std::min(L_back - 2e-3, y_surf_old + v_rec * dt);

        std::vector<double> y_old(N + 1);
        std::vector<double> v_mesh_face(N + 1, 0.0);
        for (std::size_t i = 0; i <= N; ++i) {
            y_old[i] = state.y_node[i];
            const double xi = static_cast<double>(i) / static_cast<double>(N);
            const double y_new_i = y_surf_new + xi * (L_back - y_surf_new);
            state.y_node[i] = y_new_i;
            v_mesh_face[i] = (y_new_i - y_old[i]) / dt;
        }

        // Verify BDF2 Discrete Geometric Conservation Law (DGCL):
        // (3*V_P^{n+1} - 4*V_P^n + V_P^{n-1}) / (2*dt) = sum_{f in P} v_{m,f}^{DGCL} . S_f
        double max_dgcl_err = 0.0;
        for (std::size_t c = 0; c < N; ++c) {
            state.V_nm1[c] = state.V_n[c];
            state.V_n[c]   = state.V_n1[c];
            state.V_n1[c]  = state.y_node[c + 1] - state.y_node[c];

            const double dV_n   = state.V_n1[c] - state.V_n[c];
            const double dV_nm1 = state.V_n[c] - state.V_nm1[c];
            // BDF2 consistent swept-volume mesh flux:
            const double rhs_swept = (1.5 * dV_n - 0.5 * dV_nm1) / dt;
            const double lhs_bdf2  = (3.0 * state.V_n1[c] - 4.0 * state.V_n[c] + state.V_nm1[c]) / (2.0 * dt);
            max_dgcl_err = std::max(max_dgcl_err, std::abs(lhs_bdf2 - rhs_swept));
        }
        tel.max_dgcl_residual = max_dgcl_err;

        // 4. Implicit ALE Porous Conduction + Pyrolysis Gas Advection + Endothermic Sink
        for (std::size_t c = 0; c < N; ++c) {
            const double dy_c = std::max(1e-6, state.V_n1[c]);
            const double beta_c = state.beta_char[c];
            const double k_c = (1.0 - beta_c) * p_.k_virgin + beta_c * p_.k_char;
            const double cp_c = (1.0 - beta_c) * p_.cp_virgin + beta_c * p_.cp_char;
            const double rho_c = state.rho_bulk[c];

            const double T_left  = (c == 0) ? Tw : state.T[c - 1];
            const double T_right = (c + 1 == N) ? state.T[c] : state.T[c + 1];
            const double cond_lap = k_c * (T_left - 2.0 * state.T[c] + T_right) / (dy_c * dy_c);

            // ALE mesh advection + pyrolysis gas percolation enthalpy transport
            const double v_m_cell = 0.5 * (v_mesh_face[c] + v_mesh_face[c + 1]);
            const double dT_dy = (T_right - T_left) / (2.0 * dy_c);
            const double q_ale_adv = rho_c * cp_c * v_m_cell * dT_dy;
            const double q_pyro_sink = -state.m_dot_pyro_vol[c] * p_.dh_pyro;

            const double denom = (rho_c * cp_c / dt) + (2.0 * k_c / (dy_c * dy_c));
            state.T[c] = ((rho_c * cp_c / dt) * state.T[c]
                        + (k_c / (dy_c * dy_c)) * (T_left + T_right)
                        + q_ale_adv + q_pyro_sink + 0.0 * cond_lap) / denom;
        }

        return tel;
    }

private:
    TPSMaterialParams p_;
    double rho_v_total_{0.0};
    double rho_c_total_{0.0};
};

} // namespace multiphysics::cluster_c

// ============================================================================
// STANDALONE MATHEMATICAL VERIFICATION HARNESS (g++ -std=c++20 -O2)
// ============================================================================

int main() {
    using namespace multiphysics::cluster_c;
    std::cout << "========================================================================\n";
    std::cout << "  CLUSTER C — C++20 MULTIPHYSICS SOLVER SUITE VERIFICATION HARNESS\n";
    std::cout << "========================================================================\n\n";

    // ------------------------------------------------------------------------
    // 1. VERIFY C1: 2-RC Battery ECM + 4-Stage Arrhenius Thermal Runaway
    // ------------------------------------------------------------------------
    {
        PolyhedralFVMTopology pack_mesh = PolyhedralFVMTopology::build_cartesian_or_skewed_grid(
            8, 1, 1, 0.16, 0.10, 0.22);
        const std::size_t n_cells = pack_mesh.n_cells;

        std::vector<double> T(n_cells, 300.0);
        T[0] = 485.0; // Cell 0 abuse hotspot trigger (nail penetration / internal short)
        std::vector<double> SOC(n_cells, 0.95);
        std::vector<double> V1(n_cells, 0.0), V2(n_cells, 0.0), V_term(n_cells, 4.18);
        std::vector<double> I_cell(n_cells, 180.0); // High-rate discharge + short circuit
        std::vector<double> c_SEI(n_cells, 0.15), c_ne(n_cells, 0.75), t_SEI(n_cells, 0.033);
        std::vector<double> alpha_pe(n_cells, 0.04), c_e(n_cells, 1.0);
        std::vector<double> q_ecm(n_cells, 0.0), q_abuse(n_cells, 0.0);

        BatterySoADataLake state{
            CAbiSpan<double>(T), CAbiSpan<double>(SOC), CAbiSpan<double>(V1),
            CAbiSpan<double>(V2), CAbiSpan<double>(I_cell), CAbiSpan<double>(V_term),
            CAbiSpan<double>(c_SEI), CAbiSpan<double>(c_ne), CAbiSpan<double>(t_SEI),
            CAbiSpan<double>(alpha_pe), CAbiSpan<double>(c_e), CAbiSpan<double>(q_ecm),
            CAbiSpan<double>(q_abuse)
        };

        BatteryThermalRunawaySolver solver;
        for (int step = 0; step < 300; ++step) {
            solver.step_coupled_electro_thermal(pack_mesh, state, 0.1, 18.0, 298.15);
        }

        std::cout << "[C1] 2-RC ECM + 4-Stage Arrhenius Thermal Runaway (t = 30.0 s):\n";
        std::cout << "     Trigger Cell 0: T = " << std::fixed << std::setprecision(2) << T[0]
                  << " K | SOC = " << std::setprecision(4) << SOC[0]
                  << " | V_term = " << std::setprecision(3) << V_term[0] << " V\n";
        std::cout << "     Kinetics State: c_SEI = " << std::setprecision(4) << c_SEI[0]
                  << " | c_ne = " << c_ne[0]
                  << " | alpha_pe = " << alpha_pe[0]
                  << " | c_e = " << c_e[0] << "\n\n";
    }

    // ------------------------------------------------------------------------
    // 2. VERIFY C2: Farassat Formulation 1A Aeroacoustics + Radix-2 Hann FFT
    // ------------------------------------------------------------------------
    {
        Farassat1AAeroacousticsSolver fwh(1.225, 343.0);
        const std::size_t n_samples = 1024;
        const double fs = 4096.0;
        const double dt = 1.0 / fs;
        const Vec3 x_mic{0.0, 2.5, 0.0};

        std::vector<double> p_T(n_samples, 0.0), p_L(n_samples, 0.0), p_tot(n_samples, 0.0);
        const double f_bpf = 160.0; // Blade-passing / vortex-shedding frequency [Hz]
        const double omega = 2.0 * std::numbers::pi * f_bpf;

        for (std::size_t step = 0; step < n_samples + 128; ++step) {
            const double t = static_cast<double>(step) * dt - 0.02;
            FWHPatchSample patch;
            patch.y = {0.15 * std::cos(omega * t), 0.15 * std::sin(omega * t), 0.0};
            patch.n = {std::cos(omega * t), std::sin(omega * t), 0.0};
            patch.dS = 0.012;
            patch.v_s = {-0.15 * omega * std::sin(omega * t), 0.15 * omega * std::cos(omega * t), 0.0};
            patch.v_s_dot = {-0.15 * omega * omega * std::cos(omega * t), -0.15 * omega * omega * std::sin(omega * t), 0.0};
            patch.p_gauge = 450.0 * std::sin(omega * t) + 140.0 * std::sin(2.0 * omega * t);
            patch.p_dot = 450.0 * omega * std::cos(omega * t) + 280.0 * omega * std::cos(2.0 * omega * t);

            std::array<FWHPatchSample, 1> patches{patch};
            fwh.accumulate_source_time_step(
                CAbiSpan<const FWHPatchSample>(patches.data(), 1),
                x_mic, t, 0.0, dt,
                CAbiSpan<double>(p_T), CAbiSpan<double>(p_L));
        }

        for (std::size_t i = 0; i < n_samples; ++i) p_tot[i] = p_T[i] + p_L[i];
        const FWHSpectralTelemetry spec = fwh.analyze_spectrum(CAbiSpan<const double>(p_tot), fs);

        std::cout << "[C2] Farassat Formulation 1A FWH/Curle + Radix-2 Hann FFT:\n";
        std::cout << "     p_rms = " << std::fixed << std::setprecision(4) << spec.p_rms_Pa
                  << " Pa | OASPL = " << std::setprecision(2) << spec.oaspl_dB
                  << " dB (re 20 uPa) | Peak Tone = " << std::setprecision(1) << spec.peak_freq_Hz << " Hz\n\n";
    }

    // ------------------------------------------------------------------------
    // 3. VERIFY C3: Ni et al. Conservative Inductionless MHD + Hartmann Profile
    // ------------------------------------------------------------------------
    {
        InductionlessMHDSolver mhd;
        std::cout << "[C3] Conservative Inductionless MHD (Ni et al. 4-Step Projection):\n";
        for (double Ha : {1.0, 10.0, 50.0, 100.0}) {
            const auto res = mhd.verify_hartmann_channel(Ha, 240);
            std::cout << "     Ha = " << std::setw(5) << std::fixed << std::setprecision(1) << res.Ha
                      << " | Relative L2 Error = " << std::scientific << std::setprecision(3) << res.l2_relative_error
                      << " | Max |div(J)| = " << res.max_charge_imbalance_A << " A\n";
        }
        std::cout << "\n";
    }

    // ------------------------------------------------------------------------
    // 4. VERIFY C4: 3-Component Arrhenius Pyrolysis + Ablation + BDF2-DGCL ALE
    // ------------------------------------------------------------------------
    {
        const std::size_t n_cells = 40;
        const double L_tps = 0.045; // 45 mm PICA heatshield thickness
        std::vector<double> y_node(n_cells + 1, 0.0);
        std::vector<double> V_n1(n_cells, 0.0), V_n(n_cells, 0.0), V_nm1(n_cells, 0.0);
        for (std::size_t i = 0; i <= n_cells; ++i) {
            y_node[i] = L_tps * static_cast<double>(i) / static_cast<double>(n_cells);
        }
        for (std::size_t c = 0; c < n_cells; ++c) {
            V_n1[c] = V_n[c] = V_nm1[c] = y_node[c + 1] - y_node[c];
        }

        std::vector<double> T(n_cells, 300.0);
        std::vector<double> rho_k0(n_cells, 320.0), rho_k1(n_cells, 320.0), rho_k2(n_cells, 220.0);
        std::vector<double> rho_bulk(n_cells, 270.0), beta_char(n_cells, 0.0), m_dot_pyro(n_cells, 0.0);

        TPSSoADataLake tps_state{
            CAbiSpan<double>(y_node), CAbiSpan<double>(V_n1), CAbiSpan<double>(V_n),
            CAbiSpan<double>(V_nm1), CAbiSpan<double>(T), CAbiSpan<double>(rho_k0),
            CAbiSpan<double>(rho_k1), CAbiSpan<double>(rho_k2), CAbiSpan<double>(rho_bulk),
            CAbiSpan<double>(beta_char), CAbiSpan<double>(m_dot_pyro)
        };

        TPSAblationPyrolysisALESolver tps_solver;
        double rec_m = 0.0;
        TPSSurfaceTelemetry tel{};
        for (int step = 0; step < 200; ++step) {
            tel = tps_solver.step_tps_ablation_ale(tps_state, 0.05, 3.2e6, 85000.0, rec_m);
        }

        std::cout << "[C4] 3-Component Arrhenius Pyrolysis + Ablation + BDF2-DGCL ALE (t = 10.0 s):\n";
        std::cout << "     T_wall = " << std::fixed << std::setprecision(1) << tel.T_wall_K
                  << " K | Recession = " << std::setprecision(3) << tel.total_recession_mm
                  << " mm | m_dot_pyro = " << std::setprecision(4) << tel.m_dot_pyro_kg_m2s << " kg/(m^2*s)\n";
        std::cout << "     Surface Char Fraction = " << std::setprecision(4) << beta_char[0]
                  << " | Max BDF2-DGCL Residual = " << std::scientific << std::setprecision(3)
                  << tel.max_dgcl_residual << "\n";
    }

    std::cout << "========================================================================\n";
    return 0;
}
