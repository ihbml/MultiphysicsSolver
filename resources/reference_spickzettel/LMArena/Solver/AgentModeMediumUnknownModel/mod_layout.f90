!================================================================================
! mod_layout -- unknown numbering, element->matrix slot maps, sparsity, colours.
!
! The three things that dominate assembly cost in a naive FEM solver are
!   (i)   "where does this entry go in the CSR?"   (binary search per entry)
!   (ii)  "who owns this face?"                     (hash lookups)
!   (iii) data races when the assembly is threaded.
!
! This module removes all three at INGEST time, once, so that every subsequent
! Newton assembly is a pure, allocation-free, race-free sweep:
!
!   * slot_map(ia,ib,e) -- for element e, the index into bval(:,:,  ) of the
!     block coupling local node ia to local node ib.  Assembly becomes
!        bval(:,:,slot_map(ia,ib,e)) += Ke_ab
!     with no searching, no branches, no integer division.
!
!   * cell_color(c) -- a proper colouring of the cell adjacency graph (two
!     cells adjacent iff they share a node).  Since cells of one colour share
!     no node, they share no matrix entry and no residual entry, so threads
!     can sweep each colour independently with ZERO atomics, ZERO locks and
!     bit-identical results to the serial run.  Residual assembly is scatter-
!     free (each colour writes disjoint a_loc ranges).
!
!   * face_owner_perm / face_neigh_perm -- for each face, the local node slot
!     in the owner / neighbour cell holding each of the face's nodes.  Face
!     terms that couple node q on the owner to node q on the neighbour then hit
!     EXACTLY opposite matrix entries, giving bit-level discrete conservation.
!================================================================================
module mod_layout
   use mod_kinds
   use mod_mesh
   use mod_linalg, only: NDOF
   use mod_util, only: engine_abort
   implicit none
   private

   public :: layout_t, layout_build, layout_destroy, matslot

   type :: layout_t
      integer :: ndof = 0                  !< total scalar unknowns = NDOF*nnodes
      integer :: nnz  = 0                  !< stored block entries
      integer :: ncolors = 0
      integer(i4), allocatable :: slot_map(:,:,:)      !< (npe,npe,ncells)
      integer(i4), allocatable :: a_loc(:,:)           !< (npe,ncells) scalar dof offset (0-based)
      integer(i4), allocatable :: face_owner_perm(:,:) !< (4,nfaces) local slot in owner
      integer(i4), allocatable :: face_neigh_perm(:,:) !< (4,nfaces) local slot in neighbour (0=bd)
      integer(i4), allocatable :: node_ptr(:)          !< (nnodes+1) block CSR row pointers
      integer(i4), allocatable :: node_col(:)          !< (nnz) block column indices, sorted
      integer(i4), allocatable :: cell_color(:)        !< (ncells)
   end type layout_t


contains

   !> Slot of block (ia,ib) of cell e inside the global bval array.
   function matslot(L, e, ia, ib) result(s)
      type(layout_t), intent(in) :: L
      integer, intent(in) :: e, ia, ib
      integer(i4) :: s
      s = L%slot_map(ia, ib, e)
   end function matslot

   !===========================================================================
   subroutine layout_build(L, m)
      type(layout_t), intent(inout) :: L
      type(mesh_t), intent(in) :: m

      integer :: npe, nnodes, ncells, e, i, j, n, k, p, q, a, b
      integer, allocatable :: cnt(:), pos(:)
      integer(i4), allocatable :: wide(:)      ! unsorted adjacency with duplicates
      integer :: nnz, w
      integer :: fid, c, nb
      integer, allocatable :: ftmp(:)

      call layout_destroy(L)
      npe = m%nodes_per_elem
      nnodes = m%nnodes
      ncells = m%ncells
      L%ndof = NDOF*nnodes

      !------------------------------------------------------------------------
      ! residual scatter offsets (0-based so they can be used directly in
      ! array sections: x(a_loc+1 : a_loc+NDOF))
      !------------------------------------------------------------------------
      allocate(L%a_loc(npe, ncells), source=0_i4)
      do e = 1, ncells
         do i = 1, npe
            L%a_loc(i,e) = int(NDOF*(m%elem_nodes(i,e)-1), i4)
         end do
      end do

      !------------------------------------------------------------------------
      ! nodal adjacency (clique on the nodes of every cell), counting sort
      !------------------------------------------------------------------------
      allocate(cnt(nnodes), source=0)
      allocate(pos(nnodes), source=0)
      do e = 1, ncells
         do i = 1, npe
            cnt(m%elem_nodes(i,e)) = cnt(m%elem_nodes(i,e)) + npe
         end do
      end do
      allocate(L%node_ptr(nnodes+1))
      L%node_ptr(1) = 1
      do n = 1, nnodes
         L%node_ptr(n+1) = L%node_ptr(n) + cnt(n)
      end do
      allocate(wide(L%node_ptr(nnodes+1)-1))
      do e = 1, ncells
         do i = 1, npe
            n = m%elem_nodes(i,e)
            do j = 1, npe
               k = L%node_ptr(n) + pos(n)
               wide(k) = int(m%elem_nodes(j,e), i4)
               pos(n) = pos(n) + 1
            end do
         end do
      end do

      ! sort + unique each row -> final block CSR pattern (rows stay sorted,
      ! which is what find_block/binary search in mod_linalg expects)
      nnz = 0
      do n = 1, nnodes
         p = L%node_ptr(n); q = L%node_ptr(n+1)-1
         call sort_i4(wide(p:q))
         ! Fortran does NOT short-circuit .or., so the original
         !   if (k == p .or. wide(k) /= wide(k-1))
         ! read wide(p-1) -- an out-of-bounds access on row 1 (and on every row
         ! whose p == 1, i.e. the first row).  It "worked" at -O3 only because
         ! the value was discarded; -fcheck=all traps it.  Peel the first entry.
         wide(p) = wide(p)          ! first entry is always kept
         w = p + 1
         do k = p+1, q
            if (wide(k) /= wide(k-1)) then
               wide(w) = wide(k); w = w + 1
            end if
         end do
         pos(n) = w - p          ! unique length of row n
         nnz = nnz + (w - p)
      end do
      ! NOTE: the in-place compaction above left each row at its OLD offset, so
      ! the rows are not contiguous yet -- they must be gathered into the final
      ! array.  (Assuming they were already packed silently corrupts the
      ! pattern, which then shows up as "missing element coupling".)
      deallocate(L%node_ptr)
      allocate(L%node_ptr(nnodes+1))
      allocate(L%node_col(nnz))
      L%node_ptr(1) = 1
      do n = 1, nnodes
         L%node_ptr(n+1) = L%node_ptr(n) + pos(n)
      end do
      block
         integer :: wstart
         wstart = 1
         do n = 1, nnodes
            do k = 1, pos(n)
               L%node_col(L%node_ptr(n)+k-1) = wide(wstart+k-1)
            end do
            wstart = wstart + cnt(n)
         end do
      end block
      L%nnz = nnz
      deallocate(wide, cnt, pos)

      !------------------------------------------------------------------------
      ! element -> block slot map (binary search once per (cell,node,node))
      !------------------------------------------------------------------------
      allocate(L%slot_map(npe, npe, ncells), source=0_i4)
      do e = 1, ncells
         do i = 1, npe
            a = m%elem_nodes(i,e)
            do j = 1, npe
               b = m%elem_nodes(j,e)
               L%slot_map(i,j,e) = int(binsearch(L, a, b), i4)
               if (L%slot_map(i,j,e) == 0) call engine_abort( &
                  'sparsity pattern is missing an element coupling','layout_build')
            end do
         end do
      end do

      !------------------------------------------------------------------------
      ! face node permutations (exact scalar pairing across a face)
      !------------------------------------------------------------------------
      allocate(L%face_owner_perm(4, m%nfaces), source=0_i4)
      allocate(L%face_neigh_perm(4, m%nfaces), source=0_i4)
      do fid = 1, m%nfaces
         c  = m%face_owner(fid)
         nb = m%face_neigh(fid)
         do q = 1, 4
            n = m%face_nodes(q,fid)
            do i = 1, npe
               if (m%elem_nodes(i,c) == n) L%face_owner_perm(q,fid) = int(i, i4)
               if (nb > 0) then
                  if (m%elem_nodes(i,nb) == n) L%face_neigh_perm(q,fid) = int(i, i4)
               end if
            end do
            if (L%face_owner_perm(q,fid) == 0) call engine_abort( &
               'face node not found in owner cell','layout_build')
            if (nb > 0) then
               if (L%face_neigh_perm(q,fid) == 0) call engine_abort( &
                  'face node not found in neighbour cell','layout_build')
            end if
         end do
      end do

      !------------------------------------------------------------------------
      ! greedy graph colouring of the cell adjacency graph
      !------------------------------------------------------------------------
      allocate(L%cell_color(ncells), source=0_i4)
      allocate(ftmp(ncells+1), source=0)          ! colour-taken stamp array
      L%ncolors = 0
      do c = 1, ncells
         ! mark colours of already coloured neighbours
         do q = m%face_ptr(c), m%face_ptr(c+1)-1
            fid = m%elem_faces(q)
            nb = m%face_owner(fid); if (nb == c) nb = m%face_neigh(fid)
            if (nb > 0) then
               if (L%cell_color(nb) > 0) ftmp(L%cell_color(nb)) = c
            end if
         end do
         k = 1
         do while (ftmp(k) == c)
            k = k + 1
         end do
         L%cell_color(c) = int(k, i4)
         L%ncolors = max(L%ncolors, k)
      end do
      deallocate(ftmp)

      write(*,'(a,i10,a,i12,a,i8)') '  [layout] nodes=', nnodes, &
         '  block-nnz=', nnz, '  colours=', L%ncolors
   end subroutine layout_build

   !> Index into bcol of entry (a,b).  Returns 0 if not present.
   integer function binsearch(L, a, b) result(p)
      type(layout_t), intent(in) :: L
      integer, intent(in) :: a, b
      integer :: lo, hi, mid
      lo = L%node_ptr(a); hi = L%node_ptr(a+1)-1
      p = 0
      do while (lo <= hi)
         mid = (lo+hi)/2
         if (L%node_col(mid) < b) then
            lo = mid + 1
         else if (L%node_col(mid) > b) then
            hi = mid - 1
         else
            p = mid; return
         end if
      end do
   end function binsearch

   subroutine layout_destroy(L)
      type(layout_t), intent(inout) :: L
      if (allocated(L%slot_map))        deallocate(L%slot_map)
      if (allocated(L%a_loc))           deallocate(L%a_loc)
      if (allocated(L%face_owner_perm)) deallocate(L%face_owner_perm)
      if (allocated(L%face_neigh_perm)) deallocate(L%face_neigh_perm)
      if (allocated(L%node_ptr))        deallocate(L%node_ptr)
      if (allocated(L%node_col))        deallocate(L%node_col)
      if (allocated(L%cell_color))      deallocate(L%cell_color)
      L%nnz = 0; L%ndof = 0; L%ncolors = 0
   end subroutine layout_destroy

   !> Insertion sort on a short integer slice (rows are ~27 long).
   subroutine sort_i4(a)
      integer(i4), intent(inout) :: a(:)
      integer :: i, j, n
      integer(i4) :: t
      n = size(a)
      do i = 2, n
         t = a(i); j = i - 1
         do while (j >= 1)
            if (a(j) <= t) exit
            a(j+1) = a(j); j = j - 1
         end do
         a(j+1) = t
      end do
   end subroutine sort_i4

end module mod_layout
