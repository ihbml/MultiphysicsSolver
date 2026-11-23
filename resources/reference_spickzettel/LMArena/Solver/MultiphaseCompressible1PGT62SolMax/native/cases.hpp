#pragma once

#include "film.hpp"
#include <memory>
#include <string_view>

namespace cb {

constexpr double pi=3.14159265358979323846;
enum class Case { Bubble, Sod, Acoustic, Film };
inline std::string_view name(Case c) {
    switch (c) {
        case Case::Bubble: return "bubble";
        case Case::Sod: return "sod";
        case Case::Acoustic: return "acoustic";
        case Case::Film: return "film";
    }
    return {};
}
inline Eos eos(Kind type) {
    if (type==Kind::Stiffened) return {type,2.8,1.8};
    if (type==Kind::PengRobinson) return {type,1.4,0,1.,.13,.21,.04};
    return {};
}
inline double pulse(double x) {
    const double d=x-.24-std::floor(x-.24+.5);
    return .006*std::exp(-sq(d/.092));
}

inline Flow flow_case(Case c,Kind kind,Options o,int nx,int ny=1) {
    const bool bubble=c==Case::Bubble;
    auto mesh=box(nx,ny,bubble?3.2:1.,bubble?1.5:1.,c==Case::Acoustic);
    const auto primary=eos(kind);
    Material m{bubble&&kind==Kind::PengRobinson ? eos(Kind::Stiffened) : primary,
        bubble&&kind==Kind::PengRobinson ? primary : eos(Kind::Ideal),bubble};
    std::vector<U> a;
    a.reserve(mesh.cells.size());
    for (const auto& cell:mesh.cells) {
        const auto x=cell.x;
        Q q{};
        if (bubble) {
            constexpr double p0=.20, rho0=1., mach=1.66;
            const auto s=m.liquid;
            const double c0=std::sqrt(from_p(s,rho0,p0).c2);
            const double r=(s.gamma+1)*mach*mach/((s.gamma-1)*mach*mach+2);
            const double p2=(p0+s.pinf)*(1+2*s.gamma/(s.gamma+1)*(mach*mach-1))-s.pinf;
            const double u2=c0*mach*(1-1/r);
            const double angle=.030;
            const V n{std::cos(angle),std::sin(angle),0};
            const double front=dot(n,x-V{.60,.75,0});
            const V from=x-V{1.68,.75,0};
            const double theta=std::atan2(from.y,from.x);
            const double R=.390*(1+.016*std::cos(3*theta)+.010*std::sin(5*theta));
            const double v=clip(.5+(R-norm(from))/(1.55*mesh.lx/nx),0.,1.);
            const double gas=v*.095, rho=gas+(1-v)*(front<0 ? r : rho0);
            q=primitive(m,rho,front<0 ? n*u2 : V{},front<0 ? p2 : p0,gas/rho,v);
        } else if (c==Case::Sod) {
            q=primitive(m,x.x<.5 ? 1. : .125,{},x.x<.5 ? 1. : .1);
        } else {
            const auto base=from_p(primary,1.,1.);
            const double a0=std::sqrt(base.c2), dp=pulse(x.x);
            q=primitive(m,1+dp/base.c2,{.02*a0+dp/a0,0,0},1+dp);
        }
        if (!q.ok) throw std::runtime_error("invalid initial thermodynamics");
        a.push_back(conserved(q));
    }
    return Flow{std::move(mesh),m,o,std::move(a)};
}

struct FilmCase {
    const int nx,ny;
    const double lx{.120},ly{.060};
    Film film;
    Spray spray;
    double cfl,t{},dt{};
    int steps{},pulses{};
    long double initial;

    FilmCase(int n,int m,double f) : nx(n),ny(m),film(surface_box(nx,ny,lx,ly)),cfl(f) {
        for (std::size_t i=0;i<film.state.size();++i) {
            film.state[i]={film.rho*2.e-6,{},film.rho*2.e-6*1.25e5};
            const auto x=film.mesh.cells[i].x;
            film.loads[i]={{.48,.09*std::cos(2*pi*x.y/ly),0},{-150.,0,0},{1.6,.32,0},.0014,670.};
        }
        initial=film.total();
    }
    void release() {
        for (int i=0;i<36;++i) {
            const double theta=i*2.39996322972865+pulses*.71;
            const double r=std::sqrt((i+.5)/36.);
            const double band=static_cast<double>((i*19+pulses*7)%31)/30.;
            const double slow=i%13==0 ? .55 : i%17==0 ? 2.8 : 7.+13.*band;
            const V origin{.029+.004*r*std::cos(theta),.029+.004*r*std::sin(theta),.019};
            const V u{slow*(.95+.53*r*std::cos(theta)),slow*.84*r*std::sin(theta),-slow};
            const double dia=(80.+90.*band)*1.e-6;
            spray.inlet({origin,u,(.7+.6*band)*3.1e-8,dia,1.55e5});
        }
        ++pulses;
    }
    void advance(double target) {
        while (t+1.e-13<target) {
            if (t+1.e-12>=.0018*pulses && t<.232) release();
            dt=min(target-t,.00072*clip(cfl/.42,.28,1.));
            spray.advance(film,dt,[&](V a,V b) -> Hit {
                if (a.z<0||b.z>=0) return {};
                const double f=a.z/(a.z-b.z);
                const V x=a+(b-a)*f;
                if (x.x<0 || x.y<0 || x.x>=lx || x.y>=ly) return {};
                const int i=static_cast<int>(x.x*nx/lx), j=static_cast<int>(x.y*ny/ly);
                return {f,x,{0,0,1},j*nx+i};
            },[](V x) { return V{4.2,.35*std::cos(90*x.y),-.54}; },
                [&](V x) { return x.x<0 || x.x>=lx || x.y<0 || x.y>=ly || x.z<0 || x.z>.10; });
            film.advance(dt); t+=dt; ++steps;
        }
    }
    double error() const {
        return static_cast<double>(std::abs((film.total()+film.evaporated+spray.airborne()+spray.escaped)
            -(initial+spray.injected))/(initial+spray.injected));
    }
};

constexpr int metrics=23, channels=4;
struct Run {
    int nx{},ny{},frames{}; double lx{},ly{},end{};
    std::unique_ptr<Flow> flow;
    std::unique_ptr<FilmCase> film;
    std::array<double,metrics> m{};
    std::vector<float> fields;

    Run(Case c,Kind kind,Options options,int quality,int override_n=0) {
        if (c==Case::Bubble) {
            nx=override_n>0?override_n:quality==2?192:quality==1?128:80;
            ny=static_cast<int>(nx*1.5/3.2+.5);
            lx=3.2; ly=1.5; end=.448; frames=71;
        } else if (c==Case::Film) {
            nx=quality==2?128:quality==1?100:72; ny=nx/2;
            lx=.120; ly=.060; end=.30; frames=65;
        } else {
            nx=override_n>0?override_n:quality==2?420:quality==1?320:220;
            ny=1; lx=1.; ly=1.; end=c==Case::Sod?.20:.43; frames=65;
        }
        if (c==Case::Film) film=std::make_unique<FilmCase>(nx,ny,options.cfl);
        else flow=std::make_unique<Flow>(flow_case(c,kind,options,nx,ny));
        fields.resize(nx*ny*channels);
        capture();
    }
    void capture() {
        m.fill(0.);
        const int n=nx*ny;
        m[7]=1.e90;
        if (flow) {
            const auto& f=*flow;
            const auto total=f.integrals();
            m[0]=f.t; m[1]=f.dt; m[2]=f.steps;
            m[3]=static_cast<double>(std::abs(total.mass-f.initial.mass-f.exchanged.mass)/f.initial.mass);
            m[4]=static_cast<double>(std::abs(total.energy-f.initial.energy-f.exchanged.energy)/std::abs(f.initial.energy));
            for (int i=0;i<n;++i) {
                const auto q=decode(f.material,f.state[i]);
                const double ma=norm(q.u)/q.c;
                fields[i]=q.rho; fields[n+i]=q.p; fields[2*n+i]=ma; fields[3*n+i]=q.a;
                m[5]=max(m[5],q.p); m[6]=max(m[6],ma); m[7]=min(m[7],q.rho); m[8]=max(m[8],q.rho);
                m[9]+=q.a*f.mesh.cells[i].volume;
            }
            m[10]=f.patch.center.x; m[11]=f.patch.center.y; m[12]=f.patch.radius;
            m[13]=f.patch.com; m[14]=f.patch.trn; m[15]=f.patch.imt; m[16]=f.repairs;
            m[17]=static_cast<double>(total.mass);
            m[18]=f.patch.shift.x; m[19]=f.patch.shift.y;
        } else {
            const auto& f=*film;
            m[0]=f.t; m[1]=f.dt; m[2]=f.steps; m[3]=f.error();
            for (int i=0;i<n;++i) {
                const auto& s=f.film.state[i];
                const double h=s.mass*1.e6/f.film.rho, speed=norm(film_velocity(s));
                fields[i]=h; fields[n+i]=speed; fields[2*n+i]=s.mass>1.e-14 ? s.enthalpy/s.mass*.001 : 0.;
                fields[3*n+i]=f.film.deposited[i]*1.e6/f.film.rho;
                m[5]=max(m[5],h); m[6]=max(m[6],speed); m[7]=min(m[7],h); m[8]=max(m[8],h);
            }
            m[13]=static_cast<double>(f.spray.parcels.size());
            m[14]=f.pulses*36;
            m[15]=static_cast<double>(f.spray.escaped); m[17]=static_cast<double>(f.film.total());
            m[18]=static_cast<double>(f.film.evaporated);
            for (int i=0;i<4;++i) m[19+i]=f.spray.impacts[i];
        }
    }
    void advance(double target) {
        if (flow) flow->advance(min(target,end)); else film->advance(min(target,end));
        capture();
    }
};

} // namespace cb