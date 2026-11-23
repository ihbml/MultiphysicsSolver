#pragma once

#include "fvm.hpp"

namespace cb {

CB_HD inline V tangent(V u,V n) { return u-n*dot(u,n); }

struct FaCell { V x, n; double area; };
struct FaEdge { int i,j; V co_normal; double length; }; // Points outward from i.
struct FaMesh {
    std::vector<FaCell> cells;
    std::vector<FaEdge> edges;
};
inline FaMesh surface_box(int nx, int ny, double lx, double ly) {
    FaMesh m;
    const double dx=lx/nx, dy=ly/ny;
    for (int j=0;j<ny;++j) for (int i=0;i<nx;++i) {
        const int k=j*nx+i;
        m.cells.push_back({{(i+.5)*dx,(j+.5)*dy,0},{0,0,1},dx*dy});
        if (i) m.edges.push_back({k-1,k,{1,0,0},dy});
        if (j) m.edges.push_back({k-nx,k,{0,1,0},dx});
    }
    return m;
}

struct FilmValue {
    double mass{}; V momentum{}; double enthalpy{};
    CB_HD FilmValue operator+(const FilmValue& b) const { return {mass+b.mass,momentum+b.momentum,enthalpy+b.enthalpy}; }
    CB_HD FilmValue operator*(double s) const { return {mass*s,momentum*s,enthalpy*s}; }
};
struct FilmLoad { V shear{}, grad_p{}, gravity{}; double evaporation{}, heat{}; };
CB_HD inline V film_velocity(const FilmValue& h) { return h.mass>1.e-14 ? h.momentum/h.mass : V{}; }
CB_HD inline double surface_speed(FilmValue l,FilmValue r,V normal) {
    return .5*dot(film_velocity(l)+film_velocity(r),normal);
}
CB_HD inline FilmValue surface_flux(FilmValue l, FilmValue r,V normal) {
    const double a=surface_speed(l,r,normal);
    return (a>=0 ? l : r)*a;
}
CB_HD inline double add_load(FilmValue& f,const FilmLoad& s,V n,double rho,double mu,double dt) {
    if (f.mass<=1.e-14) return 0.;
    const double thickness=f.mass/rho;
    f.momentum+=tangent(s.shear-s.grad_p*thickness+s.gravity*f.mass,n)*dt;
    const double drag=3*mu/(rho*sq(max(thickness,1.e-9)));
    f.momentum=f.momentum/(1.+drag*dt);
    f.enthalpy=max(f.enthalpy+dt*s.heat,0.);
    constexpr double latent=2.26e6;
    const double h=f.enthalpy/f.mass;
    const double loss=min(max(s.evaporation,0.)*dt,min(f.mass,f.enthalpy/(latent+h)));
    f.momentum=f.momentum*(1.-loss/f.mass);
    f.enthalpy=max(f.enthalpy-loss*(latent+h),0.);
    f.mass-=loss;
    return loss;
}
class Film {
public:
    FaMesh mesh;
    std::vector<FilmValue> state;
    std::vector<FilmLoad> loads;
    std::vector<double> deposited;
    double rho{998.}, viscosity{.001};
    long double evaporated{};

    explicit Film(FaMesh m) : mesh(std::move(m)),state(mesh.cells.size()),loads(mesh.cells.size()),deposited(mesh.cells.size()) {}
    void inject(int i, double mass, V u, double enthalpy) {
        const double areal=mass/mesh.cells[i].area;
        state[i]=state[i]+FilmValue{areal,tangent(u,mesh.cells[i].n)*areal,areal*enthalpy};
        deposited[i]+=areal;
    }
    long double total() const {
        long double sum=0.;
        for (std::size_t i=0;i<state.size();++i) sum+=state[i].mass*mesh.cells[i].area;
        return sum;
    }
    void advance(double dt) {
        std::vector<FilmValue> residual(state.size());
        std::vector<double> outflow(state.size());
        while (dt>1.e-12) {
            std::fill(outflow.begin(),outflow.end(),0.);
            for (const auto& e:mesh.edges) {
                const double f=surface_speed(state[e.i],state[e.j],e.co_normal)*e.length;
                if (f>=0) outflow[e.i]+=f/mesh.cells[e.i].area;
                else outflow[e.j]-=f/mesh.cells[e.j].area;
            }
            double sub=dt;
            for (double o:outflow) if (o>0.) sub=min(sub,.75/o);
            std::fill(residual.begin(),residual.end(),FilmValue{});
            for (const auto& e:mesh.edges) {
                const auto f=surface_flux(state[e.i],state[e.j],e.co_normal)*e.length;
                residual[e.i]=residual[e.i]+f*(-1/mesh.cells[e.i].area);
                residual[e.j]=residual[e.j]+f*(1/mesh.cells[e.j].area);
            }
            for (std::size_t i=0;i<state.size();++i) {
                auto& h=state[i];
                const auto& face=mesh.cells[i];
                h=h+residual[i]*sub;
                h.momentum=tangent(h.momentum,face.n);
                evaporated+=add_load(h,loads[i],face.n,rho,viscosity,sub)*face.area;
            }
            dt-=sub;
        }
    }
};

enum class Impact { Stick, Rebound, Spread, Splash };
struct ImpactInfo { Impact regime; double we, la, splash_we; };
CB_HD inline ImpactInfo bai_gosman(double normal_speed,double diameter,double rho,double mu,double tension,bool wet) {
    const double we=rho*sq(normal_speed)*diameter/tension, la=rho*tension*diameter/sq(mu);
    const double limit=max(20.,(wet?1320.:2630.)*::pow(la,-.183));
    const auto kind=we>=limit ? Impact::Splash : we<2. ? Impact::Stick
        : we<20. ? (wet ? Impact::Rebound : Impact::Stick) : Impact::Spread;
    return {kind,we,la,limit};
}
struct Triangle { V a,b,c,n; int cell; };
struct Hit { double fraction{2.}; V x{},n{}; int cell{-1}; };
inline Hit intersect(V a,V b,std::span<const Triangle> wall) {
    Hit h;
    for (const auto& tri:wall) {
        const double v=dot(b-a,tri.n);
        if (v>=-1.e-15) continue;
        const double f=dot(tri.a-a,tri.n)/v;
        if (f<0||f>1||f>=h.fraction) continue;
        const V p=a+(b-a)*f;
        if (dot(cross(tri.b-tri.a,p-tri.a),tri.n)>=-1.e-14
         && dot(cross(tri.c-tri.b,p-tri.b),tri.n)>=-1.e-14
         && dot(cross(tri.a-tri.c,p-tri.c),tri.n)>=-1.e-14) h={f,p,tri.n,tri.cell};
    }
    return h;
}

struct Parcel { V x,u; double mass, diameter, h; int generations{}; };
struct Spray {
    std::vector<Parcel> parcels;
    std::array<int,4> impacts{};
    long double injected{}, escaped{};
    void inlet(const Parcel& p) { parcels.push_back(p); injected+=p.mass; }
    long double airborne() const {
        long double m=0;
        for (auto& p:parcels) m+=p.mass;
        return m;
    }
    template <class Collider, class Gas, class Exits>
    void advance(Film& film,double dt,Collider collision,Gas gas,Exits escaped_domain) {
        std::vector<Parcel> next;
        next.reserve(parcels.size()+32);
        for (auto p:parcels) {
            double left=dt;
            for (int sub=0;sub<5&&left>1.e-12;++sub) {
                const V ug=gas(p.x), grav{0,0,-9.81};
                const V rel=p.u-ug;
                constexpr double visc=1.82e-5, rho_g=1.20;
                const double re=rho_g*norm(rel)*p.diameter/visc;
                const double tau=film.rho*sq(p.diameter)/(18*visc*(1+.15*::pow(re,.687)));
                const double decay=::exp(-left/tau);
                const V eq=ug+grav*tau;
                const V a=p.x+(eq*left+(p.u-eq)*(tau*(1-decay)));
                const auto hit=collision(p.x,a);
                if (hit.cell<0) {
                    p.x=a; p.u=eq+(p.u-eq)*decay; break;
                }
                const double used=left*hit.fraction;
                p.u=eq+(p.u-eq)*::exp(-used/tau);
                left-=used;
                const double normal=dot(p.u,hit.n);
                const bool wet=film.state[hit.cell].mass/film.rho>4.e-6;
                auto map=bai_gosman(normal,p.diameter,film.rho,film.viscosity,.072,wet);
                if (p.generations>2) map.regime=Impact::Spread;
                ++impacts[static_cast<int>(map.regime)];
                const V ut=tangent(p.u,hit.n);
                p.x=hit.x+hit.n*1.e-8;
                if (map.regime==Impact::Rebound) {
                    ++p.generations;
                    p.u=ut*.7-hit.n*(.45*normal);
                    continue;
                }
                if (map.regime==Impact::Splash) {
                    const double rest=clip(.28+.12*(map.we/map.splash_we-1.),.28,.62);
                    const double second=p.mass*rest;
                    film.inject(hit.cell,p.mass-second,ut*.55,p.h);
                    const V lateral=cross(hit.n,ut);
                    for (int dir:{-1,1}) next.push_back({p.x,ut*.48+lateral*(.20*dir)-hit.n*(.33*normal),
                        second*.5,p.diameter*::cbrt(rest*.5),p.h,p.generations+1});
                } else film.inject(hit.cell,p.mass,map.regime==Impact::Stick ? V{} : ut*.65,p.h);
                p.mass=0.; break;
            }
            if (p.mass<=0) continue;
            if (escaped_domain(p.x)) escaped+=p.mass;
            else next.push_back(p);
        }
        parcels.swap(next);
    }
};

} // namespace cb