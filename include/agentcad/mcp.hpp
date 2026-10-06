#pragma once
#include "agentcad/service.hpp"
#include <istream>
#include <ostream>
#include <optional>

namespace agentcad {
class McpSession {
public:
  explicit McpSession(Service& service) : service_(service) {}
  std::optional<Json> handle(const Json& request);
private:
  Service& service_;
  enum class State { fresh, initializing, ready } state_ = State::fresh;
};
void serve(Service& service, std::istream& input, std::ostream& output);
}
