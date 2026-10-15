#pragma once
#include "agentcad/storage.hpp"

namespace agentcad {
// Borrowed call context, scoped to the serial Service invocation (screenshots
// use their own thread). It never keeps a session-wide command/design history.
class FailureCallScope {
public:
  FailureCallScope(const std::string& tool,const Json& args);
  ~FailureCallScope();
  FailureCallScope(const FailureCallScope&)=delete;
  FailureCallScope& operator=(const FailureCallScope&)=delete;
  static Json context();
private:
  const std::string& tool_;
  const Json& args_;
  const FailureCallScope* previous_;
};
// Best effort diagnostics must never replace the original modeling error.
Error capture_geometry_failure(const fs::path& workspace,const Json& model,
  const Json& request,const Json& budget,const Error& error);
void validate_failure_arguments(const Json& args);
Json failure_tool_definition();
Json failure_call(const fs::path& workspace,const Json& args);
}
