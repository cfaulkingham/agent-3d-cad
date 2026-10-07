#pragma once
#include "agentcad/json.hpp"
#include <filesystem>
#include <memory>

namespace agentcad {
struct QueryLimits {
  std::size_t topology_entities = 10000;
  std::size_t mesh_vertices = 200000;
  std::size_t mesh_triangles = 200000;
  std::size_t edge_points = 200000;
};
// OCCT handles never cross the service boundary. No thread-safety guarantee.
class BuiltModel {
public:
  explicit BuiltModel(const Json& model);
  // Private-format, exact B-rep snapshots for disposable caches. Restoring still
  // validates shapes; assemblies rederive placements and ownership from cached
  // exact part sources. No OCCT objects or persistent topology IDs escape here.
  BuiltModel(const Json& model, const Json& snapshot);
  Json snapshot() const;
  ~BuiltModel();
  BuiltModel(BuiltModel&&) noexcept;
  BuiltModel& operator=(BuiltModel&&) noexcept;
  BuiltModel(const BuiltModel&) = delete;
  BuiltModel& operator=(const BuiltModel&) = delete;
  Json summary(const std::string& feature_id = "") const;
  // IDs are local to this evaluation and feature. They are never design references.
  Json topology(const std::string& feature_id = "", const QueryLimits& limits = {}) const;
  Json mesh(const std::string& feature_id = "", const QueryLimits& limits = {}) const;
  // Exact B-rep hidden-line views / true planar sections. Drawing entities are
  // derived output, not persistent topology references.
  Json drawing(const Json& spec) const;
  void export_file(const std::filesystem::path& path, const std::string& format) const;
private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
std::string kernel_version();
void configure_kernel_logging();
}
