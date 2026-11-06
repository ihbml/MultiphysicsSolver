// DiFVM Mesher — cell renumbering for cache/partition locality.
//
// The solver walks cells along a space-filling curve so that Dynamic
// Redistributing (spec §6.4) can shift partition boundaries by pointer only.
#pragma once

#include <string>

#include "difvm/graph_mesh.hpp"

namespace difvm {

enum class ReorderMethod { kNone = 0, kRcm, kMorton, kHilbert };

ReorderMethod parse_reorder(const std::string& s);
const char* to_string(ReorderMethod m);

struct ReorderReport {
  ReorderMethod method = ReorderMethod::kNone;
  double bandwidth_before = 0.0;   // max |owner - neighbour| over internal faces
  double bandwidth_after = 0.0;
  double mean_span_before = 0.0;   // mean |owner - neighbour|
  double mean_span_after = 0.0;
  double time_ms = 0.0;
  std::string to_string() const;
};

// Renumbers cells in place (all cell-indexed arrays, owner/neighbour, face
// order and adjacency are kept consistent) and reports locality metrics.
ReorderReport reorder_mesh(GraphMesh& gm, ReorderMethod method);

}  // namespace difvm
