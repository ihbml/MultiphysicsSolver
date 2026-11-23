// =====================================================================================
//  mt::Mesh.cpp -- polyhedral FVM mesh kernel implementation
//  FaceBaseSoA / CorrectionSoA / CellFaceCSR
// =====================================================================================
#include "mt/Mesh.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace mt {

// -------------------------------------------------------------------------------------
//  FaceBaseSoA
// -------------------------------------------------------------------------------------
void FaceBaseSoA::reserve(std::size_t n) {
    owner.reserve(n);       neighbour.reserve(n);
    faceCentre.reserve(n);  areaVector.reserve(n);
    area.reserve(n);        unitNormal.reserve(n);
    bcId.reserve(n);        distance.reserve(n); invDistance.reserve(n);
}

// -------------------------------------------------------------------------------------
//  CorrectionSoA
// -------------------------------------------------------------------------------------
void CorrectionSoA::resize(int n) {
    nonOrthogonality.assign(static_cast<std::size_t>(n), 0.0);
    skewness.assign(static_cast<std::size_t>(n), 0.0);
    centroidWeight.assign(static_cast<std::size_t>(n), 0.0);
    highOrderCoeff.clear();
    highOrderFace.clear();
}

void CorrectionSoA::clearHighOrder() {
    highOrderCoeff.clear();
    highOrderFace.clear();
}

std::size_t CorrectionSoA::bytes() const {
    return nonOrthogonality.size() * sizeof(double) + skewness.size() * sizeof(double)
         + centroidWeight.size() * sizeof(double) + highOrderCoeff.size() * sizeof(double)
         + highOrderFace.size() * sizeof(int);
}

double CorrectionSoA::overheadPercent(const FaceBaseSoA& base) const {
    std::size_t baseBytes = base.owner.size() * sizeof(int) * 2
                          + base.faceCentre.size() * sizeof(Vec3)
                          + base.areaVector.size() * sizeof(Vec3)
                          + base.unitNormal.size() * sizeof(Vec3)
                          + base.area.size() * sizeof(double)
                          + base.distance.size() * sizeof(double) * 2
                          + base.bcId.size() * sizeof(int);
    if (baseBytes == 0) return 0.0;
    return 100.0 * static_cast<double>(bytes()) / static_cast<double>(baseBytes);
}

// -------------------------------------------------------------------------------------
//  CellFaceCSR
// -------------------------------------------------------------------------------------
void CellFaceCSR::clear() {
    rowStart.clear(); faceIndex.clear(); weight.clear();
}

void CellFaceCSR::build(const FaceBaseSoA& faces, int nCells) {
    clear();
    rowStart.assign(static_cast<std::size_t>(nCells) + 1, 0);
    const int nf = faces.size();
    for (int f = 0; f < nf; ++f) {
        const int o = faces.owner[f];
        if (o >= 0 && o < nCells) ++rowStart[static_cast<std::size_t>(o) + 1];
        const int nb = faces.neighbour[f];
        if (nb >= 0 && nb < nCells) ++rowStart[static_cast<std::size_t>(nb) + 1];
    }
    for (int c = 0; c < nCells; ++c) rowStart[static_cast<std::size_t>(c) + 1] += rowStart[static_cast<std::size_t>(c)];
    faceIndex.assign(static_cast<std::size_t>(rowStart[static_cast<std::size_t>(nCells)]), -1);
    weight.assign(faceIndex.size(), 0.0);
    std::vector<int> cursor(rowStart.begin(), rowStart.end() - 1);
    for (int f = 0; f < nf; ++f) {
        const int o = faces.owner[f];
        const int nb = faces.neighbour[f];
        if (o >= 0 && o < nCells) {
            const int e = cursor[static_cast<std::size_t>(o)]++;
            faceIndex[static_cast<std::size_t>(e)] = f;
            weight[static_cast<std::size_t>(e)] = faces.invDistance[static_cast<std::size_t>(f)];
        }
        if (nb >= 0 && nb < nCells) {
            const int e = cursor[static_cast<std::size_t>(nb)]++;
            faceIndex[static_cast<std::size_t>(e)] = f;
            weight[static_cast<std::size_t>(e)] = faces.invDistance[static_cast<std::size_t>(f)];
        }
    }
}

// -------------------------------------------------------------------------------------
//  Mat3 helpers (declared in Types.hpp)
// -------------------------------------------------------------------------------------
Mat3 Mat3::rotation(const Vec3& axis, double angleRad) {
    const Vec3 a = unit(axis);
    const double c = std::cos(angleRad), s = std::sin(angleRad), t = 1.0 - c;
    Mat3 r;
    r(0, 0) = t * a.x * a.x + c;       r(0, 1) = t * a.x * a.y - s * a.z; r(0, 2) = t * a.x * a.z + s * a.y;
    r(1, 0) = t * a.x * a.y + s * a.z; r(1, 1) = t * a.y * a.y + c;       r(1, 2) = t * a.y * a.z - s * a.x;
    r(2, 0) = t * a.x * a.z - s * a.y; r(2, 1) = t * a.y * a.z + s * a.x; r(2, 2) = t * a.z * a.z + c;
    return r;
}

Mat3 Mat3::transpose() const {
    Mat3 r = Mat3::zero();
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) r(i, j) = (*this)(j, i);
    return r;
}

Mat3 Mat3::operator*(const Mat3& o) const {
    Mat3 r = Mat3::zero();
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            double v = 0.0;
            for (int k = 0; k < 3; ++k) v += (*this)(i, k) * o(k, j);
            r(i, j) = v;
        }
    return r;
}

Mat3 Mat3::operator*(double s) const {
    Mat3 r = Mat3::zero();
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) r(i, j) = (*this)(i, j) * s;
    return r;
}

Vec3 Mat3::operator*(const Vec3& v) const {
    return {(*this)(0, 0) * v.x + (*this)(0, 1) * v.y + (*this)(0, 2) * v.z,
            (*this)(1, 0) * v.x + (*this)(1, 1) * v.y + (*this)(1, 2) * v.z,
            (*this)(2, 0) * v.x + (*this)(2, 1) * v.y + (*this)(2, 2) * v.z};
}

double Mat3::det() const {
    return (*this)(0, 0) * ((*this)(1, 1) * (*this)(2, 2) - (*this)(1, 2) * (*this)(2, 1))
         - (*this)(0, 1) * ((*this)(1, 0) * (*this)(2, 2) - (*this)(1, 2) * (*this)(2, 0))
         + (*this)(0, 2) * ((*this)(1, 0) * (*this)(2, 1) - (*this)(1, 1) * (*this)(2, 0));
}

// -------------------------------------------------------------------------------------
//  Mesh
// -------------------------------------------------------------------------------------
int Mesh::addCell(const Vec3& centre, double volume, int region, int zone) {
    const int id = nCells++;
    cellCentre.push_back(centre);
    cellVolume.push_back(volume);
    cellRegion.push_back(region);
    cellZone.push_back(zone);
    return id;
}

int Mesh::addFace(int owner, int neighbour, const Vec3& centre, const Vec3& areaVector, int bcId) {
    const int f = faces.size();
    faces.owner.push_back(owner);
    faces.neighbour.push_back(neighbour);
    faces.faceCentre.push_back(centre);
    faces.areaVector.push_back(areaVector);
    faces.area.push_back(mag(areaVector));
    faces.unitNormal.push_back(mag(areaVector) > kSmall ? areaVector / mag(areaVector) : Vec3{0, 0, 0});
    faces.bcId.push_back(bcId);
    faces.distance.push_back(0.0);
    faces.invDistance.push_back(0.0);
    return f;
}

int Mesh::addBoundaryCondition(const BoundaryCondition& bc) {
    bcs.push_back(bc);
    const int id = static_cast<int>(bcs.size()) - 1;
    bcs.back().id = id;
    return id;
}

void Mesh::computeGeometry() {
    const int nf = faces.size();
    // ---- face distances / non-orthogonality / skewness
    corr.resize(nf);
    for (int f = 0; f < nf; ++f) {
        const int o = faces.owner[f];
        const int nb = faces.neighbour[f];
        if (nb >= 0) {
            const Vec3& xp = cellCentre[static_cast<std::size_t>(o)];
            const Vec3& xn = cellCentre[static_cast<std::size_t>(nb)];
            const Vec3 dvec = xn - xp;
            const double d = mag(dvec);
            faces.distance[static_cast<std::size_t>(f)] = d;
            faces.invDistance[static_cast<std::size_t>(f)] = d > kSmall ? 1.0 / d : 0.0;
            const double cosTheta = d > kSmall ? std::abs(dot(faces.unitNormal[static_cast<std::size_t>(f)], dvec / d)) : 0.0;
            corr.nonOrthogonality[static_cast<std::size_t>(f)] = clampd(cosTheta, 0.0, 1.0);
        } else {
            const Vec3& xp = cellCentre[static_cast<std::size_t>(o)];
            const Vec3 dvec = faces.faceCentre[static_cast<std::size_t>(f)] - xp;
            const double d = mag(dvec);
            faces.distance[static_cast<std::size_t>(f)] = d;
            faces.invDistance[static_cast<std::size_t>(f)] = d > kSmall ? 1.0 / d : 0.0;
            corr.nonOrthogonality[static_cast<std::size_t>(f)] = 1.0;
        }
        const Vec3& xf = faces.faceCentre[static_cast<std::size_t>(f)];
        const Vec3& xp = cellCentre[static_cast<std::size_t>(o)];
        corr.skewness[static_cast<std::size_t>(f)] = dot(xf - xp, faces.unitNormal[static_cast<std::size_t>(f)]);
        corr.centroidWeight[static_cast<std::size_t>(f)] = 0.5;
    }
    // ---- cell -> face CSR (atomic free gather)
    cellFaces.build(faces, nCells);
    // ---- cell volumes from the polyhedral divergence theorem
    //      V_P = 1/3 sum_f  A_f (x_f - x_P) . n_f   (n_f oriented outwards from P)
    std::fill(cellVolume.begin(), cellVolume.end(), 0.0);
    for (int c = 0; c < nCells; ++c) {
        double v = 0.0;
        const Vec3& xp = cellCentre[static_cast<std::size_t>(c)];
        for (int e = cellFaces.begin(c); e < cellFaces.end(c); ++e) {
            const int f = cellFaces.faceIndex[static_cast<std::size_t>(e)];
            const bool owned = (faces.owner[static_cast<std::size_t>(f)] == c);
            const Vec3 s = owned ? faces.areaVector[static_cast<std::size_t>(f)]
                                 : (-faces.areaVector[static_cast<std::size_t>(f)]);
            v += (1.0 / 3.0) * dot(s, faces.faceCentre[static_cast<std::size_t>(f)] - xp);
        }
        cellVolume[static_cast<std::size_t>(c)] = v;
    }
    // ---- wall distances
    computeWallDistances();
}

void Mesh::computeWallDistances() {
    wallDistance.assign(static_cast<std::size_t>(nCells), 1.0e30);
    for (int f = 0; f < faces.size(); ++f) {
        const int bc = faces.bcId[static_cast<std::size_t>(f)];
        if (bc < 0) continue;
        if (bcs[static_cast<std::size_t>(bc)].type != BCType::Wall) continue;
        const int o = faces.owner[static_cast<std::size_t>(f)];
        const Vec3& xf = faces.faceCentre[static_cast<std::size_t>(f)];
        const Vec3& xp = cellCentre[static_cast<std::size_t>(o)];
        const double dist = std::abs(dot(xp - xf, faces.unitNormal[static_cast<std::size_t>(f)]));
        wallDistance[static_cast<std::size_t>(o)] = std::min(wallDistance[static_cast<std::size_t>(o)], dist);
    }
    for (auto& d : wallDistance) if (d > 1.0e29) d = 1.0e3;
}

double Mesh::cellSize(int c) const {
    const double v = cellVolume[static_cast<std::size_t>(c)];
    return v > kSmall ? std::cbrt(v) : 1.0;
}

int Mesh::nearestWallFace(int cell) const {
    int best = -1;
    double bestDist = 1.0e30;
    const int e0 = cellFaces.begin(cell), e1 = cellFaces.end(cell);
    for (int e = e0; e < e1; ++e) {
        const int f = cellFaces.faceIndex[static_cast<std::size_t>(e)];
        const int bc = faces.bcId[static_cast<std::size_t>(f)];
        if (bc < 0 || bcs[static_cast<std::size_t>(bc)].type != BCType::Wall) continue;
        const double d = faces.distance[static_cast<std::size_t>(f)];
        if (d < bestDist) { bestDist = d; best = f; }
    }
    return best;
}

bool Mesh::hasCHTInterface() const {
    for (const auto& bc : bcs) if (bc.type == BCType::InterfaceCHT) return true;
    return false;
}

// -------------------------------------------------------------------------------------
//  Least-squares cell gradient reconstruction (robust on polyhedral meshes).
//
//  For every face of the cell the linear face value phi_f is assembled and the
//  overdetermined system   sum_f w_f (x_f-x_P)(x_f-x_P)^T grad(phi) = sum_f w_f (x_f-x_P)(phi_f-phi_P)
//  is solved with w_f = 1/|x_f-x_P|^2.  Boundary faces use the prescribed boundary datum
//  when available, which restores second order accuracy at the walls.
// -------------------------------------------------------------------------------------
namespace {

bool solve3x3(double M[3][4], Vec3& x) {
    for (int col = 0; col < 3; ++col) {
        int piv = col;
        for (int r = col + 1; r < 3; ++r)
            if (std::abs(M[r][col]) > std::abs(M[piv][col])) piv = r;
        if (std::abs(M[piv][col]) < 1e-30) return false;
        if (piv != col) for (int k = 0; k < 4; ++k) std::swap(M[col][k], M[piv][k]);
        for (int r = 0; r < 3; ++r) {
            if (r == col) continue;
            const double f = M[r][col] / M[col][col];
            for (int k = col; k < 4; ++k) M[r][k] -= f * M[col][k];
        }
    }
    x = Vec3{M[0][3], M[1][3], M[2][3]};
    return true;
}

} // namespace

std::vector<Vec3> Mesh::cellGradient(const std::vector<double>& phi) const {
    std::vector<Vec3> grad(static_cast<std::size_t>(nCells), Vec3{0, 0, 0});
    for (int c = 0; c < nCells; ++c) {
        const Vec3& xp = cellCentre[static_cast<std::size_t>(c)];
        double M[3][4] = {{0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}};
        for (int e = cellFaces.begin(c); e < cellFaces.end(c); ++e) {
            const int f = cellFaces.faceIndex[static_cast<std::size_t>(e)];
            const Vec3 d = faces.faceCentre[static_cast<std::size_t>(f)] - xp;
            const double len2 = mag2(d);
            if (len2 < 1e-24) continue;
            const int nb = faces.neighbour[static_cast<std::size_t>(f)];
            double phif = phi[static_cast<std::size_t>(c)];
            if (nb >= 0) {
                const Vec3 dpn = cellCentre[static_cast<std::size_t>(nb)] - xp;
                const double t = dot(d, dpn) / std::max(mag2(dpn), 1e-24);
                phif = phi[static_cast<std::size_t>(c)] + t * (phi[static_cast<std::size_t>(nb)] - phi[static_cast<std::size_t>(c)]);
            } else {
                const int bc = faces.bcId[static_cast<std::size_t>(f)];
                if (bc >= 0) {
                    const BoundaryCondition& bcv = bcs[static_cast<std::size_t>(bc)];
                    if (bcv.type == BCType::Wall && bcv.specifiedTemperature) phif = bcv.value;
                    else if (bcv.type == BCType::Inlet) phif = bcv.value;
                }
            }
            const double w = 1.0 / len2;
            const Vec3 rhs = d * ((phif - phi[static_cast<std::size_t>(c)]) / len2);
            M[0][0] += w * d.x * d.x; M[0][1] += w * d.x * d.y; M[0][2] += w * d.x * d.z;
            M[1][0] += w * d.y * d.x; M[1][1] += w * d.y * d.y; M[1][2] += w * d.y * d.z;
            M[2][0] += w * d.z * d.x; M[2][1] += w * d.z * d.y; M[2][2] += w * d.z * d.z;
            M[0][3] += rhs.x; M[1][3] += rhs.y; M[2][3] += rhs.z;
        }
        Vec3 g;
        if (solve3x3(M, g)) grad[static_cast<std::size_t>(c)] = g;
    }
    return grad;
}

std::vector<Vec3> Mesh::cellGradient(const std::vector<Vec3>& phi) const {
    std::vector<Vec3> grad(static_cast<std::size_t>(nCells), Vec3{0, 0, 0});
    for (int f = 0; f < faces.size(); ++f) {
        const int o = faces.owner[f];
        const int nb = faces.neighbour[f];
        Vec3 phiFace = phi[static_cast<std::size_t>(o)];
        if (nb >= 0) {
            const double wp = faces.invDistance[static_cast<std::size_t>(f)];
            phiFace = phi[static_cast<std::size_t>(o)] + wp * (phi[static_cast<std::size_t>(nb)] - phi[static_cast<std::size_t>(o)]);
        }
        const Vec3& s = faces.areaVector[static_cast<std::size_t>(f)];
        grad[static_cast<std::size_t>(o)] += s * (phiFace - phi[static_cast<std::size_t>(o)]);
        if (nb >= 0) grad[static_cast<std::size_t>(nb)] -= s * (phiFace - phi[static_cast<std::size_t>(nb)]);
    }
    for (int c = 0; c < nCells; ++c) {
        const double v = cellVolume[static_cast<std::size_t>(c)];
        if (v > kSmall) grad[static_cast<std::size_t>(c)] = grad[static_cast<std::size_t>(c)] / v;
    }
    return grad;
}

double Mesh::totalVolume() const {
    double v = 0.0;
    for (double x : cellVolume) v += x;
    return v;
}

double Mesh::totalArea() const {
    double a = 0.0;
    for (double x : faces.area) a += x;
    return a;
}

// -------------------------------------------------------------------------------------
//  Hilbert curve ordering (4-quadrant recursive Hilbert index on a 2^m grid)
// -------------------------------------------------------------------------------------
namespace {

//  Classic 4-quadrant Hilbert curve index (Wikipedia "xy2d"), generalised to 3-D by
//  using the third coordinate as the slowest-varying (interleaved) dimension.
long long hilbertXY2D(long long n, long long x, long long y) {
    long long d = 0;
    for (long long s = n / 2; s > 0; s /= 2) {
        const long long rx = (x & s) > 0 ? 1 : 0;
        const long long ry = (y & s) > 0 ? 1 : 0;
        d += s * s * ((3 * rx) ^ ry);
        if (ry == 0) {
            if (rx == 1) { x = s - 1 - x; y = s - 1 - y; }
            else { const long long t = x; x = y; y = t; }
        }
    }
    return d;
}

} // namespace

std::vector<int> Mesh::hilbertOrder() const {
    std::vector<int> order(static_cast<std::size_t>(nCells));
    std::iota(order.begin(), order.end(), 0);
    if (nCells == 0) return order;

    Vec3 lo{1e30, 1e30, 1e30}, hi{-1e30, -1e30, -1e30};
    for (const auto& c : cellCentre) {
        lo.x = std::min(lo.x, c.x); lo.y = std::min(lo.y, c.y); lo.z = std::min(lo.z, c.z);
        hi.x = std::max(hi.x, c.x); hi.y = std::max(hi.y, c.y); hi.z = std::max(hi.z, c.z);
    }
    const int n = 32;   // 2^5 resolution
    const Vec3 extent{std::max(hi.x - lo.x, 1e-12), std::max(hi.y - lo.y, 1e-12), std::max(hi.z - lo.z, 1e-12)};
    std::vector<std::pair<long long, int>> keyed(static_cast<std::size_t>(nCells));
    for (int c = 0; c < nCells; ++c) {
        const Vec3& p = cellCentre[static_cast<std::size_t>(c)];
        const int ix = clampd(static_cast<int>((p.x - lo.x) / extent.x * (n - 1)), 0, n - 1);
        const int iy = clampd(static_cast<int>((p.y - lo.y) / extent.y * (n - 1)), 0, n - 1);
        const int iz = clampd(static_cast<int>((p.z - lo.z) / extent.z * (n - 1)), 0, n - 1);
        const long long h = hilbertXY2D(n, ix, iy) * static_cast<long long>(n) + iz;
        keyed[static_cast<std::size_t>(c)] = {h, c};
    }
    std::stable_sort(keyed.begin(), keyed.end(),
                     [](const auto& a, const auto& b) { return a.first < b.first; });
    for (std::size_t i = 0; i < keyed.size(); ++i) order[i] = keyed[i].second;
    return order;
}

// -------------------------------------------------------------------------------------
//  MeshBuilder
// -------------------------------------------------------------------------------------
void MeshBuilder::buildSlab(Mesh& mesh, int nCells, double y0, double y1,
                            double lx, double lz, BCType bcLow, BCType bcHigh, int region) {
    BoundaryCondition bcWall;
    bcWall.type = BCType::Wall;
    bcWall.name = "slab-low";
    const int idLow = mesh.addBoundaryCondition(bcWall);
    BoundaryCondition bcTop = bcWall;
    bcTop.name = "slab-high";
    const int idHigh = mesh.addBoundaryCondition(bcTop);
    BoundaryCondition bcSym;
    bcSym.type = BCType::Symmetry;
    bcSym.name = "slab-sym";
    const int idSym = mesh.addBoundaryCondition(bcSym);

    const double dy = (y1 - y0) / nCells;
    for (int c = 0; c < nCells; ++c) {
        const Vec3 centre{0.5 * lx, y0 + (c + 0.5) * dy, 0.5 * lz};
        mesh.addCell(centre, lx * lz * dy, region, 0);
    }
    for (int c = 0; c + 1 < nCells; ++c) {
        const Vec3 centre{0.5 * lx, y0 + (c + 1) * dy, 0.5 * lz};
        const Vec3 areaVec{0.0, lx * lz, 0.0};
        mesh.addFace(c, c + 1, centre, areaVec, -1);
    }
    // y = y0 wall
    mesh.addFace(0, -1, Vec3{0.5 * lx, y0, 0.5 * lz}, Vec3{0.0, -lx * lz, 0.0}, idLow);
    // y = y1 wall
    mesh.addFace(nCells - 1, -1, Vec3{0.5 * lx, y1, 0.5 * lz}, Vec3{0.0, lx * lz, 0.0}, idHigh);
    // x symmetry
    for (int c = 0; c < nCells; ++c) {
        const double yc = y0 + (c + 0.5) * dy;
        mesh.addFace(c, -1, Vec3{0.0, yc, 0.5 * lz}, Vec3{-lz * dy, 0.0, 0.0}, idSym);
        mesh.addFace(c, -1, Vec3{lx, yc, 0.5 * lz}, Vec3{lz * dy, 0.0, 0.0}, idSym);
        mesh.addFace(c, -1, Vec3{0.5 * lx, yc, 0.0}, Vec3{0.0, 0.0, -lx * dy}, idSym);
        mesh.addFace(c, -1, Vec3{0.5 * lx, yc, lz}, Vec3{0.0, 0.0, lx * dy}, idSym);
    }
    mesh.computeGeometry();
}

void MeshBuilder::buildChannel(Mesh& mesh, int nCells, double halfHeight, double lx, double lz,
                               const std::vector<double>& yCoordinates) {
    BoundaryCondition bcWall;
    bcWall.type = BCType::Wall;
    bcWall.name = "channel-wall";
    const int idWall = mesh.addBoundaryCondition(bcWall);
    BoundaryCondition bcSym;
    bcSym.type = BCType::Symmetry;
    bcSym.name = "channel-symmetry";
    const int idSym = mesh.addBoundaryCondition(bcSym);

    std::vector<double> yc(static_cast<std::size_t>(nCells));
    for (int c = 0; c < nCells; ++c) {
        const double dy = yCoordinates[static_cast<std::size_t>(c + 1)] - yCoordinates[static_cast<std::size_t>(c)];
        yc[static_cast<std::size_t>(c)] = 0.5 * (yCoordinates[static_cast<std::size_t>(c)] + yCoordinates[static_cast<std::size_t>(c + 1)]);
        mesh.addCell(Vec3{0.5 * lx, yc[static_cast<std::size_t>(c)], 0.5 * lz}, lx * lz * dy, 0, 0);
    }
    for (int c = 0; c + 1 < nCells; ++c) {
        const double yf = yCoordinates[static_cast<std::size_t>(c + 1)];
        mesh.addFace(c, c + 1, Vec3{0.5 * lx, yf, 0.5 * lz}, Vec3{0.0, lx * lz, 0.0}, -1);
    }
    mesh.addFace(0, -1, Vec3{0.5 * lx, 0.0, 0.5 * lz}, Vec3{0.0, -lx * lz, 0.0}, idWall);
    mesh.addFace(nCells - 1, -1, Vec3{0.5 * lx, 2.0 * halfHeight, 0.5 * lz}, Vec3{0.0, lx * lz, 0.0}, idWall);
    for (int c = 0; c < nCells; ++c) {
        const double dy = yCoordinates[static_cast<std::size_t>(c + 1)] - yCoordinates[static_cast<std::size_t>(c)];
        const double ycc = yc[static_cast<std::size_t>(c)];
        mesh.addFace(c, -1, Vec3{0.0, ycc, 0.5 * lz}, Vec3{-lz * dy, 0.0, 0.0}, idSym);
        mesh.addFace(c, -1, Vec3{lx, ycc, 0.5 * lz}, Vec3{lz * dy, 0.0, 0.0}, idSym);
        mesh.addFace(c, -1, Vec3{0.5 * lx, ycc, 0.0}, Vec3{0.0, 0.0, -lx * dy}, idSym);
        mesh.addFace(c, -1, Vec3{0.5 * lx, ycc, lz}, Vec3{0.0, 0.0, lx * dy}, idSym);
    }
    mesh.computeGeometry();
}

void MeshBuilder::buildBlock(Mesh& mesh, int nx, int ny, int nz,
                              const Vec3& origin, const Vec3& dx, const Vec3& dy, const Vec3& dz) {
    BoundaryCondition bcWall;
    bcWall.type = BCType::Wall;
    bcWall.name = "block-wall";
    const int idWall = mesh.addBoundaryCondition(bcWall);

    const double vx = dx.x / nx, vy = dy.y / ny, vz = dz.z / nz;
    auto cellId = [nx, ny](int i, int j, int k) { return i + nx * (j + ny * k); };
    for (int k = 0; k < nz; ++k)
        for (int j = 0; j < ny; ++j)
            for (int i = 0; i < nx; ++i) {
                const Vec3 c{origin.x + (i + 0.5) * vx, origin.y + (j + 0.5) * vy, origin.z + (k + 0.5) * vz};
                mesh.addCell(c, vx * vy * vz, 0, 0);
            }
    auto faceAreaX = vy * vz, faceAreaY = vx * vz, faceAreaZ = vx * vy;
    for (int k = 0; k < nz; ++k)
        for (int j = 0; j < ny; ++j)
            for (int i = 0; i < nx; ++i) {
                if (i + 1 < nx)
                    mesh.addFace(cellId(i, j, k), cellId(i + 1, j, k),
                                 Vec3{origin.x + (i + 1) * vx, origin.y + (j + 0.5) * vy, origin.z + (k + 0.5) * vz},
                                 Vec3{faceAreaX, 0, 0}, -1);
                else
                    mesh.addFace(cellId(i, j, k), -1,
                                 Vec3{origin.x + (i + 1) * vx, origin.y + (j + 0.5) * vy, origin.z + (k + 0.5) * vz},
                                 Vec3{faceAreaX, 0, 0}, idWall);
                if (i > 0)
                    mesh.addFace(cellId(i, j, k), cellId(i - 1, j, k),
                                 Vec3{origin.x + i * vx, origin.y + (j + 0.5) * vy, origin.z + (k + 0.5) * vz},
                                 Vec3{-faceAreaX, 0, 0}, -1);
                if (j + 1 < ny)
                    mesh.addFace(cellId(i, j, k), cellId(i, j + 1, k),
                                 Vec3{origin.x + (i + 0.5) * vx, origin.y + (j + 1) * vy, origin.z + (k + 0.5) * vz},
                                 Vec3{0, faceAreaY, 0}, -1);
                else
                    mesh.addFace(cellId(i, j, k), -1,
                                 Vec3{origin.x + (i + 0.5) * vx, origin.y + (j + 1) * vy, origin.z + (k + 0.5) * vz},
                                 Vec3{0, faceAreaY, 0}, idWall);
                if (j > 0)
                    mesh.addFace(cellId(i, j, k), cellId(i, j - 1, k),
                                 Vec3{origin.x + (i + 0.5) * vx, origin.y + j * vy, origin.z + (k + 0.5) * vz},
                                 Vec3{0, -faceAreaY, 0}, -1);
                if (k + 1 < nz)
                    mesh.addFace(cellId(i, j, k), cellId(i, j, k + 1),
                                 Vec3{origin.x + (i + 0.5) * vx, origin.y + (j + 0.5) * vy, origin.z + (k + 1) * vz},
                                 Vec3{0, 0, faceAreaZ}, -1);
                else
                    mesh.addFace(cellId(i, j, k), -1,
                                 Vec3{origin.x + (i + 0.5) * vx, origin.y + (j + 0.5) * vy, origin.z + (k + 1) * vz},
                                 Vec3{0, 0, faceAreaZ}, idWall);
                if (k > 0)
                    mesh.addFace(cellId(i, j, k), cellId(i, j, k - 1),
                                 Vec3{origin.x + (i + 0.5) * vx, origin.y + (j + 0.5) * vy, origin.z + k * vz},
                                 Vec3{0, 0, -faceAreaZ}, -1);
            }
    mesh.computeGeometry();
}

} // namespace mt
