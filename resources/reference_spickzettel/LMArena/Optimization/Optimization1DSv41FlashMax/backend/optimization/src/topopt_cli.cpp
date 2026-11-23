// SPDX-License-Identifier: MIT
// Headless driver: ./topopt_cli [nx] [ny] [volFrac] [rmin] > design.json
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "mdo/topopt.hpp"

int main(int argc, char** argv) {
  mdo::TopoOptions opts;
  opts.physics.nx = argc > 1 ? std::atoi(argv[1]) : 32;
  opts.physics.ny = argc > 2 ? std::atoi(argv[2]) : 20;
  opts.volFrac = argc > 3 ? std::atof(argv[3]) : 0.42;
  opts.rmin = argc > 4 ? std::atof(argv[4]) : 3.4;
  opts.betaLevels = 5;
  opts.mmaIterationsPerLevel = 24;
  opts.snapshotEvery = 10;
  mdo::TopologyOptimizer opt(opts);
  const mdo::TopoRunResult res = opt.run();
  std::cout << std::setprecision(8);
  std::cout << "grid=" << res.nx << "x" << res.ny << " objective=" << res.objective << " volume=" << res.volume
            << " grayness=" << res.grayness << " peakT=" << res.peakTemperature << " dp=" << res.pressureDrop
            << " iterations=" << res.history.size() << " time=" << res.elapsedSeconds << "s" << std::endl;
  std::ofstream out("design_field.json");
  out << "{\"nx\":" << res.nx << ",\"ny\":" << res.ny << ",\"objective\":" << res.objective << ",\"volume\":" << res.volume
      << ",\"density\":[";
  for (std::size_t i = 0; i < res.gammaBar.size(); ++i) out << (i ? "," : "") << res.gammaBar[i];
  out << "]}\n";
  out.close();
  return 0;
}
