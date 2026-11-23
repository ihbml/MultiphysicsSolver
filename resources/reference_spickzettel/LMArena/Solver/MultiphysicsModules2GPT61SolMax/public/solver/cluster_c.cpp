// VECTOR Cluster C, dependency-free C++20 numerical reference.
// g++ -std=c++20 -O2 cluster_c.cpp -o cluster_c
// ./cluster_c --verify [--json]   |   ./cluster_c --demo battery|fwh|mhd|tps
// Define CLUSTER_C_LIBRARY to omit main and expose the C-compatible span adapters.
// SI units throughout. See README.md for assumptions and verification scope.
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numbers>
#include <numeric>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

extern "C" {
struct CAbiSpan { double* data; std::uint64_t size; };
struct CAbiIndexSpan { const std::int32_t* data; std::uint64_t size; };
struct CAbiVec3 { CAbiSpan x, y, z; };
struct CAbiMesh {
  CAbiVec3 center, face_center, area, correction;
  CAbiSpan volume, alpha;
  CAbiIndexSpan owner, neighbor;
};
struct CAbiBatteryState {
  CAbiSpan temperature, soc, v1, v2, c_sei, c_ne, alpha_pe, c_e;
};
struct CAbiMhdState {
  CAbiVec3 velocity, magnetic_field, current, lorentz;
  CAbiSpan conductivity, potential, face_current, joule_heat;
};
struct CAbiPyrolysisState { CAbiSpan temperature, density, gas_source, energy_source; };
struct CAbiPyrolysisStage {
  CAbiSpan density;
  double A,E,order,gamma,char_density,enthalpy;
};
struct CAbiPolyConnectivity {
  std::uint64_t cells;
  CAbiIndexSpan owner,neighbor,face_offsets,face_nodes;
};
}

namespace cluster_c {
using Vec = std::array<double, 3>;
constexpr double pi = std::numbers::pi;
constexpr double gas_R = 8.31446261815324;
constexpr double sigma_SB = 5.670374419e-8;
Vec operator+(Vec a, const Vec& b) { for (int i=0;i<3;++i) a[i]+=b[i]; return a; }
Vec operator-(Vec a, const Vec& b) { for (int i=0;i<3;++i) a[i]-=b[i]; return a; }
Vec operator*(Vec a, double s) { for (double& v:a) v*=s; return a; }
Vec operator/(Vec a, double s) { return a*(1/s); }
double dot(const Vec& a,const Vec& b) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
Vec cross(const Vec& a,const Vec& b) { return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]}; }
double norm(const Vec& a) { return std::sqrt(dot(a,a)); }
void require(bool ok,const char* text) { if(!ok) throw std::invalid_argument(text); }
std::span<double> view(CAbiSpan s) { require(s.size==0||s.data,"null span"); return {s.data,static_cast<std::size_t>(s.size)}; }
Vec get(const CAbiVec3& v,std::size_t i) { return {v.x.data[i],v.y.data[i],v.z.data[i]}; }
void put(CAbiVec3& v,std::size_t i,const Vec& a) { v.x.data[i]=a[0];v.y.data[i]=a[1];v.z.data[i]=a[2]; }
void check_span(CAbiSpan s,std::size_t n) { require(s.data&&s.size==n,"incorrect field span size"); }
void check_vec(const CAbiVec3& v,std::size_t n) { check_span(v.x,n);check_span(v.y,n);check_span(v.z,n); }
double harmonic(double a,double b) { require(a>0&&b>0,"nonpositive transport coefficient");return 2*a*b/(a+b); }

struct Mesh {
  CAbiMesh abi;
  std::size_t nc,nf;
  std::vector<std::vector<std::pair<std::size_t,int>>> cell_faces;
  explicit Mesh(CAbiMesh m):abi(m),nc(m.volume.size),nf(m.owner.size),cell_faces(nc) {
    require(nc>0&&nf>0,"empty mesh");check_span(abi.volume,nc);check_vec(abi.center,nc);check_vec(abi.face_center,nf);
    check_vec(abi.area,nf);check_vec(abi.correction,nf);check_span(abi.alpha,nf);
    require(abi.owner.data&&abi.neighbor.data&&abi.neighbor.size==nf,"invalid topology spans");
    for(std::size_t p=0;p<nc;++p) require(std::isfinite(abi.volume.data[p])&&abi.volume.data[p]>0,"invalid cell volume");
    for(std::size_t f=0;f<nf;++f) {
      int p=abi.owner.data[f],n=abi.neighbor.data[f];
      require(p>=0&&static_cast<std::size_t>(p)<nc&&n>=-1&&(n<0||static_cast<std::size_t>(n)<nc)&&p!=n,"invalid face indices");
      require(abi.alpha.data[f]>0&&std::isfinite(abi.alpha.data[f])&&norm(get(abi.area,f))>0,"invalid face geometry");
      cell_faces[p].push_back({f,1});if(n>=0) cell_faces[n].push_back({f,-1});
    }
  }
};

double owner_weight(const Mesh& m,std::size_t f){
  int p=m.abi.owner.data[f],n=m.abi.neighbor.data[f];if(n<0)return 1;
  Vec S=get(m.abi.area,f),xf=get(m.abi.face_center,f);
  double dp=std::abs(dot(S,xf-get(m.abi.center,p))),dn=std::abs(dot(S,get(m.abi.center,n)-xf));
  require(dp+dn>0,"degenerate interpolation distances");return dn/(dp+dn);
}
Vec face_mean(const Mesh& m,std::size_t f,Vec owner,Vec neighbor){double w=owner_weight(m,f);return owner*w+neighbor*(1-w);}
double face_harmonic(const Mesh& m,std::size_t f,double owner,double neighbor){
  require(owner>0&&neighbor>0,"nonpositive transport coefficient");double w=owner_weight(m,f);return 1/((1-w)/owner+w/neighbor);
}

struct OwnedVec {
  std::array<std::vector<double>,3> values;
  explicit OwnedVec(std::size_t n=0) { for(auto& v:values) v.resize(n); }
  void push(Vec a) {for(int k=0;k<3;++k)values[k].push_back(a[k]);}
  CAbiVec3 abi() {return {{values[0].data(),values[0].size()},{values[1].data(),values[1].size()},{values[2].data(),values[2].size()}};}
};
struct OwnedMesh {
  OwnedVec center,face,area,corr;
  std::vector<double> volume,alpha;
  std::vector<std::int32_t> owner,neighbor;
  CAbiMesh abi() {return {center.abi(),face.abi(),area.abi(),corr.abi(),{volume.data(),volume.size()},{alpha.data(),alpha.size()},{owner.data(),owner.size()},{neighbor.data(),neighbor.size()}};}
};

// Affine skewing gives genuine non-orthogonal faces, not perturbed connectivity.
OwnedMesh box_mesh(int nx,int ny,int nz,Vec lengths={1,1,1},double skew=0) {
  require(nx>0&&ny>0&&nz>0,"invalid box dimensions");
  OwnedMesh m;Vec h={lengths[0]/nx,lengths[1]/ny,lengths[2]/nz};
  auto transform=[&](Vec a){return Vec{a[0]+skew*a[1],a[1],a[2]};};
  auto idx=[&](int i,int j,int k){return i+nx*(j+ny*k);};
  for(int k=0;k<nz;++k)for(int j=0;j<ny;++j)for(int i=0;i<nx;++i){m.center.push(transform({(i+.5)*h[0],(j+.5)*h[1],(k+.5)*h[2]}));m.volume.push_back(h[0]*h[1]*h[2]);}
  auto add=[&](int p,int n,Vec xf,Vec sf){
    Vec xp={m.center.values[0][p],m.center.values[1][p],m.center.values[2][p]};
    Vec d=n<0?xf-xp:Vec{m.center.values[0][n],m.center.values[1][n],m.center.values[2][n]}-xp;
    double a=dot(sf,sf)/dot(sf,d);require(a>0,"inverted face");
    m.owner.push_back(p);m.neighbor.push_back(n);m.face.push(xf);m.area.push(sf);m.alpha.push_back(a);m.corr.push(sf-d*a);
  };
  for(int k=0;k<nz;++k)for(int j=0;j<ny;++j)for(int i=0;i<=nx;++i){
    bool lo=i==0,hi=i==nx;int p=idx(lo?0:i-1,j,k),n=(lo||hi)?-1:idx(i,j,k);double sign=lo?-1:1;
    add(p,n,transform({i*h[0],(j+.5)*h[1],(k+.5)*h[2]}),{sign*h[1]*h[2],-sign*skew*h[1]*h[2],0});
  }
  for(int k=0;k<nz;++k)for(int j=0;j<=ny;++j)for(int i=0;i<nx;++i){
    bool lo=j==0,hi=j==ny;int p=idx(i,lo?0:j-1,k),n=(lo||hi)?-1:idx(i,j,k);double sign=lo?-1:1;
    add(p,n,transform({(i+.5)*h[0],j*h[1],(k+.5)*h[2]}),{0,sign*h[0]*h[2],0});
  }
  for(int k=0;k<=nz;++k)for(int j=0;j<ny;++j)for(int i=0;i<nx;++i){
    bool lo=k==0,hi=k==nz;int p=idx(i,j,lo?0:k-1),n=(lo||hi)?-1:idx(i,j,k);double sign=lo?-1:1;
    add(p,n,transform({(i+.5)*h[0],(j+.5)*h[1],k*h[2]}),{0,0,sign*h[0]*h[1]});
  }
  return m;
}

Vec solve3(std::array<std::array<double,3>,3> A,Vec b) {
  for(int k=0;k<3;++k){
    int pivot=k;for(int j=k+1;j<3;++j)if(std::abs(A[j][k])>std::abs(A[pivot][k]))pivot=j;
    if(std::abs(A[pivot][k])<1e-25){A[k][k]+=1e-18;continue;}
    std::swap(A[k],A[pivot]);std::swap(b[k],b[pivot]);
    double d=A[k][k];for(int j=k;j<3;++j)A[k][j]/=d;b[k]/=d;
    for(int i=0;i<3;++i)if(i!=k){double a=A[i][k];for(int j=k;j<3;++j)A[i][j]-=a*A[k][j];b[i]-=a*b[k];}
  }return b;
}
enum class BcType { Flux,Dirichlet };
struct FaceBc {BcType type=BcType::Flux;double value=0;};
using BCs=std::vector<FaceBc>;

std::vector<Vec> gradient(const Mesh& m,std::span<const double> phi,const BCs& bc,
                          const std::vector<double>& diffusion,const std::vector<double>& base) {
  std::vector<Vec> result(m.nc);
  for(std::size_t p=0;p<m.nc;++p){
    std::array<std::array<double,3>,3>A{};Vec b{};Vec xp=get(m.abi.center,p);
    for(auto [f,sign]:m.cell_faces[p]){
      int own=m.abi.owner.data[f],nei=m.abi.neighbor.data[f],other=sign>0?nei:own;
      Vec d{};double dp=0;
      if(other>=0){d=get(m.abi.center,other)-xp;dp=phi[other]-phi[p];}
      else if(bc[f].type==BcType::Dirichlet){d=get(m.abi.face_center,f)-xp;dp=bc[f].value-phi[p];}
      else {Vec sf=get(m.abi.area,f);double area=norm(sf);double length=norm(get(m.abi.face_center,f)-xp);d=sf*(length/area);dp=(base[f]-bc[f].value)*length/(diffusion[f]*area);}
      double w=1/std::max(dot(d,d),1e-30);
      for(int i=0;i<3;++i){b[i]+=w*d[i]*dp;for(int j=0;j<3;++j)A[i][j]+=w*d[i]*d[j];}
    }result[p]=solve3(A,b);
  }return result;
}

struct SparseSystem {
  std::vector<double> diag;
  std::vector<std::vector<std::pair<int,double>>> off;
  bool gauge=false;
  explicit SparseSystem(std::size_t n):diag(n),off(n){}
  std::vector<double> apply(const std::vector<double>& x)const {
    std::vector<double> y(x.size());for(std::size_t i=0;i<x.size();++i){if(gauge&&i==0){y[i]=x[i];continue;}y[i]=diag[i]*x[i];for(auto [j,a]:off[i])y[i]+=a*x[j];}return y;
  }
};
double vdot(const std::vector<double>& a,const std::vector<double>& b){return std::inner_product(a.begin(),a.end(),b.begin(),0.0);}
double pcg(const SparseSystem& A,const std::vector<double>& rhs,std::vector<double>& x,double tol=1e-12,int maxit=20000){
  if(A.gauge)x[0]=0;
  auto Ax=A.apply(x);std::vector<double>r(x.size()),z(x.size()),p(x.size());
  for(std::size_t i=0;i<x.size();++i){r[i]=rhs[i]-Ax[i];if(A.gauge&&i==0)r[i]=0;z[i]=r[i]/(A.gauge&&i==0?1:A.diag[i]);}p=z;
  double rz=vdot(r,z),scale=std::max(std::sqrt(vdot(rhs,rhs)),1.0),res=std::sqrt(vdot(r,r));
  for(int it=0;res>tol*scale&&it<maxit;++it){auto Ap=A.apply(p);double denom=vdot(p,Ap);require(denom>0,"PCG matrix not positive definite");double a=rz/denom;
    for(std::size_t i=0;i<x.size();++i){x[i]+=a*p[i];r[i]-=a*Ap[i];z[i]=r[i]/(A.gauge&&i==0?1:A.diag[i]);}
    double next=vdot(r,z),beta=next/rz;for(std::size_t i=0;i<x.size();++i)p[i]=z[i]+beta*p[i];rz=next;res=std::sqrt(vdot(r,r));
  }require(res<=tol*scale*10,"PCG failed to converge");return res;
}

struct Projection {std::vector<double> flux;double divergence=0;int corrections=0;};
// The same non-orthogonal correction is used in assembly and final face fluxes.
Projection project_flux(const Mesh& m,const std::vector<double>& diffusion,const std::vector<double>& base,
                        const BCs& bc,std::vector<double>& phi,const std::vector<double>& target={},int outer_max=150){
  require(diffusion.size()==m.nf&&base.size()==m.nf&&bc.size()==m.nf&&phi.size()==m.nc,"projection size mismatch");
  SparseSystem A(m.nc);bool fixed=false;
  for(std::size_t f=0;f<m.nf;++f){int p=m.abi.owner.data[f],n=m.abi.neighbor.data[f];double a=diffusion[f]*m.abi.alpha.data[f];require(a>0,"nonpositive diffusivity");
    if(n>=0){A.diag[p]+=a;A.diag[n]+=a;A.off[p].push_back({n,-a});A.off[n].push_back({p,-a});}
    else if(bc[f].type==BcType::Dirichlet){A.diag[p]+=a;fixed=true;}
  }A.gauge=!fixed;
  for(double d:A.diag)require(d>0,"disconnected or unconstrained mesh");
  Projection out;out.flux.resize(m.nf);
  for(int outer=0;outer<outer_max;++outer){
    auto grad=gradient(m,phi,bc,diffusion,base);std::vector<double>rhs(m.nc),corr(m.nf);
    if(!target.empty()){require(target.size()==m.nc,"projection source mismatch");rhs=target;}
    for(std::size_t f=0;f<m.nf;++f){int p=m.abi.owner.data[f],n=m.abi.neighbor.data[f];
      if(n<0&&bc[f].type==BcType::Flux){rhs[p]-=bc[f].value;continue;}
      Vec gf=n>=0?face_mean(m,f,grad[p],grad[n]):grad[p];corr[f]=diffusion[f]*dot(gf,get(m.abi.correction,f));double b=base[f]-corr[f];rhs[p]-=b;
      if(n>=0)rhs[n]+=b;else rhs[p]+=diffusion[f]*m.abi.alpha.data[f]*bc[f].value;
    }
    if(A.gauge){double sum=std::accumulate(rhs.begin(),rhs.end(),0.0);require(std::abs(sum)<1e-9*std::max(1.0,std::sqrt(vdot(rhs,rhs))),"incompatible Neumann flux/source balance");rhs[0]=0;}
    pcg(A,rhs,phi);auto final_grad=gradient(m,phi,bc,diffusion,base);std::vector<double>div(m.nc);
    double flux_scale=1;
    for(std::size_t f=0;f<m.nf;++f){int p=m.abi.owner.data[f],n=m.abi.neighbor.data[f];double q;
      if(n<0&&bc[f].type==BcType::Flux)q=bc[f].value;
      else{Vec gf=n>=0?face_mean(m,f,final_grad[p],final_grad[n]):final_grad[p];double pn=n>=0?phi[n]:bc[f].value;q=base[f]-diffusion[f]*(m.abi.alpha.data[f]*(pn-phi[p])+dot(gf,get(m.abi.correction,f)));}
      out.flux[f]=q;div[p]+=q;if(n>=0)div[n]-=q;flux_scale=std::max(flux_scale,std::abs(q));
    }
    out.divergence=0;for(std::size_t p=0;p<m.nc;++p)out.divergence=std::max(out.divergence,std::abs(div[p]-(target.empty()?0:target[p])));
    out.corrections=outer+1;if(out.divergence<2e-10*flux_scale)return out;
  }throw std::runtime_error("non-orthogonal corrections failed to converge; improve mesh or relax corrections");
}

std::vector<Vec> reconstruct(const Mesh& m,const std::vector<double>& flux){
  std::vector<Vec>result(m.nc);for(std::size_t p=0;p<m.nc;++p){Vec sum{};for(auto [f,sign]:m.cell_faces[p])sum=sum+(get(m.abi.face_center,f)-get(m.abi.center,p))*(sign*flux[f]);result[p]=sum/m.abi.volume.data[p];}return result;
}

void implicit_energy(const Mesh& m,std::span<double> T,std::span<const double> rho_cp,
                     std::span<const double> conductivity,std::span<const double> source_W,
                     double dt,double boundary_h=0,double ambient=298.15){
  require(dt>0&&boundary_h>=0&&ambient>0&&T.size()==m.nc&&rho_cp.size()==m.nc&&conductivity.size()==m.nc&&source_W.size()==m.nc,"invalid energy input");
  std::vector<double>old(T.begin(),T.end()),x=old,kf(m.nf),zero(m.nf);BCs bc(m.nf);SparseSystem A(m.nc);std::vector<double>rhs0(m.nc);
  for(std::size_t p=0;p<m.nc;++p){require(rho_cp[p]>0&&T[p]>0&&conductivity[p]>0,"invalid thermal state");A.diag[p]=rho_cp[p]*m.abi.volume.data[p]/dt;rhs0[p]=A.diag[p]*old[p]+source_W[p];}
  for(std::size_t f=0;f<m.nf;++f){int p=m.abi.owner.data[f],n=m.abi.neighbor.data[f];kf[f]=n>=0?face_harmonic(m,f,conductivity[p],conductivity[n]):conductivity[p];
    if(n>=0){double a=kf[f]*m.abi.alpha.data[f];A.diag[p]+=a;A.diag[n]+=a;A.off[p].push_back({n,-a});A.off[n].push_back({p,-a});}
    else {double hA=boundary_h*norm(get(m.abi.area,f));A.diag[p]+=hA;rhs0[p]+=hA*ambient;}
  }
  for(int outer=0;outer<50;++outer){
    for(std::size_t f=0;f<m.nf;++f)if(m.abi.neighbor.data[f]<0)bc[f].value=boundary_h*norm(get(m.abi.area,f))*(x[m.abi.owner.data[f]]-ambient);
    auto grad=gradient(m,x,bc,kf,zero);auto rhs=rhs0;
    for(std::size_t f=0;f<m.nf;++f){int p=m.abi.owner.data[f],n=m.abi.neighbor.data[f];if(n<0)continue;double corr=kf[f]*dot(face_mean(m,f,grad[p],grad[n]),get(m.abi.correction,f));rhs[p]+=corr;rhs[n]-=corr;}
    auto prev=x;pcg(A,rhs,x);double err=0;for(std::size_t p=0;p<m.nc;++p)err=std::max(err,std::abs(x[p]-prev[p]));if(err<1e-10){std::copy(x.begin(),x.end(),T.begin());return;}
  }throw std::runtime_error("energy non-orthogonal iteration failed");
}

struct EcmResult {double terminal,ocv,signed_irr,heat_rev,heat_gen;};
EcmResult ecm_step(double& soc,double& v1,double& v2,double I,double T,double dt,double Q_Ah=5){
  require(T>0&&dt>0&&Q_Ah>0&&std::isfinite(I)&&soc>=0&&soc<=1,"invalid ECM state");
  double factor=std::exp(1800*(1/T-1/298.15))*(1+.2*(1-soc));
  double R0=.014*factor;std::array<double,2> Rs={.006*factor,.012*factor},Cs={2400/factor,11000/factor};
  double* vs[2]={&v1,&v2};for(int k=0;k<2;++k){double e=std::exp(-dt/(Rs[k]*Cs[k]));*vs[k]=*vs[k]*e+I*Rs[k]*(1-e);}
  constexpr double dU_dT=-.00012;
  double U=3.2+.95*soc+.08*std::tanh(10*(soc-.5))+dU_dT*(T-298.15);
  double terminal=U-I*R0-v1-v2,signed_irr=I*(terminal-U),rev=I*T*dU_dT;
  soc=std::clamp(soc-I*dt/(3600*Q_Ah),0.0,1.0);
  // signed_irr is the user's signed electrical expression, not positive heat.
  return {terminal,U,signed_irr,rev,-signed_irr+rev};
}
struct Reaction {double A,E,H,mass;};
constexpr std::array<Reaction,4> abuse={{{1.667e15,135080,257000,.014},{2.5e13,135080,1714000,.012},{6.667e13,139600,314000,.018},{5.14e25,274000,155000,.01}}};
struct ReactionUpdate {std::array<double,4> state;double released;};
ReactionUpdate patankar(const std::array<double,4>& c,double T,double dt){
  require(T>0&&dt>=0,"invalid source time or temperature");ReactionUpdate out{c,0};
  for(std::size_t k=0;k<4;++k){require(c[k]>=0&&c[k]<=1,"reaction state outside [0,1]");double z=dt*abuse[k].A*std::exp(-abuse[k].E/(gas_R*T));
    out.state[k]=k==2?(c[k]==0?0:1-(1-c[k])/(1+z*c[k])):c[k]/(1+z);
    out.released+=std::abs(out.state[k]-c[k])*abuse[k].H*abuse[k].mass;
  }return out;
}
double implicit_abuse(std::array<double,4>& c,double T,double dt,double heat_capacity,double external_W=0){
  require(heat_capacity>0,"nonpositive heat capacity");double budget=0;
  for(std::size_t k=0;k<4;++k)budget+=(k==2?1-c[k]:c[k])*abuse[k].H*abuse[k].mass;
  double lo=std::max(1.0,T+dt*external_W/heat_capacity),hi=lo+budget/heat_capacity+1;
  // Positive diagonal Patankar concentrations, nonlinear future-temperature closure.
  // For multiple implicit roots bisection selects one admissible root; accuracy still
  // needs time refinement. Boundedness is NOT permission to take arbitrary dt.
  for(int it=0;it<90;++it){double mid=(lo+hi)*.5;auto step=patankar(c,mid,dt);double residual=heat_capacity*(mid-T)-dt*external_W-step.released;if(residual>0)hi=mid;else lo=mid;}
  double next=(lo+hi)*.5;auto update=patankar(c,next,dt);c=update.state;return next;
}

void battery_step(const Mesh& m,CAbiBatteryState& s,double I,double dt,double coolant=298.15,double h=12){
  for(CAbiSpan a:{s.temperature,s.soc,s.v1,s.v2,s.c_sei,s.c_ne,s.alpha_pe,s.c_e})check_span(a,m.nc);
  std::vector<double>rhocp(m.nc),k(m.nc,.5),source(m.nc);
  for(std::size_t p=0;p<m.nc;++p){auto e=ecm_step(s.soc.data[p],s.v1.data[p],s.v2.data[p],I,s.temperature.data[p],dt);
    std::array<double,4>c={s.c_sei.data[p],s.c_ne.data[p],s.alpha_pe.data[p],s.c_e.data[p]};
    double heat_capacity=2.5e6*m.abi.volume.data[p];rhocp[p]=2.5e6;
    s.temperature.data[p]=implicit_abuse(c,s.temperature.data[p],dt,heat_capacity,e.heat_gen);
    s.c_sei.data[p]=c[0];s.c_ne.data[p]=c[1];s.alpha_pe.data[p]=c[2];s.c_e.data[p]=c[3];
  }implicit_energy(m,view(s.temperature),rhocp,k,source,dt,h,coolant);
}

// Farassat 1A: Q is mass-normal flux; L is traction plus momentum flux.
// Source-time dot products use the instantaneous radiation vector, not d(rhat)/dt.
struct FwhPatch {
  Vec position{},normal{1,0,0},normal_dot{},surface_velocity{},surface_acceleration{};
  Vec fluid_velocity{},fluid_acceleration{},viscous_traction{},viscous_traction_dot{};
  double area=1,area_dot=0,rho=1.225,rho_dot=0,pressure=0,pressure_dot=0;
  bool permeable=false;
};
struct AcousticPressure {double thickness=0,loading=0;double total()const{return thickness+loading;}};
AcousticPressure farassat1a(const FwhPatch& f,Vec mic,double c0=343,double rho0=1.225){
  require(c0>0&&f.area>0,"invalid acoustic reference");Vec rvec=mic-f.position;double r=norm(rvec);require(r>1e-10,"receiver on source surface");
  Vec rh=rvec/r,M=f.surface_velocity/c0,Mdot=f.surface_acceleration/c0;double M2=dot(M,M),Mr=dot(M,rh),D=1-Mr;
  require(M2<1&&D>1e-6,"Formulation 1A requires a subsonic, nonsingular source");
  double vn=dot(f.surface_velocity,f.normal),vndot=dot(f.surface_acceleration,f.normal)+dot(f.surface_velocity,f.normal_dot);
  double rel=dot(f.fluid_velocity-f.surface_velocity,f.normal);
  double reldot=dot(f.fluid_acceleration-f.surface_acceleration,f.normal)+dot(f.fluid_velocity-f.surface_velocity,f.normal_dot);
  double Q=rho0*vn,Qdot=rho0*vndot;Vec L=f.normal*f.pressure-f.viscous_traction,Ldot=f.normal*f.pressure_dot+f.normal_dot*f.pressure-f.viscous_traction_dot;
  if(f.permeable){Q+=f.rho*rel;Qdot+=f.rho_dot*rel+f.rho*reldot;L=L+f.fluid_velocity*(f.rho*rel);Ldot=Ldot+f.fluid_velocity*(f.rho_dot*rel+f.rho*reldot)+f.fluid_acceleration*(f.rho*rel);}
  // Area-rate terms permit a smoothly deforming quadrature patch.
  Qdot+=Q*f.area_dot/f.area;Ldot=Ldot+L*(f.area_dot/f.area);
  double Lr=dot(L,rh),LM=dot(L,M),motion=r*dot(Mdot,rh)+c0*(Mr-M2);
  double pT=Qdot/(r*D*D)+Q*motion/(r*r*D*D*D);
  double pL=dot(Ldot,rh)/(c0*r*D*D)+(Lr-LM)/(r*r*D*D)+Lr*motion/(c0*r*r*D*D*D);
  return {pT*f.area/(4*pi),pL*f.area/(4*pi)};
}
FwhPatch interpolate(const FwhPatch& a,const FwhPatch& b,double w){
  FwhPatch f=a;auto lerp=[&](Vec x,Vec y){return x*(1-w)+y*w;};auto scalar=[&](double x,double y){return x*(1-w)+y*w;};
  f.position=lerp(a.position,b.position);f.normal=lerp(a.normal,b.normal);f.normal=f.normal/norm(f.normal);
  f.normal_dot=lerp(a.normal_dot,b.normal_dot);f.surface_velocity=lerp(a.surface_velocity,b.surface_velocity);f.surface_acceleration=lerp(a.surface_acceleration,b.surface_acceleration);
  f.fluid_velocity=lerp(a.fluid_velocity,b.fluid_velocity);f.fluid_acceleration=lerp(a.fluid_acceleration,b.fluid_acceleration);
  f.viscous_traction=lerp(a.viscous_traction,b.viscous_traction);f.viscous_traction_dot=lerp(a.viscous_traction_dot,b.viscous_traction_dot);
  f.area=scalar(a.area,b.area);f.area_dot=scalar(a.area_dot,b.area_dot);f.rho=scalar(a.rho,b.rho);f.rho_dot=scalar(a.rho_dot,b.rho_dot);f.pressure=scalar(a.pressure,b.pressure);f.pressure_dot=scalar(a.pressure_dot,b.pressure_dot);return f;
}
struct FwhHistory {
  std::vector<double>times;
  std::vector<std::vector<FwhPatch>> frames;
  FwhPatch sample(std::size_t face,double t)const{
    auto high=std::upper_bound(times.begin(),times.end(),t);std::size_t j=high==times.end()?times.size()-1:std::max<std::size_t>(1,high-times.begin());
    return interpolate(frames[j-1][face],frames[j][face],(t-times[j-1])/(times[j]-times[j-1]));
  }
  AcousticPressure observer(Vec mic,double tobs,double c0=343)const{
    require(times.size()>1&&frames.size()==times.size(),"invalid FWH history");for(std::size_t j=1;j<times.size();++j)require(times[j]>times[j-1]&&frames[j].size()==frames[0].size(),"nonmonotone time or changing patch count");
    AcousticPressure out;
    for(std::size_t f=0;f<frames[0].size();++f){double lo=times.front(),hi=times.back();auto arrival=[&](double tau){return tau+norm(mic-sample(f,tau).position)/c0;};
      if(tobs<arrival(lo)||tobs>arrival(hi))continue;
      for(int it=0;it<65;++it){double mid=(lo+hi)*.5;if(arrival(mid)>tobs)hi=mid;else lo=mid;}
      auto p=farassat1a(sample(f,(lo+hi)*.5),mic,c0);out.thickness+=p.thickness;out.loading+=p.loading;
    }return out;
  }
};
std::vector<FwhPatch> fwh_patches(const Mesh& m,std::span<const std::int32_t> selected,std::span<const double> pf,std::span<const double> pdot){
  require(pf.size()==m.nf&&pdot.size()==m.nf,"FWH face data size mismatch");std::vector<FwhPatch>out;
  for(int index:selected){require(index>=0&&static_cast<std::size_t>(index)<m.nf,"invalid FWH patch index");FwhPatch f;f.position=get(m.abi.face_center,index);Vec S=get(m.abi.area,index);f.area=norm(S);f.normal=S/f.area;f.pressure=pf[index];f.pressure_dot=pdot[index];out.push_back(f);}return out;
}

void fft(std::vector<std::complex<double>>& a){
  std::size_t n=a.size();require(n>1&&(n&(n-1))==0,"FFT size is not radix 2");
  for(std::size_t i=1,j=0;i<n;++i){std::size_t bit=n>>1;for(;j&bit;bit>>=1)j^=bit;j^=bit;if(i<j)std::swap(a[i],a[j]);}
  for(std::size_t len=2;len<=n;len<<=1){auto root=std::polar(1.0,-2*pi/len);for(std::size_t i=0;i<n;i+=len){std::complex<double>w=1;for(std::size_t j=0;j<len/2;++j){auto u=a[i+j],v=a[i+j+len/2]*w;a[i+j]=u+v;a[i+j+len/2]=u-v;w*=root;}}}
}
struct Spectrum {std::vector<double>frequency,psd,band_center,band_db;double rms=0,oaspl=0;};
Spectrum analyze(std::span<const double> signal,double fs){
  require(signal.size()>1&&fs>0,"invalid signal");std::size_t n=signal.size();std::vector<std::complex<double>>a(n);double mean=std::accumulate(signal.begin(),signal.end(),0.0)/n,U=0,var=0;
  for(std::size_t i=0;i<n;++i){double w=.5*(1-std::cos(2*pi*i/(n-1)));U+=w*w;a[i]=(signal[i]-mean)*w;var+=(signal[i]-mean)*(signal[i]-mean);}fft(a);Spectrum out;
  for(std::size_t k=0;k<=n/2;++k){out.frequency.push_back(k*fs/n);out.psd.push_back((k==0||k==n/2?1:2)*std::norm(a[k])/(fs*U));}
  out.rms=std::sqrt(var/n);out.oaspl=out.rms>0?20*std::log10(out.rms/20e-6):-std::numeric_limits<double>::infinity();
  for(int j=-20;j<=10;++j){double fc=1000*std::pow(2.0,j/3.0),low=fc/std::pow(2.0,1/6.0),high=fc*std::pow(2.0,1/6.0),power=0;
    if(high>fs/2)continue;
    for(std::size_t k=0;k<out.psd.size();++k)if(out.frequency[k]>=low&&out.frequency[k]<high)power+=out.psd[k]*fs/n;
    out.band_center.push_back(fc);out.band_db.push_back(power>0?10*std::log10(power/4e-10):-std::numeric_limits<double>::infinity());
  }return out;
}

struct MhdResult {double charge_residual;std::vector<double> face_current;};
MhdResult electric_projection(const Mesh& m,CAbiMhdState& s,const BCs& bc){
  check_vec(s.velocity,m.nc);check_vec(s.magnetic_field,m.nc);check_vec(s.current,m.nc);check_vec(s.lorentz,m.nc);
  for(CAbiSpan a:{s.conductivity,s.potential,s.joule_heat})check_span(a,m.nc);
  check_span(s.face_current,m.nf);
  std::vector<double>sigmaf(m.nf),base(m.nf),phi(view(s.potential).begin(),view(s.potential).end());
  // Steps 1-3: motional EMF, non-orthogonal electric solve, conservative face flux.
  for(std::size_t f=0;f<m.nf;++f){int p=m.abi.owner.data[f],n=m.abi.neighbor.data[f];Vec u=get(s.velocity,p),B=get(s.magnetic_field,p);double sig=s.conductivity.data[p];
    if(n>=0){u=face_mean(m,f,u,get(s.velocity,n));B=face_mean(m,f,B,get(s.magnetic_field,n));sig=face_harmonic(m,f,sig,s.conductivity.data[n]);}
    require(sig>0,"nonpositive electrical conductivity");sigmaf[f]=sig;base[f]=sig*dot(cross(u,B),get(m.abi.area,f));
  }
  auto projection=project_flux(m,sigmaf,base,bc,phi);std::copy(phi.begin(),phi.end(),s.potential.data);std::copy(projection.flux.begin(),projection.flux.end(),s.face_current.data);
  // Step 4: face-moment reconstruction, then Lorentz and nonnegative Joule heat.
  auto J=reconstruct(m,projection.flux);
  for(std::size_t p=0;p<m.nc;++p){put(s.current,p,J[p]);put(s.lorentz,p,cross(J[p],get(s.magnetic_field,p)));s.joule_heat.data[p]=dot(J[p],J[p])/s.conductivity.data[p];}
  return {projection.divergence,std::move(projection.flux)};
}

struct MomentumProjection {std::vector<double> face_velocity_flux;double divergence;};
MomentumProjection incompressible_projection(const Mesh& m,CAbiVec3& velocity,double density,double dt){
  require(density>0&&dt>0,"invalid momentum projection");std::vector<double>d(m.nf,dt/density),base(m.nf),p(m.nc);BCs impermeable(m.nf);
  for(std::size_t f=0;f<m.nf;++f){int own=m.abi.owner.data[f],nei=m.abi.neighbor.data[f];Vec u=get(velocity,own);if(nei>=0)u=face_mean(m,f,u,get(velocity,nei));base[f]=dot(u,get(m.abi.area,f));}
  auto flux=project_flux(m,d,base,impermeable,p);auto grad=gradient(m,p,impermeable,d,base);
  for(std::size_t cell=0;cell<m.nc;++cell)put(velocity,cell,get(velocity,cell)-grad[cell]*(dt/density));
  // Store/use projected face flux in continuity and advection, not reinterpolated u.
  return {std::move(flux.flux),flux.divergence};
}
MomentumProjection mhd_momentum_step(const Mesh& m,CAbiMhdState& s,const BCs& electric_bc,double density,double dt,CAbiVec3 external_acceleration){
  check_vec(external_acceleration,m.nc);require(density>0&&dt>0,"invalid MHD time step");
  for(std::size_t p=0;p<m.nc;++p)put(s.velocity,p,get(s.velocity,p)+get(external_acceleration,p)*dt);
  electric_projection(m,s,electric_bc);
  for(std::size_t p=0;p<m.nc;++p)put(s.velocity,p,get(s.velocity,p)+get(s.lorentz,p)*(dt/density));
  auto flow=incompressible_projection(m,s.velocity,density,dt);
  electric_projection(m,s,electric_bc);return flow;
}
MomentumProjection mhd_thermal_step(const Mesh& m,CAbiMhdState& state,const BCs& electric_bc,
                                   CAbiVec3 acceleration,CAbiSpan temperature,std::span<const double> rho_cp,
                                   std::span<const double> thermal_conductivity,std::span<const double> other_heat_W,
                                   double density,double dt){
  check_span(temperature,m.nc);require(other_heat_W.size()==m.nc,"MHD thermal source size mismatch");
  auto flow=mhd_momentum_step(m,state,electric_bc,density,dt,acceleration);std::vector<double>source(m.nc);
  for(std::size_t p=0;p<m.nc;++p)source[p]=other_heat_W[p]+state.joule_heat.data[p]*m.abi.volume.data[p];
  implicit_energy(m,view(temperature),rho_cp,thermal_conductivity,source,dt);return flow;
}

std::vector<double> tridiagonal(std::vector<double>a,std::vector<double>b,std::vector<double>c,std::vector<double>d){
  for(std::size_t i=1;i<b.size();++i){double f=a[i]/b[i-1];b[i]-=f*c[i-1];d[i]-=f*d[i-1];}
  std::vector<double>x(b.size());x.back()=d.back()/b.back();for(int i=static_cast<int>(x.size())-2;i>=0;--i)x[i]=(d[i]-c[i]*x[i+1])/b[i];return x;
}
double hartmann_exact(double y,double Ha){return (std::cosh(Ha)-std::cosh(Ha*y))/(std::cosh(Ha)-1);}
struct Hartmann {std::vector<double> u;double error;};
Hartmann hartmann(double Ha,int N){
  require(Ha>=1&&Ha<=100&&N>2,"invalid Hartmann benchmark");double h=2.0/N,a=-1/(h*h);
  auto v=tridiagonal(std::vector<double>(N-1,a),std::vector<double>(N-1,-2*a+Ha*Ha),std::vector<double>(N-1,a),std::vector<double>(N-1,Ha*Ha));
  double umax=*std::max_element(v.begin(),v.end());std::vector<double>u(N+1);for(int i=1;i<N;++i)u[i]=v[i-1]/umax;
  double err=0;for(int i=0;i<=N;++i)err+=std::pow(u[i]-hartmann_exact(-1+2.0*i/N,Ha),2);return {u,std::sqrt(err/(N+1))};
}

struct PyrolysisReaction {double A=1.2e7,E=105000,n=1,gamma=1,char_density=120,enthalpy=240000;};
struct PyrolysisUpdate {double density,gas_rate,energy_sink;};
PyrolysisUpdate pyrolysis(double rho,double T,double dt,const PyrolysisReaction& k={}){
  require(rho>=k.char_density&&k.char_density>0&&T>0&&dt>0&&k.n>=1&&k.gamma>=0&&k.A>=0&&k.E>=0&&k.enthalpy>=0,"invalid pyrolysis state");
  double available=rho-k.char_density,coefficient=k.gamma*k.A*std::exp(-k.E/(gas_R*T))*std::pow(available,k.n-1);
  double next=k.char_density+available/(1+dt*coefficient),rate=(rho-next)/dt;return {next,rate,rate*k.enthalpy};
}
void pyrolysis_sources(CAbiPyrolysisState& s,double dt){
  std::size_t n=s.temperature.size;for(CAbiSpan a:{s.temperature,s.density,s.gas_source,s.energy_source})check_span(a,n);
  for(std::size_t i=0;i<n;++i){auto p=pyrolysis(s.density.data[i],s.temperature.data[i],dt);s.density.data[i]=p.density;s.gas_source.data[i]=p.gas_rate;s.energy_source.data[i]=-p.energy_sink;}
}
void multistage_pyrolysis(CAbiPyrolysisState& s,std::span<CAbiPyrolysisStage> stages,double dt){
  std::size_t n=s.temperature.size;require(!stages.empty(),"empty pyrolysis mechanism");
  for(CAbiSpan a:{s.temperature,s.density,s.gas_source,s.energy_source})check_span(a,n);
  for(const auto& stage:stages)check_span(stage.density,n);
  std::fill_n(s.gas_source.data,n,0);std::fill_n(s.energy_source.data,n,0);
  for(const auto& stage:stages){
    PyrolysisReaction k{stage.A,stage.E,stage.order,stage.gamma,stage.char_density,stage.enthalpy};
    for(std::size_t i=0;i<n;++i){double before=stage.density.data[i];auto update=pyrolysis(before,s.temperature.data[i],dt,k);
      stage.density.data[i]=update.density;s.density.data[i]-=before-update.density;
      s.gas_source.data[i]+=update.gas_rate;s.energy_source.data[i]-=update.energy_sink;
    }
  }
}

Projection pyrolysis_gas_projection(const Mesh& m,std::span<const double> generation,
                                   std::span<const double> permeability,double gas_density,double gas_viscosity,
                                   const BCs& gas_bc,std::vector<double>& pressure){
  require(generation.size()==m.nc&&permeability.size()==m.nc&&gas_density>0&&gas_viscosity>0,"invalid porous-gas state");
  std::vector<double>diffusion(m.nf),base(m.nf),mass_source(m.nc);
  for(std::size_t p=0;p<m.nc;++p)mass_source[p]=generation[p]*m.abi.volume.data[p];
  for(std::size_t f=0;f<m.nf;++f){int p=m.abi.owner.data[f],n=m.abi.neighbor.data[f];double K=n<0?permeability[p]:face_harmonic(m,f,permeability[p],permeability[n]);require(K>0,"nonpositive permeability");diffusion[f]=gas_density*K/gas_viscosity;}
  // Quasi-steady Darcy gas continuity: div(-rho_g K/mu grad(p_g)) = gas_generation.
  return project_flux(m,diffusion,base,gas_bc,pressure,mass_source);
}
std::vector<double> gas_enthalpy_outflow(const Mesh& m,const std::vector<double>& mass_flux,
                                       std::span<const double> temperature,double gas_cp=1200,double Tref=300){
  require(mass_flux.size()==m.nf&&temperature.size()==m.nc&&gas_cp>0,"invalid gas enthalpy input");std::vector<double>out(m.nc);
  for(std::size_t f=0;f<m.nf;++f){int p=m.abi.owner.data[f],n=m.abi.neighbor.data[f];double q=mass_flux[f];
    double Tup=q>=0?temperature[p]:n>=0?temperature[n]:Tref;double h=q*gas_cp*(Tup-Tref);out[p]+=h;if(n>=0)out[n]-=h;
  }return out;
}
struct PorousSolidResult {Projection gas;std::vector<double>pressure;};
PorousSolidResult porous_solid_step(const Mesh& m,CAbiPyrolysisState& state,
                                   std::span<const double> thermal_conductivity,std::span<const double> permeability,
                                   const BCs& gas_bc,std::span<const double> boundary_heat_W,double dt,
                                   double solid_cp=1200,double gas_density=.3,double gas_viscosity=3e-5){
  require(thermal_conductivity.size()==m.nc&&boundary_heat_W.size()==m.nf&&solid_cp>0,"porous solid field size mismatch");
  pyrolysis_sources(state,dt);std::vector<double>pressure(m.nc);
  auto gas=pyrolysis_gas_projection(m,view(state.gas_source),permeability,gas_density,gas_viscosity,gas_bc,pressure);
  auto enthalpy=gas_enthalpy_outflow(m,gas.flux,view(state.temperature));std::vector<double>capacity(m.nc),source(m.nc);
  for(std::size_t p=0;p<m.nc;++p){capacity[p]=state.density.data[p]*solid_cp;source[p]=state.energy_source.data[p]*m.abi.volume.data[p]-enthalpy[p];}
  for(std::size_t f=0;f<m.nf;++f)if(m.abi.neighbor.data[f]<0)source[m.abi.owner.data[f]]+=boundary_heat_W[f];
  // boundary_heat_W is net conduction into the solid AFTER a wall balance; do
  // not count a supplied boundary enthalpy contribution twice in the heat input.
  implicit_energy(m,view(state.temperature),capacity,thermal_conductivity,source,dt);
  return {std::move(gas),std::move(pressure)};
}
struct WallBalance {double Tw,conduction,radiation,pyrolysis,ablation,mchar,residual;};
WallBalance wall_balance(double incident,double interior,double distance,double conductivity,double gas_flux,double Tinf=300){
  require(incident>=0&&interior>0&&distance>0&&conductivity>0&&gas_flux>=0,"invalid TPS wall input");
  auto evaluate=[&](double T){double rad=.85*sigma_SB*(std::pow(T,4)-std::pow(Tinf,4)),pyro=gas_flux*240000,mchar=3000*std::exp(-200000/(gas_R*T)),abl=mchar*2e7,cond=conductivity*(T-interior)/distance;return WallBalance{T,cond,rad,pyro,abl,mchar,cond+rad+pyro+abl-incident};};
  double lo=1,hi=6000;require(evaluate(lo).residual<=0&&evaluate(hi).residual>=0,"wall balance not bracketed");
  for(int i=0;i<90;++i){double mid=(lo+hi)*.5;if(evaluate(mid).residual>0)hi=mid;else lo=mid;}return evaluate((lo+hi)*.5);
}
Vec recession_velocity(double mchar,double rho_wall,Vec normal){require(mchar>=0&&rho_wall>0&&std::abs(norm(normal)-1)<1e-9,"invalid recession normal");return normal*(mchar/rho_wall);}
Vec ale_node_bdf2(Vec xn,Vec xnm1,Vec prescribed_velocity,double dt){require(dt>0,"invalid ALE dt");return (xn*4-xnm1+prescribed_velocity*(2*dt))/3;}
double bdf2_volume_rate(double Vnp1,double Vn,double Vnm1,double dt){return (3*Vnp1-4*Vn+Vnm1)/(2*dt);}
double swept_bdf2_flux(double swept_n_to_np1,double swept_nm1_to_n,double dt){return (3*swept_n_to_np1-swept_nm1_to_n)/(2*dt);}

void check_poly(const CAbiPolyConnectivity& c,const CAbiVec3& nodes){
  std::size_t nf=c.owner.size;check_vec(nodes,nodes.x.size);
  require(c.cells>0&&nf>0&&c.owner.data&&c.neighbor.data&&c.face_offsets.data&&c.face_nodes.data,"null polyhedral connectivity");
  require(c.neighbor.size==nf&&c.face_offsets.size==nf+1&&c.face_offsets.data[0]==0,"invalid polygon offsets");
  require(c.face_offsets.data[nf]>=0&&static_cast<std::uint64_t>(c.face_offsets.data[nf])==c.face_nodes.size,"invalid polygon index count");
  for(std::size_t f=0;f<nf;++f){require(c.face_offsets.data[f+1]-c.face_offsets.data[f]>=3,"face has fewer than three vertices");
    int p=c.owner.data[f],n=c.neighbor.data[f];require(p>=0&&static_cast<std::uint64_t>(p)<c.cells&&n>=-1&&(n<0||static_cast<std::uint64_t>(n)<c.cells),"invalid polygon owner-neighbor");}
  for(std::size_t j=0;j<c.face_nodes.size;++j){int v=c.face_nodes.data[j];require(v>=0&&static_cast<std::uint64_t>(v)<nodes.x.size,"invalid polygon vertex");}
}
std::vector<double> polyhedral_volumes(const CAbiPolyConnectivity& c,const CAbiVec3& nodes){
  check_poly(c,nodes);std::vector<double>V(c.cells);
  for(std::size_t f=0;f<c.owner.size;++f){int first=c.face_offsets.data[f],end=c.face_offsets.data[f+1];Vec a=get(nodes,c.face_nodes.data[first]);double vol=0;
    for(int j=first+1;j<end-1;++j)vol+=dot(a,cross(get(nodes,c.face_nodes.data[j]),get(nodes,c.face_nodes.data[j+1])))/6;
    V[c.owner.data[f]]+=vol;if(c.neighbor.data[f]>=0)V[c.neighbor.data[f]]-=vol;
  }for(double v:V)require(v>0,"inverted or inconsistently oriented polyhedron");return V;
}
double triangle_sweep(Vec a,Vec b,Vec c,Vec A,Vec B,Vec C){
  Vec velocity_integral=((A-a)+(B-b)+(C-c))/3;
  Vec S0=cross(b-a,c-a)*.5,S1=cross(B-A,C-A)*.5;
  Vec Sm=cross((b+B-a-A)*.5,(c+C-a-A)*.5)*.5;
  // Linear vertex trajectories give a quadratic area vector. Simpson quadrature
  // is exact for its swept-volume integral, including face rotation/deformation.
  return dot(velocity_integral,(S0+Sm*4+S1)/6);
}
std::vector<double> polyhedral_ale_flux(const CAbiPolyConnectivity& c,const CAbiVec3& nm1,
                                      const CAbiVec3& now,const CAbiVec3& next,double dt){
  require(dt>0,"invalid polyhedral ALE dt");check_poly(c,now);check_vec(nm1,now.x.size);check_vec(next,now.x.size);std::vector<double>flux(c.owner.size);
  for(std::size_t f=0;f<c.owner.size;++f){int start=c.face_offsets.data[f],end=c.face_offsets.data[f+1],a=c.face_nodes.data[start];double forward=0,backward=0;
    for(int j=start+1;j<end-1;++j){int b=c.face_nodes.data[j],v=c.face_nodes.data[j+1];
      forward+=triangle_sweep(get(now,a),get(now,b),get(now,v),get(next,a),get(next,b),get(next,v));
      backward+=triangle_sweep(get(nm1,a),get(nm1,b),get(nm1,v),get(now,a),get(now,b),get(now,v));
    }flux[f]=swept_bdf2_flux(forward,backward,dt);
  }return flux;
}
void move_ale_nodes(CAbiVec3& next,const CAbiVec3& now,const CAbiVec3& previous,const CAbiVec3& velocity,double dt){
  std::size_t count=now.x.size;check_vec(now,count);check_vec(previous,count);check_vec(next,count);check_vec(velocity,count);
  for(std::size_t i=0;i<count;++i)put(next,i,ale_node_bdf2(get(now,i),get(previous,i),get(velocity,i),dt));
}
void rebuild_polyhedral_geometry(CAbiMesh& mesh,const CAbiPolyConnectivity& c,const CAbiVec3& nodes){
  check_poly(c,nodes);std::size_t nc=c.cells,nf=c.owner.size;check_span(mesh.volume,nc);check_span(mesh.alpha,nf);
  check_vec(mesh.center,nc);check_vec(mesh.face_center,nf);check_vec(mesh.area,nf);check_vec(mesh.correction,nf);
  require(mesh.owner.data&&mesh.neighbor.data&&mesh.owner.size==nf&&mesh.neighbor.size==nf,"ALE geometry topology mismatch");
  for(std::size_t f=0;f<nf;++f)require(mesh.owner.data[f]==c.owner.data[f]&&mesh.neighbor.data[f]==c.neighbor.data[f],"ALE geometry connectivity mismatch");
  auto volumes=polyhedral_volumes(c,nodes);std::vector<Vec>centroid(nc);
  for(std::size_t f=0;f<nf;++f){int start=c.face_offsets.data[f],end=c.face_offsets.data[f+1];Vec a=get(nodes,c.face_nodes.data[start]),S{};
    for(int j=start+1;j<end-1;++j)S=S+cross(get(nodes,c.face_nodes.data[j])-a,get(nodes,c.face_nodes.data[j+1])-a)*.5;
    double area=norm(S);require(area>0,"collapsed moving face");Vec normal=S/area,face_centroid{};
    for(int j=start;j<end;++j){Vec d=get(nodes,c.face_nodes.data[j])-a;require(std::abs(dot(d,normal))<1e-9*std::max(1.0,norm(d)),"warped ALE face: triangulate it before FVM geometry reconstruction");}
    for(int j=start+1;j<end-1;++j){Vec b=get(nodes,c.face_nodes.data[j]),v=get(nodes,c.face_nodes.data[j+1]);
      double signed_volume=dot(a,cross(b,v))/6;Vec moment=(a+b+v)*(signed_volume/4);
      centroid[c.owner.data[f]]=centroid[c.owner.data[f]]+moment;if(c.neighbor.data[f]>=0)centroid[c.neighbor.data[f]]=centroid[c.neighbor.data[f]]-moment;
      double signed_area=dot(cross(b-a,v-a)*.5,normal);face_centroid=face_centroid+(a+b+v)*(signed_area/3);
    }put(mesh.face_center,f,face_centroid/area);put(mesh.area,f,S);
  }
  for(std::size_t p=0;p<nc;++p){mesh.volume.data[p]=volumes[p];put(mesh.center,p,centroid[p]/volumes[p]);}
  for(std::size_t f=0;f<nf;++f){int p=c.owner.data[f],n=c.neighbor.data[f];Vec S=get(mesh.area,f),d=(n<0?get(mesh.face_center,f):get(mesh.center,n))-get(mesh.center,p);double denom=dot(S,d);require(denom>0,"ALE face-center orientation invalid");double alpha=dot(S,S)/denom;mesh.alpha.data[f]=alpha;put(mesh.correction,f,S-d*alpha);}
}

// A DGCL projection is useful after geometric swept-face flux construction on a
// polyhedral ALE mesh. It enforces the BDF2 volume identity cell by cell.
Projection enforce_dgcl(const Mesh& m,const std::vector<double>& raw_mesh_flux,const std::vector<double>& Vnp1,
                        const std::vector<double>& Vn,const std::vector<double>& Vnm1,double dt){
  require(Vnp1.size()==m.nc&&Vn.size()==m.nc&&Vnm1.size()==m.nc,"ALE volume size mismatch");
  std::vector<double>target(m.nc),diff(m.nf,1),psi(m.nc);BCs bc(m.nf);
  for(std::size_t p=0;p<m.nc;++p){require(Vnp1[p]>0,"ALE cell inversion");target[p]=bdf2_volume_rate(Vnp1[p],Vn[p],Vnm1[p],dt);}
  for(std::size_t f=0;f<m.nf;++f)if(m.abi.neighbor.data[f]<0)bc[f].value=raw_mesh_flux[f];
  return project_flux(m,diff,raw_mesh_flux,bc,psi,target);
}

struct TpsColumn {
  int n;double initial_length,wall_n=0,wall_nm1=0,recession=0;
  std::vector<double>T,rho,Vn,Vnm1;
  explicit TpsColumn(int cells=60,double length=.03):n(cells),initial_length(length),T(cells,300),rho(cells,280),Vn(cells,length/cells),Vnm1(Vn){require(cells>2&&length>0,"invalid TPS column");}
  WallBalance advance(double incident,double dt,bool first_step){
    double old_length=initial_length-wall_n,dx=old_length/n,gas=0;
    std::vector<double>sink(n);for(int i=0;i<n;++i){auto p=pyrolysis(rho[i],T[i],dt);rho[i]=p.density;sink[i]=p.energy_sink;gas+=p.gas_rate*dx;}
    auto wall=wall_balance(incident,T[0],dx*.5,.35,gas);
    // Initial Euler node move, followed by BDF2. Positive material-normal speed
    // moves the exposed wall into the solid; the rear wall remains fixed.
    double velocity=wall.mchar/rho[0];double next_wall=first_step?wall_n+dt*velocity:(4*wall_n-wall_nm1+2*dt*velocity)/3;
    require(next_wall<initial_length*.9,"TPS column consumed: remesh or terminate");double next_dx=(initial_length-next_wall)/n;
    std::vector<double>Vnew(n,next_dx),mesh_flux(n+1),gas_flux(n+1);mesh_flux[n]=0;gas_flux[n]=0;
    for(int i=0;i<=n;++i){double fn=wall_n+(initial_length-wall_n)*i/n,fm=wall_nm1+(initial_length-wall_nm1)*i/n,fp=next_wall+(initial_length-next_wall)*i/n;
      mesh_flux[i]=first_step?(fp-fn)/dt:swept_bdf2_flux(fp-fn,fn-fm,dt);
    }
    // Gas drains toward the hot surface. In-solid convection uses a conservative
    // upwind enthalpy flux; pyrolysis formation enthalpy is a separate sink.
    for(int i=n-1;i>=0;--i)gas_flux[i]=gas_flux[i+1]-sink[i]/240000*dx;
    std::vector<double>a(n),b(n),c(n),rhs(n);double order=first_step?1:1.5;
    for(int i=0;i<n;++i){double capacity=rho[i]*1200,cond=.35/next_dx;
      double historical=first_step?Vn[i]*T[i]:2*Vn[i]*T[i]-.5*Vnm1[i]*previous_T[i];
      b[i]=order*capacity*Vnew[i]/dt;rhs[i]=capacity*historical/dt-sink[i]*dx;
      if(i>0){a[i]-=cond;b[i]+=cond;}else{b[i]+=2*cond;rhs[i]+=2*cond*wall.Tw;}
      if(i<n-1){c[i]-=cond;b[i]+=cond;}
      // Relative ALE material-energy advection: -rho cp * mesh velocity.
      double left=-capacity*mesh_flux[i],right=-capacity*mesh_flux[i+1];
      if(i>0){if(left>=0)a[i]-=left;else b[i]-=left;}else{if(left>=0)rhs[i]+=left*wall.Tw;else b[i]-=left;}
      if(i<n-1){if(right>=0)b[i]+=right;else c[i]+=right;}
      double gas_right_T=i+1<n?T[i+1]:T[i],gas_left_T=T[i];
      rhs[i]-=1200*(gas_flux[i+1]*gas_right_T-gas_flux[i]*gas_left_T);
    }
    auto next_T=tridiagonal(a,b,c,rhs);previous_T=T;T=std::move(next_T);Vnm1=Vn;Vn=Vnew;wall_nm1=wall_n;wall_n=next_wall;recession=next_wall;return wall;
  }
private:std::vector<double>previous_T=std::vector<double>(n,300);
};

struct Test {std::string name,module,detail,value;bool passed;};
std::string scientific(double x){std::ostringstream s;s<<std::scientific<<std::setprecision(3)<<x;return s.str();}
std::vector<Test> verify(){
  std::vector<Test>tests;auto add=[&](std::string name,std::string module,std::string detail,double error,double tolerance,std::string unit="error"){
    tests.push_back({name,module,detail,scientific(error)+" "+unit,std::isfinite(error)&&error<=tolerance});};
  double soc=1,v1=0,v2=0;auto e=ecm_step(soc,v1,v2,5,298.15,360);
  add("Coulomb counting","battery","5 A, 5 Ah, 360 s; analytical SOC = 0.9.",std::abs(soc-.9),1e-13,"SOC error");
  add("2-RC exact integration","battery","Both frozen-coefficient RC branches versus their exponential closed forms.",std::max(std::abs(v1-.03*(1-std::exp(-360/14.4))),std::abs(v2-.06*(1-std::exp(-360/132.0)))),1e-13,"V error");
  double sp=.8,sm=.8,vp1=0,vp2=0,vm1=0,vm2=0;auto ep=ecm_step(sp,vp1,vp2,0,298.16,.001),em=ecm_step(sm,vm1,vm2,0,298.14,.001);
  add("OCV entropic derivative","battery","The temperature-dependent OCV law and the specified I*T*dU/dT coefficient use the same derivative.",std::abs((ep.ocv-em.ocv)/.02+.00012),1e-11,"V/K error");
  add("Discharge heat sign","battery","Signed I(Vterm-Uocv) is negative; generated heat uses the opposite sign.",e.signed_irr<0&&e.heat_gen>0?0:1,0);
  std::array<double,4>c0={.15,.75,.04,1};auto stiff=patankar(c0,2000,1e6);double violation=0;for(double c:stiff.state)violation=std::max(violation,std::max(-c,c-1));
  add("Stiff-source boundedness","battery","2000 K, dt = 1e6 s; all four Patankar reaction states remain in [0,1].",violation,0);
  auto c=c0;double Tnext=implicit_abuse(c,450,2,65,10);double released=0;for(int k=0;k<4;++k)released+=std::abs(c[k]-c0[k])*abuse[k].mass*abuse[k].H;
  add("Reaction-energy closure","battery","Coupled implicit temperature and reaction extent conserve released chemical energy.",std::abs(65*(Tnext-450)-20-released),1e-8,"J residual");
  auto two=box_mesh(2,1,1);Mesh tm(two.abi());std::vector<double>T={300,400},cp(2,1000),k(2,.3),source(2);implicit_energy(tm,T,cp,k,source,.7);
  add("FVM thermal conservation","battery","Closed two-cell system: internal diffusive heat cancels exactly.",std::abs(T[0]+T[1]-700),1e-10,"K-sum error");
  FwhPatch f;f.area=.02;f.pressure=120;f.pressure_dot=450;double r=3;auto sound=farassat1a(f,{r,0,0});double exact=f.area/(4*pi)*(450/(343*r)+120/(r*r));
  add("Curle stationary limit","acoustics","Farassat 1A reduces to the stationary loading dipole, including its near-field term.",std::abs(sound.loading-exact)+std::abs(sound.thickness),1e-14,"Pa error");
  FwhPatch moving;moving.surface_velocity={.3*343,0,0};moving.surface_acceleration={1,0,0};moving.normal={0,1,0};moving.normal_dot={1/(1.225*.3*343),0,0};auto doppler=farassat1a(moving,{3,0,0});
  add("Subsonic Doppler term","acoustics","Isolated Qdot term at Mr=0.3 scales with (1-Mr)^-2.",std::abs(doppler.thickness-1/(4*pi*3*.7*.7)),1e-13,"Pa error");
  FwhHistory history;for(int j=0;j<=100;++j){double t=j*.001;FwhPatch p;p.area=.02;p.pressure=std::sin(2*pi*10*t);p.pressure_dot=2*pi*10*std::cos(2*pi*10*t);history.times.push_back(t);history.frames.push_back({p});}
  double tau=.025,tobs=tau+3/343.0;auto ret=history.observer({3,0,0},tobs);auto expected=farassat1a(history.frames[25][0],{3,0,0});
  add("Retarded-time recovery","acoustics","Monotone arrival-time bisection recovers a known source time.",std::abs(ret.total()-expected.total()),1e-13,"Pa error");
  std::vector<double>signal(2048);for(std::size_t i=0;i<signal.size();++i)signal[i]=std::sqrt(2.0)*std::sin(2*pi*128*i/signal.size());auto spec=analyze(signal,2048);
  double integrated=std::accumulate(spec.psd.begin(),spec.psd.end(),0.0);
  add("One-sided Hann PSD","acoustics","Unit-RMS, 128 Hz signal; window-power normalized PSD integrates to 1 Pa2.",std::abs(integrated-1),1e-6,"Pa2 error");
  add("OASPL pressure reference","acoustics","1 Pa RMS / 20 microPa = 93.9794000867 dB.",std::abs(spec.oaspl-93.9794000867),1e-8,"dB error");
  double worst=0;for(double Ha:{1.,10.,25.,50.,100.})worst=std::max(worst,hartmann(Ha,1024).error);
  add("Hartmann Ha 1-100","mhd","Five Hartmann numbers; 1024 intervals; RMS normalized velocity error.",worst,1e-4,"L2 error");
  double ratio=hartmann(10,128).error/hartmann(10,256).error;
  add("Hartmann grid convergence","mhd","Second-order channel operator; coarse/fine error ratio approaches four.",std::abs(ratio-4),.12,"ratio deviation");
  auto owned=box_mesh(4,3,2,{1,1,1},.32);Mesh mesh(owned.abi());std::vector<double>flux(mesh.nf);Vec J0={2,-3,.75};
  for(std::size_t face=0;face<mesh.nf;++face)flux[face]=dot(J0,get(mesh.abi.area,face));
  auto reconstructed=reconstruct(mesh,flux);double moment_err=0;
  for(auto J:reconstructed)moment_err=std::max(moment_err,norm(J-J0));
  add("Polyhedral current moment","mhd","Affine-skewed 3D cells: sum Jn,f (xf-xP) / V exactly reconstructs a constant current.",moment_err,1e-12,"A/m2 error");
  std::vector<double>d(mesh.nf,1),base(mesh.nf),phi(mesh.nc);BCs bc(mesh.nf);Vec exact_grad={1,2,-.5};
  for(std::size_t face=0;face<mesh.nf;++face)if(mesh.abi.neighbor.data[face]<0)bc[face]={BcType::Dirichlet,dot(exact_grad,get(mesh.abi.face_center,face))};
  auto projected=project_flux(mesh,d,base,bc,phi);double phierr=0;for(std::size_t p=0;p<mesh.nc;++p)phierr=std::max(phierr,std::abs(phi[p]-dot(exact_grad,get(mesh.abi.center,p))));
  add("Non-orthogonal potential","mhd","Over-relaxed alpha and deferred correction reproduce a manufactured linear potential.",phierr,1e-8,"V error");
  add("Discrete charge conservation","mhd","The reconstructed face flux uses the same stencil as the elliptic potential solve.",projected.divergence,3e-10,"A/cell residual");
  OwnedVec U(mesh.nc),Bfield(mesh.nc),Jfield(mesh.nc),force(mesh.nc);
  std::vector<double>sig(mesh.nc,3.46),potential(mesh.nc),jf(mesh.nf),joule(mesh.nc);
  auto span=[](std::vector<double>& v){return CAbiSpan{v.data(),v.size()};};
  CAbiMhdState state{U.abi(),Bfield.abi(),Jfield.abi(),force.abi(),span(sig),span(potential),span(jf),span(joule)};
  Vec u={1,.2,0},B={0,2,0};for(std::size_t p=0;p<mesh.nc;++p){put(state.velocity,p,u);put(state.magnetic_field,p,B);}
  BCs insulating(mesh.nf);electric_projection(mesh,state,insulating);double blocked=0;
  for(std::size_t p=0;p<mesh.nc;++p)blocked=std::max(blocked,norm(get(state.current,p)));
  add("Insulating motional cancellation","mhd","Full SoA electric projection cancels a uniform motional EMF with grad(phi).",blocked,1e-8,"A/m2 error");
  BCs conducting(mesh.nf);for(std::size_t f=0;f<mesh.nf;++f)if(mesh.abi.neighbor.data[f]<0)conducting[f]={BcType::Dirichlet,0};
  std::fill(potential.begin(),potential.end(),0);electric_projection(mesh,state,conducting);double work_error=0;
  for(std::size_t p=0;p<mesh.nc;++p)work_error=std::max(work_error,std::abs(dot(u,get(state.lorentz,p))+joule[p]));
  add("Lorentz-Joule work identity","mhd","Full face-flux reconstruction and source assembly: u dot (J cross B) + |J|2/sigma = 0 for grad(phi)=0.",work_error,1e-9,"W/m3 residual");
  for(std::size_t p=0;p<mesh.nc;++p){Vec x=get(mesh.abi.center,p);put(state.velocity,p,{x[0]*x[0],x[0]*x[1],x[2]*x[2]});}
  auto momentum=incompressible_projection(mesh,state.velocity,1000,.01);
  add("Incompressible flux projection","mhd","Skewed collocated 3D grid; projected velocity face flux is divergence free.",momentum.divergence,1e-9,"m3/s per-cell residual");
  OwnedVec acceleration(mesh.nc);std::vector<double>mhd_temperature(mesh.nc,300),mhd_rhocp(mesh.nc,2.5e6),mhd_k(mesh.nc,.35),other_heat(mesh.nc);
  for(std::size_t p=0;p<mesh.nc;++p){put(state.velocity,p,{0,0,0});put(state.magnetic_field,p,{0,0,0});}std::fill(potential.begin(),potential.end(),0);
  mhd_thermal_step(mesh,state,bc,acceleration.abi(),span(mhd_temperature),mhd_rhocp,mhd_k,other_heat,1000,.01);double gained=0,ohmic=0;
  for(std::size_t p=0;p<mesh.nc;++p){gained+=mhd_rhocp[p]*(mhd_temperature[p]-300)*mesh.abi.volume.data[p];ohmic+=.01*joule[p]*mesh.abi.volume.data[p];}
  add("Joule heat-energy coupling","mhd","Electric-potential driven conduction: the common implicit FVM energy equation receives exactly J2/sigma times cell volume.",std::abs(gained-ohmic),5e-6,"J residual");
  auto pyro=pyrolysis(280,2000,1e6);add("Pyrolysis positivity","tps","Stiff virgin-to-char Patankar step respects rho_char and the initial density.",std::max({120-pyro.density,pyro.density-280,0.0}),0,"kg/m3 violation");
  add("Pyrolysis mass-enthalpy","tps","Released gas mass times formation enthalpy equals the solid-energy sink.",std::abs(pyro.energy_sink-pyro.gas_rate*240000),1e-14,"W/m3 error");
  std::vector<double>temperature={1100},density={300},gas_source(1),energy_source(1),phase1={150},phase2={120};
  CAbiPyrolysisState pyro_state{span(temperature),span(density),span(gas_source),span(energy_source)};
  std::array<CAbiPyrolysisStage,2>stages={{{span(phase1),1.2e7,105000,1,1,50,240000},{span(phase2),3e8,135000,2,1,40,300000}}};
  multistage_pyrolysis(pyro_state,stages,.01);
  double multi_error=std::abs(density[0]-(30+phase1[0]+phase2[0]))+std::abs((300-density[0])/.01-gas_source[0]);
  add("Multi-stage pyrolysis mass","tps","Two nonlinear Arrhenius constituents plus inert mass; stage gas sources sum to bulk mass loss.",multi_error,1e-9,"kg/m3/s residual");
  std::vector<double>generation(mesh.nc,.001),permeability(mesh.nc,1e-11),pg(mesh.nc);BCs gas_bc(mesh.nf);gas_bc[0]={BcType::Dirichlet,0};
  auto gasflow=pyrolysis_gas_projection(mesh,generation,permeability,.3,3e-5,gas_bc,pg);double gasout=0;
  for(std::size_t face=0;face<mesh.nf;++face)if(mesh.abi.neighbor.data[face]<0)gasout+=gasflow.flux[face];
  add("3D porous-gas blowing","tps","Quasi-steady Darcy projection on a skewed 3D mesh: boundary gas outflow equals integrated pyrolysis generation.",std::abs(gasout-.001),1e-8,"kg/s residual");
  std::vector<double>gasT(mesh.nc);for(std::size_t p=0;p<mesh.nc;++p)gasT[p]=700+20*get(mesh.abi.center,p)[0];
  auto gas_h=gas_enthalpy_outflow(mesh,gasflow.flux,gasT);double boundary_h=0;
  for(std::size_t face=0;face<mesh.nf;++face)if(mesh.abi.neighbor.data[face]<0)boundary_h+=gasflow.flux[face]*1200*(gasT[mesh.abi.owner.data[face]]-300);
  add("Gas enthalpy conservation","tps","Upwind internal gas-energy fluxes cancel pairwise; net cell outflow equals boundary enthalpy flux.",std::abs(std::accumulate(gas_h.begin(),gas_h.end(),0.0)-boundary_h),1e-9,"W residual");
  std::vector<double>solid_T(mesh.nc,700),solid_rho(mesh.nc,280),solid_gas(mesh.nc),solid_heat(mesh.nc),solid_k(mesh.nc,.35),surface_heat(mesh.nf);
  CAbiPyrolysisState solid{span(solid_T),span(solid_rho),span(solid_gas),span(solid_heat)};
  auto porous=porous_solid_step(mesh,solid,solid_k,permeability,gas_bc,surface_heat,.0001);bool valid_solid=true;
  for(std::size_t p=0;p<mesh.nc;++p)valid_solid=valid_solid&&std::isfinite(solid_T[p])&&solid_T[p]>0&&solid_rho[p]>=120;
  add("3D porous thermal coupling","tps","Full SoA source, Darcy blowing, gas enthalpy transport, and polyhedral implicit solid-energy step.",valid_solid?porous.gas.divergence:1e10,1e-7,"kg/s per-cell residual");
  auto wall=wall_balance(350000,800,.0005,.35,.002);
  add("TPS surface energy balance","tps","Incident heating = conduction + reradiation + gas enthalpy + sublimation.",std::abs(wall.residual),1e-7,"W/m2 residual");
  double Vp=.98,V=.99,Vm=1,dt=1;double g=swept_bdf2_flux(Vp-V,V-Vm,dt);
  add("BDF2 discrete GCL","tps","Signed swept-face BDF2 flux equals the BDF2 cell-volume derivative.",std::abs(g-bdf2_volume_rate(Vp,V,Vm,dt)),1e-14,"m3/s error");
  double constant=17,qnew=(4*V*constant-Vm*constant+2*dt*g*constant)/(3*Vp);
  add("ALE free-stream preservation","tps","Uniform field remains uniform on a receding 1D mesh satisfying DGCL.",std::abs(qnew-constant),1e-12,"field error");
  std::vector<double>raw(mesh.nf),Vnew(mesh.nc),Vold(mesh.nc),Volder(mesh.nc);
  for(std::size_t p=0;p<mesh.nc;++p){double v=mesh.abi.volume.data[p];Vnew[p]=.99*v;Vold[p]=v;Volder[p]=1.01*v;}
  for(std::size_t f=0;f<mesh.nf;++f){Vec xf=get(mesh.abi.face_center,f);raw[f]=dot(Vec{-.01*xf[0],0,0},get(mesh.abi.area,f));if(mesh.abi.neighbor.data[f]>=0)raw[f]+=.001*std::sin(static_cast<double>(f));}
  auto gcl=enforce_dgcl(mesh,raw,Vnew,Vold,Volder,1);
  add("Polyhedral DGCL projection","tps","Perturbed internal mesh fluxes on a skewed 3D topology are corrected to the BDF2 volume derivative.",gcl.divergence,1e-9,"m3/s residual");
  std::vector<std::int32_t>tet_owner(4,0),tet_neighbor(4,-1),offsets={0,3,6,9,12},indices={0,2,1,0,1,3,0,3,2,1,2,3};
  CAbiPolyConnectivity connectivity{1,{tet_owner.data(),4},{tet_neighbor.data(),4},{offsets.data(),5},{indices.data(),12}};
  OwnedVec nodes_now(4),nodes_previous(4),nodes_next(4),node_velocity(4);
  auto xn=nodes_now.abi(),xm=nodes_previous.abi(),xp=nodes_next.abi(),vel=node_velocity.abi();
  std::array<Vec,4>reference={{{0,0,0},{1,0,0},{0,1,0},{0,0,1}}};
  for(std::size_t i=0;i<4;++i){Vec X=reference[i],v={-.01*X[0],-.02*X[1],-.03*X[2]};put(xn,i,X);put(xm,i,X-v);put(vel,i,v);}
  move_ale_nodes(xp,xn,xm,vel,1);double node_error=0;
  for(std::size_t i=0;i<4;++i)node_error=std::max(node_error,norm(get(xp,i)-get(xn,i)-get(vel,i)));
  add("BDF2 nodal recession","tps","SoA 3D boundary-node motion reproduces an affine material-normal recession exactly.",node_error,1e-14,"m error");
  auto swept=polyhedral_ale_flux(connectivity,xm,xn,xp,1);auto vp=polyhedral_volumes(connectivity,xp),vn=polyhedral_volumes(connectivity,xn),vm=polyhedral_volumes(connectivity,xm);
  add("Exact 3D swept-volume DGCL","tps","Triangle-sweep quadrature on a deforming tetrahedron: face-flux sum equals BDF2 volume change without a correction solve.",std::abs(std::accumulate(swept.begin(),swept.end(),0.0)-bdf2_volume_rate(vp[0],vn[0],vm[0],1)),1e-14,"m3/s error");
  OwnedMesh tetra;tetra.center=OwnedVec(1);tetra.face=OwnedVec(4);tetra.area=OwnedVec(4);tetra.corr=OwnedVec(4);tetra.volume.resize(1);tetra.alpha.resize(4);tetra.owner=tet_owner;tetra.neighbor=tet_neighbor;auto tetra_abi=tetra.abi();
  rebuild_polyhedral_geometry(tetra_abi,connectivity,xp);Mesh tetra_mesh(tetra_abi);std::vector<double>tet_flux(4);
  for(std::size_t face=0;face<4;++face)tet_flux[face]=dot(J0,get(tetra_abi.area,face));
  auto tet_J=reconstruct(tetra_mesh,tet_flux);
  add("ALE geometry-current consistency","tps","Updated polyhedral centroids, volumes, area vectors, alpha and correction preserve the constant-current moment identity.",norm(tet_J[0]-J0),1e-12,"A/m2 error");
  TpsColumn column;bool finite=true;for(int i=0;i<100;++i){column.advance(350000,.01,i==0);for(double value:column.T)finite=finite&&std::isfinite(value)&&value>0;}
  add("ALE TPS coupled smoke test","tps","One second of pyrolysis, blowing, nonlinear wall balance, conduction, and recession.",finite&&column.recession>=0?0:1,0);
  return tests;
}

std::string escape_json(const std::string& s){std::string out;for(char c:s){if(c=='"'||c=='\\')out+='\\';if(c=='\n')out+="\\n";else out+=c;}return out;}
void print_report(const std::vector<Test>& tests,bool json){
  if(json){std::cout<<"{\n  \"compiler\": \"g++ "<<__VERSION__<<" / C++20 / -O2\",\n  \"command\": \"g++ -std=c++20 -O2 cluster_c.cpp -o cluster_c && ./cluster_c --verify --json\",\n  \"tests\": [\n";
    for(std::size_t i=0;i<tests.size();++i){const auto&t=tests[i];std::cout<<"    {\"name\":\""<<escape_json(t.name)<<"\",\"module\":\""<<t.module<<"\",\"detail\":\""<<escape_json(t.detail)<<"\",\"value\":\""<<escape_json(t.value)<<"\",\"passed\":"<<(t.passed?"true":"false")<<"}"<<(i+1==tests.size()?"\n":",\n");}std::cout<<"  ]\n}\n";
  }else for(const auto&t:tests)std::cout<<(t.passed?"PASS":"FAIL")<<" | "<<t.module<<" | "<<t.name<<" | "<<t.value<<'\n';
}

void demo(const std::string& module){
  if(module=="mhd"){auto h=hartmann(25,256);std::cout<<"y_over_L,numerical,analytical\n";for(std::size_t i=0;i<h.u.size();++i){double y=-1+2.0*i/(h.u.size()-1);std::cout<<y<<','<<h.u[i]<<','<<hartmann_exact(y,25)<<'\n';}}
  else if(module=="fwh"){std::cout<<"time_s,thickness_Pa,loading_Pa\n";for(int i=0;i<2048;++i){double t=i/8192.0,tau=t-5/343.0;FwhPatch f;f.area=.025;f.pressure=360*std::sin(2*pi*160*tau);f.pressure_dot=360*2*pi*160*std::cos(2*pi*160*tau);auto p=farassat1a(f,{5,0,0});std::cout<<t<<','<<p.thickness<<','<<p.loading<<'\n';}}
  else if(module=="tps"){TpsColumn tps;std::cout<<"time_s,wall_K,backface_K,recession_m,wall_residual_W_m2\n";for(int i=0;i<1200;++i){auto w=tps.advance(350000,.01,i==0);if(i%10==0)std::cout<<(i+1)*.01<<','<<w.Tw<<','<<tps.T.back()<<','<<tps.recession<<','<<w.residual<<'\n';}}
  else if(module=="battery"){
    auto owned=box_mesh(8,6,1,{.168,.126,.06349});Mesh m(owned.abi());std::vector<double>T(m.nc,305),soc(m.nc,.92),v1(m.nc),v2(m.nc),sei(m.nc,.15),ne(m.nc,.75),pe(m.nc,.04),el(m.nc,1);T[27]=403;
    auto s=[](std::vector<double>&v){return CAbiSpan{v.data(),v.size()};};CAbiBatteryState state{s(T),s(soc),s(v1),s(v2),s(sei),s(ne),s(pe),s(el)};
    std::cout<<"time_s,peak_K,mean_K,soc\n";for(int i=0;i<1200;++i){if(i<500)T[27]+=108*.1/(2.5e6*m.abi.volume.data[27]);battery_step(m,state,25,.1,298,12);if(i%5==0)std::cout<<(i+1)*.1<<','<<*std::max_element(T.begin(),T.end())<<','<<std::accumulate(T.begin(),T.end(),0.0)/m.nc<<','<<soc[0]<<'\n';}
  }else throw std::invalid_argument("demo must be battery, fwh, mhd, or tps");
}
} // namespace cluster_c

static thread_local std::string cluster_c_error;
extern "C" const char* cluster_c_last_error(){return cluster_c_error.c_str();}
extern "C" int cluster_c_battery_step(CAbiMesh* mesh,CAbiBatteryState* state,double current,double dt,double coolant,double h){
  try{cluster_c::require(mesh&&state,"null ABI argument");cluster_c::Mesh m(*mesh);cluster_c::battery_step(m,*state,current,dt,coolant,h);cluster_c_error.clear();return 0;}catch(const std::exception&e){cluster_c_error=e.what();return -1;}
}
extern "C" int cluster_c_mhd_project(CAbiMesh* mesh,CAbiMhdState* state){
  try{cluster_c::require(mesh&&state,"null ABI argument");cluster_c::Mesh m(*mesh);cluster_c::BCs insulating(m.nf);cluster_c::electric_projection(m,*state,insulating);cluster_c_error.clear();return 0;}catch(const std::exception&e){cluster_c_error=e.what();return -1;}
}
extern "C" int cluster_c_pyrolysis_step(CAbiPyrolysisState* state,double dt){
  try{cluster_c::require(state,"null ABI argument");cluster_c::pyrolysis_sources(*state,dt);cluster_c_error.clear();return 0;}catch(const std::exception&e){cluster_c_error=e.what();return -1;}
}
extern "C" int cluster_c_pyrolysis_stages(CAbiPyrolysisState* state,CAbiPyrolysisStage* stages,std::uint64_t count,double dt){
  try{cluster_c::require(state&&stages&&count>0,"null or empty pyrolysis mechanism");cluster_c::multistage_pyrolysis(*state,{stages,static_cast<std::size_t>(count)},dt);cluster_c_error.clear();return 0;}catch(const std::exception&e){cluster_c_error=e.what();return -1;}
}
extern "C" int cluster_c_ale_nodes(CAbiVec3* next,const CAbiVec3* now,const CAbiVec3* previous,const CAbiVec3* velocity,double dt){
  try{cluster_c::require(next&&now&&previous&&velocity,"null ALE node spans");cluster_c::move_ale_nodes(*next,*now,*previous,*velocity,dt);cluster_c_error.clear();return 0;}catch(const std::exception&e){cluster_c_error=e.what();return -1;}
}
extern "C" int cluster_c_ale_geometry(CAbiMesh* mesh,const CAbiPolyConnectivity* topology,const CAbiVec3* previous,const CAbiVec3* now,const CAbiVec3* next,CAbiSpan mesh_flux,double dt){
  try{cluster_c::require(mesh&&topology&&previous&&now&&next,"null ALE geometry arguments");cluster_c::check_span(mesh_flux,topology->owner.size);auto flux=cluster_c::polyhedral_ale_flux(*topology,*previous,*now,*next,dt);cluster_c::rebuild_polyhedral_geometry(*mesh,*topology,*next);std::copy(flux.begin(),flux.end(),mesh_flux.data);cluster_c_error.clear();return 0;}catch(const std::exception&e){cluster_c_error=e.what();return -1;}
}

#ifndef CLUSTER_C_LIBRARY
int main(int argc,char**argv){
  try{bool json=false;std::string mode="--verify",module;for(int i=1;i<argc;++i){std::string arg=argv[i];if(arg=="--json")json=true;else if(arg=="--demo"&&i+1<argc){mode=arg;module=argv[++i];}else if(arg=="--verify")mode=arg;else throw std::invalid_argument("usage: cluster_c --verify [--json] | --demo battery|fwh|mhd|tps");}
    std::cout<<std::setprecision(12);if(mode=="--demo"){cluster_c::demo(module);return 0;}auto tests=cluster_c::verify();cluster_c::print_report(tests,json);return std::all_of(tests.begin(),tests.end(),[](const auto&t){return t.passed;})?0:1;
  }catch(const std::exception&e){std::cerr<<"Cluster C error: "<<e.what()<<'\n';return 2;}
}
#endif