#include "cases.hpp"
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <string>

using namespace cb;
static int failures=0;
static bool machine=false;

void check(std::string_view id,std::string_view label,double value,double limit,bool less=true) {
    const bool ok=std::isfinite(value)&&(less ? value<limit : value>limit);
    failures+=!ok;
    if (machine) std::cout<<id<<'\t'<<label<<'\t'<<value<<'\t'<<limit<<'\t'<<(less?"<":">")<<'\t'<<ok<<'\n';
    else std::cout<<(ok?"PASS  ":"FAIL  ")<<std::left<<std::setw(43)<<label<<std::scientific<<value
                  <<(less ? " < " : " > ")<<limit<<'\n';
}

double exact_sod(double xi) {
    constexpr double g=1.4, rL=1., rR=.125, pL=1., pR=.1;
    const double cL=std::sqrt(g*pL/rL), cR=std::sqrt(g*pR/rR);
    const auto f=[&](double p,double r,double p0,double c) {
        if (p<=p0) return 2*c/(g-1)*(std::pow(p/p0,(g-1)/(2*g))-1);
        return (p-p0)*std::sqrt((2/((g+1)*r))/(p+(g-1)/(g+1)*p0));
    };
    double lo=pR,hi=pL;
    for (int i=0;i<64;++i) {
        const double mid=.5*(lo+hi);
        if (f(mid,rL,pL,cL)+f(mid,rR,pR,cR)>0) hi=mid; else lo=mid;
    }
    const double p=.5*(lo+hi), u=.5*(f(p,rR,pR,cR)-f(p,rL,pL,cL));
    if (xi<=u) {
        if (xi<=-cL) return rL;
        const double cs=cL*std::pow(p/pL,(g-1)/(2*g));
        if (xi>=u-cs) return rL*std::pow(p/pL,1/g);
        const double cf=2./(g+1)*(cL-.5*(g-1)*xi);
        return rL*std::pow(cf/cL,2/(g-1));
    }
    const double shock=cR*std::sqrt((g+1)/(2*g)*(p/pR)+(g-1)/(2*g));
    if (xi>=shock) return rR;
    const double mu=(g-1)/(g+1);
    return rR*(p/pR+mu)/(mu*p/pR+1.);
}

void test_eos() {
    for (auto kind:{Kind::Ideal,Kind::Stiffened,Kind::PengRobinson}) {
        const auto s=eos(kind);
        const auto a=from_p(s,.94,1.72), b=from_e(s,.94,a.e);
        const auto rho=.94, h=1.e-5;
        const double pP=from_e(s,rho+h,a.e+h*a.p/(rho*rho)).p;
        const double pM=from_e(s,rho-h,a.e-h*a.p/(rho*rho)).p;
        check("eos","Pressure/energy round trip",a.ok&&b.ok ? std::abs(b.p-a.p) : 10.,1.e-12);
        check("eos","Isentropic sound-speed derivative",std::abs((pP-pM)/(2*h)-a.c2)/a.c2,1.e-8);
    }
    Material m{eos(Kind::Stiffened),eos(Kind::PengRobinson),true};
    const auto x=primitive(m,.82,{.20,0,0},.7,.036,.24);
    const auto y=decode(m,conserved(x));
    check("eos","PR / stiffened-gas mixture inversion",x.ok&&y.ok ? std::abs(x.p-y.p) : 10.,1.e-10);
    const Material liquid{eos(Kind::Stiffened),eos(Kind::Ideal),true};
    const Cavitation c;
    const auto l=conserved(primitive(liquid,1.,{},.08));
    const auto v=conserved(primitive(liquid,.85,{},.20,.035,.17));
    const auto l2=phase_change(liquid,c,l,.001), v2=phase_change(liquid,c,v,.001);
    check("eos","Schnerr-Sauer evaporation flux",l2.gas-l.gas,0.,false);
    check("eos","Schnerr-Sauer condensation flux",v.gas-v2.gas,0.,false);
    check("eos","Phase-change total mass and energy",std::abs(l2.rho-l.rho)+std::abs(l2.E-l.E)+std::abs(v2.rho-v.rho)+std::abs(v2.E-v.E),1.e-15);
}

void test_flux() {
    const Material m;
    const V normal{.6,.8,0};
    const auto q=primitive(m,1.2,{.7,-.3,.42},.84);
    const auto f=physical(q,normal).f;
    for (auto s:{Scheme::AllMach,Scheme::Ausm,Scheme::Hllc}) {
        const auto d=flux(s,q,q,normal).f-f;
        check("flux","Oblique uniform-state flux consistency",std::abs(d.rho)+norm(d.m)+std::abs(d.E),1.e-14);
        const auto r=primitive(m,.18,{-.08,.21,0},.09);
        const auto pair=flux(s,q,r,normal).f+flux(s,r,q,-normal).f;
        check("flux","Interior-face antisymmetry",std::abs(pair.rho)+norm(pair.m)+std::abs(pair.E),1.e-13);
    }
    const auto slow=primitive(m,1.,{.02,0,0},1.), fast=primitive(m,1.,{3.,0,0},1.);
    check("flux","Mach-scaled acoustic limiter",std::abs(beta_hllc(slow,slow)-.02/slow.c),1.e-15);
    check("flux","Supersonic recovery of HLLC",std::abs(beta_hllc(fast,fast)-1.),1.e-15);
    const auto incoming=primitive(m,.8,{2.8,.15,0},.7);
    for (auto s:{Scheme::AllMach,Scheme::Ausm}) {
        const auto d=flux(s,fast,incoming,{1,0,0}).f-physical(fast,{1,0,0}).f;
        check("flux",s==Scheme::AllMach ? "HLLC supersonic upwind flux" : "AUSM+-up supersonic upwind flux",
              std::abs(d.rho)+norm(d.m)+std::abs(d.E),1.e-13);
    }
    const auto contact=flux(Scheme::AllMach,primitive(m,1.,{},1.),primitive(m,.1,{},1.),{1,0,0}).f;
    check("flux","Stationary contact mass flux",std::abs(contact.rho)+std::abs(contact.m.x-1.),1.e-15);
    const double d=1.e-4, v=venkat(d,-2*d,2*d,1.e-6);
    check("flux","Venkat smooth-extremum preservation",std::abs(v-1.),1.e-6);
    const auto fn=[](double x){ return x*venkat(x,-.23,.4,.004,1.e-5); };
    const double h=1.e-8;
    check("flux","Venkat derivative across zero slope",std::abs((fn(h)-fn(0))/h-(fn(0)-fn(-h))/h),1.e-6);
    Mesh skew;
    skew.cells={{{},1},{{1.,.1,.08},1},{{.3,1.,.27},1},{{.05,.32,1.},1},{{-.74,-.6,.25},1}};
    for (int i=1;i<=4;++i) skew.faces.push_back({0,i,{},skew.cells[i].x*.51,skew.cells[i].x*-.49,1.});
    const auto stencil=least_squares(skew);
    std::vector<Fields> q0(skew.cells.size());
    for (std::size_t i=0;i<q0.size();++i) q0[i][0]=2+dot(skew.cells[i].x,V{.8,-.45,1.6});
    const auto lin=reconstruct_gradient(q0.data(),0,stencil[0]);
    check("flux","Affine gradient on skew polyhedral stencil",norm(lin.g[0]-V{.8,-.45,1.6}),1.e-13);
}

void test_polyhedra() {
    const V x{1.,.27,.11}, y{.14,1.,.16}, z{.20,.17,1.};
    const std::array<V,3> side={cross(y,z),cross(z,x),cross(x,y)};
    const double volume=dot(x,side[0]);
    const auto transform=[&](V v){ return x*v.x+y*v.y+z*v.z; };
    constexpr int n=3;
    const std::array<int,3> stride={1,n,n*n};
    const std::array<double,4> grid={0.,.74,1.61,3.};
    Mesh m;
    for (int k=0;k<n;++k) for (int j=0;j<n;++j) for (int i=0;i<n;++i) {
        const double dx=grid[i+1]-grid[i], dy=grid[j+1]-grid[j], dz=grid[k+1]-grid[k];
        const V p{grid[i]+.5*dx,grid[j]+.5*dy,grid[k]+.5*dz};
        m.cells.push_back({transform(p),volume*dx*dy*dz});
    }
    for (int k=0;k<n;++k) for (int j=0;j<n;++j) for (int i=0;i<n;++i) {
        const std::array<int,3> index={i,j,k};
        const int cell=i+j*n+k*n*n;
        for (int d=0;d<3;++d) {
            const int a=(d+1)%3,b=(d+2)%3;
            const V normal=side[d]/norm(side[d]);
            const double area=norm(side[d])*(grid[index[a]+1]-grid[index[a]])*(grid[index[b]+1]-grid[index[b]]);
            const int adjacent=index[d]<n-1 ? cell+stride[d] : -1;
            V v{}; v[d]=.5*(grid[index[d]+1]-grid[index[d]]);
            const auto offset=transform(v);
            const V point=m.cells[cell].x+offset;
            m.faces.push_back({cell,adjacent,normal,offset,adjacent>=0 ? point-m.cells[adjacent].x : V{},area});
            if (index[d]==0) m.faces.push_back({cell,-1,-normal,-offset,{},area});
        }
    }
    const Material mat;
    const auto base=conserved(primitive(mat,1.02,{.39,-.21,.28},1.16));
    std::vector<U> initial(m.cells.size(),base);
    Flow f(std::move(m),mat,{Scheme::AllMach,.42,false,false},std::move(initial));
    f.advance(.15);
    double error=0.;
    for (const auto& q:f.state) error=max(error,std::abs(q.rho-base.rho)+norm(q.m-base.m)+std::abs(q.E-base.E));
    check("flux","3D skew polyhedra free-stream preservation",error,1.e-13);
}

void test_interface() {
    double closure=0., reconstructed=0.;
    for (int p=0;p<60;++p) {
        V sum{};
        const double theta=.1*p;
        const V center{.70*std::cos(theta),.70*std::sin(theta),.01*p};
        for (int y=-5;y<=5;++y) for (int x=-5;x<=5;++x) {
            const V j{.27*x+.02*std::sin(y+.1*x),.28*y,.08*std::cos(x+y)};
            if (dot(j,j)<1.00) sum+=wendland_gradient(j-center,1.,3)*.004;
        }
        const auto g=complete_boundary(sum);
        // Constant isotropic pressure plus uniform advective momentum tensor.
        const V velocity{.08,-.03,.02};
        const auto force=[&](V a){ return a*1.2+velocity*(.91*dot(velocity,a)); };
        closure=max(closure,norm(force(sum+g.area)));
        reconstructed=max(reconstructed,norm(force(sum+g.direction*g.radial_times_volume)));
    }
    check("ghost","Curved-wall constant-field force",closure,1.e-15);
    check("ghost","Ghost direction / radial reconstruction",reconstructed,1.e-15);
    const std::array<Sample,6> base={{{{.31,.06,.04},.19},{{-.12,.17,-.12},.17},{{.27,.22,-.13},.23},
        {{-.42,.06,.09},.12},{{.15,-.43,.05},.13},{{-.28,-.12,.28},.27}}};
    const auto f=[](V x) {
        Fields y{};
        for (int k=0;k<8;++k) y[k]=.39*k+dot(x,V{.18*k+.05,.13-.08*k,-.09*k});
        return y;
    };
    double error=0.;
    const V target{.028,-.081,.13}, other{.14,.07,-.02};
    std::array<Fields,6> a{},b{}; auto moving=base;
    for (int i=0;i<6;++i) { a[i]=f(base[i].x); moving[i].x+=target; b[i]=f(moving[i].x); }
    const auto to_particle=linear_transfer({},target,f({}),base,a,1.);
    const auto to_mesh=linear_transfer(target,other,to_particle,moving,b,1.);
    for (int k=0;k<8;++k) error=max(error,max(std::abs(to_mesh[k]-f(other)[k]),std::abs(to_particle[k]-f(target)[k])));
    check("opmis","Bidirectional 3D affine state transfer",error,1.e-13);
    const auto rank=moment({},std::span<const Sample>(base).first(2),1.);
    check("opmis","Degenerate-stencil detection",rank.full_rank ? 1. : 0.,.5);
}

void test_benchmarks() {
    constexpr double t=.20;
    for (auto scheme:{Scheme::AllMach,Scheme::Ausm}) {
        auto f=flow_case(Case::Sod,Kind::Ideal,{scheme,.42,false,false},320);
        f.advance(t);
        double error=0.;
        for (std::size_t i=0;i<f.state.size();++i) error+=std::abs(f.state[i].rho-exact_sod((f.mesh.cells[i].x.x-.5)/t))/320.;
        check("sod",scheme==Scheme::AllMach?"Sod density L1 / All-Mach HLLC":"Sod density L1 / AUSM+-up",error,.012);
        check("sod","Sod mass budget (incl. boundary flux)",static_cast<double>(std::abs(f.integrals().mass-f.initial.mass-f.exchanged.mass)/f.initial.mass),5.e-13);
    }
    for (auto scheme:{Scheme::AllMach,Scheme::Ausm}) {
        auto f=flow_case(Case::Acoustic,Kind::Ideal,{scheme,.42,false,false},220);
        constexpr double at=.43;
        f.advance(at);
        double pmax=0., error=0.;
        const double c=std::sqrt(1.4);
        for (std::size_t i=0;i<f.state.size();++i) {
            const auto q=decode(f.material,f.state[i]);
            const double expected=pulse(f.mesh.cells[i].x.x-1.02*c*at);
            pmax=max(pmax,q.p-1.);
            error+=std::abs((q.p-1.)-expected)/f.state.size();
        }
        check("acoustic",scheme==Scheme::AllMach?"Acoustic peak retention / HLLC":"Acoustic peak retention / AUSM+-up",pmax/.006,.96,false);
        check("acoustic","Acoustic reference profile L1 / amplitude",error/.006,.020);
    }
    const auto entropy_error=[](int n) {
        auto grid=box(n,1,1.,1.,true);
        const Material m;
        std::vector<U> q;
        const auto density=[](double x){ return 1.+.13*std::sin(2*pi*x); };
        for (const auto& c:grid.cells) q.push_back(conserved(primitive(m,density(c.x.x),{.7,0,0},1.)));
        Flow f(std::move(grid),m,{Scheme::AllMach,.42,false,false},std::move(q));
        f.advance(.24);
        double sum=0.;
        for (int i=0;i<n;++i) sum+=std::abs(f.state[i].rho-density(f.mesh.cells[i].x.x-.7*.24))/n;
        return sum;
    };
    check("flux","Smooth entropy-wave convergence order",std::log2(entropy_error(52)/entropy_error(104)),1.85,false);
    Run b(Case::Bubble,Kind::Stiffened,{Scheme::AllMach,.42,true,true},0);
    const double a=b.m[9], x=b.m[10];
    b.advance(.34);
    check("bubble","Bubble area reduction after shock",1.-b.m[9]/a,.16,false);
    check("bubble","Adaptive patch center displacement",b.m[10]-x,.01,false);
    check("bubble","Bubble mass budget (incl. boundary flux)",b.m[3],5.e-12);
    check("bubble","Bubble energy budget (incl. boundary flux)",b.m[4],5.e-12);
    check("bubble","Core particle support count",b.m[13],20.,false);
    Run pr(Case::Bubble,Kind::PengRobinson,{Scheme::Ausm,.42,true,true},0,64);
    const double area=pr.m[9];
    pr.advance(.448);
    check("bubble","Real-gas bubble area reduction / AUSM+-up",1.-pr.m[9]/area,.25,false);
    check("bubble","Real-gas coupled mass budget",pr.m[3],5.e-12);
    Run wall(Case::Film,Kind::Ideal,{},0);
    wall.advance(.30);
    check("film","Spray + film + vapor mass budget",wall.m[3],1.e-12);
    check("film","Positive film thickness",wall.m[7],0.,false);
    check("film","Evaporated mass [kg]",wall.m[18],0.,false);
    check("film","Secondary splash events",wall.m[22],0.,false);
    const V n{0,0,1};
    const std::array<Triangle,1> tris={{{{0,0,0},{1,0,0},{0,1,0},n,2}}};
    const auto hit=intersect({.1,.1,.3},{.2,.2,-.1},tris);
    check("film","Segment-to-faMesh triangle collision",std::abs(hit.fraction-.75)+std::abs(hit.cell-2),1.e-14);
    int bad=0;
    for (auto [v,k]:std::array<std::pair<double,Impact>,4>{{{.25,Impact::Stick},{1.8,Impact::Rebound},
          {7.,Impact::Spread},{28.,Impact::Splash}}})
        bad+=bai_gosman(v,1.e-4,998.,.001,.072,true).regime!=k;
    check("film","Bai-Gosman regime-map classification",bad,.5);
}

int main(int argc,char**) {
    machine=argc>1;
    std::cout<<std::setprecision(9);
    test_eos(); test_flux(); test_polyhedra(); test_interface(); test_benchmarks();
    if (!machine) std::cout<<(failures?"Native verification failed.\n":"All native checks passed.\n");
    return failures ? 1 : 0;
}