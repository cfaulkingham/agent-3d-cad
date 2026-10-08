#pragma once
#include "agentcad/artifact_review.hpp"
#include "agentcad/jobs.hpp"
#include "agentcad/hash.hpp"
#include "tinyxml2.h"
#include <array>
#include <map>
#include <set>
#include <vector>
#include <functional>

namespace agentcad::artifact_detail {
using Vec = std::array<double, 3>;
using Mat = std::array<double, 16>; // row-major, column vectors, millimeters
[[noreturn]] void invalid(const std::string& message);
[[noreturn]] void unsupported(const std::string& message);
void require(bool condition, const std::string& message);
double finite(double value);
double real(const std::string& text);
std::size_t integer(const std::string& text, std::size_t maximum);
std::vector<double> numbers(const std::string& text, std::size_t count);
double unit_scale(const std::string& unit);
std::uint32_t u32(const std::string& bytes, std::size_t offset);
std::uint16_t u16(const std::string& bytes, std::size_t offset);
std::uint32_t crc32(const std::string& bytes);
std::string relative_uri(const std::string& uri, bool directory = false);
Mat identity(); Mat product(const Mat& a, const Mat& b);
Mat translation(Vec xyz); Mat pose(const std::vector<double>& xyzrpy, double scale);
double determinant(const Mat& matrix); Vec transform(const Mat& matrix, Vec point);
void affine(const Mat& matrix);
void safe_xml(tinyxml2::XMLDocument& document, const std::string& bytes);
std::string tag(const tinyxml2::XMLElement* element);
std::string attribute(const tinyxml2::XMLElement* element, const char* key, const std::string& fallback = "");
void children(const tinyxml2::XMLElement* element, const std::set<std::string>& allowed);
struct Geometry {
  std::size_t curve_points=0;
  Json positions=Json::array(), triangles=Json::array(), triangle_groups=Json::array();
  Json polylines=Json::array(), groups=Json::array();
  void mesh(const std::vector<Vec>& vertices, const std::vector<std::array<std::size_t,3>>& indices,
            const Mat& matrix, const std::string& source, Json metadata=Json::object());
  void line(const std::vector<Vec>& vertices, const std::string& source, Json metadata=Json::object());
  Json result() const; Json summary(const std::string& representation) const;
};
struct Reference { std::string uri, bytes, hash, units; fs::path path; };
using References = std::map<std::string, Reference>;
struct Parsed { Geometry geometry; Json metadata=Json::object(), limitations=Json::array(); std::string representation; };
Parsed stl(const std::string& bytes, double scale);
Parsed glb(const std::string& bytes);
Parsed dxf(const std::string& bytes, const std::string& units);
std::map<std::string,std::string> zip(const std::string& bytes);
Parsed three_mf(const std::string& bytes);
Parsed robot(const std::string& bytes, const std::string& format, const References& references);
}
