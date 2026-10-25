#pragma once

#include <Caracal/API.h>
#include <filesystem>
#include <optional>

namespace Caracal
{
    [[nodiscard]] CARACAL_API std::optional<std::filesystem::path> findParentGitRepository(const std::filesystem::path& directory);
    CARACAL_API bool initializeRepository(const std::filesystem::path& projectDirectory);
}
