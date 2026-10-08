#pragma once
#include <string>
#include <filesystem>
#include <functional>

namespace agentcad {
std::string sha256(const std::string& text);
std::string sha256_file(const std::filesystem::path& path, std::size_t max_bytes,
                        const std::function<void()>& checkpoint = {});
}
