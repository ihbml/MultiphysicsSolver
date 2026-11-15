// ORCHESTRATION PSEUDOCODE, not a standalone MPI/CUDA program.
// halo_phi / halo_grad represent application-specific communication.
// Assumptions: CUDA-aware MPI, device packing buffers, stable topology.

zero_face_corrections(compute_stream);
cudaEventRecord(corrections_zeroed, compute_stream);
cudaStreamWaitEvent(halo_stream, corrections_zeroed);
pack_phi_on_gpu(compute_stream);
cudaEventRecord(phi_packed, compute_stream);
cudaEventSynchronize(phi_packed); // portable host-side handoff to MPI
halo_phi.start();                // Irecv before Isend

base_coefficients_all_inner(compute_stream);
gradients_with_local_stencils(gradient_stream);
cudaEventRecord(inner_grad_done, gradient_stream);
cudaStreamWaitEvent(compute_stream, inner_grad_done);
corrections_with_local_gradient_stencils(compute_stream);
halo_phi.wait();                 // MPI must make actual progress
unpack_phi_and_update_material_fields(halo_stream);
gradients_near_rank_boundary(halo_stream);

// A phi halo alone does NOT provide the neighbour gradients.
cudaStreamWaitEvent(halo_stream, inner_grad_done);
pack_boundary_gradients(halo_stream);
cudaEventRecord(grad_packed, halo_stream);
cudaEventSynchronize(grad_packed);
halo_grad.start();

// Independent corrections can still run while the halo progresses.
halo_grad.wait();
unpack_gradients(halo_stream);
base_coefficients_all_halo(halo_stream);
corrections_dependent_faces(halo_stream);

cudaEventRecord(inner_done, compute_stream);
cudaEventRecord(halo_done, halo_stream);
cudaStreamWaitEvent(solver_stream, inner_done);
cudaStreamWaitEvent(solver_stream, halo_done);
gather_owned_rows(solver_stream);
// Add physical boundary conditions, time terms and sources before solve.