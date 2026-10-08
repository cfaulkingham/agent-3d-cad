#pragma once
#include "agentcad/json.hpp"
#include <algorithm>
#include <cmath>

namespace agentcad::test {
// Exact B-rep serialization can change the last integration digits. Compare
// measured geometry numerically; strings, IDs, counts and structure stay exact.
inline bool geometry_equivalent(const Json& left,const Json& right) {
  if(left.is_number()&&right.is_number()) {
    if(left.is_number_integer()&&right.is_number_integer())return left==right;
    const auto a=left.get<double>(),b=right.get<double>();
    return std::isfinite(a)&&std::isfinite(b)&&std::abs(a-b)<=1e-8+1e-10*std::max(std::abs(a),std::abs(b));
  }
  if(left.type()!=right.type()||left.size()!=right.size())return false;
  if(left.is_object()) {
    for(const auto& item:left.items())if(!right.contains(item.key())||!geometry_equivalent(item.value(),right.at(item.key())))return false;
    return true;
  }
  if(left.is_array()) {
    for(std::size_t i=0;i<left.size();++i)if(!geometry_equivalent(left[i],right[i]))return false;
    return true;
  }
  return left==right;
}
}
