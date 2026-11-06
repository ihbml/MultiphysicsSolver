// Simplified 3D Eulerian two-fluid prototype on tetrahedral control volumes.
// Read README.md: this is not a validated rising-bubble CFD solver.
#include <petscksp.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <map>
#include <string>
#include <vector>

using R=PetscReal;
using V=std::array<R,3>;
static V add(V a,V b){return {a[0]+b[0],a[1]+b[1],a[2]+b[2]};}
static V sub(V a,V b){return {a[0]-b[0],a[1]-b[1],a[2]-b[2]};}
static V mul(R x,V a){return {x*a[0],x*a[1],x*a[2]};}
static R dot(V a,V b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
static V cross(V a,V b){return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
struct Face { PetscInt neighbor=-1; V area_vector{}; R distance=0; }; // area vector points outward
struct Cell { PetscInt id; std::array<V,4> vertices; V center{}; R volume; std::array<Face,4> faces; };
struct Mesh { PetscInt total=0, begin=0; std::vector<Cell> cells; };
struct Physics { R rho_l=1000, rho_g=10, drag=1000, gravity=-9.81, dt=0.001; };
// Demo only. Generates and temporarily replicates the global tetrahedral mesh;
// application meshes should instead supply rank-owned cells and paired faces.
static PetscErrorCode make_demo_mesh(MPI_Comm comm,PetscInt n,Mesh& mesh) {
  PetscFunctionBeginUser;
  int rank,size; PetscCallMPI(MPI_Comm_rank(comm,&rank)); PetscCallMPI(MPI_Comm_size(comm,&size));
  const PetscInt N=6*n*n*n; mesh.total=N;
  PetscInt start=N*rank/size,end=N*(rank+1)/size; mesh.begin=start;
  struct Full { Cell cell; std::array<PetscInt,4> ids; };
  std::vector<Full> all((size_t)N);
  const int perm[6][3]={{0,1,2},{0,2,1},{1,0,2},{1,2,0},{2,0,1},{2,1,0}};
  const PetscInt s[3]={1,n+1,(n+1)*(n+1)};
  auto node=[&](PetscInt id)->V {return {(R)(id%(n+1))/n,(R)((id/(n+1))%(n+1))/n,(R)(id/((n+1)*(n+1)))/n};};
  for(PetscInt q=0;q<N;++q){
    PetscInt cube=q/6, x=cube%n,y=(cube/n)%n,z=cube/(n*n);
    PetscInt a=x+(n+1)*(y+(n+1)*z); const int* p=perm[q%6];
    PetscInt b=a+s[p[0]],c=b+s[p[1]],d=c+s[p[2]];
    auto& f=all[(size_t)q]; f.ids={a,b,c,d}; f.cell.id=q;
    for(int i=0;i<4;++i){f.cell.vertices[i]=node(f.ids[i]); f.cell.center=add(f.cell.center,mul(.25,f.cell.vertices[i]));}
    f.cell.volume=PetscAbsReal(dot(sub(f.cell.vertices[1],f.cell.vertices[0]),cross(sub(f.cell.vertices[2],f.cell.vertices[0]),sub(f.cell.vertices[3],f.cell.vertices[0]))))/6;
  }
  // Match faces by the three global vertex IDs.
  std::map<std::array<PetscInt,3>,std::pair<PetscInt,int>> face_map;
  for(PetscInt q=0;q<N;++q) for(int f=0;f<4;++f){
    auto& c=all[(size_t)q].cell; std::array<int,3> v{}; int k=0;
    for(int i=0;i<4;++i)if(i!=f)v[k++]=i;
    auto key=std::array<PetscInt,3>{all[(size_t)q].ids[v[0]],all[(size_t)q].ids[v[1]],all[(size_t)q].ids[v[2]]};
    std::sort(key.begin(),key.end());
    V a=c.vertices[v[0]],b=c.vertices[v[1]],d=c.vertices[v[2]];
    V normal=mul(.5,cross(sub(b,a),sub(d,a)));
    if(dot(normal,sub(c.vertices[f],a))>0) normal=mul(-1,normal);
    c.faces[f].area_vector=normal;
    auto it=face_map.find(key);
    if(it==face_map.end()) face_map.emplace(key,std::make_pair(q,f));
    else {
      PetscInt other=it->second.first; int of=it->second.second;
      c.faces[f].neighbor=other; all[(size_t)other].cell.faces[of].neighbor=q;
      face_map.erase(it);
    }
  }
  for(PetscInt q=0;q<N;++q) for(auto& f:all[(size_t)q].cell.faces) if(f.neighbor>=0){
    R area=std::sqrt(dot(f.area_vector,f.area_vector));
    f.distance=dot(sub(all[(size_t)f.neighbor].cell.center,all[(size_t)q].cell.center),f.area_vector)/area;
    PetscCheck(f.distance>1e-14,comm,PETSC_ERR_ARG_OUTOFRANGE,"Invalid paired face distance");
  }
  mesh.cells.reserve((size_t)(end-start));
  for(PetscInt q=start;q<end;++q)mesh.cells.push_back(all[(size_t)q].cell);
  PetscFunctionReturn(PETSC_SUCCESS);
}
// Sparse ghost gather: one copy of each cell needed by this rank.
struct Halo {
  std::vector<PetscInt> ids;
  std::map<PetscInt,PetscInt> index;
  Vec local=nullptr; VecScatter scatter=nullptr; PetscInt components=0;
  PetscErrorCode create(Vec global,const Mesh& mesh,PetscInt nc) {
    PetscFunctionBeginUser; components=nc;
    for(const auto& c:mesh.cells){ids.push_back(c.id);for(const auto& f:c.faces)if(f.neighbor>=0)ids.push_back(f.neighbor);}
    std::sort(ids.begin(),ids.end());ids.erase(std::unique(ids.begin(),ids.end()),ids.end());
    std::vector<PetscInt> indices; indices.reserve(ids.size()*(size_t)nc);
    for(size_t i=0;i<ids.size();++i){index[ids[i]]=(PetscInt)i;for(PetscInt k=0;k<nc;++k)indices.push_back(nc*ids[i]+k);}
    IS from,to;
    PetscCall(VecCreateSeq(PETSC_COMM_SELF,(PetscInt)indices.size(),&local));
    PetscCall(ISCreateGeneral(PETSC_COMM_SELF,(PetscInt)indices.size(),indices.data(),PETSC_COPY_VALUES,&from));
    PetscCall(ISCreateStride(PETSC_COMM_SELF,(PetscInt)indices.size(),0,1,&to));
    PetscCall(VecScatterCreate(global,from,local,to,&scatter));
    PetscCall(ISDestroy(&from));PetscCall(ISDestroy(&to));PetscFunctionReturn(PETSC_SUCCESS);
  }
  PetscErrorCode update(Vec global){PetscFunctionBeginUser;PetscCall(VecScatterBegin(scatter,global,local,INSERT_VALUES,SCATTER_FORWARD));PetscCall(VecScatterEnd(scatter,global,local,INSERT_VALUES,SCATTER_FORWARD));PetscFunctionReturn(PETSC_SUCCESS);}
  PetscInt at(PetscInt id) const {return components*index.at(id);}
  PetscErrorCode destroy(){PetscFunctionBeginUser;PetscCall(VecScatterDestroy(&scatter));PetscCall(VecDestroy(&local));PetscFunctionReturn(PETSC_SUCCESS);}
};
static V getv(const PetscScalar* a,PetscInt i,int offset){return {PetscRealPart(a[i+offset]),PetscRealPart(a[i+offset+1]),PetscRealPart(a[i+offset+2])};}
static R length(V a){return std::sqrt(dot(a,a));}
struct Solver {
 MPI_Comm comm; const Mesh& m; Physics p;
 Vec alpha=nullptr,vel=nullptr,pressure=nullptr,rhs=nullptr,star=nullptr,budget=nullptr;
 Mat A=nullptr; KSP ksp=nullptr; MatNullSpace ns=nullptr;
 Halo ah,vh,ph,sh,bh;
 Solver(MPI_Comm c,const Mesh& mm,Physics pp):comm(c),m(mm),p(pp){}
 PetscErrorCode create(){
  PetscFunctionBeginUser;
  PetscInt n=(PetscInt)m.cells.size();
  PetscCheck(p.rho_l>0&&p.rho_g>0&&p.drag>=0&&p.dt>0,comm,PETSC_ERR_ARG_OUTOFRANGE,"Invalid physical parameters");
  PetscCall(VecCreateMPI(comm,n,m.total,&alpha));PetscCall(VecDuplicate(alpha,&pressure));PetscCall(VecDuplicate(alpha,&rhs));
  PetscInt lo,hi;PetscCall(VecGetOwnershipRange(alpha,&lo,&hi));
  PetscCheck(lo==m.begin && hi-lo==n,comm,PETSC_ERR_ARG_INCOMP,"Mesh cells must match contiguous PETSc row ownership");
  for(PetscInt i=0;i<n;++i){const auto& c=m.cells[(size_t)i];
   PetscCheck(c.id==lo+i && c.volume>0,comm,PETSC_ERR_ARG_INCOMP,"Invalid local cell ID or volume");
   for(const auto& f:c.faces){PetscCheck(f.neighbor>=-1 && f.neighbor<m.total,comm,PETSC_ERR_ARG_OUTOFRANGE,"Invalid face neighbor");
    PetscCheck(length(f.area_vector)>0 && (f.neighbor<0 || f.distance>0),comm,PETSC_ERR_ARG_OUTOFRANGE,"Invalid face area or distance");}
  }
  PetscCall(VecCreateMPI(comm,6*n,6*m.total,&vel));PetscCall(VecDuplicate(vel,&star));
  PetscCall(VecCreateMPI(comm,2*n,2*m.total,&budget));
  PetscCall(ah.create(alpha,m,1));PetscCall(vh.create(vel,m,6));PetscCall(ph.create(pressure,m,1));PetscCall(sh.create(star,m,6));PetscCall(bh.create(budget,m,2));
  PetscCall(MatCreateAIJ(comm,n,n,m.total,m.total,std::min((PetscInt)5,n),nullptr,std::min((PetscInt)4,m.total-n),nullptr,&A));
  PetscCall(MatSetOption(A,MAT_NEW_NONZERO_ALLOCATION_ERR,PETSC_FALSE));
  PetscCall(MatNullSpaceCreate(comm,PETSC_TRUE,0,nullptr,&ns));PetscCall(MatSetNullSpace(A,ns));
  PetscCall(KSPCreate(comm,&ksp));PetscCall(KSPSetOptionsPrefix(ksp,"pressure_"));PetscCall(KSPSetOperators(ksp,A,A));
  PetscCall(KSPSetType(ksp,KSPCG));PC pc;PetscCall(KSPGetPC(ksp,&pc));PetscCall(PCSetType(pc,PCJACOBI));
  PetscCall(KSPSetTolerances(ksp,1e-10,1e-12,PETSC_DEFAULT,5000));PetscCall(KSPSetErrorIfNotConverged(ksp,PETSC_TRUE));PetscCall(KSPSetFromOptions(ksp));
  PetscCall(VecSet(alpha,0));PetscCall(VecSet(vel,0));PetscCall(VecSet(pressure,0));
  PetscFunctionReturn(PETSC_SUCCESS);
 }
 PetscErrorCode initial_bubble(){
  PetscFunctionBeginUser;PetscScalar *a;PetscCall(VecGetArray(alpha,&a));
  for(size_t i=0;i<m.cells.size();++i){const V x=m.cells[i].center; R r2=(x[0]-.5)*(x[0]-.5)+(x[1]-.5)*(x[1]-.5)+(x[2]-.3)*(x[2]-.3); a[i]=.001+.8*std::exp(-r2/.025);}
  PetscCall(VecRestoreArray(alpha,&a));PetscFunctionReturn(PETSC_SUCCESS);
 }
 // Symmetric implicit drag: K=drag*alpha_g*alpha_l. Conserves local phase momentum.
 PetscErrorCode predict(){
  PetscFunctionBeginUser;const PetscScalar *a,*u;PetscScalar *s;
  PetscCall(VecGetArrayRead(alpha,&a));PetscCall(VecGetArrayRead(vel,&u));PetscCall(VecGetArray(star,&s));
  for(size_t i=0;i<m.cells.size();++i){R ag=PetscRealPart(a[i]),al=1-ag;
   R x=p.dt*p.drag*ag/p.rho_l,y=p.dt*p.drag*al/p.rho_g,det=1+x+y;
   for(int d=0;d<3;++d){R ul=PetscRealPart(u[6*i+d])+(d==2?p.dt*p.gravity:0),ug=PetscRealPart(u[6*i+3+d])+(d==2?p.dt*p.gravity:0);
    s[6*i+d]=((1+y)*ul+x*ug)/det; s[6*i+3+d]=(y*ul+(1+x)*ug)/det;}
  }
  PetscCall(VecRestoreArrayRead(alpha,&a));PetscCall(VecRestoreArrayRead(vel,&u));PetscCall(VecRestoreArray(star,&s));
  PetscCall(ah.update(alpha));PetscCall(sh.update(star));PetscFunctionReturn(PETSC_SUCCESS);
 }
 // Pressure projection of mixture face flux. Two-point approximation neglects
 // non-orthogonal correction, so accuracy on general tetrahedra is limited.
 PetscErrorCode project(){
  PetscFunctionBeginUser;PetscCall(MatZeroEntries(A));PetscCall(VecSet(rhs,0));
  const PetscScalar *a,*u;PetscCall(VecGetArrayRead(ah.local,&a));PetscCall(VecGetArrayRead(sh.local,&u));
  for(const auto& c:m.cells){PetscInt i=c.id,ai=ah.at(i),ui=sh.at(i);R diag=0,div=0;
   for(const auto& f:c.faces)if(f.neighbor>=0){PetscInt j=f.neighbor,aj=ah.at(j),uj=sh.at(j);R ag=.5*(PetscRealPart(a[ai])+PetscRealPart(a[aj]));
    V ul=mul(.5,add(getv(u,ui,0),getv(u,uj,0))),ug=mul(.5,add(getv(u,ui,3),getv(u,uj,3)));
    div+=dot(add(mul(1-ag,ul),mul(ag,ug)),f.area_vector);
    R coeff=p.dt*length(f.area_vector)*((1-ag)/p.rho_l+ag/p.rho_g)/f.distance;
    diag+=coeff;PetscCall(MatSetValue(A,i,j,-coeff,ADD_VALUES));
   }
   PetscCall(MatSetValue(A,i,i,diag,ADD_VALUES));PetscCall(VecSetValue(rhs,i,-div,INSERT_VALUES));
  }
  PetscCall(VecRestoreArrayRead(ah.local,&a));PetscCall(VecRestoreArrayRead(sh.local,&u));
  PetscCall(MatAssemblyBegin(A,MAT_FINAL_ASSEMBLY));PetscCall(MatAssemblyEnd(A,MAT_FINAL_ASSEMBLY));
  PetscCall(VecAssemblyBegin(rhs));PetscCall(VecAssemblyEnd(rhs));PetscCall(MatNullSpaceRemove(ns,rhs));
  PetscCall(KSPSolve(ksp,rhs,pressure));PetscCall(MatNullSpaceRemove(ns,pressure));PetscCall(ph.update(pressure));
  PetscFunctionReturn(PETSC_SUCCESS);
 }
 // Reconstructed pressure-gradient correction of each phase's cell velocity.
 PetscErrorCode correct_velocity(){
  PetscFunctionBeginUser;const PetscScalar *pp,*ss;PetscScalar *uu;
  PetscCall(VecGetArrayRead(ph.local,&pp));PetscCall(VecGetArrayRead(star,&ss));PetscCall(VecGetArray(vel,&uu));
  for(size_t q=0;q<m.cells.size();++q){const auto& c=m.cells[q];R pi=PetscRealPart(pp[ph.at(c.id)]);V grad={0,0,0};
   for(const auto& f:c.faces){R pf=f.neighbor<0?pi:.5*(pi+PetscRealPart(pp[ph.at(f.neighbor)]));grad=add(grad,mul(pf/c.volume,f.area_vector));}
   for(int d=0;d<3;++d){uu[6*q+d]=ss[6*q+d]-p.dt*grad[d]/p.rho_l;uu[6*q+3+d]=ss[6*q+3+d]-p.dt*grad[d]/p.rho_g;}
  }
  PetscCall(VecRestoreArrayRead(ph.local,&pp));PetscCall(VecRestoreArrayRead(star,&ss));PetscCall(VecRestoreArray(vel,&uu));
  PetscCall(vh.update(vel));PetscFunctionReturn(PETSC_SUCCESS);
 }
 // Projected mixture volume flux and relative phase flux (outward from cell).
 void face_flux(const Cell& c,const Face& f,const PetscScalar* a,const PetscScalar* u,const PetscScalar* pr,R& qm,R& slip) const {
  PetscInt i=c.id,j=f.neighbor,ai=ah.at(i),aj=ah.at(j),ui=sh.at(i),uj=sh.at(j);
  R ag=.5*(PetscRealPart(a[ai])+PetscRealPart(a[aj])),dp=PetscRealPart(pr[ph.at(j)]-pr[ph.at(i)]);
  V normal=mul(1/length(f.area_vector),f.area_vector);
  V ul=mul(.5,add(getv(u,ui,0),getv(u,uj,0))),ug=mul(.5,add(getv(u,ui,3),getv(u,uj,3)));
  ul=sub(ul,mul(p.dt*dp/(p.rho_l*f.distance),normal));ug=sub(ug,mul(p.dt*dp/(p.rho_g*f.distance),normal));
  qm=dot(add(mul(1-ag,ul),mul(ag,ug)),f.area_vector);
  slip=ag*(1-ag)*dot(sub(ug,ul),f.area_vector);
 }
 // Compute conservative outgoing budgets for baseline mixture transport and
 // opposite gas/liquid slip transport. Both adjacent ranks use the same face.
 PetscErrorCode compute_budgets(){
  PetscFunctionBeginUser; const PetscScalar *a,*s,*pr;PetscScalar *b;
  PetscCall(VecGetArrayRead(ah.local,&a));PetscCall(VecGetArrayRead(sh.local,&s));PetscCall(VecGetArrayRead(ph.local,&pr));PetscCall(VecGetArray(budget,&b));
  R local_cfl=0,global_cfl=0;
  for(size_t q=0;q<m.cells.size();++q){const auto& c=m.cells[q];R ag=PetscRealPart(a[ah.at(c.id)]),bg=0,bl=0,sg=0,sl=0,out=0;
   for(const auto& f:c.faces)if(f.neighbor>=0){R qm,slip;face_flux(c,f,a,s,pr,qm,slip);
    R up=qm>=0?ag:PetscRealPart(a[ah.at(f.neighbor)]);R qg=qm*up,ql=qm-qg;
    bg+=std::max(qg,(R)0);bl+=std::max(ql,(R)0);sg+=std::max(slip,(R)0);sl+=std::max(-slip,(R)0);out+=std::max(qm,(R)0);
   }
   local_cfl=std::max(local_cfl,p.dt*out/c.volume);
   R rg=std::max((R)0,ag*c.volume/p.dt-bg),rl=std::max((R)0,(1-ag)*c.volume/p.dt-bl);
   b[2*q]=sg>0?std::min((R)1,rg/sg):1;b[2*q+1]=sl>0?std::min((R)1,rl/sl):1;
  }
  PetscCall(VecRestoreArrayRead(ah.local,&a));PetscCall(VecRestoreArrayRead(sh.local,&s));PetscCall(VecRestoreArrayRead(ph.local,&pr));PetscCall(VecRestoreArray(budget,&b));
  PetscCallMPI(MPI_Allreduce(&local_cfl,&global_cfl,1,MPIU_REAL,MPI_MAX,comm));
  PetscCheck(global_cfl<=0.9,comm,PETSC_ERR_NOT_CONVERGED,"Mixture transport CFL %.4g exceeds 0.9; reduce dt",(double)global_cfl);
  PetscCall(bh.update(budget));PetscFunctionReturn(PETSC_SUCCESS);
 }
 // Conservative donor-cell phase-volume and phase-momentum transport.
 PetscErrorCode transport(){
  PetscFunctionBeginUser;const PetscScalar *a,*s,*pr,*b,*u;PetscScalar *an,*un;
  PetscCall(VecGetArrayRead(ah.local,&a));PetscCall(VecGetArrayRead(sh.local,&s));PetscCall(VecGetArrayRead(ph.local,&pr));
  PetscCall(VecGetArrayRead(bh.local,&b));PetscCall(VecGetArrayRead(vh.local,&u));
  PetscCall(VecGetArray(alpha,&an));PetscCall(VecGetArray(vel,&un));
  R local_min=1,local_max=0,local_div=0;
  for(size_t q=0;q<m.cells.size();++q){const auto& c=m.cells[q];PetscInt i=c.id,ai=ah.at(i),ui=vh.at(i),bi=bh.at(i);R ag=PetscRealPart(a[ai]);
   R sumg=0,div=0;V mg=mul(ag*p.rho_g,getv(u,ui,3)),ml=mul((1-ag)*p.rho_l,getv(u,ui,0));
   for(const auto& f:c.faces)if(f.neighbor>=0){PetscInt j=f.neighbor,aj=ah.at(j),uj=vh.at(j),bj=bh.at(j);R qm,slip;face_flux(c,f,a,s,pr,qm,slip);div+=qm;
    R up=qm>=0?ag:PetscRealPart(a[aj]);
    R factor=slip>=0?std::min(PetscRealPart(b[bi]),PetscRealPart(b[bj+1])):std::min(PetscRealPart(b[bj]),PetscRealPart(b[bi+1]));
    R qg=qm*up+factor*slip,ql=qm-qg;sumg+=qg;
    V vg=qg>=0?getv(u,ui,3):getv(u,uj,3),vl=ql>=0?getv(u,ui,0):getv(u,uj,0);
    mg=sub(mg,mul(p.dt*p.rho_g*qg/c.volume,vg));ml=sub(ml,mul(p.dt*p.rho_l*ql/c.volume,vl));
   }
   R anew=ag-p.dt*sumg/c.volume;local_min=std::min(local_min,anew);local_max=std::max(local_max,anew);local_div=std::max(local_div,PetscAbsReal(div)/c.volume);
   an[q]=anew;
   // If a phase is truly absent, its velocity is undefined. This threshold can
   // discard tiny phase momentum; production implementations need phase-appearance treatment.
   for(int d=0;d<3;++d){un[6*q+d]=(1-anew>1e-12?ml[d]/(p.rho_l*(1-anew)):0);un[6*q+3+d]=(anew>1e-12?mg[d]/(p.rho_g*anew):0);}
  }
  PetscCall(VecRestoreArrayRead(ah.local,&a));PetscCall(VecRestoreArrayRead(sh.local,&s));PetscCall(VecRestoreArrayRead(ph.local,&pr));
  PetscCall(VecRestoreArrayRead(bh.local,&b));PetscCall(VecRestoreArrayRead(vh.local,&u));PetscCall(VecRestoreArray(alpha,&an));PetscCall(VecRestoreArray(vel,&un));
  R minall,maxall,divall;
  PetscCallMPI(MPI_Allreduce(&local_min,&minall,1,MPIU_REAL,MPI_MIN,comm));PetscCallMPI(MPI_Allreduce(&local_max,&maxall,1,MPIU_REAL,MPI_MAX,comm));
  PetscCallMPI(MPI_Allreduce(&local_div,&divall,1,MPIU_REAL,MPI_MAX,comm));
  PetscCheck(minall>=-1e-10 && maxall<=1+1e-10,comm,PETSC_ERR_NOT_CONVERGED,"Phase fraction outside [0,1]: [%g,%g]",(double)minall,(double)maxall);
  PetscCall(PetscPrintf(comm,"    alpha range [%.6g, %.6g], max projected mixture divergence %.3e\n",(double)minall,(double)maxall,(double)divall));
  PetscFunctionReturn(PETSC_SUCCESS);
 }
 PetscErrorCode step(){PetscFunctionBeginUser;PetscCall(predict());PetscCall(project());PetscCall(correct_velocity());PetscCall(compute_budgets());PetscCall(transport());PetscFunctionReturn(PETSC_SUCCESS);}
 PetscErrorCode diagnostics(R* gas_volume=nullptr,R* center=nullptr){
  PetscFunctionBeginUser;const PetscScalar *a;PetscCall(VecGetArrayRead(alpha,&a));R local[2]={0,0},global[2];
  for(size_t i=0;i<m.cells.size();++i){R v=PetscRealPart(a[i])*m.cells[i].volume;local[0]+=v;local[1]+=v*m.cells[i].center[2];}
  PetscCall(VecRestoreArrayRead(alpha,&a));PetscCallMPI(MPI_Allreduce(local,global,2,MPIU_REAL,MPI_SUM,comm));
  if(gas_volume) *gas_volume=global[0];
  if(center) *center=global[1]/global[0];
  PetscCall(PetscPrintf(comm,"    gas volume %.12e, gas centroid z %.8f\n",(double)global[0],(double)(global[1]/global[0])));PetscFunctionReturn(PETSC_SUCCESS);
 }
 PetscErrorCode write_vtk(const std::string& prefix){
  PetscFunctionBeginUser;int rank,size;PetscCallMPI(MPI_Comm_rank(comm,&rank));PetscCallMPI(MPI_Comm_size(comm,&size));
  std::string path=prefix+"_rank"+std::to_string(rank)+".vtu";std::ofstream o(path);
  PetscCheck(o.good(),comm,PETSC_ERR_FILE_OPEN,"Cannot open %s",path.c_str());
  const PetscScalar *a,*u,*pr;PetscCall(VecGetArrayRead(alpha,&a));PetscCall(VecGetArrayRead(vel,&u));PetscCall(VecGetArrayRead(pressure,&pr));
  const size_t n=m.cells.size();o<<std::setprecision(16)<<"<?xml version=\"1.0\"?>\n<VTKFile type=\"UnstructuredGrid\" version=\"0.1\" byte_order=\"LittleEndian\">\n<UnstructuredGrid><Piece NumberOfPoints=\""<<4*n<<"\" NumberOfCells=\""<<n<<"\">\n";
  o<<"<Points><DataArray type=\"Float64\" NumberOfComponents=\"3\" format=\"ascii\">\n";
  for(const auto& c:m.cells)for(V v:c.vertices)o<<v[0]<<' '<<v[1]<<' '<<v[2]<<'\n';
  o<<"</DataArray></Points><Cells><DataArray type=\"Int64\" Name=\"connectivity\" format=\"ascii\">\n";
  for(size_t i=0;i<n;++i)o<<4*i<<' '<<4*i+1<<' '<<4*i+2<<' '<<4*i+3<<'\n';
  o<<"</DataArray><DataArray type=\"Int64\" Name=\"offsets\" format=\"ascii\">\n";for(size_t i=0;i<n;++i)o<<4*(i+1)<<' ';
  o<<"\n</DataArray><DataArray type=\"UInt8\" Name=\"types\" format=\"ascii\">\n";for(size_t i=0;i<n;++i)o<<"10 ";
  o<<"\n</DataArray></Cells><CellData>\n";
  auto scalar=[&](const char* name,const PetscScalar* v){o<<"<DataArray type=\"Float64\" Name=\""<<name<<"\" format=\"ascii\">\n";for(size_t i=0;i<n;++i)o<<PetscRealPart(v[i])<<'\n';o<<"</DataArray>\n";};
  scalar("gas_fraction",a);scalar("pressure",pr);
  for(int phase=0;phase<2;++phase){o<<"<DataArray type=\"Float64\" Name=\""<<(phase==0?"liquid_velocity":"gas_velocity")<<"\" NumberOfComponents=\"3\" format=\"ascii\">\n";
   for(size_t i=0;i<n;++i) { o<<PetscRealPart(u[6*i+3*phase])<<' '<<PetscRealPart(u[6*i+3*phase+1])<<' '<<PetscRealPart(u[6*i+3*phase+2])<<'\n'; }
   o<<"</DataArray>\n";}
  o<<"</CellData></Piece></UnstructuredGrid></VTKFile>\n";o.close();
  PetscCall(VecRestoreArrayRead(alpha,&a));PetscCall(VecRestoreArrayRead(vel,&u));PetscCall(VecRestoreArrayRead(pressure,&pr));
  PetscCheck(o.good(),comm,PETSC_ERR_FILE_WRITE,"Error writing %s",path.c_str());
  PetscCallMPI(MPI_Barrier(comm));
  if(rank==0){std::string meta=prefix+".pvtu";std::ofstream mfile(meta);PetscCheck(mfile.good(),PETSC_COMM_SELF,PETSC_ERR_FILE_OPEN,"Cannot open %s",meta.c_str());
   mfile<<"<?xml version=\"1.0\"?>\n<VTKFile type=\"PUnstructuredGrid\" version=\"0.1\" byte_order=\"LittleEndian\"><PUnstructuredGrid GhostLevel=\"0\">\n<PPoints><PDataArray type=\"Float64\" NumberOfComponents=\"3\"/></PPoints><PCellData>\n";
   mfile<<"<PDataArray type=\"Float64\" Name=\"gas_fraction\"/><PDataArray type=\"Float64\" Name=\"pressure\"/><PDataArray type=\"Float64\" Name=\"liquid_velocity\" NumberOfComponents=\"3\"/><PDataArray type=\"Float64\" Name=\"gas_velocity\" NumberOfComponents=\"3\"/></PCellData>\n";
   // Refer to pieces relative to the index, even if prefix includes a directory.
   auto pos=prefix.find_last_of("/\\");std::string base=(pos==std::string::npos?prefix:prefix.substr(pos+1));
   for(int r=0;r<size;++r)mfile<<"<Piece Source=\""<<base<<"_rank"<<r<<".vtu\"/>\n";
   mfile<<"</PUnstructuredGrid></VTKFile>\n";mfile.close();PetscCheck(mfile.good(),PETSC_COMM_SELF,PETSC_ERR_FILE_WRITE,"Error writing %s",meta.c_str());
  }
  PetscFunctionReturn(PETSC_SUCCESS);
 }
 PetscErrorCode destroy(){PetscFunctionBeginUser;PetscCall(ah.destroy());PetscCall(vh.destroy());PetscCall(ph.destroy());PetscCall(sh.destroy());PetscCall(bh.destroy());
  PetscCall(KSPDestroy(&ksp));PetscCall(MatDestroy(&A));PetscCall(MatNullSpaceDestroy(&ns));
  PetscCall(VecDestroy(&alpha));PetscCall(VecDestroy(&vel));PetscCall(VecDestroy(&pressure));PetscCall(VecDestroy(&rhs));PetscCall(VecDestroy(&star));PetscCall(VecDestroy(&budget));PetscFunctionReturn(PETSC_SUCCESS);}
};
int main(int argc,char**argv){
 PetscCall(PetscInitialize(&argc,&argv,nullptr,"Simplified two-fluid 3D rising-bubble prototype\n"));
 PetscInt n=4,steps=10;Physics p;PetscBool vtk=PETSC_FALSE,check_rise=PETSC_FALSE,check_stationary=PETSC_FALSE;
 PetscCall(PetscOptionsGetInt(nullptr,nullptr,"-n",&n,nullptr));PetscCall(PetscOptionsGetInt(nullptr,nullptr,"-steps",&steps,nullptr));
 PetscCall(PetscOptionsGetReal(nullptr,nullptr,"-dt",&p.dt,nullptr));PetscCall(PetscOptionsGetReal(nullptr,nullptr,"-rho_l",&p.rho_l,nullptr));
 PetscCall(PetscOptionsGetReal(nullptr,nullptr,"-rho_g",&p.rho_g,nullptr));PetscCall(PetscOptionsGetReal(nullptr,nullptr,"-drag",&p.drag,nullptr));
 PetscCall(PetscOptionsGetReal(nullptr,nullptr,"-gravity",&p.gravity,nullptr));PetscCall(PetscOptionsGetBool(nullptr,nullptr,"-write_vtk",&vtk,nullptr));
 PetscCall(PetscOptionsGetBool(nullptr,nullptr,"-check_rise",&check_rise,nullptr));
 PetscCall(PetscOptionsGetBool(nullptr,nullptr,"-check_stationary",&check_stationary,nullptr));
 PetscCheck(n>=2&&n<=100&&steps>0,PETSC_COMM_WORLD,PETSC_ERR_ARG_OUTOFRANGE,"n must be 2..100 and steps > 0");
 Mesh mesh;PetscCall(make_demo_mesh(PETSC_COMM_WORLD,n,mesh));Solver s(PETSC_COMM_WORLD,mesh,p);PetscCall(s.create());PetscCall(s.initial_bubble());
 R initial,final,centroid0,centroid1;PetscCall(PetscPrintf(PETSC_COMM_WORLD,"Initial state\n"));PetscCall(s.diagnostics(&initial,&centroid0));
 for(PetscInt i=0;i<steps;++i){PetscCall(PetscPrintf(PETSC_COMM_WORLD,"Step %" PetscInt_FMT "\n",i+1));PetscCall(s.step());PetscCall(s.diagnostics());}
 PetscCall(s.diagnostics(&final,&centroid1));R err=PetscAbsReal(final-initial)/initial;
 PetscCall(PetscPrintf(PETSC_COMM_WORLD,"Relative gas-volume change %.3e; centroid displacement %.6e\n",(double)err,(double)(centroid1-centroid0)));
 PetscCheck(err<1e-8,PETSC_COMM_WORLD,PETSC_ERR_NOT_CONVERGED,"Gas-volume conservation test failed");
 if(check_rise) PetscCheck(centroid1>centroid0+1e-9,PETSC_COMM_WORLD,PETSC_ERR_NOT_CONVERGED,"Bubble centroid did not rise");
 if(check_stationary) PetscCheck(PetscAbsReal(centroid1-centroid0)<1e-12,PETSC_COMM_WORLD,PETSC_ERR_NOT_CONVERGED,"Centroid moved unexpectedly");
 if(vtk) { PetscCall(s.write_vtk("two_fluid_final")); }
 PetscCall(s.destroy());PetscCall(PetscFinalize());return 0;
}
