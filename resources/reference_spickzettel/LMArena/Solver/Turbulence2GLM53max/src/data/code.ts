// Raw C++20 / CUDA sources, inlined at build time so the workbench can display
// and export exactly the code that was compiled and verified.
import core from "../cpp/cluster_a_core.hpp?raw";
import turb from "../cpp/cluster_a_turbulence.hpp?raw";
import wall from "../cpp/cluster_a_wall.hpp?raw";
import cht from "../cpp/cluster_a_cht.hpp?raw";
import porous from "../cpp/cluster_a_porous.hpp?raw";
import radiation from "../cpp/cluster_a_radiation.hpp?raw";
import cuda from "../cpp/cluster_a_cuda.cu?raw";
import verify from "../cpp/cluster_a_verify.cpp?raw";

export type CodeFile = {
  name: string;
  lang: "cpp" | "cu";
  lines: number;
  bytes: number;
  blurb: string;
  src: string;
};

const mk = (name: string, lang: "cpp" | "cu", src: string, blurb: string): CodeFile => ({
  name,
  lang,
  src,
  blurb,
  lines: src.split("\n").length,
  bytes: new TextEncoder().encode(src).length,
});

export const CODE_FILES: CodeFile[] = [
  mk("cluster_a_core.hpp", "cpp", core,
    "FaceBaseSoA · CellFaceCSR (atomic-free gather) · compact CorrectionSoA · exact polyhedral geometry · CSR LinSys with BiCGStab / Gauss-Seidel"),
  mk("cluster_a_turbulence.hpp", "cpp", turb,
    "Positivity-preserving k-ω SST (Menter 2003): F1/F2, cross-diffusion, Bradshaw limiter, Patankar source split · Spalart-Allmaras · LES WALE"),
  mk("cluster_a_wall.hpp", "cpp", wall,
    "Continuous all-y+ wall treatment: y_P = (x_P−x_f)·S_f/‖S_f‖, ω = √(ω_vis²+ω_log²), Kader T⁺, implicit τ_w, Spalding reference law"),
  mk("cluster_a_cht.hpp", "cpp", cht,
    "mappedWall twin faces · supermesh-AMI weights · harmonic-resistance interface flux · monolithic cross-region CSR coupling · Robin-Robin"),
  mk("cluster_a_porous.hpp", "cpp", porous,
    "Anisotropic Darcy-Forchheimer: D = R diag(d₁,d₂,d₃) Rᵀ, implicit diagonally-dominant sink, Ergun coefficients"),
  mk("cluster_a_radiation.hpp", "cpp", radiation,
    "P1 (Marshak BC) + fvDOM with specular symmetry reflection · Gauss-Legendre ordinate sets · exact 1-D slab solution via E₂/E₃"),
  mk("cluster_a_cuda.cu", "cu", cuda,
    "CUDA back-end: 6 kernels (SST blend, Patankar, all-y+ wall, CHT couple, porous sink, P1 Jacobi). Builds with nvcc or g++ -DCLA_HOST_MIRROR"),
  mk("cluster_a_verify.cpp", "cpp", verify,
    "Verification driver: 69 checks across geometry, closures, wall treatment, CHT, porous media and radiation; emits JSON"),
];

export const BUILD_COMMANDS = [
  "g++ -std=c++20 -O2 -Wall -Wextra -I. -o cla_verify cluster_a_verify.cpp",
  "./cla_verify > cluster_a_results.json",
  "nvcc -std=c++20 -O3 -arch=sm_80 -I. -o cla_cuda cluster_a_cuda.cu",
  "g++ -std=c++20 -O2 -DCLA_HOST_MIRROR -I. -x c++ -o cla_cuda_host cluster_a_cuda.cu",
];
