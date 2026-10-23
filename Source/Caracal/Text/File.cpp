#include <Caracal/Text/File.h>
#include <fstream>
#include <sstream>

namespace Caracal::File
{
    std::optional<std::string> readText(const std::filesystem::path& filePath)
    {
        if (!std::filesystem::exists(filePath))
        {
            return std::nullopt;
        }

        std::ifstream file(filePath, std::ios::in | std::ios::binary);
        if (!file.is_open())
        {
            return std::nullopt;
        }

        std::ostringstream contentStream;
        contentStream << file.rdbuf();
        return contentStream.str();
    }

    bool writeText(const std::filesystem::path& filePath, std::string_view content)
    {
        std::ofstream file(filePath, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!file.is_open())
        {
            return false;
        }

        file.write(content.data(), static_cast<std::streamsize>(content.size()));
        return file.good();
    }
}
