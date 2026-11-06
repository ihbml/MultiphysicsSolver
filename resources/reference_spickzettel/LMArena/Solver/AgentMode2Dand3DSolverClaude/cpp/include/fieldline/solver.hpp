#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <numeric>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fieldline {

using Index = std::uint32_t;

struct Point2 {
  double x;
  double y;
};

using Triangle = std::array<Index, 3>;

// The caller owns the mesh. Only the precomputed element geometry is retained.
struct MeshView {
  std::span<const Point2> nodes;
  std::span<const Triangle> triangles;
};

struct Material {
  double conductivity;       // W / (m K)
  double density;            // kg / m^3
  double specific_heat;      // J / (kg K)
  double young_modulus;      // Pa
  double poisson_ratio;
  double thermal_expansion;  // 1 / K
};

struct TemperatureBC {
  Index node;
  double kelvin;
};

struct DisplacementBC {
  Index node;
  unsigned component;  // 0: x, 1: y
  double meters;
};

enum class Preconditioner { incomplete_cholesky, jacobi };

struct SolverOptions {
  double time_step = 1.0;
  double reference_temperature = 293.15;
  double thickness = 1.0;
  double thermal_theta = 1.0;  // 1: backward Euler; 0.5: Crank-Nicolson.
  double relative_tolerance = 1e-10;
  double absolute_tolerance = 1e-12;
  int max_iterations = 3000;
  Preconditioner preconditioner = Preconditioner::incomplete_cholesky;
};

struct ProblemView {
  MeshView mesh;
  std::span<const Material> materials;          // One, or one per triangle.
  std::span<const double> heat_sources;         // W / m^3: zero, one, or one per triangle.
  std::span<const double> nodal_forces;         // Optional, interleaved x/y in N.
  std::span<const TemperatureBC> temperature_bcs;
  std::span<const DisplacementBC> displacement_bcs;
  SolverOptions options;
};

struct IterationInfo {
  int iterations = 0;
  double absolute_residual = 0.0;
  double relative_residual = 0.0;
};

struct StepInfo {
  IterationInfo thermal;
  IterationInfo mechanical;
  double min_temperature = 0.0;
  double max_temperature = 0.0;
  double max_displacement = 0.0;
  double time = 0.0;
};

namespace detail {

inline void require(bool condition, const char* message) {
  if (!condition) throw std::invalid_argument(message);
}

inline double dot(std::span<const double> a, std::span<const double> b) {
  double result = 0.0;
  const auto count = static_cast<std::ptrdiff_t>(a.size());
#ifdef _OPENMP
#pragma omp parallel for reduction(+ : result) if(count > 20000) schedule(static)
#endif
  for (std::ptrdiff_t i = 0; i < count; ++i) {
    result += a[static_cast<std::size_t>(i)] * b[static_cast<std::size_t>(i)];
  }
  return result;
}

class CsrMatrix {
 public:
  static CsrMatrix for_mesh(std::size_t node_count,
                            std::span<const Triangle> triangles,
                            unsigned components) {
    const std::size_t count = node_count * components;
    require(count <= std::numeric_limits<Index>::max(), "Too many degrees of freedom");
    std::vector<std::vector<Index>> columns(count);

    for (const Triangle& triangle : triangles) {
      for (Index node_a : triangle) {
        for (unsigned ca = 0; ca < components; ++ca) {
          auto& row = columns[components * node_a + ca];
          for (Index node_b : triangle) {
            for (unsigned cb = 0; cb < components; ++cb) {
              row.push_back(components * node_b + cb);
            }
          }
        }
      }
    }

    CsrMatrix matrix;
    matrix.row_ptr.reserve(count + 1);
    matrix.row_ptr.push_back(0);
    for (auto& row : columns) {
      std::sort(row.begin(), row.end());
      row.erase(std::unique(row.begin(), row.end()), row.end());
      require(!row.empty(), "Mesh contains an unused node");
      matrix.col.insert(matrix.col.end(), row.begin(), row.end());
      matrix.row_ptr.push_back(matrix.col.size());
    }
    matrix.value.resize(matrix.col.size(), 0.0);
    return matrix;
  }

  std::size_t rows() const { return row_ptr.size() - 1; }

  std::size_t position(std::size_t row, Index column) const {
    auto first = col.begin() + static_cast<std::ptrdiff_t>(row_ptr[row]);
    auto last = col.begin() + static_cast<std::ptrdiff_t>(row_ptr[row + 1]);
    auto found = std::lower_bound(first, last, column);
    if (found == last || *found != column) {
      throw std::logic_error("Missing entry in the precomputed CSR pattern");
    }
    return static_cast<std::size_t>(found - col.begin());
  }

  void multiply(std::span<const double> x, std::span<double> out) const {
    const auto count = static_cast<std::ptrdiff_t>(rows());
#ifdef _OPENMP
#pragma omp parallel for if(count > 8192) schedule(static)
#endif
    for (std::ptrdiff_t row = 0; row < count; ++row) {
      const auto i = static_cast<std::size_t>(row);
      double sum = 0.0;
      for (std::size_t p = row_ptr[i]; p < row_ptr[i + 1]; ++p) {
        sum += value[p] * x[col[p]];
      }
      out[i] = sum;
    }
  }

  std::vector<std::size_t> row_ptr;
  std::vector<Index> col;
  std::vector<double> value;
};

class ConstrainedSystem {
 public:
  ConstrainedSystem(CsrMatrix matrix, std::vector<unsigned char> fixed,
                    std::vector<double> prescribed)
      : matrix_(std::move(matrix)),
        fixed_(std::move(fixed)),
        prescribed_(std::move(prescribed)),
        rhs_shift_(matrix_.rows(), 0.0) {
    // Symmetric elimination retains an SPD matrix for conjugate gradients.
    for (std::size_t row = 0; row < matrix_.rows(); ++row) {
      for (std::size_t p = matrix_.row_ptr[row]; p < matrix_.row_ptr[row + 1]; ++p) {
        const Index column = matrix_.col[p];
        if (!fixed_[row] && fixed_[column]) {
          rhs_shift_[row] -= matrix_.value[p] * prescribed_[column];
        }
        if (fixed_[row] || fixed_[column]) {
          matrix_.value[p] = row == column && fixed_[row] ? 1.0 : 0.0;
        }
      }
    }
  }

  void apply_rhs(std::span<double> rhs) const {
    for (std::size_t i = 0; i < rhs.size(); ++i) {
      rhs[i] = fixed_[i] ? prescribed_[i] : rhs[i] + rhs_shift_[i];
    }
  }

  void enforce(std::span<double> state) const {
    for (std::size_t i = 0; i < state.size(); ++i) {
      if (fixed_[i]) state[i] = prescribed_[i];
    }
  }

  const CsrMatrix& matrix() const { return matrix_; }

 private:
  CsrMatrix matrix_;
  std::vector<unsigned char> fixed_;
  std::vector<double> prescribed_;
  std::vector<double> rhs_shift_;
};

class PcgSolver {
 public:
  PcgSolver(const CsrMatrix& matrix, const SolverOptions& options)
      : matrix_(matrix),
        options_(options),
        inverse_diagonal_(matrix.rows()),
        diagonal_position_(matrix.rows()),
        ic_diagonal_(matrix.rows()),
        ic_lower_(matrix.value.size(), 0.0),
        r_(matrix.rows()),
        z_(matrix.rows()),
        p_(matrix.rows()),
        ap_(matrix.rows()),
        work_(matrix.rows()) {
    double largest_diagonal = 0.0;
    for (std::size_t i = 0; i < matrix.rows(); ++i) {
      const std::size_t pos = matrix.position(i, static_cast<Index>(i));
      diagonal_position_[i] = pos;
      const double diagonal = matrix.value[pos];
      require(std::isfinite(diagonal) && diagonal > 0.0,
              "System has a nonpositive diagonal; check materials and constraints");
      inverse_diagonal_[i] = 1.0 / diagonal;
      largest_diagonal = std::max(largest_diagonal, diagonal);
    }

    if (options.preconditioner == Preconditioner::incomplete_cholesky) {
      for (double factor : {0.0, 1e-12, 1e-9, 1e-6, 1e-3}) {
        if (factor_ic0(factor * largest_diagonal)) {
          use_ic0_ = true;
          break;
        }
      }
    }
  }

  IterationInfo solve(std::span<const double> rhs, std::vector<double>& x) {
    matrix_.multiply(x, ap_);
    for (std::size_t i = 0; i < x.size(); ++i) r_[i] = rhs[i] - ap_[i];

    const double rhs_norm = std::sqrt(dot(rhs, rhs));
    const double target = std::max(options_.absolute_tolerance,
                                   options_.relative_tolerance * rhs_norm);
    double residual = std::sqrt(dot(r_, r_));
    auto result = [&](int iterations) {
      return IterationInfo{iterations, residual,
                           residual / (rhs_norm > 0.0 ? rhs_norm : 1.0)};
    };
    if (residual <= target) return result(0);

    precondition(r_, z_);
    std::copy(z_.begin(), z_.end(), p_.begin());
    double rz = dot(r_, z_);
    if (!std::isfinite(rz) || rz <= 0.0) {
      throw std::runtime_error("PCG preconditioner breakdown");
    }

    for (int iteration = 1; iteration <= options_.max_iterations; ++iteration) {
      matrix_.multiply(p_, ap_);
      const double denominator = dot(p_, ap_);
      if (!std::isfinite(denominator) || denominator <= 0.0) {
        throw std::runtime_error("PCG breakdown: matrix may be singular or indefinite");
      }
      const double alpha = rz / denominator;
      for (std::size_t i = 0; i < x.size(); ++i) {
        x[i] += alpha * p_[i];
        r_[i] -= alpha * ap_[i];
      }
      residual = std::sqrt(dot(r_, r_));

      // Check the true residual periodically and before declaring convergence.
      if (iteration % 64 == 0 || residual <= target) {
        matrix_.multiply(x, ap_);
        for (std::size_t i = 0; i < x.size(); ++i) r_[i] = rhs[i] - ap_[i];
        residual = std::sqrt(dot(r_, r_));
      }
      if (!std::isfinite(residual)) throw std::runtime_error("PCG residual is not finite");
      if (residual <= target) return result(iteration);

      precondition(r_, z_);
      const double next_rz = dot(r_, z_);
      if (!std::isfinite(next_rz) || next_rz <= 0.0) {
        throw std::runtime_error("PCG preconditioner breakdown");
      }
      const double beta = next_rz / rz;
      for (std::size_t i = 0; i < x.size(); ++i) p_[i] = z_[i] + beta * p_[i];
      rz = next_rz;
    }
    throw std::runtime_error("PCG did not converge in " +
                             std::to_string(options_.max_iterations) +
                             " iterations; residual = " + std::to_string(residual));
  }

 private:
  bool factor_ic0(double shift) {
    std::fill(ic_lower_.begin(), ic_lower_.end(), 0.0);
    for (std::size_t i = 0; i < matrix_.rows(); ++i) {
      for (std::size_t pos = matrix_.row_ptr[i]; pos < diagonal_position_[i]; ++pos) {
        const Index j = matrix_.col[pos];
        double sum = matrix_.value[pos];
        std::size_t a = matrix_.row_ptr[i];
        std::size_t b = matrix_.row_ptr[j];
        while (a < pos && b < diagonal_position_[j]) {
          if (matrix_.col[a] == matrix_.col[b]) {
            sum -= ic_lower_[a] * ic_lower_[b];
            ++a;
            ++b;
          } else if (matrix_.col[a] < matrix_.col[b]) {
            ++a;
          } else {
            ++b;
          }
        }
        ic_lower_[pos] = sum / ic_diagonal_[j];
      }

      double pivot = matrix_.value[diagonal_position_[i]] + shift;
      for (std::size_t pos = matrix_.row_ptr[i]; pos < diagonal_position_[i]; ++pos) {
        pivot -= ic_lower_[pos] * ic_lower_[pos];
      }
      if (!std::isfinite(pivot) || pivot <= 0.0) return false;
      ic_diagonal_[i] = std::sqrt(pivot);
    }
    return true;
  }

  void precondition(std::span<const double> input, std::span<double> output) {
    if (!use_ic0_) {
      for (std::size_t i = 0; i < input.size(); ++i) {
        output[i] = inverse_diagonal_[i] * input[i];
      }
      return;
    }

    for (std::size_t i = 0; i < input.size(); ++i) {
      double value = input[i];
      for (std::size_t p = matrix_.row_ptr[i]; p < diagonal_position_[i]; ++p) {
        value -= ic_lower_[p] * work_[matrix_.col[p]];
      }
      work_[i] = value / ic_diagonal_[i];
    }
    std::copy(work_.begin(), work_.end(), output.begin());
    for (std::size_t i = input.size(); i-- > 0;) {
      output[i] /= ic_diagonal_[i];
      for (std::size_t p = matrix_.row_ptr[i]; p < diagonal_position_[i]; ++p) {
        output[matrix_.col[p]] -= ic_lower_[p] * output[i];
      }
    }
  }

  const CsrMatrix& matrix_;
  SolverOptions options_;
  std::vector<double> inverse_diagonal_;
  std::vector<std::size_t> diagonal_position_;
  std::vector<double> ic_diagonal_;
  std::vector<double> ic_lower_;
  std::vector<double> r_, z_, p_, ap_, work_;
  bool use_ic0_ = false;
};

}  // namespace detail

// Reusable building blocks for additional discretized fields.
namespace core {
using SparseMatrix = detail::CsrMatrix;
using DirichletSystem = detail::ConstrainedSystem;
using PcgSolver = detail::PcgSolver;
}  // namespace core

// P1 triangular FEM, implicit theta-method heat transport, and small-strain
// plane-stress elasticity. Heat drives expansion; mechanical dissipation does not.
class ThermoelasticSolver {
 public:
  explicit ThermoelasticSolver(const ProblemView& problem)
      : options_(problem.options),
        thermal_rhs_operator_(make_pattern(problem.mesh, 1)),
        thermal_matrix_(thermal_rhs_operator_),
        mechanical_matrix_(make_pattern(problem.mesh, 2)),
        source_(problem.mesh.nodes.size(), 0.0),
        force_(2 * problem.mesh.nodes.size(), 0.0),
        temperature_(problem.mesh.nodes.size(), options_.reference_temperature),
        theta_(problem.mesh.nodes.size(), 0.0),
        displacement_(2 * problem.mesh.nodes.size(), 0.0),
        temperature_rhs_(problem.mesh.nodes.size(), 0.0),
        displacement_rhs_(2 * problem.mesh.nodes.size(), 0.0) {
    validate(problem);
    if (!problem.nodal_forces.empty()) {
      std::copy(problem.nodal_forces.begin(), problem.nodal_forces.end(), force_.begin());
    }
    assemble(problem);

    std::vector<unsigned char> thermal_fixed(theta_.size(), 0);
    std::vector<double> thermal_value(theta_.size(), 0.0);
    for (const TemperatureBC& bc : problem.temperature_bcs) {
      const double value = bc.kelvin - options_.reference_temperature;
      if (thermal_fixed[bc.node] && thermal_value[bc.node] != value) {
        throw std::invalid_argument("Conflicting temperature constraints");
      }
      thermal_fixed[bc.node] = 1;
      thermal_value[bc.node] = value;
    }

    std::vector<unsigned char> mechanical_fixed(displacement_.size(), 0);
    std::vector<double> mechanical_value(displacement_.size(), 0.0);
    for (const DisplacementBC& bc : problem.displacement_bcs) {
      const std::size_t dof = 2 * bc.node + bc.component;
      if (mechanical_fixed[dof] && mechanical_value[dof] != bc.meters) {
        throw std::invalid_argument("Conflicting displacement constraints");
      }
      mechanical_fixed[dof] = 1;
      mechanical_value[dof] = bc.meters;
    }

    thermal_system_ = std::make_unique<detail::ConstrainedSystem>(
        std::move(thermal_matrix_), std::move(thermal_fixed), std::move(thermal_value));
    mechanical_system_ = std::make_unique<detail::ConstrainedSystem>(
        std::move(mechanical_matrix_), std::move(mechanical_fixed), std::move(mechanical_value));
    thermal_solver_ = std::make_unique<detail::PcgSolver>(thermal_system_->matrix(), options_);
    mechanical_solver_ = std::make_unique<detail::PcgSolver>(mechanical_system_->matrix(), options_);

    thermal_system_->enforce(theta_);
    for (std::size_t i = 0; i < theta_.size(); ++i) {
      temperature_[i] = options_.reference_temperature + theta_[i];
    }
    mechanical_system_->enforce(displacement_);
  }

  void set_initial_temperature(std::span<const double> kelvin) {
    detail::require(kelvin.size() == theta_.size(), "Initial temperature size mismatch");
    for (std::size_t i = 0; i < kelvin.size(); ++i) {
      detail::require(std::isfinite(kelvin[i]), "Initial temperature must be finite");
      theta_[i] = kelvin[i] - options_.reference_temperature;
    }
    thermal_system_->enforce(theta_);
    for (std::size_t i = 0; i < theta_.size(); ++i) {
      temperature_[i] = options_.reference_temperature + theta_[i];
    }
    std::fill(displacement_.begin(), displacement_.end(), 0.0);
    mechanical_system_->enforce(displacement_);
    time_ = 0.0;
  }

  StepInfo step(double heat_scale = 1.0) {
    detail::require(std::isfinite(heat_scale), "Heat scale must be finite");
    next_theta_ = theta_;
    next_displacement_ = displacement_;

    thermal_rhs_operator_.multiply(theta_, temperature_rhs_);
    for (std::size_t i = 0; i < theta_.size(); ++i) {
      temperature_rhs_[i] += heat_scale * source_[i];
    }
    thermal_system_->apply_rhs(temperature_rhs_);
    const IterationInfo thermal_info = thermal_solver_->solve(temperature_rhs_, next_theta_);

    std::copy(force_.begin(), force_.end(), displacement_rhs_.begin());
    for (const Cell& cell : cells_) {
      const double mean_theta = (next_theta_[cell.nodes[0]] +
                                 next_theta_[cell.nodes[1]] +
                                 next_theta_[cell.nodes[2]]) / 3.0;
      const double load = cell.thermal_force_coefficient * mean_theta;
      for (unsigned a = 0; a < 3; ++a) {
        displacement_rhs_[2 * cell.nodes[a]] += load * cell.gradient[a].x;
        displacement_rhs_[2 * cell.nodes[a] + 1] += load * cell.gradient[a].y;
      }
    }
    mechanical_system_->apply_rhs(displacement_rhs_);
    const IterationInfo mechanical_info =
        mechanical_solver_->solve(displacement_rhs_, next_displacement_);

    theta_.swap(next_theta_);
    displacement_.swap(next_displacement_);
    time_ += options_.time_step;

    StepInfo result;
    result.thermal = thermal_info;
    result.mechanical = mechanical_info;
    result.time = time_;
    result.min_temperature = std::numeric_limits<double>::infinity();
    result.max_temperature = -std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < theta_.size(); ++i) {
      temperature_[i] = options_.reference_temperature + theta_[i];
      result.min_temperature = std::min(result.min_temperature, temperature_[i]);
      result.max_temperature = std::max(result.max_temperature, temperature_[i]);
      result.max_displacement = std::max(
          result.max_displacement,
          std::hypot(displacement_[2 * i], displacement_[2 * i + 1]));
    }
    return result;
  }

  std::span<const double> temperatures() const { return temperature_; }
  std::span<const double> displacements() const { return displacement_; }
  double time() const { return time_; }

 private:
  struct Cell {
    Triangle nodes;
    std::array<Point2, 3> gradient;
    double thermal_force_coefficient;
  };

  static detail::CsrMatrix make_pattern(const MeshView& mesh, unsigned components) {
    detail::require(!mesh.nodes.empty() && !mesh.triangles.empty(), "Mesh must not be empty");
    for (const Triangle& triangle : mesh.triangles) {
      for (Index node : triangle) {
        detail::require(node < mesh.nodes.size(), "Triangle references an invalid node");
      }
      detail::require(triangle[0] != triangle[1] && triangle[1] != triangle[2] &&
                          triangle[2] != triangle[0], "Triangle contains repeated nodes");
    }
    return detail::CsrMatrix::for_mesh(mesh.nodes.size(), mesh.triangles, components);
  }

  void validate(const ProblemView& problem) const {
    const auto& o = options_;
    detail::require(std::isfinite(o.time_step) && o.time_step > 0.0, "Time step must be positive");
    detail::require(std::isfinite(o.thickness) && o.thickness > 0.0, "Thickness must be positive");
    detail::require(std::isfinite(o.thermal_theta) && o.thermal_theta >= 0.5 &&
                        o.thermal_theta <= 1.0,
                    "Thermal theta must be between 0.5 and 1");
    detail::require(std::isfinite(o.reference_temperature), "Reference temperature must be finite");
    detail::require(std::isfinite(o.relative_tolerance) && o.relative_tolerance > 0.0,
                    "Relative tolerance must be positive");
    detail::require(std::isfinite(o.absolute_tolerance) && o.absolute_tolerance > 0.0,
                    "Absolute tolerance must be positive");
    detail::require(o.max_iterations > 0, "Maximum iterations must be positive");
    detail::require(problem.materials.size() == 1 ||
                        problem.materials.size() == problem.mesh.triangles.size(),
                    "Provide one material or one per triangle");
    detail::require(problem.heat_sources.empty() || problem.heat_sources.size() == 1 ||
                        problem.heat_sources.size() == problem.mesh.triangles.size(),
                    "Provide zero, one, or one heat source per triangle");
    detail::require(problem.nodal_forces.empty() ||
                        problem.nodal_forces.size() == 2 * problem.mesh.nodes.size(),
                    "Nodal forces size mismatch");

    for (const Point2& node : problem.mesh.nodes) {
      detail::require(std::isfinite(node.x) && std::isfinite(node.y),
                      "Mesh coordinates must be finite");
    }
    for (const Material& m : problem.materials) {
      detail::require(std::isfinite(m.conductivity) && m.conductivity > 0.0 &&
                          std::isfinite(m.density) && m.density > 0.0 &&
                          std::isfinite(m.specific_heat) && m.specific_heat > 0.0 &&
                          std::isfinite(m.young_modulus) && m.young_modulus > 0.0 &&
                          std::isfinite(m.poisson_ratio) && m.poisson_ratio > -1.0 &&
                          m.poisson_ratio < 0.5 && std::isfinite(m.thermal_expansion),
                      "Invalid material parameters");
    }
    for (double source : problem.heat_sources) {
      detail::require(std::isfinite(source), "Heat sources must be finite");
    }
    for (double force : problem.nodal_forces) {
      detail::require(std::isfinite(force), "Nodal forces must be finite");
    }
    for (const TemperatureBC& bc : problem.temperature_bcs) {
      detail::require(bc.node < problem.mesh.nodes.size() && std::isfinite(bc.kelvin),
                      "Invalid temperature boundary condition");
    }
    for (const DisplacementBC& bc : problem.displacement_bcs) {
      detail::require(bc.node < problem.mesh.nodes.size() && bc.component < 2 &&
                          std::isfinite(bc.meters), "Invalid displacement boundary condition");
    }
    validate_rigid_modes(problem);
  }

  void validate_rigid_modes(const ProblemView& problem) const {
    const std::size_t count = problem.mesh.nodes.size();
    std::vector<Index> parent(count);
    std::iota(parent.begin(), parent.end(), Index{0});
    auto root = [&](Index node) {
      while (parent[node] != node) {
        parent[node] = parent[parent[node]];
        node = parent[node];
      }
      return node;
    };
    for (const Triangle& triangle : problem.mesh.triangles) {
      const Index first = root(triangle[0]);
      parent[root(triangle[1])] = first;
      parent[root(triangle[2])] = first;
    }

    struct Component {
      Point2 origin{};
      double min_x = std::numeric_limits<double>::infinity();
      double max_x = -std::numeric_limits<double>::infinity();
      double min_y = std::numeric_limits<double>::infinity();
      double max_y = -std::numeric_limits<double>::infinity();
      std::array<std::array<double, 3>, 3> basis{};
      unsigned rank = 0;
      bool present = false;
    };
    std::vector<Component> components(count);
    for (std::size_t i = 0; i < count; ++i) {
      Component& component = components[root(static_cast<Index>(i))];
      const Point2& point = problem.mesh.nodes[i];
      if (!component.present) component.origin = point;
      component.present = true;
      component.min_x = std::min(component.min_x, point.x);
      component.max_x = std::max(component.max_x, point.x);
      component.min_y = std::min(component.min_y, point.y);
      component.max_y = std::max(component.max_y, point.y);
    }

    // Each connected body needs constraints spanning x/y translation and rotation.
    for (const DisplacementBC& bc : problem.displacement_bcs) {
      Component& component = components[root(bc.node)];
      const Point2& point = problem.mesh.nodes[bc.node];
      const double scale = std::max(component.max_x - component.min_x,
                                    component.max_y - component.min_y);
      std::array<double, 3> mode = bc.component == 0
          ? std::array<double, 3>{1.0, 0.0, -(point.y - component.origin.y) / scale}
          : std::array<double, 3>{0.0, 1.0, (point.x - component.origin.x) / scale};
      for (unsigned j = 0; j < component.rank; ++j) {
        const auto& basis = component.basis[j];
        const double projection = mode[0] * basis[0] + mode[1] * basis[1] +
                                  mode[2] * basis[2];
        for (unsigned k = 0; k < 3; ++k) mode[k] -= projection * basis[k];
      }
      const double length = std::sqrt(mode[0] * mode[0] + mode[1] * mode[1] +
                                      mode[2] * mode[2]);
      if (length > 1e-10 && component.rank < 3) {
        for (double& value : mode) value /= length;
        component.basis[component.rank++] = mode;
      }
    }
    for (const Component& component : components) {
      if (component.present) {
        detail::require(component.rank == 3,
                        "Displacement constraints must remove rigid modes on every body");
      }
    }
  }

  void assemble(const ProblemView& problem) {
    const auto& mesh = problem.mesh;
    cells_.reserve(mesh.triangles.size());
    for (std::size_t e = 0; e < mesh.triangles.size(); ++e) {
      const Triangle& t = mesh.triangles[e];
      const Point2& a = mesh.nodes[t[0]];
      const Point2& b = mesh.nodes[t[1]];
      const Point2& c = mesh.nodes[t[2]];
      const double twice_signed_area =
          (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
      const double abx = b.x - a.x, aby = b.y - a.y;
      const double bcx = c.x - b.x, bcy = c.y - b.y;
      const double cax = a.x - c.x, cay = a.y - c.y;
      const double largest_edge_squared = std::max({
          abx * abx + aby * aby, bcx * bcx + bcy * bcy,
          cax * cax + cay * cay});
      detail::require(std::isfinite(twice_signed_area) &&
                          std::abs(twice_signed_area) > 1e-14 * largest_edge_squared,
                      "Degenerate or near-degenerate triangle");

      const double area = 0.5 * std::abs(twice_signed_area);
      const double volume = area * options_.thickness;
      const std::array<Point2, 3> grad{{
          {(b.y - c.y) / twice_signed_area, (c.x - b.x) / twice_signed_area},
          {(c.y - a.y) / twice_signed_area, (a.x - c.x) / twice_signed_area},
          {(a.y - b.y) / twice_signed_area, (b.x - a.x) / twice_signed_area}}};
      const Material& m = problem.materials[problem.materials.size() == 1 ? 0 : e];
      const double source = problem.heat_sources.empty()
                                ? 0.0
                                : problem.heat_sources[problem.heat_sources.size() == 1 ? 0 : e];
      const double elastic = m.young_modulus / (1.0 - m.poisson_ratio * m.poisson_ratio);
      const double shear = elastic * (1.0 - m.poisson_ratio) / 2.0;
      const double coupling = elastic * (1.0 + m.poisson_ratio) *
                              m.thermal_expansion * volume;
      cells_.push_back(Cell{t, grad, coupling});

      for (unsigned i = 0; i < 3; ++i) {
        source_[t[i]] += source * volume / 3.0;
        for (unsigned j = 0; j < 3; ++j) {
          const std::size_t thermal_pos = thermal_matrix_.position(t[i], t[j]);
          const double mass = m.density * m.specific_heat * volume *
                              (i == j ? 2.0 : 1.0) / (12.0 * options_.time_step);
          const double conductivity = m.conductivity * volume *
              (grad[i].x * grad[j].x + grad[i].y * grad[j].y);
          thermal_rhs_operator_.value[thermal_pos] +=
              mass - (1.0 - options_.thermal_theta) * conductivity;
          thermal_matrix_.value[thermal_pos] +=
              mass + options_.thermal_theta * conductivity;

          const double gix = grad[i].x, giy = grad[i].y;
          const double gjx = grad[j].x, gjy = grad[j].y;
          const double kxx = volume * (elastic * gix * gjx + shear * giy * gjy);
          const double kxy = volume * (elastic * m.poisson_ratio * gix * gjy +
                                       shear * giy * gjx);
          const double kyx = volume * (elastic * m.poisson_ratio * giy * gjx +
                                       shear * gix * gjy);
          const double kyy = volume * (elastic * giy * gjy + shear * gix * gjx);
          mechanical_matrix_.value[mechanical_matrix_.position(2 * t[i], 2 * t[j])] += kxx;
          mechanical_matrix_.value[mechanical_matrix_.position(2 * t[i], 2 * t[j] + 1)] += kxy;
          mechanical_matrix_.value[mechanical_matrix_.position(2 * t[i] + 1, 2 * t[j])] += kyx;
          mechanical_matrix_.value[mechanical_matrix_.position(2 * t[i] + 1, 2 * t[j] + 1)] += kyy;
        }
      }
    }
  }

  SolverOptions options_;
  detail::CsrMatrix thermal_rhs_operator_;
  detail::CsrMatrix thermal_matrix_;
  detail::CsrMatrix mechanical_matrix_;
  std::vector<Cell> cells_;
  std::vector<double> source_, force_;
  std::vector<double> temperature_, theta_, displacement_;
  std::vector<double> next_theta_, next_displacement_;
  std::vector<double> temperature_rhs_, displacement_rhs_;
  std::unique_ptr<detail::ConstrainedSystem> thermal_system_, mechanical_system_;
  std::unique_ptr<detail::PcgSolver> thermal_solver_, mechanical_solver_;
  double time_ = 0.0;
};

}  // namespace fieldline