#pragma once

#include <cmath>

#if defined(__CUDACC__)
#define CB_HD __host__ __device__
#else
#define CB_HD
#endif

namespace cb {

CB_HD inline double min(double a, double b) { return a < b ? a : b; }
CB_HD inline double max(double a, double b) { return a > b ? a : b; }
CB_HD inline double clip(double a, double l, double h) { return min(max(a,l),h); }
CB_HD inline double sq(double a) { return a*a; }
CB_HD inline bool finite(double a) { return a >= -1.e290 && a <= 1.e290; }

struct V {
    double x{}, y{}, z{};
    CB_HD double operator[](int i) const { return i == 0 ? x : i == 1 ? y : z; }
    CB_HD double& operator[](int i) { return i == 0 ? x : i == 1 ? y : z; }
    CB_HD V operator+(V b) const { return {x+b.x,y+b.y,z+b.z}; }
    CB_HD V operator-(V b) const { return {x-b.x,y-b.y,z-b.z}; }
    CB_HD V operator-() const { return {-x,-y,-z}; }
    CB_HD V operator*(double s) const { return {x*s,y*s,z*s}; }
    CB_HD V operator/(double s) const { return *this*(1./s); }
    CB_HD V& operator+=(V b) { return *this = *this+b; }
    CB_HD V& operator-=(V b) { return *this = *this-b; }
};
CB_HD inline double dot(V a, V b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
CB_HD inline double norm(V a) { return ::sqrt(dot(a,a)); }
CB_HD inline V cross(V a, V b) {
    return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
}

enum class Kind { Ideal, Stiffened, PengRobinson };
struct Eos {
    Kind kind{Kind::Ideal};
    double gamma{1.4}, pinf{0.}, R{1.}, tc{1.}, pc{1.}, omega{0.04};
};
struct Thermo { double p{}, e{}, c2{}, de_dp{}; bool ok{}; };

struct PrTerms { double v,b,den,A,B,I,cv,R; };
CB_HD inline PrTerms pr_terms(const Eos& s, double rho) {
    constexpr double rt2 = 1.4142135623730950488;
    const double v=1./rho, b=.07780*s.R*s.tc/s.pc;
    const double a=.45724*sq(s.R*s.tc)/s.pc;
    const double k=.37464+1.54226*s.omega-.26992*sq(s.omega);
    return {v,b,v*v+2*b*v-b*b,::sqrt(a)*(1+k),::sqrt(a/s.tc)*k,
        v>b ? ::log1p(2*rt2*b/(v+(1-rt2)*b))/(2*rt2*b) : 0.,s.R/(s.gamma-1),s.R};
}

// In sqrt(T), both the pressure and the departure-energy inversions are quadratic.
CB_HD inline double positive_root(double a, double b, double c) {
    const double d=b*b-4*a*c;
    if (!(d>=0) || !(a>0)) return -1.;
    const double root=::sqrt(d);
    if (b>=0.) return b+root>0 ? -2*c/(b+root) : -1.;
    return (-b+root)/(2*a);
}
CB_HD inline Thermo pr_at(const PrTerms& t, double w) {
    if (!(w>0) || !(t.v>t.b)) return {};
    const double T=w*w, a=sq(t.A-t.B*w), aT=t.B*t.B-t.A*t.B/w;
    const double p=t.R*T/(t.v-t.b)-a/t.den;
    const double e=t.cv*T+t.A*(t.B*w-t.A)*t.I;
    const double pT=t.R/(t.v-t.b)-aT/t.den;
    const double pv=-t.R*T/sq(t.v-t.b)+2*a*(t.v+t.b)/sq(t.den);
    const double cv=t.cv+t.A*t.B*t.I/(2*w);
    const double c2=t.v*t.v*(-pv+T*pT*pT/cv);
    return {p,e,c2,cv/pT,finite(e)&&finite(c2)&&c2>0&&pv<0&&cv>0&&pT>0&&p>0};
}
CB_HD inline Thermo from_p(const Eos& s, double rho, double p) {
    if (!(rho>0) || !finite(p)) return {};
    if (s.kind!=Kind::PengRobinson) {
        const double off=s.kind==Kind::Ideal ? 0. : s.pinf;
        return {p,(p+s.gamma*off)/((s.gamma-1)*rho),s.gamma*(p+off)/rho,
            1./((s.gamma-1)*rho), p+off>1.e-12};
    }
    const auto t=pr_terms(s,rho);
    if (t.v<=t.b) return {};
    return pr_at(t,positive_root(t.R/(t.v-t.b)-sq(t.B)/t.den,
                2*t.A*t.B/t.den,-sq(t.A)/t.den-p));
}
CB_HD inline Thermo from_e(const Eos& s, double rho, double e) {
    if (!(rho>0) || !finite(e)) return {};
    if (s.kind!=Kind::PengRobinson) {
        const double off=s.kind==Kind::Ideal ? 0. : s.pinf;
        return from_p(s,rho,(s.gamma-1)*rho*e-s.gamma*off);
    }
    const auto t=pr_terms(s,rho);
    return pr_at(t,positive_root(t.cv,t.A*t.B*t.I,-sq(t.A)*t.I-e));
}

struct Material {
    Eos liquid{}, vapor{};
    bool mixture{false};
};
struct Q {
    double rho{}; V u{}; double p{}, e{}, c{}, y{}, a{};
    bool ok{};
    double ideal_gamma{};
};
struct U {
    double rho{}; V m{}; double E{}, gas{}, a{};
    CB_HD U operator+(const U& b) const { return {rho+b.rho,m+b.m,E+b.E,gas+b.gas,a+b.a}; }
    CB_HD U operator-(const U& b) const { return {rho-b.rho,m-b.m,E-b.E,gas-b.gas,a-b.a}; }
    CB_HD U operator*(double s) const { return {rho*s,m*s,E*s,gas*s,a*s}; }
};

CB_HD inline bool real_mix(const Material& m) {
    return m.liquid.kind==Kind::PengRobinson || m.vapor.kind==Kind::PengRobinson;
}
CB_HD inline double volume_fraction(const Material& m, double rho, double gas, double a) {
    if (m.mixture && real_mix(m)) {
        // Near a smeared material interface the advected volume and partial
        // mass can disagree. Keep trace PR phases on the dilute branch by
        // projecting volume, not conserved mass or energy.
        if (m.vapor.kind==Kind::PengRobinson) {
            const auto& e=m.vapor;
            a=max(a,gas*.07780*e.R*e.tc/(.15*e.pc));
        }
        if (m.liquid.kind==Kind::PengRobinson) {
            const auto& e=m.liquid;
            a=min(a,1-(rho-gas)*.07780*e.R*e.tc/(.15*e.pc));
        }
    }
    return clip(a,0.,1.);
}
CB_HD inline Eos affine_mix(const Material& m, double a) {
    const double w=(1.-a)/(m.liquid.gamma-1), v=a/(m.vapor.gamma-1);
    const double g=1+1/(w+v);
    return {Kind::Stiffened,g,(w*m.liquid.gamma*m.liquid.pinf+v*m.vapor.gamma*m.vapor.pinf)
        /((w+v)*g)};
}

CB_HD inline Thermo mixture_at(const Material& m, double rho, double y, double a, double p) {
    if (!m.mixture) return from_p(m.liquid,rho,p);
    if (!real_mix(m)) return from_p(affine_mix(m,a),rho,p);
    if (a<1.e-7 || y<1.e-9) return from_p(m.liquid,rho,p);
    if (a>1.-1.e-7 || y>1.-1.e-9) return from_p(m.vapor,rho,p);
    const double rl=rho*(1-y)/(1-a), rv=rho*y/a;
    const auto l=from_p(m.liquid,rl,p), r=from_p(m.vapor,rv,p);
    if (!l.ok || !r.ok) return {};
    const double vL=1./rl, vR=1./rv;
    // Frozen volume fraction closure: use the phase energy derivatives
    // with the same compression ratio in each phase.
    const double dp=(1-y)*l.de_dp+y*r.de_dp;
    const double c2=((1-y)*l.de_dp*l.c2/vL+y*r.de_dp*r.c2/vR)/(rho*dp);
    return {p,(1-y)*l.e+y*r.e,c2,dp,c2>0&&finite(c2)};
}
CB_HD inline Thermo mixture_from_e(const Material& m, double rho, double y, double a, double e) {
    if (!m.mixture) return from_e(m.liquid,rho,e);
    const auto guess=from_e(affine_mix(m,a),rho,e);
    if (!real_mix(m)) return guess;
    if (a<1.e-7 || y<1.e-9) return from_e(m.liquid,rho,e);
    if (a>1.-1.e-7 || y>1.-1.e-9) return from_e(m.vapor,rho,e);
    double p=max(guess.p,1.e-7);
    for (int k=0;k<30;++k) {
        auto t=mixture_at(m,rho,y,a,p);
        if (!t.ok) { p=2*p+.01; continue; }
        const double res=t.e-e;
        if (::fabs(res)<2.e-12*(1+::fabs(e))) return t;
        p=max(.5*p,p-res/t.de_dp);
    }
    return {};
}
CB_HD inline Q primitive(const Material& m, double rho, V u, double p, double y=0., double a=0.) {
    a=volume_fraction(m,rho,rho*y,a);
    const auto t=mixture_at(m,rho,y,a,p);
    const double g=!m.mixture&&m.liquid.kind==Kind::Ideal ? m.liquid.gamma : 0.;
    return {rho,u,p,t.e,::sqrt(max(t.c2,0.)),y,a,t.ok,g};
}
CB_HD inline U conserved(const Q& q) { return {q.rho,q.u*q.rho,q.rho*(q.e+.5*dot(q.u,q.u)),q.rho*q.y,q.a}; }
CB_HD inline Q decode(const Material& m, const U& u) {
    if (!(u.rho>1.e-11) || !finite(u.E) || u.gas<-1.e-9*u.rho || u.gas>u.rho*(1.+1.e-9)
        || u.a<-1.e-9 || u.a>1.+1.e-9) return {};
    const V vel=u.m/u.rho;
    const double e=u.E/u.rho-.5*dot(vel,vel), y=clip(u.gas/u.rho,0.,1.), a=volume_fraction(m,u.rho,u.gas,u.a);
    const auto t=mixture_from_e(m,u.rho,y,a,e);
    const double g=!m.mixture&&m.liquid.kind==Kind::Ideal ? m.liquid.gamma : 0.;
    return {u.rho,vel,t.p,e,::sqrt(max(t.c2,0.)),y,a,t.ok,g};
}

CB_HD inline double smooth_abs(double n, double eps) { return ::sqrt(n*n+eps*eps); }
CB_HD inline double smax(double a, double b, double eps) { return .5*(a+b+smooth_abs(a-b,eps)); }
CB_HD inline double smin(double a, double b, double eps) { return .5*(a+b-smooth_abs(a-b,eps)); }
CB_HD inline double venkat(double d, double lo, double hi, double eps2, double s=1.e-13) {
    const double ad=smooth_abs(d,s), up=.5*(1+d/ad), a=up*hi-(1-up)*lo, b=ad-s;
    return (a*a+2*a*b+eps2)/(a*a+a*b+2*b*b+eps2);
}

enum class Scheme { AllMach, Ausm, Hllc, Hlle };
struct Flux { U f{}; double v{}; };
CB_HD inline Flux physical(const Q& q, V n) {
    const auto u=conserved(q);
    const double v=dot(q.u,n);
    return {{u.rho*v,u.m*v+n*q.p,(u.E+q.p)*v,u.gas*v,q.a*v},v};
}
CB_HD inline double beta_hllc(const Q& l, const Q& r, double floor=1.e-3) {
    return clip(max(norm(l.u)/l.c,norm(r.u)/r.c),floor,1.);
}
CB_HD inline Flux hllc(const Q& l, const Q& r, V n, Scheme scheme, double floor) {
    const double vl=dot(l.u,n), vr=dot(r.u,n);
    const double sl=min(vl-l.c,vr-r.c), sr=max(vl+l.c,vr+r.c);
    if (sl>=0) return physical(l,n);
    if (sr<=0) return physical(r,n);
    const double al=l.rho*(vl-sl), ar=r.rho*(sr-vr);
    const auto fl=physical(l,n), fr=physical(r,n);
    const double sm=(al*vl+ar*vr+l.p-r.p)/(al+ar);
    if (scheme==Scheme::Hlle || !(sm>sl && sm<sr)) {
        U f=(fl.f*sr-fr.f*sl+(conserved(r)-conserved(l))*(sl*sr))*(1/(sr-sl));
        const double v=(sr*vl-sl*vr)/(sr-sl);
        return {f,v};
    }
    const bool left=sm>=0;
    const Q& q=left ? l : r;
    const double s=left ? sl : sr, v=left ? vl : vr;
    const double ps=q.p+q.rho*(s-v)*(sm-v);
    const auto u=conserved(q);
    const double rs=q.rho*(s-v)/(s-sm);
    const U us{rs,(q.u+n*(sm-v))*rs,((s-v)*u.E-q.p*v+ps*sm)/(s-sm),rs*q.y,q.a};
    auto f=(left ? fl.f : fr.f)+(us-u)*s;
    if (scheme==Scheme::AllMach) {
        // Modify only the velocity-jump contribution to star pressure. The
        // physical wave bounds and pressure-jump stabilization are unchanged.
        const double sensor=clip(6*::fabs(l.p-r.p)/min(l.rho*sq(l.c),r.rho*sq(r.c)),0.,1.);
        const double beta=max(beta_hllc(l,r,floor),sensor);
        const double dp=(beta-1.)*(al*ar/(al+ar))*(vl-vr);
        f.m+=n*dp;
        f.E+=sm*dp;
    }
    f.a=q.a*sm;
    return {f,sm};
}

CB_HD inline double m2(double m, int s) { return s*.25*sq(m+s); }
CB_HD inline double m4(double m, int s) {
    return ::fabs(m)>=1 ? .5*(m+s*::fabs(m)) : m2(m,s)*(1-s*2*m2(m,-s));
}
CB_HD inline double p5(double m, int s, double alpha) {
    return ::fabs(m)>=1 ? .5*(1+s*(m>0 ? 1 : -1)) : m2(m,s)*(s*2-m-s*16*alpha*m*m2(m,-s));
}
CB_HD inline double critical_sound(const Q& q,double vn) {
    const double h=q.e+q.p/q.rho+.5*dot(q.u,q.u);
    const double c2=2*(q.ideal_gamma-1)/(q.ideal_gamma+1)*h;
    return c2/max(::sqrt(c2),vn);
}
CB_HD inline Flux ausm(const Q& l, const Q& r, V n, double ref_mach) {
    const double vl=dot(l.u,n), vr=dot(r.u,n);
    const double a=l.ideal_gamma>1 && r.ideal_gamma>1 ? min(critical_sound(l,vl),critical_sound(r,-vr)) : .5*(l.c+r.c);
    const double ml=vl/a, mr=vr/a, mbar=.5*(sq(ml)+sq(mr));
    const double mo=::sqrt(clip(mbar,sq(ref_mach),1.)), fa=mo*(2-mo);
    const double alpha=3./16.*(5*fa*fa-4);
    const double mp=-.25/fa*max(1-mbar,0.)*(r.p-l.p)/(.5*(l.rho+r.rho)*a*a);
    const double mf=m4(ml,1)+m4(mr,-1)+mp;
    const double pl=p5(ml,1,alpha), pr=p5(mr,-1,alpha);
    const double pf=pl*l.p+pr*r.p-.75*pl*pr*(l.rho+r.rho)*fa*a*(vr-vl);
    const Q& q=mf>=0 ? l : r;
    const double v=a*mf, mass=q.rho*v;
    return {{mass,q.u*mass+n*pf,mass*(q.e+.5*dot(q.u,q.u)+q.p/q.rho),mass*q.y,q.a*v},v};
}
CB_HD inline Flux flux(Scheme s, const Q& l, const Q& r, V n) {
    return s==Scheme::Ausm ? ausm(l,r,n,.06) : hllc(l,r,n,s,1.e-3);
}

struct Cavitation {
    double psat{.12}, nuclei{5.e3}, seed{1.e-6}, rho_l{1.}, rho_v{.06}, cv{.30}, cc{.10};
};
CB_HD inline double schnerr_sauer(const Cavitation& c, const Q& q) {
    const bool growing=q.p<c.psat;
    const double a=clip(q.a+(growing ? c.seed : 0.),1.e-16,1.-1.e-12);
    const double radius=::cbrt(3*a/(4*3.14159265358979323846*c.nuclei*(1-a)));
    const double dalpha=3*a*(1-a)/radius*::sqrt(2*::fabs(c.psat-q.p)/(3*c.rho_l));
    return (growing ? c.cv : -c.cc)*(growing || q.a>0 ? 1 : 0)*c.rho_l*c.rho_v/q.rho*dalpha;
}
CB_HD inline U phase_change(const Material& m, const Cavitation& c, U u, double dt) {
    const auto q=decode(m,u);
    if (!q.ok || (q.p>=c.psat && q.a<=0.) || (q.p<c.psat && q.a>=1.)) return u;
    const double k=(1-q.a)/c.rho_v+q.a/c.rho_l;
    double dm=clip(dt*schnerr_sauer(c,q),max(-u.gas,-u.a/k),min(u.rho-u.gas,(1-u.a)/k));
    for (int it=0;it<16 && dm!=0.;++it) {
        auto trial=u; trial.gas+=dm; trial.a=volume_fraction(m,trial.rho,trial.gas,trial.a+dm*k);
        const auto v=decode(m,trial);
        if (v.ok && (q.p-c.psat)*(v.p-c.psat)>=0.) return trial;
        dm*=.5;
    }
    return u;
}

} // namespace cb