#include <Caracal/Project/ProjectLoader.h>
#include <Caracal/Text/File.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <utility>

namespace Caracal
{
    namespace
    {
        class ParseErrorCollector final : public nlohmann::json_sax<nlohmann::json>
        {
        public:
            std::string message;

            bool null() override { return true; }
            bool boolean(bool) override { return true; }
            bool number_integer(number_integer_t) override { return true; }
            bool number_unsigned(number_unsigned_t) override { return true; }
            bool number_float(number_float_t, const string_t&) override { return true; }
            bool string(string_t&) override { return true; }
            bool binary(binary_t&) override { return true; }
            bool start_object(std::size_t) override { return true; }
            bool key(string_t&) override { return true; }
            bool end_object() override { return true; }
            bool start_array(std::size_t) override { return true; }
            bool end_array() override { return true; }

            bool parse_error(std::size_t, const std::string&, const nlohmann::detail::exception& exception) override
            {
                message = exception.what();
                return false;
            }
        };
    }

    static std::string ParseErrorMessage(std::string_view json)
    {
        ParseErrorCollector collector;
        nlohmann::json::sax_parse(json, &collector);

        std::string_view message = collector.message;
        const auto idEnd = message.find("] ");
        if (idEnd != std::string_view::npos)
            message.remove_prefix(idEnd + 2);

        return std::string(message);
    }

    static ProjectResult Failure(std::string error)
    {
        return { std::nullopt, std::move(error) };
    }

    std::optional<std::string> checkProjectName(std::string_view name)
    {
        if (name.empty())
            return "it is empty";

        for (const char character : name)
        {
            if (static_cast<unsigned char>(character) < 32)
                return "it contains a control character";

            if (std::string_view("<>:\"/\\|?*").find(character) != std::string_view::npos)
                return std::string("it contains '") + character + "'";
        }

        if (name.back() == ' ' || name.back() == '.')
            return "it ends with a space or a dot";

        // Windows reserves these names with any extension, in any case https://learn.microsoft.com/en-us/windows/win32/fileio/naming-a-file
        constexpr std::array<std::string_view, 22> reservedNames{
            "CON", "PRN", "AUX", "NUL",
            "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
            "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9" };
        const auto nameBeforeExtension = name.substr(0, name.find('.'));
        for (const auto reservedName : reservedNames)
        {
            if (nameBeforeExtension.size() != reservedName.size())
                continue;

            bool isReserved = true;
            for (size_t index = 0; index < reservedName.size(); ++index)
            {
                if (std::toupper(static_cast<unsigned char>(nameBeforeExtension[index])) != reservedName[index])
                    isReserved = false;
            }

            if (isReserved)
                return "Windows reserves the name " + std::string(reservedName);
        }

        return std::nullopt;
    }

    static std::filesystem::path NormalizedFolder(const std::string& text)
    {
        auto folder = std::filesystem::path(text).lexically_normal();
        if (folder.has_parent_path() && folder.filename().empty())
            folder = folder.parent_path();

        return folder;
    }

    static bool IsInsideProjectFolder(const std::filesystem::path& folder)
    {
        if (folder.empty() || folder.has_root_path() || folder == ".")
            return false;

        return *folder.begin() != "..";
    }

    static std::string FolderComparisonText(const std::filesystem::path& folder)
    {
        auto text = folder.generic_string();
        for (auto& character : text)
            character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));

        return text;
    }

    static bool IsFolderInside(const std::filesystem::path& folder, const std::filesystem::path& outerFolder)
    {
        return FolderComparisonText(folder).starts_with(FolderComparisonText(outerFolder) + "/");
    }

    static std::optional<std::string> ReadFolders(const nlohmann::json& document, const std::string& fileName, ProjectFolders& folders)
    {
        const std::array<std::pair<std::string_view, std::filesystem::path*>, 3> fields{ {
            { "libraries", &folders.libraries },
            { "temp", &folders.temp },
            { "output", &folders.output } } };

        const auto foldersField = document.find("folders");
        if (foldersField != document.end())
        {
            if (!foldersField->is_object())
                return "\"folders\" in " + fileName + " has to be an object";

            for (const auto& [key, value] : foldersField->items())
            {
                const auto fieldName = "\"folders." + key + "\"";
                const auto field = std::ranges::find(fields, std::string_view(key), &std::pair<std::string_view, std::filesystem::path*>::first);
                if (field == fields.end())
                    return fieldName + " in " + fileName + " is not supported";

                if (!value.is_string())
                    return fieldName + " in " + fileName + " has to be a string";

                const auto folder = NormalizedFolder(value.get<std::string>());
                if (!IsInsideProjectFolder(folder))
                    return fieldName + " in " + fileName + " has to be a folder inside the project folder";

                *field->second = folder;
            }
        }

        for (size_t first = 0; first < fields.size(); ++first)
        {
            for (size_t second = first + 1; second < fields.size(); ++second)
            {
                const auto& [firstName, firstFolder] = fields[first];
                const auto& [secondName, secondFolder] = fields[second];
                const auto firstFieldName = "\"folders." + std::string(firstName) + "\"";
                const auto secondFieldName = "\"folders." + std::string(secondName) + "\"";

                if (FolderComparisonText(*firstFolder) == FolderComparisonText(*secondFolder))
                    return firstFieldName + " and " + secondFieldName + " in " + fileName + " are the same folder";

                if (IsFolderInside(*secondFolder, *firstFolder))
                    return secondFieldName + " in " + fileName + " is inside " + firstFieldName;

                if (IsFolderInside(*firstFolder, *secondFolder))
                    return firstFieldName + " in " + fileName + " is inside " + secondFieldName;
            }
        }

        return std::nullopt;
    }

    ProjectResult loadProject(const std::filesystem::path& projectFilePath)
    {
        const auto fileName = projectFilePath.filename().string();
        const auto json = File::readText(projectFilePath);
        if (!json.has_value())
            return Failure("Could not read " + projectFilePath.string());

        const auto document = nlohmann::json::parse(json.value(), nullptr, false);
        if (document.is_discarded())
            return Failure(fileName + " is not valid JSON, " + ParseErrorMessage(json.value()));

        if (!document.is_object())
            return Failure(fileName + " has to contain an object");

        for (const auto& [key, value] : document.items())
        {
            if (key != "name" && key != "type" && key != "version" && key != "folders")
                return Failure("\"" + key + "\" in " + fileName + " is not supported");
        }

        const auto name = document.find("name");
        if (name == document.end())
            return Failure(fileName + " has no \"name\"");

        if (!name->is_string())
            return Failure("\"name\" in " + fileName + " has to be a string");

        const auto& nameText = name->get_ref<const std::string&>();
        if (const auto problem = checkProjectName(nameText))
            return Failure("\"name\" in " + fileName + " has to be a valid file name, " + problem.value());

        const auto type = document.find("type");
        if (type == document.end())
            return Failure(fileName + " has no \"type\"");

        if (!type->is_string())
            return Failure("\"type\" in " + fileName + " has to be a string");

        const auto& typeText = type->get_ref<const std::string&>();
        if (typeText != "exe")
            return Failure("\"type\" in " + fileName + " is \"" + typeText + "\" but only \"exe\" is supported");

        const auto version = document.find("version");
        if (version == document.end())
            return Failure(fileName + " has no \"version\"");

        if (!version->is_string())
            return Failure("\"version\" in " + fileName + " has to be a string");

        const auto& versionText = version->get_ref<const std::string&>();

        ProjectFolders folders{};
        if (const auto problem = ReadFolders(document, fileName, folders))
            return Failure(problem.value());

        return { Project{ nameText, projectFilePath.parent_path(), ProjectType::Executable, versionText, folders }, {} };
    }

    ProjectResult findProject(
        const std::filesystem::path& workingDirectory,
        const std::optional<std::filesystem::path>& path)
    {
        auto projectPath = workingDirectory;
        if (path.has_value())
            projectPath = workingDirectory / path.value();

        if (std::filesystem::is_directory(projectPath))
        {
            const auto projectFilePath = projectPath / "caracal.json";
            if (!std::filesystem::exists(projectFilePath))
                return Failure("No caracal.json in " + projectPath.string());

            return loadProject(projectFilePath);
        }

        if (!std::filesystem::exists(projectPath))
            return Failure(projectPath.string() + " does not exist");

        if (projectPath.extension() != ".json")
            return Failure(projectPath.string() + " is neither a folder nor a .json file");

        return loadProject(projectPath);
    }
}
