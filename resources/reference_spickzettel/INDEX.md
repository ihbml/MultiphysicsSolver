# Master-Bookmarks & Spickzettel-Index (`reference_spickzettel`)
> **Zweck:** Zentrales Lesezeichen-Verzeichnis (Bookmarks) für dich und alle KI-Instanzen (*Antigravity / Gemini, Carbon, Claude Opus 5.5*).  
> **Grundgesetz:** Wir bauen **keinen** abhängigen Bibliotheks-Flickenteppich, sondern schreiben **100 % eigenständigen C++ / Modern Fortran / Rust Code** und nutzen diese **23 geklonten Referenz-Repos + `OpenFOAM_base` + 7 Fachpaper** als direkten Spickzettel für die besten Algorithmen.

---

## Schnell-Navigation (Kategorien)
1. [Kategorie 1: CAD, B-Rep, Implizite Geometrie & Wasserdichte Boolesche Schnitte (`CAD/`)](#1-kategorie-cad--b-rep-implizite-geometrie--boolesche-schnitte)
2. [Kategorie 2: 3D-Meshing, Cut-Cell, Octree & Level-Set-Vernetzungen (`Meshing/`)](#2-kategorie-meshing--3d-vernetzung-cut-cell-octree--level-set)
3. [Kategorie 3: CFD, Graph-FVM, Adjoint & SPH-FVM Kopplung (`CFD_Multiphysics/` & `OpenFOAM_base`)](#3-kategorie-cfd_multiphysics--graph-fvm-adjoint--sph-fvm-kopplung)
4. [Kategorie 4: Gleichungslöser, Algebraic Multigrid (AMG) & Mixed-Precision HPC (`LinearSolvers_HPC/`)](#4-kategorie-linearsolvers_hpc--amg-drucklöser--mixed-precision-fp16fp64)
5. [Kategorie 5: Topologieoptimierung (SIMP, MMA, FOSM), Schwarm (PSO/MIGA) & Auto-Diff (`Optimization_AD/`)](#5-kategorie-optimization_ad--simp-mma-fosm-psomiga--auto-diff)
6. [Kategorie 6: Reaktionskinetik, Realgas-Thermodynamik & Steife ODE-Löser (`Thermo_Chemistry_Physics/`)](#6-kategorie-thermo_chemistry_physics--kinetik-realgas--steife-odes)
7. [Kategorie 7: Wissenschaftliche Paper-Bookmarks (`resources/papers/`)](#7-kategorie-papers--die-7-schlüsselpublikationen)

---

## 1. Kategorie `CAD` — B-Rep, Implizite Geometrie & Boolesche Schnitte
Ordner: [`resources/reference_spickzettel/CAD/`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CAD)

| Repository | Wichtigste Bookmark-Dateien (Direkt-Links) | Wofür wir diesen Spickzettel nutzen |
| :--- | :--- | :--- |
| **[`truck`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CAD/truck)** *(Reines Rust CAD)* | • [`truck-stepio/`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CAD/truck/truck-stepio) *(STEP-Parser & Exporter in Rust)*<br/>• [`truck-shapeops/`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CAD/truck/truck-shapeops) *(NURBS Boolesche Schnitte)*<br/>• [`truck-meshalgo/`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CAD/truck/truck-meshalgo) *(Oberflächen-Triangulierung)*<br/>• [`truck-topology/`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CAD/truck/truck-topology) *(Vertices, Edges, Faces, Shells)* | Moderne, speichersichere Rust-Vorlage für unseren STEP-Reader und B-Rep-Topologiekern (100x kompakter als OpenCASCADE). |
| **[`manifold`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CAD/manifold)** *(Garantiert wasserdichtes C++)* | • [`boolean3.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CAD/manifold/src/boolean3.cpp) *(3D Boolesche Verschneidung)*<br/>• [`csg_tree.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CAD/manifold/src/csg_tree.cpp) *(CSG Baum-Auswertung)*<br/>• [`sdf.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CAD/manifold/src/sdf.cpp) *(Level-Set / Marching Tetrahedra)*<br/>• [`smoothing.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CAD/manifold/src/smoothing.cpp) *(Netz-Glättung)* | Garantiert **2-Manifold (100 % wasserdichte)** Boolesche Operationen, um aus einem CAD-Bauteil das negative Fluid-Strömungsvolumen ohne Mikrolöcher auszuschneiden. |
| **[`libigl`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CAD/libigl)** *(ETH Zürich / NYU Header-Only C++)* | • [`signed_distance.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CAD/libigl/include/igl/signed_distance.cpp) *(Exakter Wandabstand SDF)*<br/>• [`fast_winding_number.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CAD/libigl/include/igl/fast_winding_number.cpp) *(Innen/Außen-Test für $\gamma_i$)*<br/>• [`marching_cubes.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CAD/libigl/include/igl/marching_cubes.cpp) *(SIMP $\gamma_i \to$ glattes CAD-Netz)* | **Fast Winding Numbers** entscheiden für Millionen CFD-Zellen in Millisekunden, ob eine Zelle im Festkörper ($\gamma_i=0$) oder Fluid ($\gamma_i=1$) liegt; `marching_cubes` gewinnt aus dem optimierten SIMP-Feld das fertige Bauteil zurück. |
| **[`openvdb`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CAD/openvdb)** *(OpenVDB & NanoVDB)* | • [`nanovdb/NanoVDB.h`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CAD/openvdb/nanovdb/nanovdb/NanoVDB.h) *(Single-Header Octree/Level-Set)*<br/>• [`openvdb/tools/LevelSetAdvect.h`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CAD/openvdb/openvdb/openvdb/tools/LevelSetAdvect.h) | Datenstruktur-Blaupause für flache, GPU-/SIMD-freundliche hierarchische Octree-Gitter und Level-Set-Transport ($\psi$). |
| **[`solvespace`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CAD/solvespace)** *(Parametrischer 3D-Kernel)* | • [`src/constrainteq.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CAD/solvespace/src/constrainteq.cpp) *(Geometrischer Gleichungslöser)*<br/>• [`src/srf/`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CAD/solvespace/src/srf) *(Kompakter NURBS-Flächenkern)* | Zeigt, wie ein kompletter parametrischer CAD-Constraint-Solver (Abstände, Radien, Winkel für Formoptimierung) in wenigen tausend Zeilen C++ funktioniert. |
| **[`OCCT`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CAD/OCCT)** *(OpenCASCADE)* | • [`src/STEPControl/`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CAD/OCCT/src/STEPControl) *(Vollständiger ISO-STEP-Standard)*<br/>• [`src/BRepMesh/`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CAD/OCCT/src/BRepMesh) *(B-Rep Tessellierung)* | Nachschlagewerk für seltene ISO-10303-STEP-Spezialfälle (AP203/214/242). |

---

## 2. Kategorie `Meshing` — 3D-Vernetzung, Cut-Cell, Octree & Level-Set
Ordner: [`resources/reference_spickzettel/Meshing/`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Meshing)

| Repository | Wichtigste Bookmark-Dateien (Direkt-Links) | Wofür wir diesen Spickzettel nutzen |
| :--- | :--- | :--- |
| **[`fTetWild`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Meshing/fTetWild)** *(Kugelsicherer 3D-Mesher)* | • [`CutMesh.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Meshing/fTetWild/src/CutMesh.cpp) *(Zellen entlang CAD-Dreiecken schneiden)*<br/>• [`FloatTetDelaunay.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Meshing/fTetWild/src/FloatTetDelaunay.cpp)<br/>• [`MeshImprovement.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Meshing/fTetWild/src/MeshImprovement.cpp) *(Qualitäts-Optimierung)* | `CutMesh.cpp` und `MeshImprovement.cpp` sind die perfekte Vorlage für unseren eigenen Cut-Cell-/Tetraeder-Vernetzungsalgorithmus, der selbst bei kaputten STL-Dateien nie abstürzt. |
| **[`mmg`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Meshing/mmg)** *(Level-Set & Anisotropes Remeshing)* | • [`src/mmg3d/mmg3d1.c`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Meshing/mmg/src/mmg3d/mmg3d1.c)<br/>• [`src/mmg3d/mmg3d2.c`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Meshing/mmg/src/mmg3d/mmg3d2.c) *(Implizites Level-Set $\psi = 0$ Meshing!)* | `mmg3d2.c` zeigt, wie man eine **Level-Set-Nullkontur ($\psi = 0$)** aus unserer Topologieoptimierung direkt als messerscharfe Kante in ein bestehendes 3D-Gitter schneidet! |
| **[`gmsh`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Meshing/gmsh)** *(3D-FVM & Grenzschichten)* | • [`src/mesh/meshGRegion.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Meshing/gmsh/src/mesh/meshGRegion.cpp) *(3D-Volumenvernetzung)*<br/>• [`src/mesh/meshGRegionBoundaryLayer.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Meshing/gmsh/src/mesh/meshGRegionBoundaryLayer.cpp) | Referenz für das Aufwachsen von **Grenzschicht-Prismen (Boundary Layer Extrusion für $y^+ < 1$)** an gekrümmten Wänden. |
| **[`tetgen`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Meshing/tetgen)** *(Kompakter Delaunay-Kern)* | • [`tetgen.cxx`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Meshing/tetgen/tetgen.cxx) & [`predicates.cxx`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Meshing/tetgen/predicates.cxx) | Exakte geometrische Prädikate (`orient3d`, `insphere` von Shewchuk) gegen Rundungsfehler bei spitzen Winkeln. |

---

## 3. Kategorie `CFD_Multiphysics` — Graph-FVM, Adjoint & SPH-FVM Kopplung
Ordner: [`resources/reference_spickzettel/CFD_Multiphysics/`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CFD_Multiphysics) & [`OpenFOAM_base/`](file:///G:/Datein/Projects/CFDSolver/OpenFOAM_base/openfoam/src)

| Repository | Wichtigste Bookmark-Dateien (Direkt-Links) | Wofür wir diesen Spickzettel nutzen |
| :--- | :--- | :--- |
| **[`SU2`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CFD_Multiphysics/SU2)** *(Stanford Unstructured FVM + Adjoint)* | • [`SU2_CFD/src/numerics/flow/convection/hllc.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CFD_Multiphysics/SU2/SU2_CFD/src/numerics/flow/convection/hllc.cpp)<br/>• [`SU2_CFD/src/numerics/flow/convection/roe.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CFD_Multiphysics/SU2/SU2_CFD/src/numerics/flow/convection/roe.cpp)<br/>• [`SU2_CFD/src/solvers/CFVMFlowSolverBase.inl`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CFD_Multiphysics/SU2/SU2_CFD/include/solvers/CFVMFlowSolverBase.inl)<br/>• [`SU2_CFD/src/drivers/CDiscAdjSinglezoneDriver.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CFD_Multiphysics/SU2/SU2_CFD/src/drivers/CDiscAdjSinglezoneDriver.cpp) | Sauberer, makroloser C++-Spickzettel für unstrukturierte FVM-Flüsse (HLLC, Roe, AUSM), Grenzschicht-Turbulenz ($k-\omega$ SST, SA) und diskrete Adjoint-Schleifen. |
| **[`SPHinXsys`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CFD_Multiphysics/SPHinXsys)** *(TU München Eulerian SPH + FVM)* | • [`eulerian_riemann_solver.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CFD_Multiphysics/SPHinXsys/src/shared/particle_dynamics/fluid_dynamics/eulerian_fluid_dynamics/eulerian_riemann_solver.cpp) *(HLLC mit Limiter $\beta_{HLLC}$)*<br/>• [`eulerian_ghost_boundary.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CFD_Multiphysics/SPHinXsys/src/shared/particle_dynamics/general_dynamics/boundary_condition/eulerian_ghost_boundary.cpp) *(Nullte-Ordnung-Randkonsistenz!)*<br/>• [`eulerian_compressible_fluid_integration.hpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CFD_Multiphysics/SPHinXsys/src/shared/particle_dynamics/fluid_dynamics/eulerian_fluid_dynamics/eulerian_compressible_fluid_integration.hpp) | **Der exakte Original-Code zu deinem Paper [`MeshingFVM_SPH_2312.03542v1.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/MeshingFVM_SPH_2312.03542v1.pdf)!** Enthält die direkte Kopplung von FVM-Randbedingungen und Eulerian-SPH. |
| **[`ECOGEN`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CFD_Multiphysics/ECOGEN)** *(Mehrphasen, Kavitation & Stöße)* | • [`src/Models/`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CFD_Multiphysics/ECOGEN/src/Models) *(Kapila Mehrphasen- & Kavitationsmodelle)*<br/>• [`src/Eos/`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CFD_Multiphysics/ECOGEN/src/Eos) *(Stiffened Gas EOS für Wasser/Gas-Blasen)*<br/>• [`src/Relaxations/`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CFD_Multiphysics/ECOGEN/src/Relaxations) *(Druck-/Temperatur-Relaxation & Phasenwechsel)* | Perfekte Vorlage für die kompressiblen Mehrphasen-Gleichungen (Stiffened Gas EOS Gl. 17 & 33 aus deinem Blasen-Paper [`MeshingPaperBubble1-s2.0-S030193222500134X-main.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/MeshingPaperBubble1-s2.0-S030193222500134X-main.pdf)). |
| **[`OpenFOAM_base/src/fused`](file:///G:/Datein/Projects/CFDSolver/OpenFOAM_base/openfoam/src/fused/finiteVolume)** *(Fused FVM Kernels)* | • [`fusedGaussGrad.C`](file:///G:/Datein/Projects/CFDSolver/OpenFOAM_base/openfoam/src/fused/finiteVolume/fusedGaussGrad.C#L86-L109)<br/>• [`fusedGaussConvectionScheme.C`](file:///G:/Datein/Projects/CFDSolver/OpenFOAM_base/openfoam/src/fused/finiteVolume/fusedGaussConvectionScheme.C)<br/>• [`fusedGaussLaplacianScheme.C`](file:///G:/Datein/Projects/CFDSolver/OpenFOAM_base/openfoam/src/fused/finiteVolume/fusedGaussLaplacianScheme.C)<br/>• [`CuthillMcKeeRenumber.C`](file:///G:/Datein/Projects/CFDSolver/OpenFOAM_base/openfoam/src/renumber/renumberMethods/CuthillMcKeeRenumber/CuthillMcKeeRenumber.C) | Single-Pass Gather/Scatter FVM-Schleifen und Cache-Renumbering für unser `DynamicRedistributor`-Modul. |

---

## 4. Kategorie `LinearSolvers_HPC` — AMG Drucklöser & Mixed-Precision (`FP16`/`FP64`)
Ordner: [`resources/reference_spickzettel/LinearSolvers_HPC/`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LinearSolvers_HPC)

| Repository | Wichtigste Bookmark-Dateien (Direkt-Links) | Wofür wir diesen Spickzettel nutzen |
| :--- | :--- | :--- |
| **[`amgcl`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LinearSolvers_HPC/amgcl)** *(Header-Only C++ AMG)* | • [`amgcl/amg.hpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LinearSolvers_HPC/amgcl/amgcl/amg.hpp) *(Kompletter AMG V-Cycle Aufbau)*<br/>• [`amgcl/coarsening/`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LinearSolvers_HPC/amgcl/amgcl/coarsening) *(Smoothed Aggregation & Ruge-Stüben)*<br/>• [`amgcl/relaxation/`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LinearSolvers_HPC/amgcl/amgcl/relaxation) *(SPAI0, Gauss-Seidel, ILU0, Chebyshev Glätter)*<br/>• [`amgcl/solver/bicgstab.hpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LinearSolvers_HPC/amgcl/amgcl/solver/bicgstab.hpp) & [`cg.hpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LinearSolvers_HPC/amgcl/amgcl/solver/cg.hpp) | Der mit Abstand klarste C++-Spickzettel weltweit für unseren eigenen **Algebraic Multigrid (AMG)** Druck-Poisson-Löser und unsere Fortran/C++ Krylov-Löser (CG, BiCGStab, GMRES). |
| **[`ginkgo`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LinearSolvers_HPC/ginkgo)** *(KIT Mixed-Precision HPC)* | • [`core/solver/ir.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LinearSolvers_HPC/ginkgo/core/solver/ir.cpp) *(Mixed-Precision Iterative Refinement!)*<br/>• [`core/solver/cb_gmres.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LinearSolvers_HPC/ginkgo/core/solver/cb_gmres.cpp) *(Compressed-Basis GMRES in FP16/FP32)* | Exakte Vorlage für unser **`PrecisionAddon::LowPrecisionBooster`** (äußeres Residuum in `FP64`, innerer Vorkonditionierer in `FP16`/`FP32`). |
| **[`eigen`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LinearSolvers_HPC/eigen)** *(SIMD Matrix/Vektor-Kern)* | • [`Eigen/src/Core/arch/`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LinearSolvers_HPC/eigen/Eigen/src/Core/arch) *(AVX2, AVX-512, ARM NEON, SVE Paket-Mathematik)* | Spickzettel für plattformübergreifende SIMD-Vektorisierung (x86 AVX2/AVX-512 & ARM64 NEON/SVE) und kleine $3\times 3$ / $6\times 6$ Block-Invertierungen. |

---

## 5. Kategorie `Optimization_AD` — SIMP, MMA, FOSM, PSO/MIGA & Auto-Diff
Ordner: [`resources/reference_spickzettel/Optimization_AD/`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Optimization_AD) & [`OpenFOAM_base/src/optimisation`](file:///G:/Datein/Projects/CFDSolver/OpenFOAM_base/openfoam/src/optimisation/adjointOptimisation)

| Repository | Wichtigste Bookmark-Dateien (Direkt-Links) | Wofür wir diesen Spickzettel nutzen |
| :--- | :--- | :--- |
| **[`TopOpt_in_PETSc`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Optimization_AD/TopOpt_in_PETSc)** *(DTU TopOpt Gruppe: Sigmund / Aage / Lazarov)* | • [`MMA.cc`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Optimization_AD/TopOpt_in_PETSc/MMA.cc) & [`MMA.h`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Optimization_AD/TopOpt_in_PETSc/MMA.h) *(700 Zeilen reines C++ MMA!)*<br/>• [`PDEFilter.cc`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Optimization_AD/TopOpt_in_PETSc/PDEFilter.cc) *(Helmholtz-Dichtefilter)*<br/>• [`Filter.cc`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Optimization_AD/TopOpt_in_PETSc/Filter.cc) *(Nachbarschafts- & Heaviside-Filter)*<br/>• [`TopOpt.cc`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Optimization_AD/TopOpt_in_PETSc/TopOpt.cc) | **Juwel des Ordners:** [`MMA.cc`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Optimization_AD/TopOpt_in_PETSc/MMA.cc) ist die offizielle, extrem saubere C++-Implementierung von Svanbergs *Method of Moving Asymptotes* für Millionen 3D-Zellen! |
| **[`nlopt`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Optimization_AD/nlopt)** *(MIT Optimierungs-Algorithmen)* | • [`src/algs/mma/mma.c`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Optimization_AD/nlopt/src/algs/mma/mma.c) & [`ccsa_quadratic.c`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Optimization_AD/nlopt/src/algs/mma/ccsa_quadratic.c)<br/>• [`src/algs/neldermead/nldrmd.c`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Optimization_AD/nlopt/src/algs/neldermead/nldrmd.c) *(Downhill Simplex!)*<br/>• [`src/algs/slsqp/slsqp.c`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Optimization_AD/nlopt/src/algs/slsqp/slsqp.c) | Kompakte C-Implementierung des **Downhill Simplex (Nelder-Mead)** aus deinem Turbinen-Paper ([`optimization-curved-fir-tree-turbine-15.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/optimization-curved-fir-tree-turbine-15.pdf)) sowie CCSA/MMA. |
| **[`pagmo2`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Optimization_AD/pagmo2)** *(ESA Schwarm- & Insel-Optimierung)* | • [`src/algorithms/pso.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Optimization_AD/pagmo2/src/algorithms/pso.cpp) & [`pso_gen.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Optimization_AD/pagmo2/src/algorithms/pso_gen.cpp) *(Particle Swarm)*<br/>• [`src/archipelago.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Optimization_AD/pagmo2/src/archipelago.cpp) & [`island.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Optimization_AD/pagmo2/src/island.cpp) *(Multi-Island Migration MIGA!)*<br/>• [`src/algorithms/nsga2.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Optimization_AD/pagmo2/src/algorithms/nsga2.cpp) & [`cmaes.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Optimization_AD/pagmo2/src/algorithms/cmaes.cpp) | Referenz für **Particle Swarm Optimization (PSO)** und das **Insel-Migrations-Modell (Multi-Island Genetic Algorithm MIGA)** mit parallelen Sub-Populationen im RAM. |
| **[`Enzyme`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Optimization_AD/Enzyme)** *(MIT LLVM Auto-Diff)* | • [`enzyme/Enzyme/Enzyme.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Optimization_AD/Enzyme/enzyme/Enzyme/Enzyme.cpp)<br/>• [`enzyme/test/Integration/ReverseMode/`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Optimization_AD/Enzyme/enzyme/test/Integration/ReverseMode) | Zeigt die exakten Funktions-Signaturen (`__enzyme_autodiff`), um unsere C++- und Fortran-Kerne vollautomatisch rückwärts zu differenzieren. |
| **[`OpenFOAM_base/adjointOptimisation`](file:///G:/Datein/Projects/CFDSolver/OpenFOAM_base/openfoam/src/optimisation/adjointOptimisation/adjoint/optimisation)** | • [`BorrvallPeterssonInterpolation.C`](file:///G:/Datein/Projects/CFDSolver/OpenFOAM_base/openfoam/src/optimisation/adjointOptimisation/adjoint/optimisation/designVariables/topODesignVariables/interpolationFunctions/BorrvallPetersson/BorrvallPeterssonInterpolation.C)<br/>• [`invBP.C` (RAMP)](file:///G:/Datein/Projects/CFDSolver/OpenFOAM_base/openfoam/src/optimisation/adjointOptimisation/adjoint/optimisation/designVariables/topODesignVariables/interpolationFunctions/invBP/invBP.C)<br/>• [`Helmholtz.C`](file:///G:/Datein/Projects/CFDSolver/OpenFOAM_base/openfoam/src/optimisation/adjointOptimisation/adjoint/optimisation/designVariables/topODesignVariables/regularisation/regularisationPDE/Helmoltz/Helmholtz.C)<br/>• [`LBFGS.C`](file:///G:/Datein/Projects/CFDSolver/OpenFOAM_base/openfoam/src/optimisation/adjointOptimisation/adjoint/optimisation/updateMethod/LBFGS/LBFGS.C) | Brinkman-Darcy-Interpolation für Strömungs-SIMP, RAMP und L-BFGS Zwei-Schleifen-Rekursion. |

---

## 6. Kategorie `Thermo_Chemistry_Physics` — Kinetik, Realgas & Steife ODEs
Ordner: [`resources/reference_spickzettel/Thermo_Chemistry_Physics/`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Thermo_Chemistry_Physics)

| Repository | Wichtigste Bookmark-Dateien (Direkt-Links) | Wofür wir diesen Spickzettel nutzen |
| :--- | :--- | :--- |
| **[`cantera`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Thermo_Chemistry_Physics/cantera)** *(Reaktionskinetik & Katalyse)* | • [`src/kinetics/Arrhenius.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Thermo_Chemistry_Physics/cantera/src/kinetics/Arrhenius.cpp) & [`Falloff.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Thermo_Chemistry_Physics/cantera/src/kinetics/Falloff.cpp) *(Troe/SRI Druckabhängigkeit)*<br/>• [`src/kinetics/InterfaceKinetics.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Thermo_Chemistry_Physics/cantera/src/kinetics/InterfaceKinetics.cpp) *(Heterogene Wandkatalyse / Washcoat)*<br/>• [`src/thermo/NasaPoly2.h`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Thermo_Chemistry_Physics/cantera/include/cantera/thermo/NasaPoly2.h) *(7-Koeffizienten NASA-Polynome)*<br/>• [`src/transport/MixTransport.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Thermo_Chemistry_Physics/cantera/src/transport/MixTransport.cpp) *(Maxwell-Stefan / Hirschfelder-Curtiss)* | Blaupause für unser `species_chem`- und `energy_heat`-Modul (in Modern Fortran hochvektorisiert nachgebaut!). |
| **[`CoolProp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Thermo_Chemistry_Physics/CoolProp)** *(Realgas- & Flüssigkeits-EOS)* | • [`src/Backends/Helmholtz/HelmholtzEOSMixtureBackend.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Thermo_Chemistry_Physics/CoolProp/src/Backends/Helmholtz/HelmholtzEOSMixtureBackend.cpp)<br/>• [`src/Backends/Cubics/GeneralizedCubic.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Thermo_Chemistry_Physics/CoolProp/src/Backends/Cubics/GeneralizedCubic.cpp) *(Peng-Robinson / SRK)* | Exakte Realgas-Zustandsgleichungen (Peng-Robinson, Soave-Redlich-Kwong, Helmholtz-Energie) und Dampfdruckkurven für Kavitation & Phasenwechsel. |
| **[`sundials`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Thermo_Chemistry_Physics/sundials)** *(LLNL Steife ODE-Löser)* | • [`src/cvode/cvode.c`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Thermo_Chemistry_Physics/sundials/src/cvode/cvode.c) *(BDF Backward Differentiation Formulas für steife ODEs)*<br/>• [`src/cvode/cvode_nls.c`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Thermo_Chemistry_Physics/sundials/src/cvode/cvode_nls.c) *(Nichtlineare Newton-Iteration mit Jacobi-Matrix)* | Vorlage für unseren zelllokalen Fortran-Chemie- und Batterie-Thermal-Runaway-Integrator mit variabler Ordnung und Schrittweitensteuerung. |

---

## 7. Kategorie `papers` — Die 7 Schlüsselpublikationen im Direktzugriff
Ordner: [`resources/papers/`](file:///G:/Datein/Projects/CFDSolver/resources/papers) (Zusammenfassung in [`literature_survey_2026.md`](file:///G:/Datein/Projects/CFDSolver/resources/papers/literature_survey_2026.md))

1. **Graph-FVM & Impliziter Druck-Adjoint:** [`Meshing_FVM_DiFVM_2603.15920v1.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/Meshing_FVM_DiFVM_2603.15920v1.pdf) *(Du et al., 2026)*
2. **Eulerian SPH-FVM Randkonsistenz & HLLC:** [`MeshingFVM_SPH_2312.03542v1.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/MeshingFVM_SPH_2312.03542v1.pdf) *(Wang, Haidn, Hu – TU München)*
3. **Adaptive SPH-FVM Kopplung (`OPMIS`) für Mehrphasen-Blasen:** [`MeshingPaperBubble1-s2.0-S030193222500134X-main.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/MeshingPaperBubble1-s2.0-S030193222500134X-main.pdf) *(Zhang et al., 2025)*
4. **Thermo-Fluid SIMP, Brinkman & AD:** [`thesis_Elisabetta_Chimisso.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/thesis_Elisabetta_Chimisso.pdf) *(Chimisso, Politecnico di Milano)*
5. **Generalisierte FOSM Robuste Optimierung & KS-Aggregation:** [`ArbitraryFunctionOptimization_s00158-023-03540-w.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/ArbitraryFunctionOptimization_s00158-023-03540-w.pdf) *(Kranz, Lüdeker, Kriegesmann – TU Hamburg, 2023)*
6. **Level-Set Topologieoptimierung via Jacobi-Transformation:** [`Optimization_LevelSetMethod_AIAA-2010-2842-327.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/Optimization_LevelSetMethod_AIAA-2010-2842-327.pdf) *(James & Martins, 2010)*
7. **Zweistufige Globale (MIGA) + Lokale (Downhill Simplex) Optimierung:** [`optimization-curved-fir-tree-turbine-15.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/optimization-curved-fir-tree-turbine-15.pdf) *(Tan et al.)*


---

## 8. Gold-Snippets & Direkt-Lesezeichen (Wichtigste Codeblöcke mit Zeilennummern)

Hier sind die wichtigsten „Gold-Blöcke“ aus deinen geklonten Repos direkt mit exakter Zeilennummer (`#L...`) und dem extrahierten Kern-Snippet dokumentiert:

---

### Snippet 1: Fused Single-Pass Gauss-Gradient (aus [`OpenFOAM_base/.../fusedGaussGrad.C#L86-L109`](file:///G:/Datein/Projects/CFDSolver/OpenFOAM_base/openfoam/src/fused/finiteVolume/fusedGaussGrad.C#L86-L109))
* **Warum wichtig:** Berechnet den Zellgradienten $\nabla \phi_i = \frac{1}{V_i} \sum_f \phi_f \mathbf{S}_f$ in einer einzigen Schleife über alle internen Flächen (`owner` / `neighbour`), ohne ein temporäres Oberflächenfeld im RAM anzulegen.
```cpp
// Aus fusedGaussGrad.C (Zeilen 86–109):
forAll(owner, facei)
{
    const GradType Sfssf = Sf[facei]*issf[facei];
    igGrad[owner[facei]]     += Sfssf;
    igGrad[neighbour[facei]] -= Sfssf;
}
// Rand-Patches aufaddieren und durch Zellvolumen teilen:
igGrad /= mesh.V();
```

---

### Snippet 2: Nullte-Ordnung-Randkonsistenz für Eulerian SPH-FVM (aus [`SPHinXsys/.../eulerian_ghost_boundary.cpp#L25-L41`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CFD_Multiphysics/SPHinXsys/src/shared/particle_dynamics/general_dynamics/boundary_condition/eulerian_ghost_boundary.cpp#L25-L41))
* **Warum wichtig:** Exakte Umsetzung von **Gleichung (23)** aus deinem TU-München-Paper ([`MeshingFVM_SPH_2312.03542v1.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/MeshingFVM_SPH_2312.03542v1.pdf)): Berechnet für Randpartikel mit fehlenden Nachbarn den exakten FVM-Abschlussvektor $\mathbf{A}_{iS}^{ESPH} = -\sum_j V_j \nabla W_{ij}$.
```cpp
// Aus eulerian_ghost_boundary.cpp (Zeilen 29–41):
Vecd gradient_summation = Vecd::Zero();
Neighborhood &inner_neighborhood = inner_configuration_[index_i];
for (size_t n = 0; n != inner_neighborhood.current_size_; ++n)
{
    size_t index_j = inner_neighborhood.j_[n];
    Real dW_ijV_j = inner_neighborhood.dW_ij_[n] * Vol_[index_j];
    gradient_summation += dW_ijV_j * inner_neighborhood.e_ij_[n];
}
Real ghost_particle_dW_ijV_j = -gradient_summation.norm();
Vecd ghost_particle_eij      = -gradient_summation / ghost_particle_dW_ijV_j;
Real distance_to_ghost       = fabs(sph_body_->getInitialShape().findSignedDistance(pos_[index_i]));
```

---

### Snippet 3: HLLC Riemann-Löser mit Dissipations-Limiter (aus [`SPHinXsys/.../eulerian_riemann_solver.cpp#L95-L165`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CFD_Multiphysics/SPHinXsys/src/shared/particle_dynamics/fluid_dynamics/eulerian_fluid_dynamics/eulerian_riemann_solver.cpp#L95-L165) & [`SU2/.../hllc.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CFD_Multiphysics/SU2/SU2_CFD/src/numerics/flow/convection/hllc.cpp))
* **Warum wichtig:** Löst das eindimensionale Riemann-Problem an jeder Zell-/Partikelgrenze für kompressible Strömungen, Stoßwellen und Kavitationsblasen (Gleichung 7–16 im TUM-Paper).
```cpp
// Aus eulerian_riemann_solver.cpp (Zeilen 29–40):
Real ul = -e_ij.dot(state_i.vel_);
Real ur = -e_ij.dot(state_j.vel_);
Real s_l = ul - compressible_fluid_i_.getSoundSpeed(state_i.p_, state_i.rho_);
Real s_r = ur + compressible_fluid_j_.getSoundSpeed(state_j.p_, state_j.rho_);
Real denominator = state_j.rho_ * (s_r - ur) + state_i.rho_ * (ul - s_l);
Real s_star = (state_j.rho_ * ur * (s_r - ur) + state_i.rho_ * ul * (ul - s_l) + state_i.p_ - state_j.p_) / denominator;
```

---

### Snippet 4: Svanbergs MMA (Method of Moving Asymptotes) für 3D-SIMP (aus [`TopOpt_in_PETSc/MMA.cc#L499-L580`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Optimization_AD/TopOpt_in_PETSc/MMA.cc#L499-L580))
* **Warum wichtig:** Der Goldstandard-Optimierer für Topologieoptimierung (aus deinem TU-Hamburg FOSM-Paper [`ArbitraryFunctionOptimization_s00158-023-03540-w.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/ArbitraryFunctionOptimization_s00158-023-03540-w.pdf)). Baut in `GenSub()` die beweglichen Asymptoten $L_i, U_i$ auf (`asyminit = 0.5, asymdec = 0.7, asyminc = 1.2`) und löst das konvexe Subproblem über einen schnellen Dual-Interior-Point-Löser (`SolveDIP`).
```cpp
// Aus TopOpt_in_PETSc/MMA.cc (Zeilen 499–518):
PetscErrorCode MMA::Update(Vec xval, Vec dfdx, PetscScalar* gx, Vec* dgdx, Vec xmin, Vec xmax) {
    // 1. Konvexes Approximations-Subproblem & Asymptoten L, U aufbauen:
    GenSub(xval, dfdx, gx, dgdx, xmin, xmax);
    // 2. Letzte zwei Design-Stände für Asymptoten-Trend sichern:
    VecCopy(xo1, xo2);
    VecCopy(xval, xo1);
    // 3. Duales Problem per Interior-Point-Methode lösen:
    SolveDIP(xval);
    return 0;
}
```

---

### Snippet 5: Borrvall-Petersson & RAMP Brinkman-Interpolation (aus [`OpenFOAM_base/.../BorrvallPeterssonInterpolation.C#L61-L91`](file:///G:/Datein/Projects/CFDSolver/OpenFOAM_base/openfoam/src/optimisation/adjointOptimisation/adjoint/optimisation/designVariables/topODesignVariables/interpolationFunctions/BorrvallPetersson/BorrvallPeterssonInterpolation.C#L61-L91))
* **Warum wichtig:** Schaltet in jeder Zelle stufenlos und differenzierbar zwischen Fluid und Festkörper um (inklusive exakter Ableitung für den Adjoint-Gradienten).
```cpp
// Aus BorrvallPeterssonInterpolation.C (Zeilen 73 & 88):
res   = arg / (scalar(1) + b * (scalar(1) - arg));
deriv = (b + scalar(1)) / sqr(scalar(1) + b * (scalar(1) - arg));
```

---

### Snippet 6: Mixed-Precision Iterative Refinement (`FP16`/`FP32` innen, `FP64` außen) (aus [`ginkgo/core/solver/ir.cpp#L216-L266`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LinearSolvers_HPC/ginkgo/core/solver/ir.cpp#L216-L266))
* **Warum wichtig:** Die Vorlage für unser **`PrecisionAddon::LowPrecisionBooster`**: Das Residuum $\mathbf{r} = \mathbf{b} - \mathbf{A}\mathbf{x}$ wird in voller `FP64`-Präzision berechnet, der innere Löser (`solver_->apply(residual_ptr, inner_solution)`) rechnet die Korrektur schnell in niedriger Präzision (`FP16`/`FP32`), und das Ergebnis wird auf die `FP64`-Lösung aufaddiert.
```cpp
// Aus ginkgo/core/solver/ir.cpp (Zeilen 248–262):
// 1. Äußeres Residuum r = b - A*x in FP64 aktualisieren (in update_residual)
// 2. Inneres Gleichungssystem A * inner_solution = residual in niedriger Präzision lösen:
inner_solution->fill(zero<ValueType>());
solver_->apply(residual_ptr, inner_solution);
// 3. Korrektur in FP64 aufaddieren: x = x + relaxation_factor * inner_solution
dense_x->add_scaled(relaxation_factor_, inner_solution);
```

---

### Snippet 7: Innen-/Außen-Test & Signed Distance für Millionen CFD-Zellen (aus [`libigl/include/igl/signed_distance.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CAD/libigl/include/igl/signed_distance.cpp) & [`fast_winding_number.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CAD/libigl/include/igl/fast_winding_number.cpp))
* **Warum wichtig:** Entscheidet über Hierarchische Winding-Numbers selbst bei nicht-wasserdichten CAD-Netzen in Millisekunden für jede Gitterzelle $\mathbf{x}_i$, wie weit sie von der Wand entfernt ist ($\psi_i$) und ob sie im Metall ($\gamma_i = 0$) oder im Fluid ($\gamma_i = 1$) liegt.
