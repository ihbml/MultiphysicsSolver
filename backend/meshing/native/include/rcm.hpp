#pragma once

#include <vector>
#include <cstdint>
#include "difvm_mesh.hpp"

namespace cfd {

class RCMReordering {
public:
    static std::vector<int64_t> computePermutation(const NativeMeshData& mesh);
    
    static void applyReordering(NativeMeshData& mesh, const std::vector<int64_t>& perm);
    
    static int64_t computeBandwidth(const NativeMeshData& mesh);
};

} // namespace cfd
