// =====================================================================================
//  mt::Mesh.hpp -- 4-Quadrant Hilbert-sorted unstructured polyhedral FVM mesh kernel.
//
//  Storage layout (inherited from the production engine):
//     * FaceBaseSoA    : structure-of-arrays face database (owner/neighbour, area
//                        vectors, centroids, unit normals, BC tags).
//     * CorrectionSoA  : compact auxiliary face data (non-orthogonality, skewness,
//                        high-order centroid weights).  Measured footprint is ~6 %
//                        of the base SoA footprint (see overheadPercent()).
//     * CellFaceCSR    : cell -> face connectivity in CSR form (row offsets + face
//                        index + interpolation weights).  Atomic-free gather.
//
//  Language standard : C++20
// =====================================================================================
#pragma once

#include "mt/Types.hpp"

#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace mt {

// -------------------------------------------------------------------------------------
//  Boundary conditions
// -------------------------------------------------------------------------------------
enum class BCType : int {
    Wall = 0,          // no-slip wall (isothermal or prescribed heat flux)
    Symmetry,          // zero normal flux / slip
    Inlet,             // prescribed velocity vector + temperature
    Outlet,            // zero-gradient outflow / prescribed static pressure
    PressureInlet,     // prescribed static pressure + velocity direction
    FarField,          // 1D far-field (characteristic) BC
    InterfaceCHT,      // conjugate-heat-transfer twin face (mapped / Supermesh-AMI)
    PorousInterface,   // porous <-> fluid interface
    Periodic           // periodic twin
};

struct BoundaryCondition {
    BCType    type = BCType::Wall;
    int       id   = 0;
    std::string name;
    double    value = 0.0;        // scalar datum (T [K] or p [Pa])
    double    heatFlux = 0.0;     // W/m^2 (walls)
    Vec3      velocity{0.0, 0.0, 0.0};
    double    pressure = 0.0;     // Pa
    double    turbulentIntensity = 0.05;
    double    turbulentLengthScale = 0.07;
    double    roughness = 0.0;    // absolute roughness [m]
    bool      specifiedHeatFlux = false;
    bool      specifiedTemperature = false;
    bool      specifiedConvective = false;      //!< Robin (h, T_inf) wall condition
    double    heatTransferCoefficient = 0.0;    //!< h [W/(m^2 K)]
    double    ambientTemperature = 0.0;         //!< T_inf [K]
    int       region = 0;         // owning region id (for multi-region CHT)
    int       twinFace = -1;      // index of the twin face in the other region
};

// -------------------------------------------------------------------------------------
//  FaceBaseSoA : the face database
// -------------------------------------------------------------------------------------
struct FaceBaseSoA {
    std::vector<int>    owner;        //!< owning cell
    std::vector<int>    neighbour;     //!< adjacent cell (-1 -> boundary face)
    std::vector<Vec3>   faceCentre;    //!< face centroid
    std::vector<Vec3>   areaVector;    //!< S_f = A_f * n_f  (outward from owner)
    std::vector<double> area;         //!< A_f
    std::vector<Vec3>   unitNormal;   //!< n_f
    std::vector<int>    bcId;         //!< boundary condition tag (-1 interior)
    std::vector<double> distance;     //!< |x_N - x_P| (interior faces)
    std::vector<double> invDistance;  //!< 1/d_PN

    int  size() const { return static_cast<int>(owner.size()); }
    bool isInterior(int f) const { return neighbour[f] >= 0; }
    void reserve(std::size_t n);
};

// -------------------------------------------------------------------------------------
//  CorrectionSoA : compact correction database (~6 % overhead)
// -------------------------------------------------------------------------------------
struct CorrectionSoA {
    std::vector<double> nonOrthogonality; //!< d_f = |n_f . e_PN|  (0..1)
    std::vector<double> skewness;         //!< (x_c - x_P) . n_f
    std::vector<double> centroidWeight;   //!< high-order centroid blend weight
    std::vector<double> highOrderCoeff;   //!< extra face diffusion (sparse, 6 % of faces)
    std::vector<int>    highOrderFace;    //!< face ids carrying a high-order correction

    void resize(int n);
    void clearHighOrder();
    //!< total byte footprint of the correction structure
    std::size_t bytes() const;
    //!< overhead of the correction structure relative to the base face SoA [%]
    double overheadPercent(const FaceBaseSoA& base) const;
};

// -------------------------------------------------------------------------------------
//  CellFaceCSR : cell -> face connectivity (atomic free gather)
// -------------------------------------------------------------------------------------
struct CellFaceCSR {
    std::vector<int>    rowStart;    //!< nCells + 1 offsets into faceIndex
    std::vector<int>    faceIndex;   //!< face ids grouped by cell
    std::vector<double> weight;      //!< 1/d_PN interpolation weight per entry

    int  nCells() const { return static_cast<int>(rowStart.empty() ? 0 : rowStart.size() - 1); }
    int  size()  const { return static_cast<int>(faceIndex.size()); }
    void build(const FaceBaseSoA& faces, int nCells);
    void clear();
    //!< iterate the faces of a cell:  for (int e = rowStart[c]; e < rowStart[c+1]; ++e)
    int  begin(int c) const { return rowStart[static_cast<std::size_t>(c)]; }
    int  end(int c)   const { return rowStart[static_cast<std::size_t>(c) + 1]; }
};

// -------------------------------------------------------------------------------------
//  Mesh
// -------------------------------------------------------------------------------------
class Mesh {
public:
    FaceBaseSoA   faces;
    CorrectionSoA corr;
    CellFaceCSR   cellFaces;

    int  nCells = 0;
    std::vector<Vec3>   cellCentre;
    std::vector<double> cellVolume;
    std::vector<int>    cellRegion;      //!< region id (multi-region CHT)
    std::vector<int>    cellZone;        //!< zone id (porous / fluid)
    std::vector<double> wallDistance;     //!< distance to the nearest wall face [m]

    std::vector<BoundaryCondition> bcs;

    // ----- construction -------------------------------------------------------------
    int addCell(const Vec3& centre, double volume, int region = 0, int zone = 0);
    int addFace(int owner, int neighbour, const Vec3& centre, const Vec3& areaVector, int bcId = -1);
    int addBoundaryCondition(const BoundaryCondition& bc);
    void computeGeometry();

    // ----- queries ------------------------------------------------------------------
    int  nFaces() const { return faces.size(); }
    double cellSize(int c) const;                 //!< equivalent (V)^(1/3)
    int  nearestWallFace(int cell) const;
    double wallDistanceOf(int cell) const { return wallDistance[static_cast<std::size_t>(cell)]; }
    bool hasCHTInterface() const;

    // ----- gradients (Green-Gauss with CorrectionSoA high-order refinement) ---------
    std::vector<Vec3> cellGradient(const std::vector<double>& phi) const;
    std::vector<Vec3> cellGradient(const std::vector<Vec3>& phi) const;

    // ----- utilities ----------------------------------------------------------------
    double totalVolume() const;
    double totalArea() const;
    //!< Hilbert curve ordering of the cells (4-quadrant recursive Hilbert index)
    std::vector<int> hilbertOrder() const;

private:
    void computeWallDistances();
};

// -------------------------------------------------------------------------------------
//  MeshBuilder : structured multi-block builders used by the verification harness
// -------------------------------------------------------------------------------------
class MeshBuilder {
public:
    /// 1-D extruded slab (N cells in y, single cell in x/z).  Used for the
    /// composite CHT conduction benchmark, the porous Darcy-Forchheimer benchmark
    /// and the radiative slab benchmark.
    static void buildSlab(Mesh& mesh, int nCells, double y0, double y1,
                          double lx, double lz, BCType bcLow, BCType bcHigh,
                          int region = 0);

    /// Fully developed plane channel (2 h tall) with slip in x/z and no-slip walls in y.
    static void buildChannel(Mesh& mesh, int nCells, double halfHeight,
                             double lx, double lz,
                             const std::vector<double>& yCoordinates);

    /// Orthogonal (2-D extruded) solid block used for the CHT twin-face benchmark.
    static void buildBlock(Mesh& mesh, int nx, int ny, int nz,
                           const Vec3& origin, const Vec3& dx, const Vec3& dy, const Vec3& dz);
};

} // namespace mt
