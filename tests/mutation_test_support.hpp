#pragma once
#include "agentcad/service.hpp"
#include <stdexcept>

namespace agentcad::test {
// Mutation receipts identify immutable revisions. Fetch source using both fields,
// never HEAD, so a later writer cannot change what the assertion inspects.
inline Json receipt_source(Service& service,const Json& receipt) {
  const auto record=service.call("cad_read",{{"document_id",receipt.at("document_id")},{"revision",receipt.at("revision")}});
  for(const auto* key:{"document_id","revision","kernel_version"})
    if(record.at(key)!=receipt.at(key))throw std::runtime_error(std::string("Receipt/read mismatch: ")+key);
  return record.at("model");
}
}
