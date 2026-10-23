#include <CaraTest.h>
#include <Caracal/Project/Project.h>
#include <Caracal/Project/ProjectCreator.h>
#include <Caracal/Project/ProjectLoader.h>
#include <Caracal/Project/ProjectSources.h>
#include <Caracal/Text/File.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <tuple>
#include <vector>

static std::filesystem::path ProjectsDataDirectory()
{
    return std::filesystem::absolute(std::filesystem::path(__FILE__).parent_path() / "../../TestData/Projects");
}

static std::filesystem::path FixtureProjectFile(const std::string& fixtureName)
{
    return ProjectsDataDirectory() / fixtureName / "caracal.json";
}

static std::filesystem::path EmptyScratchDirectory(const std::string& name)
{
    const auto directory = std::filesystem::temp_directory_path() / "CaracalProjectTests" / name;
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    return directory;
}

static std::string ReadText(const std::filesystem::path& filePath)
{
    return Caracal::File::readText(filePath).value_or("<missing>");
}

// sorted and separated by spaces, "a.cara b.exe"
static std::string FileNamesIn(const std::filesystem::path& directory)
{
    std::vector<std::string> fileNames{};
    for (const auto& entry : std::filesystem::directory_iterator(directory))
        fileNames.push_back(entry.path().filename().string());

    std::ranges::sort(fileNames);

    std::string joinedNames{};
    for (const auto& fileName : fileNames)
    {
        if (!joinedNames.empty())
            joinedNames += " ";

        joinedNames += fileName;
    }

    return joinedNames;
}

static void CreatingProjectWithNameUsesThatName()
{
    const auto workingDirectory = EmptyScratchDirectory("NamedProject");
    const auto result = Caracal::createProject(workingDirectory, std::string("Hello"));

    CaraTest::isTrue(result.project.has_value());
    const auto& project = result.project.value();
    CaraTest::areEqual(std::string("Hello"), project.name);
    CaraTest::areEqual(workingDirectory / "Hello", project.rootDirectory);
    CaraTest::areEqual(std::string(".gitignore caracal.json main.cara"), FileNamesIn(workingDirectory / "Hello"));

    for (const auto& fileName : { ".gitignore", "caracal.json", "main.cara" })
    {
        const auto text = ReadText(workingDirectory / "Hello" / fileName);

        CaraTest::isTrue(text.find('\r') == std::string::npos);
        CaraTest::equalsFile(ProjectsDataDirectory() / "NewProject" / fileName, text);
    }
}

static void CreatingProjectWithoutNameUsesNameOfCurrentFolder()
{
    const auto workingDirectory = EmptyScratchDirectory("Greeter");
    const auto result = Caracal::createProject(workingDirectory, std::nullopt);

    CaraTest::isTrue(result.project.has_value());
    const auto& project = result.project.value();
    CaraTest::areEqual(std::string("Greeter"), project.name);
    CaraTest::areEqual(workingDirectory, project.rootDirectory);
    CaraTest::areEqual(std::string("{\n  \"name\": \"Greeter\"\n}\n"), ReadText(workingDirectory / "caracal.json"));
}

static void CreatingProjectInFolderWithCaracalJsonChangesNothing()
{
    const auto workingDirectory = EmptyScratchDirectory("Occupied");
    Caracal::File::writeText(workingDirectory / "caracal.json", "{ \"name\": \"Old\" }\n");
    Caracal::File::writeText(workingDirectory / "main.cara", "old main\n");

    const auto result = Caracal::createProject(workingDirectory, std::nullopt);

    CaraTest::isFalse(result.project.has_value());
    CaraTest::areEqual(workingDirectory.string() + " already has a caracal.json", result.error);
    CaraTest::areEqual(std::string("{ \"name\": \"Old\" }\n"), ReadText(workingDirectory / "caracal.json"));
    CaraTest::areEqual(std::string("old main\n"), ReadText(workingDirectory / "main.cara"));
    CaraTest::isFalse(std::filesystem::exists(workingDirectory / ".gitignore"));
}

static void CreatingProjectKeepsMainCaraAndGitIgnoreItFinds()
{
    const auto workingDirectory = EmptyScratchDirectory("Cloned");
    Caracal::File::writeText(workingDirectory / "README.md", "# Cloned\n");
    Caracal::File::writeText(workingDirectory / ".gitignore", "*.log\n");
    Caracal::File::writeText(workingDirectory / "main.cara", "existing main\n");

    const auto result = Caracal::createProject(workingDirectory, std::nullopt);

    CaraTest::isTrue(result.project.has_value());
    CaraTest::areEqual(std::string("*.log\n"), ReadText(workingDirectory / ".gitignore"));
    CaraTest::areEqual(std::string("existing main\n"), ReadText(workingDirectory / "main.cara"));
    CaraTest::areEqual(std::string("# Cloned\n"), ReadText(workingDirectory / "README.md"));
    CaraTest::areEqual(std::string("{\n  \"name\": \"Cloned\"\n}\n"), ReadText(workingDirectory / "caracal.json"));
}

static void CreatingProjectWithNameThatCannotBeFileNameCreatesNoFolder()
{
    const auto workingDirectory = EmptyScratchDirectory("BadName");
    const auto result = Caracal::createProject(workingDirectory, std::string("Hello/World"));

    CaraTest::isFalse(result.project.has_value());
    CaraTest::areEqual(std::string("\"Hello/World\" cannot be a project name because it contains '/'"), result.error);
    CaraTest::isTrue(std::filesystem::is_empty(workingDirectory));
}

static void FindingProjectDoesNotLookInParentFolder()
{
    const auto workingDirectory = EmptyScratchDirectory("WithSubfolder");
    Caracal::File::writeText(workingDirectory / "caracal.json", "{ \"name\": \"Outer\" }\n");
    std::filesystem::create_directories(workingDirectory / "Sub");

    const auto result = Caracal::findProject(workingDirectory / "Sub", std::nullopt);

    CaraTest::isFalse(result.project.has_value());
    CaraTest::areEqual("No caracal.json in " + (workingDirectory / "Sub").string(), result.error);
}

static void FindingProjectFromPathToJsonFileLoadsThatFile()
{
    const auto workingDirectory = EmptyScratchDirectory("JsonFileGiven");
    Caracal::File::writeText(workingDirectory / "other.json", "{ \"name\": \"Other\" }\n");

    const auto result = Caracal::findProject(workingDirectory, std::filesystem::path("other.json"));

    CaraTest::isTrue(result.project.has_value());
    const auto& project = result.project.value();
    CaraTest::areEqual(std::string("Other"), project.name);
    CaraTest::areEqual(workingDirectory, project.rootDirectory);
}

static void LoadingCaracalJsonWithNameThatCannotBeFileNameFails()
{
    const auto result = Caracal::loadProject(FixtureProjectFile("InvalidName"));

    CaraTest::isFalse(result.project.has_value());
    CaraTest::areEqual(std::string("\"name\" in caracal.json has to be a valid file name, it contains '/'"), result.error);
}

static void LoadingCaracalJsonThatIsNotValidJsonReportsLineAndColumn()
{
    const auto result = Caracal::loadProject(FixtureProjectFile("InvalidJson"));

    CaraTest::isFalse(result.project.has_value());
    CaraTest::isTrue(result.error.starts_with("caracal.json is not valid JSON, parse error at line 3, column 1"));
}

static std::vector<std::string> RelativeSourcePaths(const Caracal::Project& project)
{
    const auto sources = Caracal::collectProjectSources(project);
    CaraTest::areEqual(std::string(), sources.error);

    std::vector<std::string> relativePaths{};
    for (const auto& sourceFile : sources.sourceFiles)
        relativePaths.push_back(sourceFile.lexically_relative(project.rootDirectory).generic_string());

    return relativePaths;
}

static void CollectingSourcesSkipsNestedProject()
{
    const auto workingDirectory = EmptyScratchDirectory("Nested");
    Caracal::File::writeText(workingDirectory / "main.cara", "");
    std::filesystem::create_directories(workingDirectory / "Inner" / "Deeper");
    Caracal::File::writeText(workingDirectory / "Inner" / "caracal.json", "{ \"name\": \"Inner\" }\n");
    Caracal::File::writeText(workingDirectory / "Inner" / "inner.cara", "");
    Caracal::File::writeText(workingDirectory / "Inner" / "Deeper" / "deeper.cara", "");

    const auto relativePaths = RelativeSourcePaths(Caracal::Project{ "Nested", workingDirectory });

    CaraTest::areEqual(size_t(1), relativePaths.size());
    CaraTest::areEqual(std::string("main.cara"), relativePaths.at(0));
}

static void CollectingSourcesSkipsProjectTempAndBuildFolders()
{
    const auto workingDirectory = EmptyScratchDirectory("TempAndBuild");
    Caracal::File::writeText(workingDirectory / "main.cara", "");
    std::filesystem::create_directories(workingDirectory / "Temp");
    std::filesystem::create_directories(workingDirectory / "Build");
    std::filesystem::create_directories(workingDirectory / "Source" / "Build");
    Caracal::File::writeText(workingDirectory / "Temp" / "temp.cara", "");
    Caracal::File::writeText(workingDirectory / "Build" / "build.cara", "");
    Caracal::File::writeText(workingDirectory / "Source" / "Build" / "nested.cara", "");

    const auto relativePaths = RelativeSourcePaths(Caracal::Project{ "TempAndBuild", workingDirectory });

    CaraTest::areEqual(size_t(2), relativePaths.size());
    CaraTest::areEqual(std::string("Source/Build/nested.cara"), relativePaths.at(0));
    CaraTest::areEqual(std::string("main.cara"), relativePaths.at(1));
}

static void CollectingSourcesSortsThemByRelativePath()
{
    const auto workingDirectory = EmptyScratchDirectory("Ordered");
    std::filesystem::create_directories(workingDirectory / "a");
    Caracal::File::writeText(workingDirectory / "b.cara", "");
    Caracal::File::writeText(workingDirectory / "a.cara", "");
    Caracal::File::writeText(workingDirectory / "a-b.cara", "");
    Caracal::File::writeText(workingDirectory / "a" / "z.cara", "");
    Caracal::File::writeText(workingDirectory / "notes.txt", "");

    const auto relativePaths = RelativeSourcePaths(Caracal::Project{ "Ordered", workingDirectory });

    CaraTest::areEqual(size_t(4), relativePaths.size());
    CaraTest::areEqual(std::string("a/z.cara"), relativePaths.at(0));
    CaraTest::areEqual(std::string("a-b.cara"), relativePaths.at(1));
    CaraTest::areEqual(std::string("a.cara"), relativePaths.at(2));
    CaraTest::areEqual(std::string("b.cara"), relativePaths.at(3));
}

static void CheckingProjectNameRejectsNamesWindowsCannotUseAsFileName(const std::string& /*testName*/, const std::string& name, const std::string& expectedProblem)
{
    const auto problem = Caracal::checkProjectName(name);

    CaraTest::isTrue(problem.has_value());
    CaraTest::areEqual(expectedProblem, problem.value());
}

static std::vector<std::tuple<std::string, std::string, std::string>> CheckingProjectNameRejectsNamesWindowsCannotUseAsFileName_Data()
{
    return {
        std::make_tuple("empty", "", "it is empty"),
        std::make_tuple("slash", "Hello/World", "it contains '/'"),
        std::make_tuple("backslash", "Hello\\World", "it contains '\\'"),
        std::make_tuple("colon", "C:Hello", "it contains ':'"),
        std::make_tuple("question mark", "Hello?", "it contains '?'"),
        std::make_tuple("control character", "Hello\tWorld", "it contains a control character"),
        std::make_tuple("trailing dot", "Hello.", "it ends with a space or a dot"),
        std::make_tuple("trailing space", "Hello ", "it ends with a space or a dot"),
        std::make_tuple("reserved name", "CON", "Windows reserves the name CON"),
        std::make_tuple("reserved name in lowercase with extension", "lpt1.txt", "Windows reserves the name LPT1"),
    };
}

static void CheckingProjectNameAcceptsNamesWindowsCanUseAsFileName(const std::string& /*testName*/, const std::string& name)
{
    CaraTest::isFalse(Caracal::checkProjectName(name).has_value());
}

static std::vector<std::tuple<std::string, std::string>> CheckingProjectNameAcceptsNamesWindowsCanUseAsFileName_Data()
{
    return {
        std::make_tuple("letters", "Hello"),
        std::make_tuple("space inside", "Hello World"),
        std::make_tuple("dot inside", "Hello.World"),
        std::make_tuple("reserved name as a prefix", "CONSOLE"),
        std::make_tuple("non-ASCII letters", "H\xC3\xA9llo"),
    };
}

static auto tests =
{
    CaraTest::addTest("creating a project with a name uses that name", CreatingProjectWithNameUsesThatName),
    CaraTest::addTest("creating a project without a name uses the name of the current folder", CreatingProjectWithoutNameUsesNameOfCurrentFolder),
    CaraTest::addTest("creating a project in a folder with a caracal.json changes nothing", CreatingProjectInFolderWithCaracalJsonChangesNothing),
    CaraTest::addTest("creating a project keeps the main.cara and .gitignore it finds", CreatingProjectKeepsMainCaraAndGitIgnoreItFinds),
    CaraTest::addTest("creating a project with a name that cannot be a file name creates no folder", CreatingProjectWithNameThatCannotBeFileNameCreatesNoFolder),
    CaraTest::addTest("finding a project does not look in the parent folder", FindingProjectDoesNotLookInParentFolder),
    CaraTest::addTest("finding a project from a path to a .json file loads that file", FindingProjectFromPathToJsonFileLoadsThatFile),
    CaraTest::addTest("loading a caracal.json with a name that cannot be a file name fails", LoadingCaracalJsonWithNameThatCannotBeFileNameFails),
    CaraTest::addTest("loading a caracal.json that is not valid JSON reports the line and column", LoadingCaracalJsonThatIsNotValidJsonReportsLineAndColumn),
    CaraTest::addTest("collecting sources skips a nested project", CollectingSourcesSkipsNestedProject),
    CaraTest::addTest("collecting sources skips the project's Temp and Build folders", CollectingSourcesSkipsProjectTempAndBuildFolders),
    CaraTest::addTest("collecting sources sorts them by relative path", CollectingSourcesSortsThemByRelativePath),
    CaraTest::addTest("checking a project name rejects names Windows cannot use as a file name", CheckingProjectNameRejectsNamesWindowsCannotUseAsFileName, CheckingProjectNameRejectsNamesWindowsCannotUseAsFileName_Data),
    CaraTest::addTest("checking a project name accepts names Windows can use as a file name", CheckingProjectNameAcceptsNamesWindowsCanUseAsFileName, CheckingProjectNameAcceptsNamesWindowsCanUseAsFileName_Data),
};
