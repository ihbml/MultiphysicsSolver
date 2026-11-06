!================================================================================
! mod_mesh -- mesh topology container.
!
! DESIGN NOTE (performance / accuracy):
!   Faces are stored in a *compressed* form driven by the element->face CSR:
!     * interior faces exist exactly once (owner = lower cell, neighbour = higher)
!     * boundary faces are stored once, flagged with neighbour = 0
!   Compared with the usual "2*nfaces" half-face layout this
!     (a) halves the memory traffic of the flux loop,
!     (b) guarantees each face flux is evaluated exactly once per sweep,
!         i.e. the discrete momentum/energy/mass fluxes are exactly antisymmetric
!         between the two adjacent cells -> discrete conservation is built in.
!
! The engine assumes the mesh is ALREADY IN MEMORY, so no file I/O lives here;
! `generate_hex8` exists only so the benchmark is self-contained.
!================================================================================
module mod_mesh
   use mod_kinds
   use mod_util, only: engine_abort
   implicit none
   private

   public :: mesh_t
   public :: mesh_allocate, mesh_destroy, mesh_compute_metrics, mesh_summary
   public :: generate_hex8
   public :: HEX8, TET4, QUAD4, TRI3

   integer, parameter :: HEX8  = 1
   integer, parameter :: TET4  = 2
   integer, parameter :: QUAD4 = 1
   integer, parameter :: TRI3  = 2

   type :: mesh_t
      !---- dimensions -------------------------------------------------------
      integer :: dim = 3          !< spatial dimension (3 -> HEX8/TET4)
      integer :: nnodes = 0
      integer :: ncells = 0
      integer :: nfaces = 0       !< compressed face count
      integer :: nefaces = 0      !< number of element-face entries
      integer :: nbfaces = 0      !< subset of nfaces that are boundary
      integer :: etype = HEX8     !< cell type (single type per mesh)
      integer :: nodes_per_elem = 8
      integer :: faces_per_elem = 6

      !---- geometry ---------------------------------------------------------
      real(wp), allocatable :: xyz(:,:)     !< (dim, nnodes) nodal coordinates

      !---- cells ------------------------------------------------------------
      integer, allocatable :: elem_nodes(:,:) !< (nodes_per_elem, ncells) connectivity
      real(wp), allocatable :: volume(:)    !< cell volume (integrated detJ * w)

      !---- faces (compressed, owner <= neighbour convention) ---------------
      integer, allocatable :: face_nodes(:,:) !< (4, nfaces)
      integer, allocatable :: face_owner(:)  !< (nfaces) cell index
      integer, allocatable :: face_neigh(:)  !< (nfaces) 0 => boundary
      integer, allocatable :: face_nloc(:)   !< (nfaces) local face id within owner
      real(wp), allocatable :: face_area(:)  !< (nfaces)
      real(wp), allocatable :: face_normal(:,:) !< (dim, nfaces) unit outward (owner)
      real(wp), allocatable :: face_cent(:,:)   !< (dim, nfaces) face centroid

      !---- inverse maps -----------------------------------------------------
      integer, allocatable :: face_ptr(:)   !< (ncells+1) CSR into elem_faces
      integer, allocatable :: elem_faces(:) !< (nefaces) global face ids

      !---- element geometry moments (precomputed, reused by physics) --------
      real(wp), allocatable :: neigh_dist(:,:) !< (faces_per_elem, ncells) owner->neighbour centroid distance

      !---- partitioning (filled by a real domain partitioner in production) -
      integer, allocatable :: cell_color(:) !< graph colour, used ONLY to schedule
      integer :: ncolors = 0                !< multi-threaded assembly
   end type mesh_t

   ! Local face -> local node map for HEX8.
   ! Node ordering (i fastest, k slowest):
   !   1=(i,j,k) 2=(i+1,j,k) 3=(i+1,j+1,k) 4=(i,j+1,k)
   !   5=(i,j,k+1) 6=(i+1,j,k+1) 7=(i+1,j+1,k+1) 8=(i,j+1,k+1)
   ! FIXED: the first four faces are the two k-planes; the i- and j-planes come
   ! after.  (Getting this wrong silently produces "faces" that are not faces,
   ! which the manifold check in generate_hex8 catches immediately.)
   integer, parameter :: HEX_FACE_NODES(4,6) = reshape( [ &
         1,2,3,4,   5,6,7,8,   1,4,8,5,   2,3,7,6,   1,2,6,5,   4,3,7,8 ], [4,6] )

contains

   !===========================================================================
   subroutine mesh_allocate(m, ncells, nnodes, nfaces, nefaces, nbfaces, dim, &
                            etype, npe, fpe, nfn)
      type(mesh_t), intent(inout) :: m
      integer, intent(in) :: ncells, nnodes, nfaces, nefaces, nbfaces
      integer, intent(in) :: dim, etype, npe, fpe, nfn
      call mesh_destroy(m)
      m%dim = dim; m%etype = etype
      m%ncells = ncells; m%nnodes = nnodes; m%nfaces = nfaces
      m%nefaces = nefaces; m%nbfaces = nbfaces
      m%nodes_per_elem = npe; m%faces_per_elem = fpe
      allocate(m%xyz(dim, nnodes), source=ZERO)
      allocate(m%elem_nodes(npe, ncells))
      allocate(m%volume(ncells), source=ZERO)
      allocate(m%face_nodes(nfn, nfaces))
      allocate(m%face_owner(nfaces), source=0)
      allocate(m%face_neigh(nfaces), source=0)
      allocate(m%face_nloc(nfaces),  source=0)
      allocate(m%face_area(nfaces), source=ZERO)
      allocate(m%face_normal(dim, nfaces), source=ZERO)
      allocate(m%face_cent(dim, nfaces), source=ZERO)
      allocate(m%face_ptr(ncells+1), source=0)
      allocate(m%elem_faces(nefaces), source=0)
      allocate(m%neigh_dist(fpe, ncells), source=ZERO)
   end subroutine mesh_allocate

   subroutine mesh_destroy(m)
      type(mesh_t), intent(inout) :: m
      if (allocated(m%xyz))         deallocate(m%xyz)
      if (allocated(m%elem_nodes))  deallocate(m%elem_nodes)
      if (allocated(m%volume))      deallocate(m%volume)
      if (allocated(m%face_nodes))  deallocate(m%face_nodes)
      if (allocated(m%face_owner))  deallocate(m%face_owner)
      if (allocated(m%face_neigh))  deallocate(m%face_neigh)
      if (allocated(m%face_nloc))   deallocate(m%face_nloc)
      if (allocated(m%face_area))   deallocate(m%face_area)
      if (allocated(m%face_normal)) deallocate(m%face_normal)
      if (allocated(m%face_cent))   deallocate(m%face_cent)
      if (allocated(m%face_ptr))    deallocate(m%face_ptr)
      if (allocated(m%elem_faces))  deallocate(m%elem_faces)
      if (allocated(m%neigh_dist))  deallocate(m%neigh_dist)
      if (allocated(m%cell_color))  deallocate(m%cell_color)
      m%ncells = 0; m%nnodes = 0; m%nfaces = 0
   end subroutine mesh_destroy

   !===========================================================================
   !> Build a structured HEX8 mesh of nx*ny*nz cells on [0,lx]x[0,ly]x[0,lz].
   !> Node (i,j,k) with i in 0..nx etc. is numbered i + j*(nx+1) + k*(nx+1)*(ny+1).
   !> Cell (i,j,k) has local nodes
   !>   1:(i  ,j  ,k  ) 2:(i+1,j  ,k  ) 3:(i+1,j+1,k  ) 4:(i  ,j+1,k  )
   !>   5:(i  ,j  ,k+1) 6:(i+1,j  ,k+1) 7:(i+1,j+1,k+1) 8:(i  ,j+1,k+1)
   !> which is the standard right-handed HEX8 ordering (matches HEX_FACE_NODES).
   !===========================================================================
   subroutine generate_hex8(m, nx, ny, nz, lx, ly, lz)
      type(mesh_t), intent(inout) :: m
      integer, intent(in) :: nx, ny, nz
      real(wp), intent(in) :: lx, ly, lz

      integer :: i, j, k, c, n, id, ii, jj, kk
      integer :: nin(0:1,0:1,0:1), npi, npj, npk
      integer, allocatable :: cell_id(:,:,:), ndone(:), fcount(:)
      integer, allocatable :: key_a(:), key_b(:), tface(:,:)
      integer :: nf, nbf, ne, nkey, kb, t
      integer :: h, nl, nr, ncount
      integer, allocatable :: sortidx(:)
      integer :: tmp(4), nc, nn

      npi = nx+1; npj = ny+1; npk = nz+1
      ! NOTE: locals, never m%components, as mesh_allocate arguments -- it
      ! destroys m first, so passing components would read freed state.
      nn = npi*npj*npk
      nc = nx*ny*nz
      ne = 6*nc

      ! provisional allocation (face count fixed up after counting)
      call mesh_allocate(m, nc, nn, 1, ne, 1, 3, HEX8, 8, 6, 4)
      deallocate(m%face_nodes); allocate(m%face_nodes(4, ne))
      deallocate(m%face_owner); allocate(m%face_owner(ne))
      deallocate(m%face_neigh); allocate(m%face_neigh(ne))
      deallocate(m%face_nloc ); allocate(m%face_nloc (ne))
      deallocate(m%face_area ); allocate(m%face_area (ne))
      deallocate(m%face_normal); allocate(m%face_normal(3, ne))
      deallocate(m%face_cent ); allocate(m%face_cent (3, ne))

      ! -- nodes --
      do k = 0, nz
         do j = 0, ny
            do i = 0, nx
               n = 1 + i + j*npi + k*npi*npj
               m%xyz(1,n) = lx*real(i,wp)/real(nx,wp)
               m%xyz(2,n) = ly*real(j,wp)/real(ny,wp)
               m%xyz(3,n) = lz*real(k,wp)/real(nz,wp)
            end do
         end do
      end do

      ! -- cells --
      c = 0
      do k = 0, nz-1
         do j = 0, ny-1
            do i = 0, nx-1
               c = c + 1
               m%elem_nodes(1,c) = 1 + i     + j*npi       + k*npi*npj
               m%elem_nodes(2,c) = 1 + i+1   + j*npi       + k*npi*npj
               m%elem_nodes(3,c) = 1 + i+1   + (j+1)*npi   + k*npi*npj
               m%elem_nodes(4,c) = 1 + i     + (j+1)*npi   + k*npi*npj
               m%elem_nodes(5,c) = 1 + i     + j*npi       + (k+1)*npi*npj
               m%elem_nodes(6,c) = 1 + i+1   + j*npi       + (k+1)*npi*npj
               m%elem_nodes(7,c) = 1 + i+1   + (j+1)*npi   + (k+1)*npi*npj
               m%elem_nodes(8,c) = 1 + i     + (j+1)*npi   + (k+1)*npi*npj
            end do
         end do
      end do

      ! -- faces: hash unique node-sets.  For HEX8 every face is a QUAD4 whose
      !    sorted node tuple is a unique key.  We use a direct-address hash on
      !    the min node id; collisions resolved by the full sorted key.
      allocate(key_a(4*ne), key_b(4*ne), fcount(ne))
      nkey = 0
      do c = 1, m%ncells
         do t = 1, 6
            tmp = m%elem_nodes(HEX_FACE_NODES(:,t), c)
            call sort4(tmp)
            nkey = nkey + 1
            key_a((nkey-1)*4+1:nkey*4) = tmp
            ! owner bookkeeping stored in parallel arrays
            key_b(nkey) = c*8 + t            ! packed owner/localfacet
            fcount(nkey) = 0
         end do
      end do
      ! simple sort of the 4-node keys (index sort by node tuple)
      allocate(sortidx(nkey))
      do i = 1, nkey
         sortidx(i) = i
      end do
      call isort_keys(key_a, sortidx, nkey)

      ! walk sorted keys, build unique faces
      nf = 0
      i = 1
      do while (i <= nkey)
         nf = nf + 1
         j = i
         do while (j < nkey)
            if (all(key_a(4*(sortidx(j+1)-1)+1:4*sortidx(j+1)) == &
                    key_a(4*(sortidx(i)-1)+1:4*sortidx(i)))) then
               j = j + 1
            else
               exit
            end if
         end do
         ! entries sortidx(i..j) share one face.
         ! NOTE: the heapsort is not stable, so the owner/neighbour pair is
         ! ordered explicitly by cell id -- never by discovery order.
         block
            integer :: c1, c2, t1, t2
            c1 = key_b(sortidx(i))/8
            t1 = key_b(sortidx(i)) - 8*c1
            m%face_nodes(:,nf) = key_a(4*(sortidx(i)-1)+1:4*(sortidx(i)-1)+4)
            if (j > i) then
               c2 = key_b(sortidx(i+1))/8
               t2 = key_b(sortidx(i+1)) - 8*c2
               if (j > i+1) call engine_abort( &
                  'non-manifold face (more than two incident cells)','generate_hex8')
               if (c1 <= c2) then
                  m%face_owner(nf) = c1; m%face_nloc(nf) = t1; m%face_neigh(nf) = c2
               else
                  m%face_owner(nf) = c2; m%face_nloc(nf) = t2; m%face_neigh(nf) = c1
               end if
            else
               m%face_owner(nf) = c1; m%face_nloc(nf) = t1; m%face_neigh(nf) = 0
            end if
         end block
         i = j + 1
      end do
      nbf = 0
      do i = 1, nf
         if (m%face_neigh(i) == 0) nbf = nbf + 1
      end do

      ! shrink face arrays to the true count
      call shrink_faces(m, nf)
      m%nfaces = nf; m%nbfaces = nbf

      ! -- element -> face CSR (counting sort) --
      m%face_ptr = 0
      do i = 1, nf
         m%face_ptr(m%face_owner(i)+1) = m%face_ptr(m%face_owner(i)+1) + 1
         if (m%face_neigh(i) > 0) &
            m%face_ptr(m%face_neigh(i)+1) = m%face_ptr(m%face_neigh(i)+1) + 1
      end do
      m%face_ptr(1) = 1
      do c = 1, m%ncells
         m%face_ptr(c+1) = m%face_ptr(c+1) + m%face_ptr(c)
      end do
      allocate(ndone(m%ncells), source=0)
      do i = 1, nf
         c = m%face_owner(i)
         ndone(c) = ndone(c) + 1
         m%elem_faces(m%face_ptr(c) + ndone(c) - 1) = i
         if (m%face_neigh(i) > 0) then
            c = m%face_neigh(i)
            ndone(c) = ndone(c) + 1
            m%elem_faces(m%face_ptr(c) + ndone(c) - 1) = i
         end if
      end do
      do c = 1, m%ncells
         if (ndone(c) /= 6) then
            write(*,*) 'cell ', c, ' has ', ndone(c), ' faces'
            call engine_abort('bad face incidence in generator','generate_hex8')
         end if
      end do

      call mesh_compute_metrics(m)
      ! needed by physics for owner->neighbour distances
      call compute_neighbour_distances(m)

      deallocate(key_a, key_b, fcount, sortidx, ndone)
   end subroutine generate_hex8

   !> slot p s.t. elem_faces(face_ptr(c) + p - 1) == fid
   integer function elem_face_slot(m, c, fid) result(p)
      type(mesh_t), intent(in) :: m
      integer, intent(in) :: c, fid
      integer :: q
      do q = m%face_ptr(c), m%face_ptr(c+1)-1
         if (m%elem_faces(q) == fid) then
            p = q - m%face_ptr(c) + 1
            return
         end if
      end do
      p = 0
   end function elem_face_slot

   subroutine shrink_faces(m, nf)
      type(mesh_t), intent(inout) :: m
      integer, intent(in) :: nf
      m%face_nodes  = m%face_nodes(:,1:nf)
      m%face_owner  = m%face_owner(1:nf)
      m%face_neigh  = m%face_neigh(1:nf)
      m%face_nloc   = m%face_nloc(1:nf)
      m%face_area   = m%face_area(1:nf)
      m%face_normal = m%face_normal(:,1:nf)
      m%face_cent   = m%face_cent(:,1:nf)
   end subroutine shrink_faces

   pure subroutine sort4(a)
      integer, intent(inout) :: a(4)
      integer :: t, q, r
      do q = 2, 4
         t = a(q); r = q - 1
         do while (r >= 1)
            if (a(r) <= t) exit
            a(r+1) = a(r); r = r - 1
         end do
         a(r+1) = t
      end do
   end subroutine sort4

   !> Lexicographic index sort of 4-int keys.  O(n log n) via heapsort on the
   !> index array -- used once at mesh ingest time, never in the solver loop.
   subroutine isort_keys(ka, idx, n)
      integer, intent(in) :: ka(:), n
      integer, intent(inout) :: idx(:)
      call heapsort(ka, idx, n)
   contains
      subroutine heapsort(k, ix, nn)
         integer, intent(in) :: k(:), nn
         integer, intent(inout) :: ix(:)
         integer :: s, b, root, child, sw
         do s = nn/2, 1, -1
            call sift(k, ix, s, nn)
         end do
         do s = nn, 2, -1
            sw = ix(1); ix(1) = ix(s); ix(s) = sw
            call sift(k, ix, 1, s-1)
         end do
      end subroutine heapsort
      subroutine sift(k, ix, start, nn)
         integer, intent(in) :: k(:), start, nn
         integer, intent(inout) :: ix(:)
         integer :: root, child, sw
         root = start
         do
            child = 2*root
            if (child > nn) exit
            if (child+1 <= nn) then
               if (kless(k, ix(child+1), ix(child))) child = child + 1
            end if
            if (kless(k, ix(child), ix(root))) then
               sw = ix(root); ix(root) = ix(child); ix(child) = sw
               root = child
            else
               exit
            end if
         end do
      end subroutine sift
      logical function kless(k, a, b)
         integer, intent(in) :: k(:), a, b
         integer :: q
         kless = .false.
         do q = 0, 3
            if (k(4*(a-1)+1+q) < k(4*(b-1)+1+q)) then
               kless = .true.; return
            else if (k(4*(a-1)+1+q) > k(4*(b-1)+1+q)) then
               return
            end if
         end do
      end function kless
   end subroutine isort_keys

   !===========================================================================
   !> Precompute volumes, face areas, unit normals (owner-outward) and centroids.
   !> These are geometric constants and are computed ONCE per mesh, so the
   !> element residual never touches coordinates again.
   !===========================================================================
   subroutine mesh_compute_metrics(m)
      type(mesh_t), intent(inout) :: m
      integer :: c, f
      real(wp) :: p1(3), p2(3), p3(3), p4(3), cr(3), ar, cen(3), vol

      ! Cell volume = integral of detJ over the isoparametric map.  A 2x2x2
      ! Gauss rule integrates detJ EXACTLY (detJ is quadratic in each local
      ! coordinate for a trilinear hex), so this is the exact volume of the
      ! general warped hexahedron.  Tetrahedral decompositions are NOT exact
      ! for non-planar-faced cells -- a 5/6-tet split loses O(h^2) volume on
      ! any distorted mesh -- so we deliberately do not use one.
      do c = 1, m%ncells
         vol = ZERO
         block
            integer :: id(8), qq, a
            real(wp) :: gq(3,8), xl(3,8), sg(3,8), xi(3), J(3,3), dj
            call hex_gauss_points(gq)
            do a = 1, 8
               xl(:,a) = m%xyz(:, m%elem_nodes(a,c))
            end do
            call hex_signs(sg)
            do qq = 1, 8
               xi = gq(:,qq)
               J = ZERO
               do a = 1, 8
                  J(1,1) = J(1,1) + xl(1,a)*0.125_wp*sg(1,a)*(ONE+xi(2)*sg(2,a))*(ONE+xi(3)*sg(3,a))
                  J(2,1) = J(2,1) + xl(2,a)*0.125_wp*sg(1,a)*(ONE+xi(2)*sg(2,a))*(ONE+xi(3)*sg(3,a))
                  J(3,1) = J(3,1) + xl(3,a)*0.125_wp*sg(1,a)*(ONE+xi(2)*sg(2,a))*(ONE+xi(3)*sg(3,a))
                  J(1,2) = J(1,2) + xl(1,a)*0.125_wp*sg(2,a)*(ONE+xi(1)*sg(1,a))*(ONE+xi(3)*sg(3,a))
                  J(2,2) = J(2,2) + xl(2,a)*0.125_wp*sg(2,a)*(ONE+xi(1)*sg(1,a))*(ONE+xi(3)*sg(3,a))
                  J(3,2) = J(3,2) + xl(3,a)*0.125_wp*sg(2,a)*(ONE+xi(1)*sg(1,a))*(ONE+xi(3)*sg(3,a))
                  J(1,3) = J(1,3) + xl(1,a)*0.125_wp*sg(3,a)*(ONE+xi(1)*sg(1,a))*(ONE+xi(2)*sg(2,a))
                  J(2,3) = J(2,3) + xl(2,a)*0.125_wp*sg(3,a)*(ONE+xi(1)*sg(1,a))*(ONE+xi(2)*sg(2,a))
                  J(3,3) = J(3,3) + xl(3,a)*0.125_wp*sg(3,a)*(ONE+xi(1)*sg(1,a))*(ONE+xi(2)*sg(2,a))
               end do
               dj = J(1,1)*(J(2,2)*J(3,3)-J(2,3)*J(3,2)) &
                  - J(1,2)*(J(2,1)*J(3,3)-J(2,3)*J(3,1)) &
                  + J(1,3)*(J(2,1)*J(3,2)-J(2,2)*J(3,1))
               vol = vol + dj
            end do
         end block
         m%volume(c) = vol
         if (vol <= 0.0_wp) call engine_abort('non-positive cell volume','mesh_compute_metrics')
      end do

      do f = 1, m%nfaces
         p1 = m%xyz(:,m%face_nodes(1,f))
         p2 = m%xyz(:,m%face_nodes(2,f))
         p3 = m%xyz(:,m%face_nodes(3,f))
         p4 = m%xyz(:,m%face_nodes(4,f))
         cen = 0.25_wp*(p1+p2+p3+p4)
         m%face_cent(:,f) = cen
         cr = cross(p3-p1, p4-p2)          ! 2x the diagonal cross-product area
         ar = 0.5_wp*norm2v(cr)
         m%face_area(f) = ar
         cr = cr/ar                         ! unit normal, sign fixed below
         ! orient outward from owner using the cell-centroid approximation
         block
            real(wp) :: cc(3)
            cc = cell_centroid(m, m%face_owner(f))
            if (dot(cr, cen-cc) < 0.0_wp) cr = -cr
         end block
         m%face_normal(:,f) = cr
      end do
   end subroutine mesh_compute_metrics

   real(wp) function tet_vol(a,b,c,d) result(v)
      real(wp), intent(in) :: a(3), b(3), c(3), d(3)
      real(wp) :: m(3,3)
      m(:,1) = b-a; m(:,2) = c-a; m(:,3) = d-a
      v = ( m(1,1)*(m(2,2)*m(3,3)-m(2,3)*m(3,2)) &
          - m(1,2)*(m(2,1)*m(3,3)-m(2,3)*m(3,1)) &
          + m(1,3)*(m(2,1)*m(3,2)-m(2,2)*m(3,1)) ) / 6.0_wp
   end function tet_vol

   pure function cross(a,b) result(c)
      real(wp), intent(in) :: a(3), b(3)
      real(wp) :: c(3)
      c(1) = a(2)*b(3)-a(3)*b(2)
      c(2) = a(3)*b(1)-a(1)*b(3)
      c(3) = a(1)*b(2)-a(2)*b(1)
   end function cross

   pure real(wp) function dot(a,b) result(d)
      real(wp), intent(in) :: a(3), b(3)
      d = a(1)*b(1)+a(2)*b(2)+a(3)*b(3)
   end function dot

   pure real(wp) function norm2v(a) result(n)
      real(wp), intent(in) :: a(3)
      n = sqrt(a(1)*a(1)+a(2)*a(2)+a(3)*a(3))
   end function norm2v

   pure function cell_centroid(m, c) result(cc)
      type(mesh_t), intent(in) :: m
      integer, intent(in) :: c
      real(wp) :: cc(3)
      integer :: q
      cc = ZERO
      do q = 1, m%nodes_per_elem
         cc = cc + m%xyz(:,m%elem_nodes(q,c))
      end do
      cc = cc/real(m%nodes_per_elem,wp)
   end function cell_centroid

   subroutine compute_neighbour_distances(m)
      type(mesh_t), intent(inout) :: m
      integer :: c, q, f, nb
      real(wp) :: cc(3), cn(3)
      do c = 1, m%ncells
         cc = cell_centroid(m, c)
         do q = 1, m%faces_per_elem
            f  = m%elem_faces(m%face_ptr(c)+q-1)
            nb = other_cell(m, f, c)
            if (nb > 0) then
               cn = cell_centroid(m, nb)
               m%neigh_dist(q,c) = norm2v(cn-cc)
            else
               m%neigh_dist(q,c) = ZERO
            end if
         end do
      end do
   end subroutine compute_neighbour_distances

   pure integer function other_cell(m, f, c) result(o)
      type(mesh_t), intent(in) :: m
      integer, intent(in) :: f, c
      if (m%face_owner(f) == c) then
         o = m%face_neigh(f)
      else
         o = m%face_owner(f)
      end if
   end function other_cell

   !===========================================================================
   subroutine mesh_summary(m)
      type(mesh_t), intent(in) :: m
      real(wp) :: vol
      vol = sum(m%volume)
      write(*,'(a)')          '  --------------------------------------------------------------'
      write(*,'(a,i12)')      '  cells               : ', m%ncells
      write(*,'(a,i12)')      '  nodes               : ', m%nnodes
      write(*,'(a,i12)')      '  faces (compressed)  : ', m%nfaces
      write(*,'(a,i12)')      '  boundary faces      : ', m%nbfaces
      write(*,'(a,es12.5)')   '  total volume        : ', vol
      write(*,'(a,i12)')      '  graph colours       : ', m%ncolors
      write(*,'(a)')          '  --------------------------------------------------------------'
   end subroutine mesh_summary

   !> 2x2x2 Gauss points in [-1,1]^3 (weights are 1).
   pure subroutine hex_gauss_points(gq)
      real(wp), intent(out) :: gq(3,8)
      real(wp) :: g, v(2)
      integer :: q, i, j, k
      g = ONE/sqrt(3.0_wp)
      v = [-g, g]
      q = 0
      do k = 1, 2
         do j = 1, 2
            do i = 1, 2
               q = q + 1
               gq(:,q) = [v(i), v(j), v(k)]
            end do
         end do
      end do
   end subroutine hex_gauss_points

   !> Local node sign table for the HEX8 used by generate_hex8.
   pure subroutine hex_signs(sg)
      real(wp), intent(out) :: sg(3,8)
      sg(:,1) = [-ONE,-ONE,-ONE]
      sg(:,2) = [ ONE,-ONE,-ONE]
      sg(:,3) = [ ONE, ONE,-ONE]
      sg(:,4) = [-ONE, ONE,-ONE]
      sg(:,5) = [-ONE,-ONE, ONE]
      sg(:,6) = [ ONE,-ONE, ONE]
      sg(:,7) = [ ONE, ONE, ONE]
      sg(:,8) = [-ONE, ONE, ONE]
   end subroutine hex_signs

end module mod_mesh
