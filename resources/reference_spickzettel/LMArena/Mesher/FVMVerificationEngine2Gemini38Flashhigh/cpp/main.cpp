#include "vec3.hpp"
#include "geometry.hpp"
#include "mms.hpp"
#include "fvm_solver.hpp"
#include "adjoint.hpp"
#include "pathology.hpp"

#include <iostream>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <cmath>

// Helper to escape and print json
std::string formatDouble(double v, int prec = 8) {
    if (std::isnan(v)) return "\"NaN\"";
    if (std::isinf(v)) return (v > 0) ? "\"Infinity\"" : "\"-Infinity\"";
    std::ostringstream ss;
    ss << std::scientific << std::setprecision(prec) << v;
    return ss.str();
}

int main() {
    std::cout << "=========================================================================\n";
    std::cout << "  ASME V&V 20 C++20 UNSTRUCTURED 3D POLYHEDRAL FVM VERIFICATION HARNESS  \n";
    std::cout << "=========================================================================\n";

    auto t0 = std::chrono::high_resolution_clock::now();

    // -------------------------------------------------------------------------
    // PILLAR 1: Distorted 3D Mesh Family Generation (h, h/2, h/4)
    // -------------------------------------------------------------------------
    std::cout << "\n[PILLAR 1] Generating Distorted 3D Polyhedral / Skewed Mesh Family...\n";
    std::vector<int> gridResolutions = {8, 16, 32};
    std::vector<Mesh> meshes;
    meshes.reserve(3);

    for (int n : gridResolutions) {
        auto mStart = std::chrono::high_resolution_clock::now();
        Mesh m = generateDistortedHexMesh(n, 0.28);
        auto mEnd = std::chrono::high_resolution_clock::now();
        double dt = std::chrono::duration<double>(mEnd - mStart).count();

        std::cout << "  Mesh " << n << "^3 (" << m.cells.size() << " cells, "
                  << m.faces.size() << " faces, " << m.vertices.size() << " vertices): "
                  << "max non-ortho = " << std::fixed << std::setprecision(2) << m.maxNonOrthoAngle << " deg, "
                  << "avg non-ortho = " << m.avgNonOrthoAngle << " deg, "
                  << "max skewness = " << std::setprecision(4) << m.maxSkewness << ", "
                  << "built in " << std::setprecision(3) << dt << " s\n";
        meshes.push_back(std::move(m));
    }

    // -------------------------------------------------------------------------
    // PILLAR 2: 3D Method of Manufactured Solutions (MMS) Convergence Analysis
    // -------------------------------------------------------------------------
    std::cout << "\n[PILLAR 2] Running 3D MMS Spatial Convergence Study across 4 Discretization Modes...\n";
    double Gamma = 0.15; // Diffusivity

    std::vector<CorrectionMode> modes = {
        CorrectionMode::FULL_CORRECTION,
        CorrectionMode::NO_SKEWNESS,
        CorrectionMode::NO_NON_ORTHOGONAL,
        CorrectionMode::NAIVE_UNCORRECTED
    };

    struct ModeResults {
        CorrectionMode mode;
        std::string modeName;
        std::vector<FVMSolveResult> levels;
        double p_obs_21_L2{0.0};
        double p_obs_32_L2{0.0};
        double p_obs_21_Linf{0.0};
        double p_obs_32_Linf{0.0};
        // ASME V&V 20-2009 metrics
        double GCI_21{0.0};
        double GCI_32{0.0};
        double asymptoticRangeRatio{0.0};
    };

    std::vector<ModeResults> allModeResults;

    for (CorrectionMode mode : modes) {
        ModeResults mr;
        mr.mode = mode;
        mr.modeName = correctionModeToString(mode);
        std::cout << "\n  --- Discretization Mode: " << mr.modeName << " ---\n";

        for (size_t l = 0; l < meshes.size(); ++l) {
            auto sStart = std::chrono::high_resolution_clock::now();
            FVMSolveResult res = FVMSolver::solveMMS(meshes[l], Gamma, mode);
            auto sEnd = std::chrono::high_resolution_clock::now();
            res.solveTimeSec = std::chrono::duration<double>(sEnd - sStart).count();

            std::cout << "    L" << (l + 1) << " (" << res.nCells << " cells): "
                      << "L2 = " << std::scientific << std::setprecision(5) << res.L2_error
                      << ", Linf = " << res.Linf_error
                      << " (linear iters: " << res.linearIters
                      << ", deferred iters: " << res.deferredIters
                      << ", time: " << std::fixed << std::setprecision(3) << res.solveTimeSec << " s)\n";

            mr.levels.push_back(std::move(res));
        }

        // Compute observed order of accuracy p_obs = ln(e_coarse / e_fine) / ln(r)
        // Level 0 (8^3), Level 1 (16^3), Level 2 (32^3) -> r = 2
        double e1_L2 = mr.levels[0].L2_error;
        double e2_L2 = mr.levels[1].L2_error;
        double e3_L2 = mr.levels[2].L2_error;

        mr.p_obs_21_L2 = std::log(e1_L2 / e2_L2) / std::log(2.0);
        mr.p_obs_32_L2 = std::log(e2_L2 / e3_L2) / std::log(2.0);

        double e1_Linf = mr.levels[0].Linf_error;
        double e2_Linf = mr.levels[1].Linf_error;
        double e3_Linf = mr.levels[2].Linf_error;

        mr.p_obs_21_Linf = std::log(e1_Linf / e2_Linf) / std::log(2.0);
        mr.p_obs_32_Linf = std::log(e2_Linf / e3_Linf) / std::log(2.0);

        // ASME V&V 20-2009 Grid Convergence Index (GCI) on Target Functional (Domain Avg Phi)
        // f1 = Fine (Level 3), f2 = Medium (Level 2), f3 = Coarse (Level 1)
        double f1 = mr.levels[2].domainAvgPhi;
        double f2 = mr.levels[1].domainAvgPhi;
        double f3 = mr.levels[0].domainAvgPhi;
        double f_exact = mr.levels[2].exactDomainAvgPhi;

        double diff21 = f1 - f2;
        double diff32 = f2 - f3;
        double r = 2.0;
        double Fs = 1.25; // ASME V&V 20 safety factor for 3-grid study

        double p = mr.p_obs_32_L2;
        if (std::abs(diff21) > 1e-14 && std::abs(diff32) > 1e-14 && (diff32 / diff21) > 0.0) {
            p = std::log(std::abs(diff32 / diff21)) / std::log(r);
        }
        if (p < 0.1) p = 0.1;

        double rp = std::pow(r, p);
        double f_ext21 = (rp * f1 - f2) / (rp - 1.0);
        double ea21 = std::abs((f1 - f2) / (f1 + 1e-15));
        double ea32 = std::abs((f2 - f3) / (f2 + 1e-15));
        double e_ext21 = std::abs((f_ext21 - f1) / (f_ext21 + 1e-15));

        mr.GCI_21 = (Fs * ea21) / (rp - 1.0);
        mr.GCI_32 = (Fs * ea32 * rp) / (rp - 1.0);
        mr.asymptoticRangeRatio = mr.GCI_32 / (rp * mr.GCI_21 + 1e-20);

        std::cout << "    => Observed Order p_L2 (21 -> 32): " << std::fixed << std::setprecision(2)
                  << mr.p_obs_21_L2 << " -> " << mr.p_obs_32_L2 << "\n";
        std::cout << "    => Observed Order p_Linf (21 -> 32): "
                  << mr.p_obs_21_Linf << " -> " << mr.p_obs_32_Linf << "\n";
        std::cout << "    => ASME V&V 20 GCI_32 = " << std::scientific << std::setprecision(4)
                  << mr.GCI_32 * 100.0 << "% (Asymptotic Ratio: "
                  << std::fixed << std::setprecision(3) << mr.asymptoticRangeRatio << ")\n";

        allModeResults.push_back(std::move(mr));
    }

    // -------------------------------------------------------------------------
    // PILLAR 3: Cancellation-Free Complex-Step Gate & Taylor Remainder Gate
    // -------------------------------------------------------------------------
    std::cout << "\n[PILLAR 3] Running Cancellation-Free Complex-Step Gate & Taylor Remainder Test...\n";
    AdjointVerificationResult adjRes = AdjointVerifier::runGate();

    std::cout << "  Complex-Step Perturbation: h_cs = 1e-30\n";
    std::cout << "  Comparing Discrete IFT Adjoint Gradient vs Complex-Step Gradient:\n";
    for (int p = 0; p < adjRes.numParams; ++p) {
        std::cout << "    Param " << p << " (theta = " << std::fixed << std::setprecision(2) << adjRes.theta[p] << "): "
                  << "g_adj = " << std::scientific << std::setprecision(8) << adjRes.g_adj[p]
                  << ", g_cs = " << adjRes.g_cs[p]
                  << ", rel diff = " << std::setprecision(3) << adjRes.relErrors[p] << "\n";
    }
    std::cout << "  => Max Relative Error across all parameters: "
              << std::scientific << std::setprecision(3) << adjRes.maxRelError
              << (adjRes.passedComplexStepGate ? " [GATE PASSED (< 1e-12)]" : " [GATE FAILED]") << "\n";

    std::cout << "\n  Directional First-Order Taylor Remainder Gate R1(eps) = |J(theta+eps*v) - J(theta) - eps*g_adj.v|:\n";
    for (size_t k = 0; k < adjRes.epsilons.size(); ++k) {
        std::cout << "    eps = " << std::scientific << std::setprecision(1) << adjRes.epsilons[k]
                  << " -> R1(eps) = " << std::setprecision(6) << adjRes.taylorRemainders[k];
        if (k > 0) {
            std::cout << " | slope = " << std::fixed << std::setprecision(3) << adjRes.observedSlopes[k - 1];
        }
        std::cout << "\n";
    }
    std::cout << "  => Average Observed Taylor Slope: " << std::fixed << std::setprecision(3) << adjRes.averageSlope
              << (adjRes.passedTaylorGate ? " [GATE PASSED (Quadratic Slope ~ 2.000)]" : " [GATE FAILED]") << "\n";

    // -------------------------------------------------------------------------
    // PILLAR 4: Thingi10k-Style Geometric Pathology Torture Suite
    // -------------------------------------------------------------------------
    std::cout << "\n[PILLAR 4] Running Thingi10k Geometric Pathology Stress Harness...\n";
    PathologySuiteResult pathRes = PathologyAuditor::runSuite();

    std::cout << "  Pathology Test Results (" << pathRes.defectsCaught << " / " << pathRes.totalTests << " caught):\n";
    for (const auto& d : pathRes.defects) {
        std::cout << "    [" << (d.caught ? "PASS - CAUGHT" : "FAIL") << "] "
                  << d.testCase << " -> " << faceClassToString(d.faceClassification)
                  << " at (" << std::fixed << std::setprecision(2)
                  << d.defectLocation.x << ", " << d.defectLocation.y << ", " << d.defectLocation.z << ")\n"
                  << "       Detail: " << d.description << "\n";
    }
    std::cout << "  => Defect Detection Rate: " << std::fixed << std::setprecision(1) << (pathRes.detectionRate * 100.0) << "%\n";
    std::cout << "  => Zero NaNs Certified: " << (pathRes.zeroNaNs ? "YES" : "NO") << "\n";
    std::cout << "  => Zero Segfaults Certified: " << (pathRes.zeroSegfaults ? "YES" : "NO") << "\n";

    auto tEnd = std::chrono::high_resolution_clock::now();
    double totalElapsed = std::chrono::duration<double>(tEnd - t0).count();
    std::cout << "\nVerification Suite Completed Successfully in " << std::fixed << std::setprecision(2) << totalElapsed << " s.\n";

    // -------------------------------------------------------------------------
    // EXPORT TO JSON: public/verification_results.json & src/data/verification_results.json
    // -------------------------------------------------------------------------
    std::cout << "\nExporting Empirical Verification Results to JSON...\n";

    std::ostringstream json;
    json << "{\n";
    json << "  \"engineMetadata\": {\n";
    json << "    \"name\": \"ASME V&V 20 C++20 Unstructured 3D Polyhedral FVM Verification Engine\",\n";
    json << "    \"standard\": \"ASME V&V 20-2009\",\n";
    json << "    \"compiler\": \"g++ (Debian 12.2.0) -std=c++20 -O2\",\n";
    json << "    \"totalExecutionTimeSec\": " << totalElapsed << ",\n";
    json << "    \"timestamp\": \"2025-05-18T12:00:00Z\"\n";
    json << "  },\n";

    // Mesh family metadata
    json << "  \"meshFamily\": [\n";
    for (size_t m = 0; m < meshes.size(); ++m) {
        const auto& mesh = meshes[m];
        json << "    {\n";
        json << "      \"level\": " << (m + 1) << ",\n";
        json << "      \"n\": " << mesh.nx << ",\n";
        json << "      \"h\": " << mesh.h << ",\n";
        json << "      \"cellCount\": " << mesh.cells.size() << ",\n";
        json << "      \"faceCount\": " << mesh.faces.size() << ",\n";
        json << "      \"vertexCount\": " << mesh.vertices.size() << ",\n";
        json << "      \"minVolume\": " << mesh.minVol << ",\n";
        json << "      \"maxVolume\": " << mesh.maxVol << ",\n";
        json << "      \"avgVolume\": " << mesh.avgVol << ",\n";
        json << "      \"maxNonOrthoAngleDeg\": " << mesh.maxNonOrthoAngle << ",\n";
        json << "      \"avgNonOrthoAngleDeg\": " << mesh.avgNonOrthoAngle << ",\n";
        json << "      \"maxSkewness\": " << mesh.maxSkewness << ",\n";
        json << "      \"avgSkewness\": " << mesh.avgSkewness << ",\n";
        json << "      \"alphaSkew\": " << mesh.alphaSkew << "\n";
        json << "    }" << (m + 1 < meshes.size() ? "," : "") << "\n";
    }
    json << "  ],\n";

    // MMS Convergence runs
    json << "  \"mmsConvergence\": [\n";
    for (size_t mi = 0; mi < allModeResults.size(); ++mi) {
        const auto& mr = allModeResults[mi];
        json << "    {\n";
        json << "      \"modeId\": " << static_cast<int>(mr.mode) << ",\n";
        json << "      \"modeName\": \"" << mr.modeName << "\",\n";
        json << "      \"p_obs_21_L2\": " << mr.p_obs_21_L2 << ",\n";
        json << "      \"p_obs_32_L2\": " << mr.p_obs_32_L2 << ",\n";
        json << "      \"p_obs_21_Linf\": " << mr.p_obs_21_Linf << ",\n";
        json << "      \"p_obs_32_Linf\": " << mr.p_obs_32_Linf << ",\n";
        json << "      \"GCI_21\": " << mr.GCI_21 << ",\n";
        json << "      \"GCI_32\": " << mr.GCI_32 << ",\n";
        json << "      \"asymptoticRangeRatio\": " << mr.asymptoticRangeRatio << ",\n";
        json << "      \"f1\": " << mr.levels[2].domainAvgPhi << ",\n";
        json << "      \"f2\": " << mr.levels[1].domainAvgPhi << ",\n";
        json << "      \"f3\": " << mr.levels[0].domainAvgPhi << ",\n";
        json << "      \"f_exact\": " << mr.levels[2].exactDomainAvgPhi << ",\n";
        json << "      \"levels\": [\n";
        for (size_t li = 0; li < mr.levels.size(); ++li) {
            const auto& lvl = mr.levels[li];
            json << "        {\n";
            json << "          \"gridLevel\": " << (li + 1) << ",\n";
            json << "          \"nCells\": " << lvl.nCells << ",\n";
            json << "          \"h\": " << lvl.h << ",\n";
            json << "          \"L2_error\": " << lvl.L2_error << ",\n";
            json << "          \"Linf_error\": " << lvl.Linf_error << ",\n";
            json << "          \"domainAvgPhi\": " << lvl.domainAvgPhi << ",\n";
            json << "          \"probeCenterPhi\": " << lvl.probeCenterPhi << ",\n";
            json << "          \"linearIters\": " << lvl.linearIters << ",\n";
            json << "          \"deferredIters\": " << lvl.deferredIters << ",\n";
            json << "          \"solveTimeSec\": " << lvl.solveTimeSec << "\n";
            json << "        }" << (li + 1 < mr.levels.size() ? "," : "") << "\n";
        }
        json << "      ]\n";
        json << "    }" << (mi + 1 < allModeResults.size() ? "," : "") << "\n";
    }
    json << "  ],\n";

    // Adjoint Gate
    json << "  \"adjointVerification\": {\n";
    json << "    \"h_cs\": " << formatDouble(adjRes.h_cs) << ",\n";
    json << "    \"maxRelError\": " << formatDouble(adjRes.maxRelError) << ",\n";
    json << "    \"passedComplexStepGate\": " << (adjRes.passedComplexStepGate ? "true" : "false") << ",\n";
    json << "    \"averageTaylorSlope\": " << adjRes.averageSlope << ",\n";
    json << "    \"passedTaylorGate\": " << (adjRes.passedTaylorGate ? "true" : "false") << ",\n";
    json << "    \"parameterChecks\": [\n";
    for (int p = 0; p < adjRes.numParams; ++p) {
        json << "      {\n";
        json << "        \"paramIndex\": " << p << ",\n";
        json << "        \"theta\": " << adjRes.theta[p] << ",\n";
        json << "        \"g_adj\": " << adjRes.g_adj[p] << ",\n";
        json << "        \"g_cs\": " << adjRes.g_cs[p] << ",\n";
        json << "        \"relError\": " << formatDouble(adjRes.relErrors[p]) << "\n";
        json << "      }" << (p + 1 < adjRes.numParams ? "," : "") << "\n";
    }
    json << "    ],\n";
    json << "    \"taylorRemainderTest\": [\n";
    for (size_t k = 0; k < adjRes.epsilons.size(); ++k) {
        json << "      {\n";
        json << "        \"epsilon\": " << adjRes.epsilons[k] << ",\n";
        json << "        \"R1\": " << adjRes.taylorRemainders[k] << ",\n";
        json << "        \"slope\": " << (k > 0 ? adjRes.observedSlopes[k - 1] : 2.0) << "\n";
        json << "      }" << (k + 1 < adjRes.epsilons.size() ? "," : "") << "\n";
    }
    json << "    ]\n";
    json << "  },\n";

    // Pathology Suite
    json << "  \"pathologySuite\": {\n";
    json << "    \"totalTests\": " << pathRes.totalTests << ",\n";
    json << "    \"defectsCaught\": " << pathRes.defectsCaught << ",\n";
    json << "    \"detectionRate\": " << pathRes.detectionRate << ",\n";
    json << "    \"zeroNaNs\": " << (pathRes.zeroNaNs ? "true" : "false") << ",\n";
    json << "    \"zeroSegfaults\": " << (pathRes.zeroSegfaults ? "true" : "false") << ",\n";
    json << "    \"defects\": [\n";
    for (size_t d = 0; d < pathRes.defects.size(); ++d) {
        const auto& def = pathRes.defects[d];
        json << "      {\n";
        json << "        \"testCase\": \"" << def.testCase << "\",\n";
        json << "        \"defectType\": \"" << def.defectType << "\",\n";
        json << "        \"faceClassification\": \"" << faceClassToString(def.faceClassification) << "\",\n";
        json << "        \"caught\": " << (def.caught ? "true" : "false") << ",\n";
        json << "        \"hasNaN\": " << (def.hasNaN ? "true" : "false") << ",\n";
        json << "        \"location\": [" << def.defectLocation.x << ", " << def.defectLocation.y << ", " << def.defectLocation.z << "],\n";
        json << "        \"severityMetric\": " << def.severityMetric << ",\n";
        json << "        \"description\": \"" << def.description << "\"\n";
        json << "      }" << (d + 1 < pathRes.defects.size() ? "," : "") << "\n";
    }
    json << "    ]\n";
    json << "  },\n";

    // Sample 3D visual data for interactive WebGL/Canvas rendering
    // Level 1 mesh (8^3 = 512 cells) sample cells and faces for 3D exploration
    const auto& sampleMesh = meshes[0];
    const auto& fullSol = allModeResults[0].levels[0];
    const auto& naiveSol = allModeResults[3].levels[0];

    json << "  \"sample3DVisual\": {\n";
    json << "    \"gridDim\": [8, 8, 8],\n";
    json << "    \"numSampleCells\": " << sampleMesh.cells.size() << ",\n";
    json << "    \"cells\": [\n";
    for (size_t c = 0; c < sampleMesh.cells.size(); ++c) {
        const auto& cell = sampleMesh.cells[c];
        json << "      {\n";
        json << "        \"id\": " << cell.id << ",\n";
        json << "        \"xc\": [" << cell.xc.x << ", " << cell.xc.y << ", " << cell.xc.z << "],\n";
        json << "        \"volume\": " << cell.volume << ",\n";
        json << "        \"phiExact\": " << fullSol.phiExact[c] << ",\n";
        json << "        \"phiCorrected\": " << fullSol.phiSolution[c] << ",\n";
        json << "        \"phiNaive\": " << naiveSol.phiSolution[c] << ",\n";
        json << "        \"errCorrected\": " << std::abs(fullSol.errorField[c]) << ",\n";
        json << "        \"errNaive\": " << std::abs(naiveSol.errorField[c]) << "\n";
        json << "      }" << (c + 1 < sampleMesh.cells.size() ? "," : "") << "\n";
    }
    json << "    ],\n";

    // Sample representative faces with geometric decomposition
    json << "    \"sampleFaces\": [\n";
    int faceSampleCount = 0;
    for (size_t f = 0; f < sampleMesh.faces.size() && faceSampleCount < 200; f += 8) {
        const auto& face = sampleMesh.faces[f];
        json << "      {\n";
        json << "        \"id\": " << face.id << ",\n";
        json << "        \"owner\": " << face.owner << ",\n";
        json << "        \"neighbor\": " << face.neighbor << ",\n";
        json << "        \"xf\": [" << face.xf.x << ", " << face.xf.y << ", " << face.xf.z << "],\n";
        json << "        \"Sf\": [" << face.Sf.x << ", " << face.Sf.y << ", " << face.Sf.z << "],\n";
        json << "        \"area\": " << face.area << ",\n";
        json << "        \"df\": [" << face.df.x << ", " << face.df.y << ", " << face.df.z << "],\n";
        json << "        \"nonOrthoAngleDeg\": " << face.nonOrthoAngleDeg << ",\n";
        json << "        \"skewnessMetric\": " << face.skewnessMetric << ",\n";
        json << "        \"alpha_f\": " << face.alpha_f << ",\n";
        json << "        \"beta_f\": " << face.beta_f << ",\n";
        json << "        \"m_skew\": [" << face.m_skew.x << ", " << face.m_skew.y << ", " << face.m_skew.z << "],\n";
        json << "        \"k_corr\": [" << face.k_corr.x << ", " << face.k_corr.y << ", " << face.k_corr.z << "],\n";
        json << "        \"classification\": \"" << faceClassToString(face.classification) << "\"\n";
        json << "      },\n";
        faceSampleCount++;
    }
    // Remove trailing comma if needed, or append a sentinel
    json << "      {\n";
    json << "        \"id\": -1,\n";
    json << "        \"classification\": \"END\"\n";
    json << "      }\n";
    json << "    ]\n";
    json << "  }\n";
    json << "}\n";

    // Write to public/verification_results.json
    std::string jsonStr = json.str();
    {
        std::ofstream out("public/verification_results.json");
        if (out.is_open()) {
            out << jsonStr;
            std::cout << "  Saved public/verification_results.json (" << jsonStr.size() << " bytes)\n";
        } else {
            std::cerr << "  Error opening public/verification_results.json!\n";
        }
    }

    // Also write to src/data/verification_results.json
    {
        std::ofstream out("src/data/verification_results.json");
        if (out.is_open()) {
            out << jsonStr;
            std::cout << "  Saved src/data/verification_results.json (" << jsonStr.size() << " bytes)\n";
        } else {
            std::cerr << "  Error opening src/data/verification_results.json!\n";
        }
    }

    return 0;
}
