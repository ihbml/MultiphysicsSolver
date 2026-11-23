// ============================================================================
// mo/revolve.hpp — Griewank & Walther optimal binomial checkpointing (Revolve)
//
//   * binomial checkpoint layout: for s checkpoints on an interval of length
//     m the segment lengths follow  C(s,0):C(s,1):...:C(s,s), so the j-th
//     checkpoint sits at  round( m * (sum_{q<j} C(s,q)) / 2^s ).
//   * a fully instrumented recompute engine (start-stop-restart sweep) with
//     S memory slots and dynamic checkpoint overwriting during recomputation,
//     counting every forward step actually executed.
//   * an EXHAUSTIVE dynamic program (state = interval + stored-checkpoint
//     bitmask) that computes the provably minimal number of recomputations
//     for small m; the engine's schedule is verified against it.
// ============================================================================
#pragma once
#include "core.hpp"
#include <functional>
#include <unordered_map>
#include <climits>

namespace mo {

struct RevolvePlan {
  int m = 0;
  int S = 0;      // total memory slots (X_0 included)
  int s = 0;      // checkpoints stored initially
  std::vector<int> cps;  // ascending checkpoint positions in [1, m-1]
};

static long long binom_small(int n, int k) {
  if (k < 0 || k > n) return 0;
  k = std::min(k, n - k);
  long long r = 1;
  for (int i = 0; i < k; ++i) r = r * (n - i) / (i + 1);
  return r;
}

inline RevolvePlan revolve_plan(int m, int S) {
  RevolvePlan p;
  p.m = m;
  p.S = S;
  p.s = std::max(0, std::min(S - 1, m - 1));
  long long total = 1LL << p.s;   // 2^s (s <= 62 safe here; we use s <= 40)
  long long acc = 0;
  for (int j = 1; j <= p.s; ++j) {
    acc += binom_small(p.s, j - 1);
    int pos = (int)std::llround((long double)m * (long double)acc / (long double)total);
    pos = std::max(1, std::min(m - 1, pos));
    if (p.cps.empty() || pos > p.cps.back()) p.cps.push_back(pos);
  }
  // dedupe
  std::vector<int> u;
  for (int v : p.cps) if (u.empty() || v > u.back()) u.push_back(v);
  p.cps = u;
  p.s = (int)p.cps.size();
  return p;
}

struct RevolveStats {
  long long fwd_initial = 0;
  long long fwd_recompute = 0;
  long long checkpoints = 0;
  long long total() const { return fwd_initial + fwd_recompute; }
};

// Generic revolve engine.
//   F(X, k)      : forward step X_{k+1}
//   adjoint_step : y_k = y_{k+1} + J_k^T y_{k+1},  J_k evaluated at X_k
struct Revolver {
  int S = 4;
  RVec X0;
  std::function<RVec(const RVec&, int)> F;
  std::function<void(const RVec&, const RVec&, RVec&)> JT;  // (X_k, y, y_out)

  RevolveStats stats;

  void run(int m, const std::vector<int>& cps, const RVec& y_final, RVec& y0) {
    int n = (int)X0.size();
    // memory: slot 0 = X_0, slots 1..S-1 free
    std::vector<RVec> store(S, RVec(n));
    std::vector<int> stored_pos(S, -1);
    stored_pos[0] = 0;
    store[0] = X0;
    stats = RevolveStats{};
    auto find_slot = [&](int pos) -> int {
      for (int s = 0; s < S; ++s) if (stored_pos[s] == pos) return s;
      return -1;
    };
    auto save = [&](int pos, const RVec& X) {
      int slot = find_slot(pos);
      if (slot >= 0) { store[slot] = X; return; }
      for (int s = 1; s < S; ++s)
        if (stored_pos[s] < 0 || stored_pos[s] > pos) {  // overwrite stale/newer
          stored_pos[s] = pos; store[s] = X; ++stats.checkpoints; return;
        }
    };
    auto release = [&](int pos) {
      int slot = find_slot(pos);
      if (slot > 0) stored_pos[slot] = -1;
    };
    // ---- initial forward ----
    RVec X = X0;
    for (int k = 0; k < m; ++k) {
      X = F(X, k);
      ++stats.fwd_initial;
      if (std::find(cps.begin(), cps.end(), k + 1) != cps.end()) save(k + 1, X);
    }
    // ---- adjoint sweep with recomputation ----
    RVec y = y_final, yk(n);
    for (int k = m - 1; k >= 0; --k) {
      RVec Xk;
      int slot = find_slot(k);
      if (slot >= 0) {
        Xk = store[slot];
        release(k);
      } else {
        // nearest stored checkpoint p < k
        int p = -1;
        for (int s = 0; s < S; ++s)
          if (stored_pos[s] >= 0 && stored_pos[s] < k && stored_pos[s] > p) p = stored_pos[s];
        // recompute path p -> k
        RVec Xr = store[find_slot(p)];
        // dynamic checkpointing: balanced intermediate positions if slots free
        long long free = 0;
        for (int s = 1; s < S; ++s) if (stored_pos[s] < 0 || stored_pos[s] > k) ++free;
        int span = k - p;
        int r = 0;
        while (r < free && (1 << (r + 1)) <= span) ++r;
        std::vector<int> save_at;
        for (int q = 1; q <= r; ++q)
          save_at.push_back(p + (int)std::llround((long double)span * q / (1 << r)));
        for (int t = p; t < k; ++t) {
          Xr = F(Xr, t);
          ++stats.fwd_recompute;
          if (std::find(save_at.begin(), save_at.end(), t + 1) != save_at.end())
            save(t + 1, Xr);
        }
        Xk = Xr;
      }
      JT(Xk, y, yk);
      y = yk;
    }
    y0 = y;
  }
};

// Exhaustive DP: provably minimal recompute count for interval [0, m)
// with S slots (X_0 free). State: (a, b, mask): positions a..b-1, mask bit i
// set <=> X_{a+i} is stored.
struct RevolveDP {
  int m, S;
  std::unordered_map<unsigned long long, double> memo;
  RevolveDP(int m_, int S_) : m(m_), S(S_) {}

  static unsigned long long key(int a, int b, int mask) {
    return (static_cast<unsigned long long>(a) << 40) | (static_cast<unsigned long long>(b) << 28) |
           static_cast<unsigned long long>(mask);
  }
  // cost of storing: popcount of mask over positions > 0
  double cost(int a, int mask) const {
    int c = 0;
    for (int i = 0; i < m; ++i)
      if ((mask & (1 << i)) && (a + i) > 0) ++c;
    return c;
  }

  double solve(int a, int b, int mask) {
    if (a == b) return 0.0;
    auto it = memo.find(key(a, b, mask));
    if (it != memo.end()) return it->second;
    int idx = b - 1 - a;                 // bit of X_{b-1}
    double best = 1e300;
    bool have = (a + idx == 0) || (mask & (1 << idx));
    if (have) {
      int m2 = mask & ~(1 << idx);
      best = std::min(best, solve(a, b - 1, m2));
    }
    int free = S - 1 - (int)cost(a, mask);
    for (int p = a; p < b - 1; ++p) {
      bool ph = (p == 0) || (mask & (1 << (p - a)));
      if (!ph) continue;
      // keep lower bits, choose Q subset of (p, b-1)
      int lowmask = mask & ((1 << idx) - 1);
      int range0 = p - a + 1;
      int len = b - 1 - (p + 1);
      double c = (double)(b - 1 - p);
      for (int Q = 0; Q < (1 << len); ++Q) {
        int pc = 0;
        for (int i = 0; i < len; ++i) pc += (Q >> i) & 1;
        if (pc > free) continue;
        int full = 0;
        for (int i = 0; i < len; ++i)
          if (Q & (1 << i)) full |= (1 << (range0 + i));
        int m2 = lowmask | full;
        best = std::min(best, c + solve(a, b - 1, m2));
      }
    }
    memo[key(a, b, mask)] = best;
    return best;
  }

  // optimum over all initial-forward checkpoint choices (free to save anywhere)
  double optimum() {
    double best = 1e300;
    int full = (1 << m) - 1;
    for (int mask = 0; mask <= full; ++mask) {
      if (cost(0, mask) > S - 1) continue;
      best = std::min(best, solve(0, m, mask));
    }
    return best;
  }
};

}  // namespace mo
