#pragma once
#include "agentcad/json.hpp"

namespace agentcad {
// Deterministic inventory from editable intent; no geometry evaluation is needed.
// An empty assembly_id selects the model output.
Json build_bom(const Json& model, const std::string& assembly_id = "");
// RFC 4180 quoting, CRLF records, stable columns and semicolon-separated part IDs.
std::string bom_csv(const Json& bom);
}
