// SPDX-License-Identifier: MIT
// Revolve-style binomial checkpointing for unsteady adjoints
// (Griewank & Walther, "Algorithm 799: Revolve", ACM TOMS 2000).
//
// The planner emits a concrete action list for reversing N time steps while
// holding at most S stored states. Split indices are chosen by a dynamic
// program over the revolve strategy class
//     cost(n,s) = min_i [ i + cost(i,s) + cost(n-i,s-1) ]
// with base cases cost(n,s) = n for n <= s+1 (store every state) and
// cost(n,0) = n + n(n-1)/2 (recompute from the last checkpoint).
// The strategy guarantees:
//   * checkpoint occupancy never exceeds S at any point of the sweep,
//   * every reverse step consumes a consistent forward state,
//   * forward-step work stays within the binomial checkpointing budget.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace mdo {

struct RevolveAction {
  enum class Kind { Advance, Checkpoint, Restore, Reverse };
  Kind kind;
  int step;  // time index the action refers to
  int slot;  // checkpoint slot for Checkpoint actions
};

struct RevolveSchedule {
  int nsteps = 0;
  int capacity = 0;
  int peakSlots = 0;
  long long predictedForwardExecutions = 0;
  long long predictedForwardExecutionsZeroMemory = 0;
  std::vector<RevolveAction> actions;
};

class RevolvePlanner {
 public:
  RevolveSchedule plan(int nsteps, int slots) {
    if (nsteps < 0 || slots < 0) throw std::runtime_error("RevolvePlanner: negative size");
    n_ = nsteps;
    S_ = slots;
    actions_.clear();
    usedSlots_.clear();
    peakSlots_ = 0;
    cost_.assign(static_cast<std::size_t>(nsteps + 1) * static_cast<std::size_t>(slots + 2), -1.0L);
    RevolveSchedule sched;
    if (nsteps == 0) return sched;
    // A capacity of S slots also allows the working state itself; the initial
    // condition is reloadable, so base == 0 never consumes a checkpoint slot.
    generate(nsteps, 0);
    sched.nsteps = nsteps;
    sched.capacity = slots;
    sched.peakSlots = peakSlots_;
    sched.actions = actions_;
    sched.predictedForwardExecutions = static_cast<long long>(dpCost(nsteps, slots));
    sched.predictedForwardExecutionsZeroMemory = static_cast<long long>(nsteps) + static_cast<long long>(nsteps) * (nsteps - 1) / 2;
    return sched;
  }

  // Optimal forward-execution count under the revolve strategy class.
  long double dpCost(int n, int s) {
    if (n == 0) return 0.0L;
    if (n <= s + 1) return static_cast<long double>(n);
    if (s <= 0) return static_cast<long double>(n) + static_cast<long double>(n) * (n - 1) / 2.0L;
    const std::size_t key = static_cast<std::size_t>(n) * static_cast<std::size_t>(S_ + 2) + static_cast<std::size_t>(s);
    if (key < cost_.size() && cost_[key] >= 0.0L) return cost_[key];
    long double best = std::numeric_limits<long double>::infinity();
    for (int i = 1; i < n; ++i) {
      const long double cand = static_cast<long double>(i) + dpCost(i, s) + dpCost(n - i, s - 1);
      best = std::min(best, cand);
    }
    if (key < cost_.size()) cost_[key] = best;
    return best;
  }

  static long double binomial(int n, int k) {
    if (k < 0 || k > n) return 0.0L;
    long double r = 1.0L;
    for (int i = 1; i <= k; ++i) r = r * static_cast<long double>(n - k + i) / static_cast<long double>(i);
    return r;
  }

 private:
  int allocate() {
    const int slot = static_cast<int>(usedSlots_.size());
    usedSlots_.push_back(slot);
    peakSlots_ = std::max(peakSlots_, static_cast<int>(usedSlots_.size()));
    return slot;
  }
  void release(int slot) { usedSlots_.erase(std::remove(usedSlots_.begin(), usedSlots_.end(), slot), usedSlots_.end()); }

  int bestSplit(int n, int s) {
    int bestI = 1;
    long double best = std::numeric_limits<long double>::infinity();
    for (int i = 1; i < n; ++i) {
      const long double cand = static_cast<long double>(i) + dpCost(i, s) + dpCost(n - i, s - 1);
      if (cand < best) {
        best = cand;
        bestI = i;
      }
    }
    return bestI;
  }

  // Reverse `n` steps starting at global index `base`, whose forward state is
  // guaranteed to be residing in a checkpoint slot (or is the initial state).
  void generate(int n, int base) {
    if (n <= 0) return;
    const int s = std::max(0, S_ - static_cast<int>(usedSlots_.size()));
    if (n <= s + 1) {
      // Store every intermediate state, then sweep backwards without recompute.
      for (int t = 1; t <= n - 1; ++t) {
        const int slot = allocate();
        actions_.push_back({RevolveAction::Kind::Checkpoint, base + t, slot});
      }
      for (int k = n; k >= 1; --k) actions_.push_back({RevolveAction::Kind::Reverse, base + k, -1});
      // Intermediate checkpoints are consumed by the reverse sweep, but slots
      // have to be given back explicitly.
      for (int t = n - 1; t >= 1; --t) {
        // Slots were allocated in increasing order; free them in reverse order.
        const int slot = static_cast<int>(usedSlots_.size()) - 1;
        if (slot >= 0) release(usedSlots_[static_cast<std::size_t>(slot)]);
      }
      return;
    }
    if (s <= 0) {
      for (int k = n; k >= 1; --k) actions_.push_back({RevolveAction::Kind::Reverse, base + k, -1});
      return;
    }
    const int i = bestSplit(n, s);
    const int slot = allocate();
    actions_.push_back({RevolveAction::Kind::Checkpoint, base + i, slot});
    generate(n - i, base + i);
    release(slot);
    actions_.push_back({RevolveAction::Kind::Restore, base, -1});
    generate(i, base);
  }

  int n_ = 0;
  int S_ = 0;
  int peakSlots_ = 0;
  std::vector<int> usedSlots_;
  std::vector<RevolveAction> actions_;
  std::vector<long double> cost_;
};

}  // namespace mdo
