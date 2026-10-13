#include "agentcad/help.hpp"
#include "agentcad/model.hpp"
#include "agentcad/service.hpp"
#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <sstream>

namespace agentcad {
namespace {
constexpr std::size_t page_bytes = 12000;
Json object(Json properties, Json required = Json::array()) {
  return {{"type", "object"}, {"properties", properties}, {"required", required}, {"additionalProperties", false}};
}
std::vector<std::string> words(const std::string& text) {
  std::vector<std::string> result;
  std::string word;
  for (unsigned char c : text) {
    if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) word += static_cast<char>(c);
    else if (!word.empty()) { result.push_back(word); word.clear(); }
  }
  if (!word.empty()) result.push_back(word);
  return result;
}
std::string slug(const std::string& text) {
  std::string result;
  for (const auto& word : words(text)) { if (!result.empty()) result += '-'; result += word; }
  return result.empty() ? "overview" : result;
}
struct Entry {
  std::string topic, kind, title, summary, content, source;
  std::size_t line = 1;
  Json schemas = Json::object();
  std::set<std::string> names, body;
};
Json summary(const Entry& entry) {
  return {{"topic", entry.topic}, {"kind", entry.kind}, {"title", entry.title},
          {"summary", entry.summary}, {"source", entry.source}, {"line", entry.line}};
}
// Model definitions use local JSON pointers. Keep their exact transitive closure
// so each returned feature/operation schema is independently usable.
Json standalone(Json schema, const Json& definitions) {
  std::set<std::string> needed;
  const std::function<void(const Json&)> visit = [&](const Json& node) {
    if (node.is_array()) { for (const auto& child : node) visit(child); }
    else if (node.is_object()) for (const auto& item : node.items()) {
      if (item.key() == "$ref" && item.value().is_string()) {
        const auto ref = item.value().get<std::string>();
        const std::string prefix = "#/$defs/";
        if (!ref.starts_with(prefix)) throw Error("internal_error", "Unexpected native help schema reference");
        const auto key = ref.substr(prefix.size());
        if (needed.insert(key).second) visit(definitions.at(key));
      } else visit(item.value());
    }
  };
  visit(schema);
  if (!needed.empty()) for (const auto& key : needed) schema["$defs"][key] = definitions.at(key);
  schema["$schema"] = "https://json-schema.org/draft/2020-12/schema";
  return schema;
}
const std::vector<Entry>& entries() {
  static const auto index = [] {
    std::vector<Entry> result;
    for (const auto& tool : tool_definitions()) {
      const auto name = text_field(tool, "name"), description = text_field(tool, "description");
      Entry e; e.topic = "tool/" + name; e.kind = "tool"; e.title = name;
      e.summary = description; e.content = description; e.source = "native tool_definitions";
      e.schemas = {{"input", tool.at("inputSchema")}, {"output", tool.at("outputSchema")}};
      result.push_back(std::move(e));
    }
    const auto definitions = model_definitions();
    for (const auto* kind : {"feature", "operation"}) {
      const auto discriminator = std::string(kind) == "feature" ? "type" : "op";
      for (const auto& variant : definitions.at(kind).at("oneOf")) {
        const auto& schema = variant.contains("$ref") ? definitions.at(text_field(variant,"$ref").substr(8)) : variant;
        const auto& choice = schema.at("properties").at(discriminator);
        const auto names = choice.contains("const") ? Json::array({choice.at("const")}) : choice.at("enum");
        for (const auto& value : names) {
          const auto name = value.get<std::string>();
          Entry e; e.topic = std::string(kind) + "/" + name; e.kind = kind; e.title = name;
          e.source = "native model_definitions";
          e.summary = std::string("Native ") + kind + ": " + name + ". Required fields: ";
          bool first = true;
          for (const auto& field : schema.at("required")) {
            if (!first) e.summary += ", ";
            e.summary += field.get<std::string>(); first = false;
          }
          e.content = e.summary + ".\nDimensions are millimeters and rotations are degrees unless the field contract says otherwise. "
            "Dependencies name earlier feature IDs. Read related guides for examples, selection requirements and limits. "
            "cad_apply requires the current expected_revision; failed edits preserve the committed model.";
          auto exact = schema;
          exact["properties"][discriminator] = {{"const", name}};
          e.schemas["input"] = standalone(std::move(exact), definitions);
          result.push_back(std::move(e));
        }
      }
    }
    for (const auto& guide : help_guides()) {
      const auto filename = guide.path.substr(5, guide.path.size() - 8);
      const auto base = "guide/" + slug(filename);
      Entry e; e.kind = "guide"; e.source = guide.path; e.topic = base; e.title = filename;
      std::map<std::string, unsigned> duplicates;
      bool fenced = false;
      auto emit = [&] {
        if (e.content.empty()) return;
        e.summary = e.title + " — " + guide.path;
        result.push_back(e);
      };
      std::istringstream stream(guide.text);
      std::string line; std::size_t line_number = 0;
      while (std::getline(stream, line)) {
        ++line_number;
        if (line.starts_with("```") || line.starts_with("~~~")) fenced = !fenced;
        const auto hashes = line.find_first_not_of('#');
        const bool heading = !fenced && hashes > 0 && hashes <= 6 && hashes < line.size() && line[hashes] == ' ';
        if (heading) {
          emit(); e.content.clear(); e.title = line.substr(hashes + 1); e.line = line_number;
          e.topic = hashes == 1 ? base : base + "/" + slug(e.title);
          auto& count = duplicates[e.topic]; if (count++) e.topic += "-" + std::to_string(count);
        }
        e.content += line + '\n';
      }
      emit();
    }
    for (auto& e : result) {
      // A document's directory/category is useful recall, but must not outrank
      // the actual section title (e.g. sheet-metal vs a four-view render sheet).
      const auto name_words = words(e.title + " " + e.kind);
      e.names.insert(name_words.begin(), name_words.end());
      const auto body_words = words(e.topic + " " + e.summary + " " + e.content);
      e.body.insert(body_words.begin(), body_words.end());
    }
    std::sort(result.begin(), result.end(), [](const Entry& a, const Entry& b) { return a.topic < b.topic; });
    return result;
  }();
  return index;
}
std::vector<std::pair<int, const Entry*>> search(const std::string& query) {
  const std::map<std::string, std::vector<std::string>> synonyms = {
    {"round", {"fillet"}}, {"rounded", {"fillet"}}, {"rounding", {"fillet"}},
    {"bevel", {"chamfer"}}, {"hollow", {"shell", "thicken"}}, {"wall", {"shell", "thicken"}},
    {"subtract", {"cut"}}, {"union", {"fuse"}}, {"join", {"fuse"}}, {"drill", {"hole", "holes"}},
    {"spin", {"revolve"}}, {"lathe", {"revolve"}}, {"pipe", {"sweep"}},
    {"repeat", {"pattern"}}, {"array", {"pattern"}}, {"move", {"transform"}},
    {"picture", {"render"}}, {"snapshot", {"render", "screenshot"}}, {"multiview", {"sheet", "render"}},
    {"save", {"export"}}, {"undo", {"restore"}}, {"collision", {"clearance", "interference"}},
    {"select", {"selection", "selector"}}, {"edges", {"edge"}}, {"faces", {"face"}}
  };
  const std::set<std::string> stop = {"a", "an", "the", "to", "of", "and", "how", "do", "i", "with", "my", "part", "model"};
  std::map<std::string, int> wanted;
  for (const auto& word : words(query)) {
    if (stop.contains(word)) continue;
    wanted[word] = 4;
    if (auto found = synonyms.find(word); found != synonyms.end())
      for (const auto& alternative : found->second) wanted[alternative] = std::max(wanted[alternative], 3);
  }
  std::vector<std::pair<int, const Entry*>> hits;
  for (const auto& e : entries()) {
    int score = 0;
    for (const auto& [word, weight] : wanted) {
      if (e.names.contains(word)) score += weight * 10;
      else if (e.body.contains(word)) score += weight;
    }
    if (query == e.title || query == e.topic) score += 1000;
    if (query.empty() || score > 0) hits.emplace_back(score, &e);
  }
  std::stable_sort(hits.begin(), hits.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
  return hits;
}
}
Json help_tool_definition() {
  const Json text = {{"type", "string"}}, integer = {{"type", "integer"}, {"minimum", 0}};
  const auto brief = object({{"topic", text}, {"kind", {{"enum", {"tool", "feature", "operation", "guide"}}}},
    {"title", text}, {"summary", text}, {"source", text}, {"line", {{"type", "integer"}, {"minimum", 1}}}},
    {"topic", "kind", "title", "summary", "source", "line"});
  auto entry = brief;
  entry["properties"].update({{"content", text}, {"offset", integer}, {"next_offset", {{"type", {"integer", "null"}}, {"minimum", 0}}},
    {"schemas", object({{"input", {{"type", "object"}}}, {"output", {{"type", "object"}}}})},
    {"related", {{"type", "array"}, {"maxItems", 6}, {"items", brief}}}});
  for (const auto* field : {"content", "offset", "next_offset", "schemas", "related"}) entry["required"].push_back(field);
  auto input = object({{"query", {{"type", "string"}, {"minLength", 1}, {"maxLength", 256}}},
    {"topic", {{"type", "string"}, {"minLength", 1}, {"maxLength", 256}}},
    {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 20}}},
    {"offset", {{"type", "integer"}, {"minimum", 0}, {"maximum", 9007199254740991ULL}}}});
  input["allOf"] = Json::array({Json{{"not", {{"required", {"query", "topic"}}}}},
    Json{{"if", {{"required", {"offset"}}}}, {"then", {{"required", {"topic"}}}}},
    Json{{"if", {{"required", {"topic"}}}}, {"then", {{"not", {{"required", {"limit"}}}}}}}});
  return {{"name", "cad_help"}, {"description", "Search bundled native modeling help with query (for example round the edges). No arguments lists topics. Read an exact returned topic for guidance, related examples and current standalone input/output schemas. Long guide text uses next_offset byte pagination. No geometry, files or network are accessed."},
    {"inputSchema", input}, {"outputSchema", object({
      {"matches", {{"type", "array"}, {"maxItems", 20}, {"items", brief}}}, {"total", integer}, {"truncated", {{"type", "boolean"}}},
      {"entry", entry}}, {"matches", "total", "truncated"})},
    {"annotations", {{"readOnlyHint", true}, {"destructiveHint", false}, {"openWorldHint", false}}}};
}
void validate_help_arguments(const Json& args) {
  fields(args, {}, {"query", "topic", "limit", "offset"});
  for (const auto* key : {"query", "topic"}) if (args.contains(key)) {
    const auto value = text_field(args, key);
    const auto count = std::count_if(value.begin(), value.end(), [](unsigned char c) { return (c & 0xc0) != 0x80; });
    if (value.empty() || count > 256 || invalid_utf8_offset(value)) throw Error("invalid_argument", "Help text must contain 1–256 UTF-8 characters");
  }
  if ((args.contains("query") && args.contains("topic")) || (args.contains("topic") && args.contains("limit")) ||
      (args.contains("offset") && !args.contains("topic"))) throw Error("invalid_argument", "Use query/limit to search or topic/offset to read help");
  if (args.contains("limit") && (!args.at("limit").is_number_integer() || args.at("limit") < 1 || args.at("limit") > 20))
    throw Error("invalid_argument", "Help search limit must be 1–20");
  if (args.contains("offset") && (!args.at("offset").is_number_integer() || args.at("offset") < 0 || args.at("offset") > 9007199254740991ULL))
    throw Error("invalid_argument", "Help offset must be a nonnegative JSON-safe byte offset");
}
Json native_help(const Json& args) {
  validate_help_arguments(args);
  Json result = {{"matches", Json::array()}, {"total", 0}, {"truncated", false}};
  if (!args.contains("topic")) {
    const auto hits = search(args.value("query", std::string{}));
    const auto limit = args.value("limit", std::size_t(10));
    result["total"] = hits.size(); result["truncated"] = hits.size() > limit;
    for (std::size_t i = 0; i < std::min(limit, hits.size()); ++i) result["matches"].push_back(summary(*hits[i].second));
    return result;
  }
  const auto topic = text_field(args, "topic");
  const auto found = std::find_if(entries().begin(), entries().end(), [&](const Entry& e) { return e.topic == topic; });
  if (found == entries().end()) throw Error("not_found", "Unknown help topic; search cad_help with query first", {{"topic", topic}});
  const auto offset = args.value("offset", std::size_t(0));
  const auto& content = found->content;
  if (offset > content.size() || (offset < content.size() && (static_cast<unsigned char>(content[offset]) & 0xc0) == 0x80))
    throw Error("invalid_argument", "Help offset must be a UTF-8 boundary within this topic");
  auto end = std::min(content.size(), offset + page_bytes);
  while (end < content.size() && (static_cast<unsigned char>(content[end]) & 0xc0) == 0x80) --end;
  auto detail = summary(*found);
  detail.update({{"content", content.substr(offset, end - offset)}, {"offset", offset},
    {"next_offset", end < content.size() ? Json(end) : Json(nullptr)}, {"schemas", found->schemas}, {"related", Json::array()}});
  for (const auto& [score, e] : search(found->title)) {
    (void)score;
    if (e->kind != "guide" || e->topic == topic) continue;
    detail["related"].push_back(summary(*e));
    if (detail["related"].size() == 6) break;
  }
  result["entry"] = std::move(detail); result["total"] = 1;
  result["truncated"] = end < content.size();
  return result;
}
}
