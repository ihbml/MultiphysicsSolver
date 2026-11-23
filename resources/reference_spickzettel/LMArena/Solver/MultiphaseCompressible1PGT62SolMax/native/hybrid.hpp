#pragma once

#include "physics.hpp"
#include <array>
#include <span>
#include <vector>

namespace cb {

struct Mat {
    double a[3][3]{};
    CB_HD V apply(V v) const {
        V o{};
        for (int i=0;i<3;++i) for (int j=0;j<3;++j) o[i]+=a[i][j]*v[j];
        return o;
    }
    CB_HD V transposed(V v) const {
        V o{};
        for (int i=0;i<3;++i) for (int j=0;j<3;++j) o[i]+=a[j][i]*v[j];
        return o;
    }
    CB_HD void outer(V v, V w) {
        for (int i=0;i<3;++i) for (int j=0;j<3;++j) a[i][j]+=v[i]*w[j];
    }
    CB_HD bool inverse(Mat& result, int dim=3) const {
        double m[3][6]{};
        double scale=0.;
        result={};
        for (int i=0;i<dim;++i) for (int j=0;j<dim;++j) {
            m[i][j]=a[i][j]; m[i][j+dim]=(i==j); scale=max(scale,::fabs(a[i][j]));
        }
        for (int k=0;k<dim;++k) {
            int p=k;
            for (int i=k+1;i<dim;++i) if (::fabs(m[i][k])>::fabs(m[p][k])) p=i;
            if (!(::fabs(m[p][k])>1.e-10*scale)) return false;
            for (int j=0;j<2*dim;++j) { const auto v=m[k][j]; m[k][j]=m[p][j]; m[p][j]=v; }
            const double d=m[k][k];
            for (int j=0;j<2*dim;++j) m[k][j]/=d;
            for (int i=0;i<dim;++i) if (i!=k) {
                const double s=m[i][k];
                for (int j=0;j<2*dim;++j) m[i][j]-=s*m[k][j];
            }
        }
        for (int i=0;i<dim;++i) for (int j=0;j<dim;++j) result.a[i][j]=m[i][j+dim];
        return true;
    }
};

struct Ghost { V area{}, direction{}; double radial_times_volume{}; };
CB_HD inline Ghost complete_boundary(V inner_sum) {
    const V A=-inner_sum;
    const double magnitude=norm(A);
    return {A,magnitude>0 ? -A/magnitude : V{},-magnitude};
}
CB_HD inline V wendland_gradient(V neighbor_offset, double support, int dim) {
    const double q=norm(neighbor_offset)/support;
    if (!(q<1.)) return {};
    const double v=1.-q, pi=3.14159265358979323846;
    const double normalization=dim==3 ? 21./(2*pi*support*support*support) : 7./(pi*support*support);
    return neighbor_offset*(20*normalization*v*v*v/(support*support));
}

struct Sample { V x{}; double volume{}; };
struct Moment { Mat B{}; V inner{}; bool full_rank{}; };
inline Moment moment(V origin, std::span<const Sample> neighbors, double h, int dim=3) {
    Mat m{}; Moment result{};
    for (const auto& j:neighbors) {
        const V d=j.x-origin, g=wendland_gradient(d,h,dim)*j.volume;
        m.outer(d,g); result.inner+=g;
    }
    result.full_rank=m.inverse(result.B,dim);
    return result;
}

using Fields=std::array<double,8>;
inline Fields values(const Q& q) {
    return {q.rho,q.u.x,q.u.y,q.u.z,q.p,q.y,q.a,conserved(q).E};
}
inline Fields linear_transfer(V origin, V target, const Fields& anchor, std::span<const Sample> neighbors,
                              std::span<const Fields> state, double h, int dim=3) {
    const auto m=moment(origin,neighbors,h,dim);
    auto out=anchor;
    if (!m.full_rank || neighbors.size()!=state.size()) return out;
    const V d=target-origin;
    for (std::size_t j=0;j<neighbors.size();++j) {
        const auto& s=neighbors[j];
        // M^T maps an affine field's gradient to the raw SPH gradient.
        const V g=m.B.transposed(wendland_gradient(s.x-origin,h,dim)*s.volume);
        for (int k=0;k<8;++k) out[k]+=(state[j][k]-anchor[k])*dot(d,g);
    }
    return out;
}

} // namespace cb