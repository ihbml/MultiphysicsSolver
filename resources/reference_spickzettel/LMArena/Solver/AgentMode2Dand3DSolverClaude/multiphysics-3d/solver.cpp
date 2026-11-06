// Distributed tetrahedral P1/P1 finite-element flow + temperature; see README.
#include <petscksp.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>

struct Node { PetscInt id; PetscReal x, y, z; };
struct Tetrahedron { std::array<Node, 4> n; };
struct Constraint { PetscInt dof; PetscScalar value; };
// Element ownership is unique across ranks; shared global node coordinates agree.
struct LocalMesh {
  PetscInt global_nodes=0;
  std::vector<Tetrahedron> elements;
  // Constraints must be listed only by the PETSc row owner.
  std::vector<Constraint> flow_bc, temperature_bc;
};
struct Physics {
  PetscReal rho=1, mu=1, heat_capacity=1, conductivity=1;
  PetscReal beta=0, reference_temperature=0, gx=0, gy=0, gz=-1;
  PetscReal dt=0.1, pressure_stabilization=0.05;
};
struct Geometry { PetscReal volume, grad[4][3], h2; };
using V3 = std::array<PetscReal,3>;
static V3 difference(const Node& a,const Node& b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
static V3 cross(const V3& a,const V3& b) {
  return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};
}
static PetscReal dot(const V3& a,const V3& b) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
static PetscErrorCode geometry(const Tetrahedron& e, Geometry& g) {
  PetscFunctionBeginUser;
  const V3 a=difference(e.n[1],e.n[0]), b=difference(e.n[2],e.n[0]), c=difference(e.n[3],e.n[0]);
  const V3 bc=cross(b,c), ca=cross(c,a), ab=cross(a,b);
  const PetscReal det=dot(a,bc);
  g.h2=0;
  for(int i=0;i<4;++i) for(int j=i+1;j<4;++j) {
    V3 edge=difference(e.n[i],e.n[j]); g.h2=std::max(g.h2,dot(edge,edge));
  }
  PetscCheck(g.h2>0 && PetscAbsReal(det)>1e-12*g.h2*std::sqrt(g.h2),PETSC_COMM_SELF,
             PETSC_ERR_ARG_OUTOFRANGE,"Degenerate or very ill-conditioned tetrahedron");
  g.volume=PetscAbsReal(det)/6.0;
  for(int d=0;d<3;++d) {
    g.grad[1][d]=bc[d]/det; g.grad[2][d]=ca[d]/det; g.grad[3][d]=ab[d]/det;
    g.grad[0][d]=-g.grad[1][d]-g.grad[2][d]-g.grad[3][d];
  }
  PetscFunctionReturn(PETSC_SUCCESS);
}
// Scatter exactly the values needed by rank-local elements, including ghosts.
// Repeated global indices are intentional and keep the assembly loop simple.
struct Gather {
  Vec local=nullptr; VecScatter scatter=nullptr;
  PetscErrorCode create(Vec global, const LocalMesh& mesh, PetscInt components) {
    PetscFunctionBeginUser;
    std::vector<PetscInt> ids;
    ids.reserve(mesh.elements.size()*4*components);
    for (const auto& e:mesh.elements) for (const auto& n:e.n)
      for (PetscInt c=0;c<components;++c) ids.push_back(components*n.id+c);
    IS from, to;
    PetscCall(VecCreateSeq(PETSC_COMM_SELF,(PetscInt)ids.size(),&local));
    PetscCall(ISCreateGeneral(PETSC_COMM_SELF,(PetscInt)ids.size(),ids.data(),PETSC_COPY_VALUES,&from));
    PetscCall(ISCreateStride(PETSC_COMM_SELF,(PetscInt)ids.size(),0,1,&to));
    PetscCall(VecScatterCreate(global,from,local,to,&scatter));
    PetscCall(ISDestroy(&from)); PetscCall(ISDestroy(&to));
    PetscFunctionReturn(PETSC_SUCCESS);
  }
  PetscErrorCode update(Vec global) {
    PetscFunctionBeginUser;
    PetscCall(VecScatterBegin(scatter,global,local,INSERT_VALUES,SCATTER_FORWARD));
    PetscCall(VecScatterEnd(scatter,global,local,INSERT_VALUES,SCATTER_FORWARD));
    PetscFunctionReturn(PETSC_SUCCESS);
  }
  PetscErrorCode destroy() {
    PetscFunctionBeginUser;
    PetscCall(VecScatterDestroy(&scatter)); PetscCall(VecDestroy(&local));
    PetscFunctionReturn(PETSC_SUCCESS);
  }
};
static PetscErrorCode make_matrix(Vec v, PetscInt estimated_nonzeros, Mat* A) {
  PetscFunctionBeginUser;
  PetscInt local, global;
  PetscCall(VecGetLocalSize(v,&local)); PetscCall(VecGetSize(v,&global));
  PetscCall(MatCreateAIJ(PetscObjectComm((PetscObject)v),local,local,global,global,
             std::min(estimated_nonzeros,local),nullptr,
             std::min(estimated_nonzeros,global-local),nullptr,A));
  // Meshes with higher-than-estimated vertex degree may reallocate on first assembly.
  PetscCall(MatSetOption(*A,MAT_NEW_NONZERO_ALLOCATION_ERR,PETSC_FALSE));
  PetscCall(MatSetOption(*A,MAT_KEEP_NONZERO_PATTERN,PETSC_TRUE));
  PetscFunctionReturn(PETSC_SUCCESS);
}
static PetscErrorCode setup_ksp(Mat A, const char* prefix, KSP* ksp) {
  PetscFunctionBeginUser;
  PetscCall(KSPCreate(PetscObjectComm((PetscObject)A),ksp));
  PetscCall(KSPSetOptionsPrefix(*ksp,prefix));
  PetscCall(KSPSetOperators(*ksp,A,A));
  PetscCall(KSPSetType(*ksp,KSPGMRES));
  PetscCall(KSPGMRESSetRestart(*ksp,150));
  PC pc; PetscCall(KSPGetPC(*ksp,&pc));
  PetscCall(PCSetType(pc,std::strcmp(prefix,"flow_")==0 ? PCBJACOBI : PCJACOBI));
  PetscCall(KSPSetTolerances(*ksp,1e-12,1e-14,PETSC_DEFAULT,4000));
  PetscCall(KSPSetErrorIfNotConverged(*ksp,PETSC_TRUE));
  PetscCall(KSPSetFromOptions(*ksp));
  PetscFunctionReturn(PETSC_SUCCESS);
}
// Apply nonzero Dirichlet data with RHS lifting; rows contain owned IDs only.
static PetscErrorCode impose(Mat A, Vec rhs, Vec work,
                             const std::vector<Constraint>& bc) {
  PetscFunctionBeginUser;
  PetscCall(VecSet(work,0));
  std::vector<PetscInt> rows; rows.reserve(bc.size());
  for (const auto& c:bc) {
    rows.push_back(c.dof);
    PetscCall(VecSetValue(work,c.dof,c.value,INSERT_VALUES));
  }
  PetscCall(VecAssemblyBegin(work)); PetscCall(VecAssemblyEnd(work));
  PetscCall(MatZeroRowsColumns(A,(PetscInt)rows.size(),rows.data(),1.0,work,rhs));
  PetscFunctionReturn(PETSC_SUCCESS);
}
static PetscErrorCode assemble_flow(const LocalMesh& mesh, const Physics& p,
                                   Gather& u_old, Gather& u_iter, Gather& temp,
                                   Mat A, Vec rhs) {
  PetscFunctionBeginUser;
  PetscCall(MatZeroEntries(A)); PetscCall(VecSet(rhs,0));
  const PetscScalar *uo,*ui,*tt;
  PetscCall(VecGetArrayRead(u_old.local,&uo)); PetscCall(VecGetArrayRead(u_iter.local,&ui));
  PetscCall(VecGetArrayRead(temp.local,&tt));
  size_t k=0;
  for(const auto& e:mesh.elements) {
    Geometry g; PetscCall(geometry(e,g));
    PetscInt id[16]; PetscScalar ae[256]={},be[16]={};
    for(int i=0;i<4;++i) for(int c=0;c<4;++c) id[4*i+c]=4*e.n[i].id+c;
    const PetscReal tau=p.pressure_stabilization*g.h2/p.mu;
    const PetscReal grav[3]={p.gx,p.gy,p.gz};
    for(int i=0;i<4;++i) for(int j=0;j<4;++j) {
      const PetscReal mass=g.volume*(i==j?2.0:1.0)/20.0;
      PetscReal gradgrad=0; for(int d=0;d<3;++d) gradgrad+=g.volume*g.grad[i][d]*g.grad[j][d];
      PetscScalar weighted_velocity[3]={0,0,0};
      for(int l=0;l<4;++l) {
        const PetscReal mil=g.volume*(i==l?2.0:1.0)/20.0;
        for(int d=0;d<3;++d) weighted_velocity[d]+=mil*ui[16*k+4*l+d];
      }
      PetscScalar conv=0; for(int d=0;d<3;++d) conv+=p.rho*weighted_velocity[d]*g.grad[j][d];
      for(int d=0;d<3;++d) {
        const int r=4*i+d, col=4*j+d;
        ae[16*r+col]+=p.rho/p.dt*mass+p.mu*gradgrad+conv;
        be[r]+=p.rho/p.dt*mass*uo[16*k+4*j+d];
        be[r]+=p.rho*p.beta*grav[d]*mass*(tt[4*k+j]-p.reference_temperature);
        ae[16*r+4*j+3]+=-g.volume/4.0*g.grad[i][d];
        ae[16*(4*i+3)+col]+=-g.volume/4.0*g.grad[j][d];
      }
      // Simple pressure Laplacian stabilization, not residual-consistent PSPG.
      ae[16*(4*i+3)+4*j+3]+=-tau*gradgrad;
    }
    PetscCall(MatSetValues(A,16,id,16,id,ae,ADD_VALUES));
    PetscCall(VecSetValues(rhs,16,id,be,ADD_VALUES)); ++k;
  }
  PetscCall(VecRestoreArrayRead(u_old.local,&uo)); PetscCall(VecRestoreArrayRead(u_iter.local,&ui));
  PetscCall(VecRestoreArrayRead(temp.local,&tt));
  PetscCall(MatAssemblyBegin(A,MAT_FINAL_ASSEMBLY)); PetscCall(MatAssemblyEnd(A,MAT_FINAL_ASSEMBLY));
  PetscCall(VecAssemblyBegin(rhs)); PetscCall(VecAssemblyEnd(rhs));
  PetscFunctionReturn(PETSC_SUCCESS);
}
static PetscErrorCode assemble_heat(const LocalMesh& mesh, const Physics& p,
                                   Gather& t_old, Gather& velocity, Mat A, Vec rhs) {
  PetscFunctionBeginUser;
  PetscCall(MatZeroEntries(A)); PetscCall(VecSet(rhs,0));
  const PetscScalar *to,*u;
  PetscCall(VecGetArrayRead(t_old.local,&to)); PetscCall(VecGetArrayRead(velocity.local,&u));
  size_t k=0;
  for(const auto& e:mesh.elements) {
    Geometry g; PetscCall(geometry(e,g));
    PetscInt id[4]; PetscScalar ae[16]={},be[4]={};
    for(int i=0;i<4;++i) id[i]=e.n[i].id;
    for(int i=0;i<4;++i) for(int j=0;j<4;++j) {
      const PetscReal mass=g.volume*(i==j?2.0:1.0)/20.0;
      PetscReal gradgrad=0; for(int d=0;d<3;++d) gradgrad+=g.volume*g.grad[i][d]*g.grad[j][d];
      PetscScalar weighted_velocity[3]={0,0,0};
      for(int l=0;l<4;++l) {
        const PetscReal mil=g.volume*(i==l?2.0:1.0)/20.0;
        for(int d=0;d<3;++d) weighted_velocity[d]+=mil*u[16*k+4*l+d];
      }
      PetscScalar conv=0; for(int d=0;d<3;++d) conv+=weighted_velocity[d]*g.grad[j][d];
      ae[4*i+j]+=p.rho*p.heat_capacity*(mass/p.dt+conv)+p.conductivity*gradgrad;
      be[i]+=p.rho*p.heat_capacity/p.dt*mass*to[4*k+j];
    }
    PetscCall(MatSetValues(A,4,id,4,id,ae,ADD_VALUES));
    PetscCall(VecSetValues(rhs,4,id,be,ADD_VALUES)); ++k;
  }
  PetscCall(VecRestoreArrayRead(t_old.local,&to)); PetscCall(VecRestoreArrayRead(velocity.local,&u));
  PetscCall(MatAssemblyBegin(A,MAT_FINAL_ASSEMBLY)); PetscCall(MatAssemblyEnd(A,MAT_FINAL_ASSEMBLY));
  PetscCall(VecAssemblyBegin(rhs)); PetscCall(VecAssemblyEnd(rhs));
  PetscFunctionReturn(PETSC_SUCCESS);
}
struct Solver {
  MPI_Comm comm; const LocalMesh& mesh; Physics p;
  Vec flow=nullptr, temp=nullptr, flow_old=nullptr, temp_old=nullptr;
  Vec flow_rhs=nullptr,temp_rhs=nullptr,flow_work=nullptr,temp_work=nullptr;
  Vec flow_last=nullptr,temp_last=nullptr;
  Mat F=nullptr,H=nullptr; KSP fk=nullptr,hk=nullptr;
  Gather go,gi,gt,gto;
  Solver(MPI_Comm c,const LocalMesh& m,const Physics& physics):comm(c),mesh(m),p(physics) {}
  PetscErrorCode create() {
    PetscFunctionBeginUser;
    PetscCheck(p.rho>0 && p.mu>0 && p.heat_capacity>0 && p.conductivity>0 && p.dt>0 && p.pressure_stabilization>0,
               comm,PETSC_ERR_ARG_OUTOFRANGE,"Physical coefficients, dt, and stabilization must be positive");
    PetscCall(VecCreateMPI(comm,PETSC_DECIDE,4*mesh.global_nodes,&flow));
    PetscCall(VecCreateMPI(comm,PETSC_DECIDE,mesh.global_nodes,&temp));
    PetscCall(VecDuplicate(flow,&flow_old)); PetscCall(VecDuplicate(flow,&flow_rhs));
    PetscCall(VecDuplicate(flow,&flow_work)); PetscCall(VecDuplicate(flow,&flow_last));
    PetscCall(VecDuplicate(temp,&temp_old)); PetscCall(VecDuplicate(temp,&temp_rhs));
    PetscCall(VecDuplicate(temp,&temp_work)); PetscCall(VecDuplicate(temp,&temp_last));
    PetscCall(VecSet(flow,0)); PetscCall(VecSet(temp,0));
    PetscCall(make_matrix(flow,80,&F)); PetscCall(make_matrix(temp,20,&H));
    PetscCall(setup_ksp(F,"flow_",&fk)); PetscCall(setup_ksp(H,"heat_",&hk));
    PetscCall(go.create(flow,mesh,4)); PetscCall(gi.create(flow,mesh,4));
    PetscCall(gt.create(temp,mesh,1)); PetscCall(gto.create(temp,mesh,1));
    PetscFunctionReturn(PETSC_SUCCESS);
  }
  PetscErrorCode step(PetscInt max_iterations, PetscReal tolerance) {
    PetscFunctionBeginUser;
    PetscCheck(max_iterations>0 && tolerance>0,comm,PETSC_ERR_ARG_OUTOFRANGE,"Invalid nonlinear controls");
    PetscCall(VecCopy(flow,flow_old)); PetscCall(VecCopy(temp,temp_old));
    PetscCall(go.update(flow_old)); PetscCall(gto.update(temp_old));
    for(PetscInt it=0;it<max_iterations;++it) {
      PetscCall(VecCopy(flow,flow_last)); PetscCall(VecCopy(temp,temp_last));
      PetscCall(gi.update(flow)); PetscCall(gt.update(temp));
      PetscCall(assemble_flow(mesh,p,go,gi,gt,F,flow_rhs));
      PetscCall(impose(F,flow_rhs,flow_work,mesh.flow_bc));
      PetscCall(KSPSolve(fk,flow_rhs,flow));
      PetscCall(gi.update(flow));
      PetscCall(assemble_heat(mesh,p,gto,gi,H,temp_rhs));
      PetscCall(impose(H,temp_rhs,temp_work,mesh.temperature_bc));
      PetscCall(KSPSolve(hk,temp_rhs,temp));
      PetscReal df,dt,nf,nt;
      PetscCall(VecAXPY(flow_last,-1,flow)); PetscCall(VecAXPY(temp_last,-1,temp));
      PetscCall(VecNorm(flow_last,NORM_2,&df)); PetscCall(VecNorm(temp_last,NORM_2,&dt));
      PetscCall(VecNorm(flow,NORM_2,&nf)); PetscCall(VecNorm(temp,NORM_2,&nt));
      const PetscReal change=std::max(df/(1+nf),dt/(1+nt));
      PetscCall(PetscPrintf(comm,"  Picard %" PetscInt_FMT ": relative update %.3e\n",it+1,(double)change));
      if(change<tolerance) PetscFunctionReturn(PETSC_SUCCESS);
    }
    SETERRQ(comm,PETSC_ERR_NOT_CONVERGED,"Picard iteration failed; reduce dt or adjust coupling/solver settings");
  }
  PetscErrorCode destroy() {
    PetscFunctionBeginUser;
    PetscCall(go.destroy()); PetscCall(gi.destroy()); PetscCall(gt.destroy()); PetscCall(gto.destroy());
    PetscCall(KSPDestroy(&fk)); PetscCall(KSPDestroy(&hk)); PetscCall(MatDestroy(&F)); PetscCall(MatDestroy(&H));
    PetscCall(VecDestroy(&flow)); PetscCall(VecDestroy(&temp)); PetscCall(VecDestroy(&flow_old)); PetscCall(VecDestroy(&temp_old));
    PetscCall(VecDestroy(&flow_rhs)); PetscCall(VecDestroy(&temp_rhs)); PetscCall(VecDestroy(&flow_work)); PetscCall(VecDestroy(&temp_work));
    PetscCall(VecDestroy(&flow_last)); PetscCall(VecDestroy(&temp_last));
    PetscFunctionReturn(PETSC_SUCCESS);
  }
};
// Demo only. Six Freudenthal tetrahedra per hexahedral cell; elements are
// partitioned by index and never replicated. Your unstructured mesh replaces it.
static Node grid_node(PetscInt id,PetscInt nx,PetscInt ny,PetscInt nz) {
  const PetscInt sx=nx+1, sy=ny+1;
  return {id,(PetscReal)(id%sx)/nx,(PetscReal)((id/sx)%sy)/ny,(PetscReal)(id/(sx*sy))/nz};
}
static bool is_boundary(PetscInt id,PetscInt nx,PetscInt ny,PetscInt nz) {
  PetscInt x=id%(nx+1),y=(id/(nx+1))%(ny+1),z=id/((nx+1)*(ny+1));
  return x==0 || x==nx || y==0 || y==ny || z==0 || z==nz;
}
static PetscErrorCode demo_mesh(MPI_Comm comm,PetscInt nx,PetscInt ny,PetscInt nz,LocalMesh& m) {
  PetscFunctionBeginUser;
  int rank,size; PetscCallMPI(MPI_Comm_rank(comm,&rank)); PetscCallMPI(MPI_Comm_size(comm,&size));
  m.global_nodes=(nx+1)*(ny+1)*(nz+1);
  const PetscInt count=6*nx*ny*nz;
  const PetscInt begin=count*rank/size,end=count*(rank+1)/size;
  const int perms[6][3]={{0,1,2},{0,2,1},{1,0,2},{1,2,0},{2,0,1},{2,1,0}};
  const PetscInt stride[3]={1,nx+1,(nx+1)*(ny+1)};
  for(PetscInt q=begin;q<end;++q) {
    const PetscInt cell=q/6, x=cell%nx,y=(cell/nx)%ny,z=cell/(nx*ny);
    const PetscInt a=x+(nx+1)*(y+(ny+1)*z);
    const int* order=perms[q%6];
    const PetscInt b=a+stride[order[0]], c=b+stride[order[1]], d=c+stride[order[2]];
    Tetrahedron e;
    e.n={grid_node(a,nx,ny,nz),grid_node(b,nx,ny,nz),
         grid_node(c,nx,ny,nz),grid_node(d,nx,ny,nz)};
    m.elements.push_back(e);
  }
  PetscFunctionReturn(PETSC_SUCCESS);
}
static PetscScalar flow_exact(const Node& n,int c,bool buoyancy) {
  if(c==0) return n.y;
  if(c==3 && buoyancy) return -0.8*n.z;
  return 0;
}
static PetscScalar temperature_exact(const Node& n,bool buoyancy) { return buoyancy?1.0:n.y; }
// Smoke test 1: u=(y,0,0), p=0, T=y. Pressure pinned at one node.
// Smoke test 2: u=(y,0,0), p=-0.8z, T=1, beta=0.8, g=(0,0,-1).
// Test 2 prescribes pressure on the boundary as well, so that the simple
// pressure-Laplacian stabilization has zero interior residual for linear p.
static PetscErrorCode demo_initial_and_boundary(Solver& s,LocalMesh& mesh,
                                                 PetscInt nx,PetscInt ny,PetscInt nz,
                                                 bool buoyancy) {
  PetscFunctionBeginUser;
  PetscInt lo,hi;
  PetscCall(VecGetOwnershipRange(s.flow,&lo,&hi));
  for(PetscInt row=lo;row<hi;++row) {
    PetscInt id=row/4,c=row%4; Node n=grid_node(id,nx,ny,nz);
    PetscScalar value=flow_exact(n,(int)c,buoyancy);
    PetscCall(VecSetValue(s.flow,row,value,INSERT_VALUES));
    bool boundary=is_boundary(id,nx,ny,nz);
    if((boundary && c<3) || (c==3 && (buoyancy?boundary:id==0)))
      mesh.flow_bc.push_back({row,value});
  }
  PetscCall(VecGetOwnershipRange(s.temp,&lo,&hi));
  for(PetscInt id=lo;id<hi;++id) {
    Node n=grid_node(id,nx,ny,nz); PetscScalar value=temperature_exact(n,buoyancy);
    PetscCall(VecSetValue(s.temp,id,value,INSERT_VALUES));
    if(is_boundary(id,nx,ny,nz)) mesh.temperature_bc.push_back({id,value});
  }
  PetscCall(VecAssemblyBegin(s.flow)); PetscCall(VecAssemblyEnd(s.flow));
  PetscCall(VecAssemblyBegin(s.temp)); PetscCall(VecAssemblyEnd(s.temp));
  PetscFunctionReturn(PETSC_SUCCESS);
}
static PetscErrorCode check_demo(Solver& s,PetscInt nx,PetscInt ny,PetscInt nz,bool buoyancy) {
  PetscFunctionBeginUser;
  PetscReal local=0,global=0; PetscInt lo,hi; const PetscScalar* a;
  PetscCall(VecGetOwnershipRange(s.flow,&lo,&hi)); PetscCall(VecGetArrayRead(s.flow,&a));
  for(PetscInt row=lo;row<hi;++row) {
    Node n=grid_node(row/4,nx,ny,nz);
    local=std::max(local,PetscAbsScalar(a[row-lo]-flow_exact(n,(int)(row%4),buoyancy)));
  }
  PetscCall(VecRestoreArrayRead(s.flow,&a));
  PetscCall(VecGetOwnershipRange(s.temp,&lo,&hi)); PetscCall(VecGetArrayRead(s.temp,&a));
  for(PetscInt id=lo;id<hi;++id)
    local=std::max(local,PetscAbsScalar(a[id-lo]-temperature_exact(grid_node(id,nx,ny,nz),buoyancy)));
  PetscCall(VecRestoreArrayRead(s.temp,&a));
  PetscCallMPI(MPI_Allreduce(&local,&global,1,MPIU_REAL,MPI_MAX,s.comm));
  PetscCall(PetscPrintf(s.comm,"Maximum nodal error against steady exact solution: %.6e\n",(double)global));
  PetscCheck(global<1e-7,s.comm,PETSC_ERR_NOT_CONVERGED,"Manufactured steady test failed");
  PetscFunctionReturn(PETSC_SUCCESS);
}
int main(int argc,char** argv) {
  PetscCall(PetscInitialize(&argc,&argv,nullptr,"3D distributed flow and heat prototype\n"));
  PetscInt nx=4,ny=4,nz=4,steps=2,max_it=20; PetscReal tol=1e-9;
  PetscBool buoyancy=PETSC_FALSE;
  PetscCall(PetscOptionsGetInt(nullptr,nullptr,"-nx",&nx,nullptr));
  PetscCall(PetscOptionsGetInt(nullptr,nullptr,"-ny",&ny,nullptr));
  PetscCall(PetscOptionsGetInt(nullptr,nullptr,"-nz",&nz,nullptr));
  PetscCall(PetscOptionsGetInt(nullptr,nullptr,"-steps",&steps,nullptr));
  PetscCall(PetscOptionsGetInt(nullptr,nullptr,"-picard_max_it",&max_it,nullptr));
  PetscCall(PetscOptionsGetReal(nullptr,nullptr,"-picard_tol",&tol,nullptr));
  PetscCall(PetscOptionsGetBool(nullptr,nullptr,"-demo_buoyancy",&buoyancy,nullptr));
  PetscCheck(nx>=2 && ny>=2 && nz>=2 && steps>0,PETSC_COMM_WORLD,PETSC_ERR_ARG_OUTOFRANGE,
             "nx, ny, nz >= 2 and steps > 0 required");
  LocalMesh mesh; PetscCall(demo_mesh(PETSC_COMM_WORLD,nx,ny,nz,mesh));
  Physics p; if(buoyancy) p.beta=0.8;
  Solver solver(PETSC_COMM_WORLD,mesh,p); PetscCall(solver.create());
  PetscCall(demo_initial_and_boundary(solver,mesh,nx,ny,nz,buoyancy));
  for(PetscInt i=0;i<steps;++i) {
    PetscCall(PetscPrintf(PETSC_COMM_WORLD,"Time step %" PetscInt_FMT "\n",i+1));
    PetscCall(solver.step(max_it,tol));
  }
  PetscCall(check_demo(solver,nx,ny,nz,buoyancy)); PetscCall(solver.destroy());
  PetscCall(PetscFinalize()); return 0;
}
