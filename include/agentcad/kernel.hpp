#pragma once
#include "agentcad/json.hpp"
#include <filesystem>
#include <memory>
#include <functional>
#include <optional>

namespace agentcad {
struct QueryLimits {
  std::size_t topology_entities = 10000;
  std::size_t mesh_vertices = 200000;
  std::size_t mesh_triangles = 200000;
  std::size_t edge_points = 200000;
};
// Internal cache callbacks carry JSON only. Workers read/stage; coordinators
// decide publication. Missing or invalid entries rebuild the affected feature.
struct FeatureCache {
  Json keys = Json::object();
  std::function<std::optional<Json>(const std::string&)> load;
  std::function<void(const std::string&,const Json&)> stage;
  Json* diagnostics = nullptr;
};
// OCCT handles never cross the service boundary. No thread-safety guarantee.
class BuiltModel {
public:
  explicit BuiltModel(const Json& model);
  BuiltModel(const Json& model, const FeatureCache& cache);
  // Private-format, exact B-rep snapshots for disposable caches. Restoring still
  // validates shapes; assemblies rederive placements and ownership from saved
  // intent and deserialized compound children. No cached topology IDs are trusted.
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
  // Exact and explicitly sampled process checks. Reports never certify production.
  Json fabrication_review(const Json& options,const std::string& feature_id = "") const;
  // Revalidate evaluated face/edge descriptors uniquely; distances use B-reps,
  // never tessellation, exploded coordinates or enumeration stability.
  Json measure(const Json& query,const Json& evaluated_topology,const std::string& feature_id = "") const;
  // Native planar intersections/material caps. Display offsets only locate
  // the source planes; returned geometry remains in committed source coordinates.
  Json section(const Json& query,const std::string& feature_id = "") const;
  // Exact B-rep hidden-line views / true planar sections. Drawing entities are
  // derived output, not persistent topology references.
  // Optional bounded exact orthographic projection snapshots are for native regression
  // diagnostics only; adapters never expose them as a tool/document contract.
  Json drawing(const Json& spec, Json* exact_projections = nullptr, const std::string& feature_id = "") const;
  void export_file(const std::filesystem::path& path, const std::string& format, const std::string& feature_id = "") const;
  // Resolve robot link/joint frames from the same evaluated assembly and datum
  // convention as the solids. Matrices are row-major with mm translations.
  Json robot_frames(const Json& model, const std::string& feature_id = "") const;
private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
std::string kernel_version();
// Drawing-wide limits (entities, points, examined edges, balloon anchors) for
// views obtained separately, e.g. from per-view projection caches. `view_budgets`
// holds each view's recorded usage ({entities, points, examined_edges}, as returned
// by BuiltModel::drawing in "view_budgets"). Throws limit_exceeded.
void check_drawing_totals(const Json& requested_views, const Json& view_budgets, const std::string& feature_id);
void configure_kernel_logging();
// Message for a caught kernel failure. OCCT may throw with an empty message;
// the OCCT exception type is reported then, so the result is never empty.
std::string kernel_failure_message(const std::exception& failure);
}
