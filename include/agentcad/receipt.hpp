#pragma once
#include "agentcad/storage.hpp"

namespace agentcad {
// Source deltas and conservative dependency impact; no kernel evaluation.
Json model_changes(const Json& before, const Json& after, const Json& base_revision);
Json changes_schema();
// Projects both current and historical full/compact mutation results. Historical
// source is read only when an older result lacks the new identity or change list.
Json committed_record_receipt(const Store& store, Json record);
}
