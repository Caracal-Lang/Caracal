#pragma once

#include <Caracal/API.h>
#include <filesystem>
#include <string>
#include <string_view>
#include <optional>

namespace Caracal::File
{
    CARACAL_API std::optional<std::string> readText(const std::filesystem::path& filePath);
    CARACAL_API bool writeText(const std::filesystem::path& filePath, std::string_view content);
}
