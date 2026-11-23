// SPDX-License-Identifier: MIT
// Two-stage hybrid shape optimisation:
//   Stage 1 (global): Multi-Island GA (ring migration, SBX, polynomial mutation,
//                     NSGA-II fast non-dominated sort + crowding distance)
//                     and/or Particle Swarm Optimisation.
//   Stage 2 (local) : Nelder-Mead downhill simplex warm-started from the global
//                     basin, followed by L-BFGS with the analytic shape gradient.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <limits>
#include <numeric>
#include <random>
#include <string>
#include <vector>

#include "mdo/linalg.hpp"
#include "mdo/thermofluid.hpp"

namespace mdo {

struct Bounds {
  Vec lo, hi;
  [[nodiscard]] int dim() const { return static_cast<int>(lo.size()); }
  [[nodiscard]] Vec clamp(const Vec& x) const {
    Vec y(x.size());
    for (std::size_t i = 0; i < x.size(); ++i) y[i] = std::min(hi[i], std::max(lo[i], x[i]));
    return y;
  }
  [[nodiscard]] Vec random(std::mt19937& rng) const {
    Vec x(lo.size());
    std::uniform_real_distribution<double> u(0.0, 1.0);
    for (std::size_t i = 0; i < lo.size(); ++i) x[i] = lo[i] + u(rng) * (hi[i] - lo[i]);
    return x;
  }
};

struct Evaluated {
  Vec x;
  double f = std::numeric_limits<double>::infinity();
  double cv = 0.0;
  std::vector<double> objectives;
  int rank = 0;
  double crowding = 0.0;
};

using FitnessFn = std::function<double(const Vec&)>;
using GradientFn = std::function<Vec(const Vec&)>;

struct GlobalOptions {
  int islands = 4;
  int popPerIsland = 24;
  int generations = 40;
  int migrationInterval = 6;
  double crossoverProb = 0.9;
  double sbxEta = 15.0;
  double mutationProb = 0.12;
  double polyEta = 20.0;
  int elite = 2;
  bool usePso = true;
  int psoSwarm = 40;
  int psoIterations = 60;
  double psoInertia = 0.72;
  double psoCognitive = 1.5;
  double psoSocial = 1.6;
  unsigned seed = 20240517u;
};

struct LocalOptions {
  int nmMaxEval = 500;
  int lbfgsMaxIter = 120;
  int lbfgsMemory = 8;
  double tol = 1e-8;
};

struct HybridResult {
  Vec bestX;
  double bestF = std::numeric_limits<double>::infinity();
  double bestCv = 0.0;
  int globalEvaluations = 0;
  int localEvaluations = 0;
  int islands = 0;
  int generations = 0;
  bool localImprovement = false;
  std::string globalStage = "MIGA";
  std::vector<double> globalHistory;
  std::vector<double> localHistory;
  std::vector<Vec> basinSeeds;
  double nmF = 0.0;
  double lbfgsF = 0.0;
  int nmEvaluations = 0;
  int lbfgsEvaluations = 0;
};

// ------------------------------------------------------------------ NSGA-II
struct Crowding {
  static void assign(std::vector<Evaluated>& pop) {
    if (pop.empty()) return;
    std::vector<int> idx(pop.size());
    std::iota(idx.begin(), idx.end(), 0);
    std::stable_sort(idx.begin(), idx.end(), [&](int a, int b) { return pop[static_cast<std::size_t>(a)].rank < pop[static_cast<std::size_t>(b)].rank; });
    std::size_t start = 0;
    while (start < idx.size()) {
      std::size_t end = start;
      while (end < idx.size() && pop[static_cast<std::size_t>(idx[end])].rank == pop[static_cast<std::size_t>(idx[start])].rank) ++end;
      std::vector<int> front(idx.begin() + static_cast<long>(start), idx.begin() + static_cast<long>(end));
      for (int id : front) pop[static_cast<std::size_t>(id)].crowding = 0.0;
      const std::size_t nobj = pop[static_cast<std::size_t>(front[0])].objectives.size();
      for (std::size_t m = 0; m < nobj; ++m) {
        std::stable_sort(front.begin(), front.end(), [&](int a, int b) {
          return pop[static_cast<std::size_t>(a)].objectives[m] < pop[static_cast<std::size_t>(b)].objectives[m];
        });
        if (front.size() < 3) continue;
        const double fmin = pop[static_cast<std::size_t>(front.front())].objectives[m];
        const double fmax = pop[static_cast<std::size_t>(front.back())].objectives[m];
        const double span = std::max(fmax - fmin, 1e-12);
        pop[static_cast<std::size_t>(front.front())].crowding = std::numeric_limits<double>::infinity();
        pop[static_cast<std::size_t>(front.back())].crowding = std::numeric_limits<double>::infinity();
        for (std::size_t k = 1; k + 1 < front.size(); ++k) {
          const double prev = pop[static_cast<std::size_t>(front[k - 1])].objectives[m];
          const double next = pop[static_cast<std::size_t>(front[k + 1])].objectives[m];
          pop[static_cast<std::size_t>(front[k])].crowding += (next - prev) / span;
        }
      }
      start = end;
    }
  }

  static void nonDominatedSort(std::vector<Evaluated>& pop) {
    const int n = static_cast<int>(pop.size());
    std::vector<std::vector<int>> dominatedBy(static_cast<std::size_t>(n));
    std::vector<int> counts(static_cast<std::size_t>(n), 0);
    std::vector<std::vector<int>> fronts(1);
    auto dominates = [](const Evaluated& a, const Evaluated& b) {
      const std::size_t m = std::min(a.objectives.size(), b.objectives.size());
      bool strictly = false;
      for (std::size_t k = 0; k < m; ++k) {
        if (a.objectives[k] > b.objectives[k]) return false;
        if (a.objectives[k] < b.objectives[k]) strictly = true;
      }
      return strictly;
    };
    for (int i = 0; i < n; ++i) {
      for (int j = 0; j < n; ++j) {
        if (i == j) continue;
        if (dominates(pop[static_cast<std::size_t>(i)], pop[static_cast<std::size_t>(j)])) {
          dominatedBy[static_cast<std::size_t>(i)].push_back(j);
        } else if (dominates(pop[static_cast<std::size_t>(j)], pop[static_cast<std::size_t>(i)])) {
          counts[static_cast<std::size_t>(i)] += 1;
        }
      }
      if (counts[static_cast<std::size_t>(i)] == 0) {
        pop[static_cast<std::size_t>(i)].rank = 0;
        fronts[0].push_back(i);
      }
    }
    int f = 0;
    while (!fronts[static_cast<std::size_t>(f)].empty()) {
      std::vector<int> next;
      for (int i : fronts[static_cast<std::size_t>(f)]) {
        for (int j : dominatedBy[static_cast<std::size_t>(i)]) {
          counts[static_cast<std::size_t>(j)] -= 1;
          if (counts[static_cast<std::size_t>(j)] == 0) {
            pop[static_cast<std::size_t>(j)].rank = f + 1;
            next.push_back(j);
          }
        }
      }
      fronts.push_back(next);
      ++f;
    }
    assign(pop);
  }
};

class HybridOptimizer {
 public:
  HybridOptimizer(int dim, Bounds bounds, FitnessFn fitness, GradientFn gradient = nullptr)
      : dim_(dim), bounds_(std::move(bounds)), fitness_(std::move(fitness)), gradient_(std::move(gradient)) {
    rng_.seed(20240517u);
  }

  void seed(unsigned s) { rng_.seed(s); }

  HybridResult run(const GlobalOptions& gopt = {}, const LocalOptions& lopt = {}) {
    HybridResult res = runGlobalMiga(gopt);
    if (gopt.usePso) {
      HybridResult pso = runGlobalPso(gopt);
      res.globalEvaluations += pso.globalEvaluations;
      res.basinSeeds.insert(res.basinSeeds.end(), pso.basinSeeds.begin(), pso.basinSeeds.end());
      if (pso.bestF < res.bestF) {
        res.bestX = pso.bestX;
        res.bestF = pso.bestF;
        res.globalStage = "PSO";
      } else {
        res.globalStage = "MIGA+PSO";
      }
      for (double v : pso.globalHistory) res.globalHistory.push_back(v);
    }
    res.basinSeeds.push_back(res.bestX);
    // -------- Stage 2a: Nelder-Mead downhill simplex warm start
    Vec x = res.bestX;
    const double fStart = res.bestF;
    res.nmF = nelderMead(x, lopt, res.nmEvaluations);
    Vec xLbfgs = x;
    // -------- Stage 2b: L-BFGS polish with the analytic gradient
    res.lbfgsF = lbfgs(xLbfgs, lopt, res.lbfgsEvaluations);
    res.localEvaluations = res.nmEvaluations + res.lbfgsEvaluations;
    res.bestX = xLbfgs;
    res.bestF = res.lbfgsF;
    res.localImprovement = res.bestF < fStart - 1e-10;
    res.localHistory = localHistory_;
    res.islands = gopt.islands;
    res.generations = gopt.generations;
    return res;
  }

  // ---------------------------------------------------------------- MIGA
  HybridResult runGlobalMiga(const GlobalOptions& gopt) {
    HybridResult res;
    res.globalStage = "MIGA";
    const int perIsland = gopt.popPerIsland;
    std::vector<std::vector<Evaluated>> islands(static_cast<std::size_t>(gopt.islands));
    for (int isl = 0; isl < gopt.islands; ++isl) {
      islands[static_cast<std::size_t>(isl)].resize(static_cast<std::size_t>(perIsland));
      for (int i = 0; i < perIsland; ++i) {
        Vec x = bounds_.random(rng_);
        // Island-specific seeding spreads the initial basin coverage.
        if (i == 0 && isl == 0) x = bounds_.clamp(Vec(static_cast<std::size_t>(dim_), 0.5));
        islands[static_cast<std::size_t>(isl)][static_cast<std::size_t>(i)] = evaluate(x);
      }
    }
    std::vector<double> bestPerGen;
    for (int gen = 0; gen < gopt.generations; ++gen) {
      for (int isl = 0; isl < gopt.islands; ++isl) {
        auto& pop = islands[static_cast<std::size_t>(isl)];
        Crowding::nonDominatedSort(pop);
        std::stable_sort(pop.begin(), pop.end(), [](const Evaluated& a, const Evaluated& b) {
          if (a.rank != b.rank) return a.rank < b.rank;
          return a.crowding > b.crowding;
        });
        std::vector<Evaluated> offspring;
        offspring.reserve(static_cast<std::size_t>(perIsland));
        for (int k = 0; k < gopt.elite && k < perIsland; ++k) offspring.push_back(pop[static_cast<std::size_t>(k)]);
        while (static_cast<int>(offspring.size()) < perIsland) {
          const Evaluated& p1 = tournament(pop);
          const Evaluated& p2 = tournament(pop);
          auto kids = sbx(p1.x, p2.x, gopt);
          for (auto& kid : kids) {
            if (static_cast<int>(offspring.size()) >= perIsland) break;
            offspring.push_back(evaluate(mutate(kid, gopt)));
          }
        }
        pop = offspring;
      }
      // Ring migration topology: island i donates its elite to island i+1.
      if (gopt.islands > 1 && (gen + 1) % gopt.migrationInterval == 0) {
        std::vector<Vec> migrants(static_cast<std::size_t>(gopt.islands));
        for (int isl = 0; isl < gopt.islands; ++isl) {
          const auto& pop = islands[static_cast<std::size_t>(isl)];
          const auto it = std::min_element(pop.begin(), pop.end(), [](const Evaluated& a, const Evaluated& b) {
            if (a.cv > 0.0 || b.cv > 0.0) return a.cv < b.cv;
            return a.f < b.f;
          });
          migrants[static_cast<std::size_t>(isl)] = it->x;
        }
        for (int isl = 0; isl < gopt.islands; ++isl) {
          const int src = (isl + gopt.islands - 1) % gopt.islands;
          auto& pop = islands[static_cast<std::size_t>(isl)];
          const auto worst = std::max_element(pop.begin(), pop.end(), [](const Evaluated& a, const Evaluated& b) { return a.f < b.f; });
          if (worst != pop.end()) *worst = evaluate(migrants[static_cast<std::size_t>(src)]);
        }
      }
      double best = std::numeric_limits<double>::infinity();
      for (const auto& island : islands)
        for (const auto& ind : island) best = std::min(best, ind.f + 1e3 * std::max(0.0, ind.cv));
      bestPerGen.push_back(best);
      res.globalEvaluations += gopt.islands * perIsland;
    }
    // best feasible across islands
    res.bestF = std::numeric_limits<double>::infinity();
    for (const auto& island : islands) {
      for (const auto& ind : island) {
        const double merit = ind.f + 1e3 * std::max(0.0, ind.cv);
        if (merit < res.bestF) {
          res.bestF = merit;
          res.bestX = ind.x;
          res.bestCv = ind.cv;
        }
      }
    }
    res.globalHistory = bestPerGen;
    for (const auto& island : islands) {
      const auto it = std::min_element(island.begin(), island.end(), [](const Evaluated& a, const Evaluated& b) {
        return a.f + 1e3 * std::max(0.0, a.cv) < b.f + 1e3 * std::max(0.0, b.cv);
      });
      if (it != island.end()) res.basinSeeds.push_back(it->x);
    }
    res.islands = gopt.islands;
    res.generations = gopt.generations;
    return res;
  }

  // ---------------------------------------------------------------- PSO
  HybridResult runGlobalPso(const GlobalOptions& gopt) {
    HybridResult res;
    res.globalStage = "PSO";
    const int n = gopt.psoSwarm;
    std::vector<Vec> pos(static_cast<std::size_t>(n)), vel(static_cast<std::size_t>(n)), best(static_cast<std::size_t>(n));
    std::vector<double> bestF(static_cast<std::size_t>(n), std::numeric_limits<double>::infinity());
    Vec gbest = bounds_.clamp(Vec(static_cast<std::size_t>(dim_), 0.5));
    double gbestF = std::numeric_limits<double>::infinity();
    std::uniform_real_distribution<double> u(0.0, 1.0);
    for (int i = 0; i < n; ++i) {
      pos[static_cast<std::size_t>(i)] = bounds_.random(rng_);
      Vec v(static_cast<std::size_t>(dim_));
      for (int d = 0; d < dim_; ++d)
        v[static_cast<std::size_t>(d)] = (u(rng_) - 0.5) * 0.2 * (bounds_.hi[static_cast<std::size_t>(d)] - bounds_.lo[static_cast<std::size_t>(d)]);
      vel[static_cast<std::size_t>(i)] = v;
      best[static_cast<std::size_t>(i)] = pos[static_cast<std::size_t>(i)];
      bestF[static_cast<std::size_t>(i)] = evaluate(pos[static_cast<std::size_t>(i)]).f;
      if (bestF[static_cast<std::size_t>(i)] < gbestF) {
        gbestF = bestF[static_cast<std::size_t>(i)];
        gbest = best[static_cast<std::size_t>(i)];
      }
    }
    for (int it = 0; it < gopt.psoIterations; ++it) {
      const double w = gopt.psoInertia * (1.0 - 0.5 * static_cast<double>(it) / std::max(1, gopt.psoIterations));
      for (int i = 0; i < n; ++i) {
        auto& pi = pos[static_cast<std::size_t>(i)];
        auto& vi = vel[static_cast<std::size_t>(i)];
        for (int d = 0; d < dim_; ++d) {
          const std::size_t dd = static_cast<std::size_t>(d);
          const double r1 = u(rng_), r2 = u(rng_);
          vi[dd] = w * vi[dd] + gopt.psoCognitive * r1 * (best[static_cast<std::size_t>(i)][dd] - pi[dd]) +
                   gopt.psoSocial * r2 * (gbest[dd] - pi[dd]);
          const double vmax = 0.3 * (bounds_.hi[dd] - bounds_.lo[dd]);
          vi[dd] = std::min(vmax, std::max(-vmax, vi[dd]));
          pi[dd] = std::min(bounds_.hi[dd], std::max(bounds_.lo[dd], pi[dd] + vi[dd]));
        }
        const Evaluated e = evaluate(pi);
        if (e.f < bestF[static_cast<std::size_t>(i)]) {
          bestF[static_cast<std::size_t>(i)] = e.f;
          best[static_cast<std::size_t>(i)] = pi;
          if (e.f < gbestF) {
            gbestF = e.f;
            gbest = pi;
          }
        }
      }
      res.globalHistory.push_back(gbestF);
      res.globalEvaluations += n;
    }
    res.bestF = gbestF;
    res.bestX = gbest;
    res.basinSeeds.push_back(gbest);
    res.globalStage = "PSO";
    return res;
  }

  // --------------------------------------------------------- Nelder-Mead
  double nelderMead(Vec& x, const LocalOptions& lopt, int& evaluations) {
    const int n = dim_;
    const double alpha = 1.0, gamma = 2.0, rho = 0.5, sigma = 0.5;
    std::vector<Vec> simplex(static_cast<std::size_t>(n + 1));
    std::vector<double> fv(static_cast<std::size_t>(n + 1));
    simplex[0] = bounds_.clamp(x);
    fv[0] = fitness_(simplex[0]);
    for (int i = 0; i < n; ++i) {
      Vec p = simplex[0];
      const double step = 0.08 * (bounds_.hi[static_cast<std::size_t>(i)] - bounds_.lo[static_cast<std::size_t>(i)]);
      p[static_cast<std::size_t>(i)] = std::min(bounds_.hi[static_cast<std::size_t>(i)], p[static_cast<std::size_t>(i)] + step);
      simplex[static_cast<std::size_t>(i + 1)] = p;
      fv[static_cast<std::size_t>(i + 1)] = fitness_(p);
    }
    evaluations = n + 1;
    int iter = 0;
    while (evaluations < lopt.nmMaxEval && iter < lopt.nmMaxEval) {
      std::vector<int> order(static_cast<std::size_t>(n + 1));
      std::iota(order.begin(), order.end(), 0);
      std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return fv[static_cast<std::size_t>(a)] < fv[static_cast<std::size_t>(b)]; });
      std::vector<Vec> s2(static_cast<std::size_t>(n + 1));
      std::vector<double> f2(static_cast<std::size_t>(n + 1));
      for (int i = 0; i <= n; ++i) {
        s2[static_cast<std::size_t>(i)] = simplex[static_cast<std::size_t>(order[static_cast<std::size_t>(i)])];
        f2[static_cast<std::size_t>(i)] = fv[static_cast<std::size_t>(order[static_cast<std::size_t>(i)])];
      }
      simplex = s2;
      fv = f2;
      double spread = 0.0;
      for (int i = 1; i <= n; ++i) {
        for (int d = 0; d < n; ++d)
          spread = std::max(spread, std::abs(simplex[static_cast<std::size_t>(i)][static_cast<std::size_t>(d)] - simplex[0][static_cast<std::size_t>(d)]));
      }
      if (spread < lopt.tol) break;
      Vec centroid(static_cast<std::size_t>(n), 0.0);
      for (int i = 0; i < n; ++i)
        for (int d = 0; d < n; ++d) centroid[static_cast<std::size_t>(d)] += simplex[static_cast<std::size_t>(i)][static_cast<std::size_t>(d)] / n;
      auto combine = [&](double t) {
        Vec p(static_cast<std::size_t>(n));
        for (int d = 0; d < n; ++d)
          p[static_cast<std::size_t>(d)] = centroid[static_cast<std::size_t>(d)] + t * (simplex[static_cast<std::size_t>(n)][static_cast<std::size_t>(d)] - centroid[static_cast<std::size_t>(d)]);
        return bounds_.clamp(p);
      };
      const Vec xr = combine(-alpha);
      const double fr = fitness_(xr);
      ++evaluations;
      if (fr < fv[0]) {
        const Vec xe = combine(-gamma);
        const double fe = fitness_(xe);
        ++evaluations;
        if (fe < fr) {
          simplex[static_cast<std::size_t>(n)] = xe;
          fv[static_cast<std::size_t>(n)] = fe;
        } else {
          simplex[static_cast<std::size_t>(n)] = xr;
          fv[static_cast<std::size_t>(n)] = fr;
        }
      } else if (fr < fv[static_cast<std::size_t>(n - 1)]) {
        simplex[static_cast<std::size_t>(n)] = xr;
        fv[static_cast<std::size_t>(n)] = fr;
      } else {
        const Vec xc = combine(rho);
        const double fc = fitness_(xc);
        ++evaluations;
        if (fc < fv[static_cast<std::size_t>(n)]) {
          simplex[static_cast<std::size_t>(n)] = xc;
          fv[static_cast<std::size_t>(n)] = fc;
        } else {
          for (int i = 1; i <= n; ++i) {
            Vec p(static_cast<std::size_t>(n));
            for (int d = 0; d < n; ++d)
              p[static_cast<std::size_t>(d)] = simplex[0][static_cast<std::size_t>(d)] + sigma * (simplex[static_cast<std::size_t>(i)][static_cast<std::size_t>(d)] - simplex[0][static_cast<std::size_t>(d)]);
            simplex[static_cast<std::size_t>(i)] = bounds_.clamp(p);
            fv[static_cast<std::size_t>(i)] = fitness_(simplex[static_cast<std::size_t>(i)]);
            ++evaluations;
          }
        }
      }
      ++iter;
    }
    const auto best = std::min_element(fv.begin(), fv.end());
    x = simplex[static_cast<std::size_t>(std::distance(fv.begin(), best))];
    localHistory_.push_back(*best);
    return *best;
  }

  // ------------------------------------------------------------- L-BFGS
  double lbfgs(Vec& x, const LocalOptions& lopt, int& evaluations) {
    const int m = lopt.lbfgsMemory;
    std::vector<Vec> sHist, yHist;
    std::vector<double> rhoHist;
    Vec grad;
    bool haveGrad = static_cast<bool>(gradient_);
    Vec xc = bounds_.clamp(x);
    double f = fitness_(xc);
    ++evaluations;
    if (haveGrad) grad = gradient_(xc);
    else grad = finiteDifferenceGradient(xc, f);
    for (int iter = 0; iter < lopt.lbfgsMaxIter; ++iter) {
      // two-loop recursion
      Vec q = grad;
      std::vector<double> alphas(sHist.size());
      for (int i = static_cast<int>(sHist.size()) - 1; i >= 0; --i) {
        alphas[static_cast<std::size_t>(i)] = rhoHist[static_cast<std::size_t>(i)] * dot(sHist[static_cast<std::size_t>(i)], q);
        q = axpy(-alphas[static_cast<std::size_t>(i)], yHist[static_cast<std::size_t>(i)], q);
      }
      double scale = 1.0;
      if (!sHist.empty()) {
        const double sy = dot(sHist.back(), yHist.back());
        const double yy = dot(yHist.back(), yHist.back());
        scale = sy / std::max(yy, 1e-12);
      }
      Vec r = scaled(q, scale);
      for (std::size_t i = 0; i < sHist.size(); ++i) {
        const double beta = rhoHist[i] * dot(yHist[i], r);
        r = axpy(alphas[i] - beta, sHist[i], r);
      }
      Vec dir = scaled(r, -1.0);
      double slope = dot(grad, dir);
      if (slope >= -1e-14) {
        dir = scaled(grad, -1.0);
        slope = -dot(grad, grad);
      }
      // Armijo backtracking line search with bound projection.
      double step = 1.0;
      Vec xNew;
      double fNew = f;
      bool accepted = false;
      for (int ls = 0; ls < 40; ++ls) {
        xNew = bounds_.clamp(axpy(step, dir, xc));
        fNew = fitness_(xNew);
        ++evaluations;
        if (fNew <= f + 1e-4 * step * slope) {
          accepted = true;
          break;
        }
        step *= 0.5;
      }
      if (!accepted) break;
      Vec gNew = haveGrad ? gradient_(xNew) : finiteDifferenceGradient(xNew, fNew);
      Vec s = Vec(static_cast<std::size_t>(dim_));
      Vec y = Vec(static_cast<std::size_t>(dim_));
      for (int d = 0; d < dim_; ++d) {
        s[static_cast<std::size_t>(d)] = xNew[static_cast<std::size_t>(d)] - xc[static_cast<std::size_t>(d)];
        y[static_cast<std::size_t>(d)] = gNew[static_cast<std::size_t>(d)] - grad[static_cast<std::size_t>(d)];
      }
      const double sy = dot(s, y);
      if (sy > 1e-12) {
        sHist.push_back(s);
        yHist.push_back(y);
        rhoHist.push_back(1.0 / sy);
        if (static_cast<int>(sHist.size()) > m) {
          sHist.erase(sHist.begin());
          yHist.erase(yHist.begin());
          rhoHist.erase(rhoHist.begin());
        }
      }
      double change = 0.0;
      for (int d = 0; d < dim_; ++d) change = std::max(change, std::abs(xNew[static_cast<std::size_t>(d)] - xc[static_cast<std::size_t>(d)]));
      const double fOld = f;
      xc = xNew;
      f = fNew;
      grad = gNew;
      localHistory_.push_back(f);
      if (change < lopt.tol || std::abs(fOld - f) < 1e-14) break;
    }
    x = xc;
    return f;
  }

  [[nodiscard]] const std::vector<double>& localHistory() const { return localHistory_; }

 private:
  Evaluated evaluate(const Vec& x) {
    Evaluated e;
    e.x = bounds_.clamp(x);
    e.f = fitness_(e.x);
    e.objectives = {e.f, std::max(0.0, e.cv)};
    return e;
  }
  const Evaluated& tournament(const std::vector<Evaluated>& pop) {
    std::uniform_int_distribution<int> pick(0, static_cast<int>(pop.size()) - 1);
    const Evaluated& a = pop[static_cast<std::size_t>(pick(rng_))];
    const Evaluated& b = pop[static_cast<std::size_t>(pick(rng_))];
    if (a.rank != b.rank) return a.rank < b.rank ? a : b;
    return a.crowding >= b.crowding ? a : b;
  }
  std::vector<Vec> sbx(const Vec& p1, const Vec& p2, const GlobalOptions& gopt) {
    std::uniform_real_distribution<double> u(0.0, 1.0);
    Vec c1(p1.size()), c2(p1.size());
    for (std::size_t i = 0; i < p1.size(); ++i) {
      if (u(rng_) <= gopt.crossoverProb) {
        const double r = u(rng_);
        const double beta = (r <= 0.5) ? std::pow(2.0 * r, 1.0 / (gopt.sbxEta + 1.0))
                                       : std::pow(1.0 / (2.0 * (1.0 - r)), 1.0 / (gopt.sbxEta + 1.0));
        c1[i] = 0.5 * ((1.0 + beta) * p1[i] + (1.0 - beta) * p2[i]);
        c2[i] = 0.5 * ((1.0 - beta) * p1[i] + (1.0 + beta) * p2[i]);
      } else {
        c1[i] = p1[i];
        c2[i] = p2[i];
      }
    }
    return {bounds_.clamp(c1), bounds_.clamp(c2)};
  }
  Vec mutate(const Vec& x, const GlobalOptions& gopt) {
    std::uniform_real_distribution<double> u(0.0, 1.0);
    Vec y = x;
    for (std::size_t i = 0; i < x.size(); ++i) {
      if (u(rng_) > gopt.mutationProb) continue;
      const double span = bounds_.hi[i] - bounds_.lo[i];
      const double delta = (u(rng_) < 0.5) ? std::pow(2.0 * u(rng_), 1.0 / (gopt.polyEta + 1.0)) - 1.0
                                           : 1.0 - std::pow(2.0 * (1.0 - u(rng_)), 1.0 / (gopt.polyEta + 1.0));
      y[i] = std::min(bounds_.hi[i], std::max(bounds_.lo[i], x[i] + delta * span * 0.2));
    }
    return y;
  }
  Vec finiteDifferenceGradient(const Vec& x, double f0) {
    Vec g(x.size());
    for (std::size_t i = 0; i < x.size(); ++i) {
      Vec xp = x;
      const double span = bounds_.hi[i] - bounds_.lo[i];
      const double h = 1e-6 * std::max(span, 1e-6);
      xp[i] = std::min(bounds_.hi[i], x[i] + h);
      const double fp = fitness_(xp);
      g[i] = (fp - f0) / std::max(xp[i] - x[i], 1e-12);
    }
    return g;
  }

  int dim_;
  Bounds bounds_;
  FitnessFn fitness_;
  GradientFn gradient_;
  std::mt19937 rng_;
  std::vector<double> localHistory_;
};

// ---------------------------------------------------------------------------
// Smooth geometric parametrisation used for shape optimisation. Each obstacle
// is a smoothed disk; the density field is the complement of the union.
struct ShapeParametrization {
  const FvmMesh* mesh = nullptr;
  int nBlobs = 3;
  double blur = 0.7;
  double floorDensity = 0.02;

  void build(const FvmMesh& m, int blobs) {
    mesh = &m;
    nBlobs = blobs;
  }
  [[nodiscard]] int dim() const { return nBlobs * 3; }

  Bounds bounds() const {
    Bounds b;
    for (int k = 0; k < nBlobs; ++k) {
      b.lo.push_back(0.5 * mesh->h);
      b.lo.push_back(0.5 * mesh->h);
      b.lo.push_back(0.7);
      b.hi.push_back((mesh->nx - 0.5) * mesh->h);
      b.hi.push_back((mesh->ny - 0.5) * mesh->h);
      b.hi.push_back(0.45 * std::min(mesh->nx, mesh->ny) * mesh->h);
    }
    return b;
  }

  std::vector<double> density(const Vec& theta) const {
    const int n = mesh->size();
    std::vector<double> gamma(static_cast<std::size_t>(n), 1.0);
    for (int e = 0; e < n; ++e) {
      const double px = (e % mesh->nx + 0.5) * mesh->h;
      const double py = (e / mesh->nx + 0.5) * mesh->h;
      double cut = 0.0;
      for (int k = 0; k < nBlobs; ++k) {
        const double cx = theta[static_cast<std::size_t>(3 * k)];
        const double cy = theta[static_cast<std::size_t>(3 * k + 1)];
        const double r = theta[static_cast<std::size_t>(3 * k + 2)];
        const double d = std::sqrt((px - cx) * (px - cx) + (py - cy) * (py - cy));
        cut += 1.0 / (1.0 + std::exp((d - r) / blur));
      }
      gamma[static_cast<std::size_t>(e)] = std::min(1.0, std::max(floorDensity, 1.0 - cut));
    }
    return gamma;
  }

  std::vector<Vec> densityJacobian(const Vec& theta) const {
    const int n = mesh->size();
    std::vector<Vec> jac(static_cast<std::size_t>(3 * nBlobs), Vec(static_cast<std::size_t>(n), 0.0));
    for (int e = 0; e < n; ++e) {
      const double px = (e % mesh->nx + 0.5) * mesh->h;
      const double py = (e / mesh->nx + 0.5) * mesh->h;
      for (int k = 0; k < nBlobs; ++k) {
        const double cx = theta[static_cast<std::size_t>(3 * k)];
        const double cy = theta[static_cast<std::size_t>(3 * k + 1)];
        const double r = theta[static_cast<std::size_t>(3 * k + 2)];
        const double dx = px - cx, dy = py - cy;
        const double d = std::max(std::sqrt(dx * dx + dy * dy), 1e-9);
        const double s = 1.0 / (1.0 + std::exp((d - r) / blur));
        const double dsdd = -s * (1.0 - s) / blur;
        const double dddx = dx / d, dddy = dy / d;
        const double gammaVal = densityAt(px, py, theta);
        const bool active = gammaVal > floorDensity + 1e-12 && gammaVal < 1.0 - 1e-12;
        if (!active) continue;
        jac[static_cast<std::size_t>(3 * k)][static_cast<std::size_t>(e)] -= dsdd * dddx;
        jac[static_cast<std::size_t>(3 * k + 1)][static_cast<std::size_t>(e)] -= dsdd * dddy;
        jac[static_cast<std::size_t>(3 * k + 2)][static_cast<std::size_t>(e)] -= dsdd * (-1.0);
      }
    }
    return jac;
  }

  [[nodiscard]] double densityAt(double px, double py, const Vec& theta) const {
    double cut = 0.0;
    for (int k = 0; k < nBlobs; ++k) {
      const double cx = theta[static_cast<std::size_t>(3 * k)];
      const double cy = theta[static_cast<std::size_t>(3 * k + 1)];
      const double r = theta[static_cast<std::size_t>(3 * k + 2)];
      const double d = std::sqrt((px - cx) * (px - cx) + (py - cy) * (py - cy));
      cut += 1.0 / (1.0 + std::exp((d - r) / blur));
    }
    return std::min(1.0, std::max(floorDensity, 1.0 - cut));
  }
};

}  // namespace mdo
