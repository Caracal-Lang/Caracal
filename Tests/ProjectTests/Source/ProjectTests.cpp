#include <CaraTest.h>
#include <Caracal/Compiler.h>
#include <Caracal/Project/Project.h>
#include <Caracal/Project/ProjectCreator.h>
#include <Caracal/Project/ProjectLoader.h>
#include <Caracal/Project/ProjectSources.h>
#include <Caracal/Text/File.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <string>
#include <tuple>
#include <vector>

static constexpr std::chrono::year_month_day CreationDay{ std::chrono::year(2026), std::chrono::month(10), std::chrono::day(20) };

static std::filesystem::path ProjectsDataDirectory()
{
    return std::filesystem::absolute(std::filesystem::path(__FILE__).parent_path() / "../../TestData/Projects").lexically_normal();
}

static std::filesystem::path RepositoryDirectory()
{
    return std::filesystem::absolute(std::filesystem::path(__FILE__).parent_path() / "../../..").lexically_normal();
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

static Caracal::ProjectResult LoadProjectText(const std::string& scratchName, const std::string& fields)
{
    const auto workingDirectory = EmptyScratchDirectory(scratchName);
    std::filesystem::create_directories(workingDirectory / "Libraries" / "Prelude");
    std::filesystem::create_directories(workingDirectory / "Libraries" / "Core");
    Caracal::File::writeText(
        workingDirectory / "caracal.json",
        "{ \"name\": \"Hello\", \"type\": \"exe\", \"version\": \"2026.10.20\"" + fields + " }\n");
    return Caracal::loadProject(workingDirectory / "caracal.json");
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

// relative to directory, sorted and separated by spaces, "Core/Entrypoint.cara Prelude/Bool.cara"
static std::string RelativeFilePathsIn(const std::filesystem::path& directory)
{
    std::vector<std::string> filePaths{};
    for (const auto& entry : std::filesystem::recursive_directory_iterator(directory))
    {
        if (entry.is_regular_file())
            filePaths.push_back(entry.path().lexically_relative(directory).generic_string());
    }

    std::ranges::sort(filePaths);

    std::string joinedPaths{};
    for (const auto& filePath : filePaths)
    {
        if (!joinedPaths.empty())
            joinedPaths += " ";

        joinedPaths += filePath;
    }

    return joinedPaths;
}

static void CreatingProjectWithNameUsesThatName()
{
    const auto workingDirectory = EmptyScratchDirectory("NamedProject");
    const auto result = Caracal::createProject(workingDirectory, std::string("Hello"), CreationDay, RepositoryDirectory());

    CaraTest::isTrue(result.project.has_value());
    const auto& project = result.project.value();
    CaraTest::areEqual(std::string("Hello"), project.name);
    CaraTest::areEqual(workingDirectory / "Hello", project.rootDirectory);
    CaraTest::areEqual(std::string(".gitignore Libraries caracal.json main.cara"), FileNamesIn(workingDirectory / "Hello"));

    for (const auto& fileName : { ".gitignore", "caracal.json", "main.cara" })
    {
        const auto text = ReadText(workingDirectory / "Hello" / fileName);

        CaraTest::isTrue(text.find('\r') == std::string::npos);
        CaraTest::equalsFile(ProjectsDataDirectory() / "NewProject" / fileName, text);
    }
}

static void CreatingProjectCopiesCompilerCoreIntoLibraries()
{
    const auto workingDirectory = EmptyScratchDirectory("CopiedLibraries");
    const auto result = Caracal::createProject(workingDirectory, std::string("Hello"), CreationDay, RepositoryDirectory());

    CaraTest::areEqual(std::string(), result.error);
    const auto librariesDirectory = workingDirectory / "Hello" / "Libraries";
    CaraTest::isTrue(std::filesystem::is_directory(librariesDirectory));
    CaraTest::areEqual(std::string("Core"), FileNamesIn(librariesDirectory));

    const auto compilerCore = RepositoryDirectory() / "Core";
    const auto projectCore = librariesDirectory / "Core";
    CaraTest::areEqual(RelativeFilePathsIn(compilerCore), RelativeFilePathsIn(projectCore));

    for (const auto& entry : std::filesystem::recursive_directory_iterator(compilerCore))
    {
        if (entry.is_regular_file())
            CaraTest::areEqual(ReadText(entry.path()), ReadText(projectCore / entry.path().lexically_relative(compilerCore)));
    }
}

static void CreatingProjectWithoutCoreNextToCompilerCreatesNothing()
{
    const auto compilerDirectory = EmptyScratchDirectory("CompilerWithoutCore");
    std::filesystem::create_directories(compilerDirectory / "Prelude");
    const auto workingDirectory = EmptyScratchDirectory("NoCore");

    const auto result = Caracal::createProject(workingDirectory, std::string("Hello"), CreationDay, compilerDirectory);

    CaraTest::isFalse(result.project.has_value());
    CaraTest::areEqual("Couldn't create the project, the Core folder in " + compilerDirectory.string() + " is missing", result.error);
    CaraTest::isTrue(std::filesystem::is_empty(workingDirectory));
}

static void CreatingProjectWithoutNameUsesNameOfCurrentFolder()
{
    const auto workingDirectory = EmptyScratchDirectory("Greeter");
    const auto result = Caracal::createProject(workingDirectory, std::nullopt, CreationDay, RepositoryDirectory());

    CaraTest::isTrue(result.project.has_value());
    const auto& project = result.project.value();
    CaraTest::areEqual(std::string("Greeter"), project.name);
    CaraTest::areEqual(workingDirectory, project.rootDirectory);
    CaraTest::isTrue(ReadText(workingDirectory / "caracal.json").starts_with("{\n  \"name\": \"Greeter\",\n"));
}

static void CreatingProjectInFolderWithCaracalJsonChangesNothing()
{
    const auto workingDirectory = EmptyScratchDirectory("Occupied");
    Caracal::File::writeText(workingDirectory / "caracal.json", "{ \"name\": \"Old\" }\n");
    Caracal::File::writeText(workingDirectory / "main.cara", "old main\n");

    const auto result = Caracal::createProject(workingDirectory, std::nullopt, CreationDay, RepositoryDirectory());

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

    const auto result = Caracal::createProject(workingDirectory, std::nullopt, CreationDay, RepositoryDirectory());

    CaraTest::isTrue(result.project.has_value());
    CaraTest::areEqual(std::string("*.log\n"), ReadText(workingDirectory / ".gitignore"));
    CaraTest::areEqual(std::string("existing main\n"), ReadText(workingDirectory / "main.cara"));
    CaraTest::areEqual(std::string("# Cloned\n"), ReadText(workingDirectory / "README.md"));
    CaraTest::isTrue(ReadText(workingDirectory / "caracal.json").starts_with("{\n  \"name\": \"Cloned\",\n"));
}

static void CreatingProjectWithNameThatCannotBeFileNameCreatesNoFolder()
{
    const auto workingDirectory = EmptyScratchDirectory("BadName");
    const auto result = Caracal::createProject(workingDirectory, std::string("Hello/World"), CreationDay, RepositoryDirectory());

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
    Caracal::File::writeText(workingDirectory / "other.json", "{ \"name\": \"Other\", \"type\": \"exe\", \"version\": \"2026.10.20\" }\n");

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

static void LoadingCaracalJsonWithoutRequiredFieldFails(const std::string& /*testName*/, const std::string& fixtureName, const std::string& expectedError)
{
    const auto result = Caracal::loadProject(FixtureProjectFile(fixtureName));

    CaraTest::isFalse(result.project.has_value());
    CaraTest::areEqual(expectedError, result.error);
}

static std::vector<std::tuple<std::string, std::string, std::string>> LoadingCaracalJsonWithoutRequiredFieldFails_Data()
{
    return {
        std::make_tuple("only a name", "NameOnly", "caracal.json has no \"type\""),
        std::make_tuple("no version", "NoVersion", "caracal.json has no \"version\""),
    };
}

static void LoadingCaracalJsonWithVersionThatIsNotStringFails()
{
    const auto result = Caracal::loadProject(FixtureProjectFile("InvalidVersion"));

    CaraTest::isFalse(result.project.has_value());
    CaraTest::areEqual(std::string("\"version\" in caracal.json has to be a string"), result.error);
}

static void LoadingCaracalJsonFillsInFoldersItLeavesOut()
{
    const auto result = LoadProjectText("PartialFolders", R"(, "folders": { "output": "Out" })");

    CaraTest::areEqual(std::string(), result.error);
    const auto& project = result.project.value();
    CaraTest::areEqual(std::filesystem::path("Libraries"), project.folders.libraries);
    CaraTest::areEqual(std::filesystem::path("Temp"), project.folders.temp);
    CaraTest::areEqual(std::filesystem::path("Out"), project.folders.output);
}

static void LoadingCaracalJsonThatCannotWorkFails(const std::string& /*testName*/, const std::string& fields, const std::string& expectedError)
{
    const auto result = LoadProjectText("BadFolders", fields);

    CaraTest::isFalse(result.project.has_value());
    CaraTest::areEqual(expectedError, result.error);
}

static std::vector<std::tuple<std::string, std::string, std::string>> LoadingCaracalJsonWithFolderOutsideProjectFails_Data()
{
    const std::string outside = "\"folders.output\" in caracal.json has to be a folder inside the project folder";
    return {
        std::make_tuple("parent folder", R"(, "folders": { "output": ".." })", outside),
        std::make_tuple("through the parent folder", R"(, "folders": { "output": "Out/../.." })", outside),
        std::make_tuple("absolute path", R"(, "folders": { "output": "/Out" })", outside),
        std::make_tuple("the project folder", R"(, "folders": { "output": "." })", outside),
        std::make_tuple("empty", R"(, "folders": { "output": "" })", outside),
    };
}

static std::vector<std::tuple<std::string, std::string, std::string>> LoadingCaracalJsonWithTwoFoldersThatAreTheSameFails_Data()
{
    const std::string same = "\"folders.temp\" and \"folders.output\" in caracal.json are the same folder";
    return {
        std::make_tuple("same name", R"(, "folders": { "temp": "Out", "output": "Out" })", same),
        std::make_tuple("different case", R"(, "folders": { "temp": "Out", "output": "out" })", same),
        std::make_tuple("trailing slash", R"(, "folders": { "temp": "Out", "output": "Out/" })", same),
        std::make_tuple("leading dot", R"(, "folders": { "temp": "Out", "output": "./Out" })", same),
    };
}

static std::vector<std::tuple<std::string, std::string, std::string>> LoadingCaracalJsonWithFolderInsideAnotherFails_Data()
{
    return {
        std::make_tuple("output inside the default temp", R"(, "folders": { "output": "Temp/Out" })", "\"folders.output\" in caracal.json is inside \"folders.temp\""),
        std::make_tuple("libraries inside output", R"(, "folders": { "libraries": "Output/Libraries" })", "\"folders.libraries\" in caracal.json is inside \"folders.output\""),
    };
}

static std::vector<std::tuple<std::string, std::string, std::string>> LoadingCaracalJsonWithFoldersOfWrongFormFails_Data()
{
    return {
        std::make_tuple("folders not an object", R"(, "folders": "Out")", "\"folders\" in caracal.json has to be an object"),
        std::make_tuple("folder not a string", R"(, "folders": { "output": 1 })", "\"folders.output\" in caracal.json has to be a string"),
        std::make_tuple("unsupported folder", R"(, "folders": { "cache": "Out" })", "\"folders.cache\" in caracal.json is not supported"),
    };
}

static void LoadingCaracalJsonKeepsDependencyOrderAndPreludeFlag()
{
    const auto result = LoadProjectText("Dependencies", R"(, "dependencies": [ { "path": "Prelude", "prelude": true }, { "path": "Core" } ])");

    CaraTest::areEqual(std::string(), result.error);
    const auto& dependencies = result.project.value().dependencies;
    CaraTest::areEqual(size_t(2), dependencies.size());
    CaraTest::areEqual(std::string("Prelude"), dependencies.at(0).path.generic_string());
    CaraTest::isTrue(dependencies.at(0).isPrelude);
    CaraTest::areEqual(std::string("Core"), dependencies.at(1).path.generic_string());
    CaraTest::isFalse(dependencies.at(1).isPrelude);
}

static void LoadingCaracalJsonWithDependenciesButNoPreludeSucceeds()
{
    const auto result = LoadProjectText("NoPreludeDependency", R"(, "dependencies": [ { "path": "Core" } ])");

    CaraTest::areEqual(std::string(), result.error);
    const auto& dependencies = result.project.value().dependencies;
    CaraTest::areEqual(size_t(1), dependencies.size());
    CaraTest::isFalse(dependencies.at(0).isPrelude);
}

static void LoadingCaracalJsonWithoutDependenciesUsesCompilerLibraries()
{
    const auto result = LoadProjectText("NoDependencies", "");

    CaraTest::areEqual(std::string(), result.error);
    CaraTest::isTrue(result.project.value().dependencies.empty());
}

static std::vector<std::tuple<std::string, std::string, std::string>> LoadingCaracalJsonWithDependenciesThatCannotWorkFails_Data()
{
    return {
        std::make_tuple("not a list", R"(, "dependencies": { "path": "Prelude" })", "\"dependencies\" in caracal.json has to be a list"),
        std::make_tuple("entry not an object", R"(, "dependencies": [ "Prelude" ])", "\"dependencies[0]\" in caracal.json has to be an object"),
        std::make_tuple("entry without a path", R"(, "dependencies": [ { "prelude": true } ])", "\"dependencies[0]\" in caracal.json has no \"path\""),
        std::make_tuple("path not a string", R"(, "dependencies": [ { "path": 1, "prelude": true } ])", "\"dependencies[0].path\" in caracal.json has to be a string"),
        std::make_tuple("prelude not true or false", R"(, "dependencies": [ { "path": "Prelude", "prelude": "yes" } ])", "\"dependencies[0].prelude\" in caracal.json has to be true or false"),
        std::make_tuple("unsupported field", R"(, "dependencies": [ { "path": "Prelude", "prelude": true, "kind": "library" } ])", "\"dependencies[0].kind\" in caracal.json is not supported"),
        std::make_tuple("path outside the libraries folder", R"(, "dependencies": [ { "path": "../Prelude", "prelude": true } ])", "\"dependencies[0].path\" in caracal.json has to be a folder inside \"folders.libraries\""),
        std::make_tuple("path that does not exist", R"(, "dependencies": [ { "path": "Prelude", "prelude": true }, { "path": "Missing" } ])", "\"dependencies[1].path\" in caracal.json is \"Missing\", but Libraries/Missing does not exist"),
        std::make_tuple("two preludes", R"(, "dependencies": [ { "path": "Prelude", "prelude": true }, { "path": "Core", "prelude": true } ])", "\"dependencies[0]\" and \"dependencies[1]\" in caracal.json both have \"prelude\": true"),
    };
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

static void CollectingSourcesSkipsProjectTempAndOutputFolders()
{
    const auto workingDirectory = EmptyScratchDirectory("TempAndOutput");
    Caracal::File::writeText(workingDirectory / "main.cara", "");
    std::filesystem::create_directories(workingDirectory / "Temp");
    std::filesystem::create_directories(workingDirectory / "Output");
    std::filesystem::create_directories(workingDirectory / "Source" / "Output");
    Caracal::File::writeText(workingDirectory / "Temp" / "temp.cara", "");
    Caracal::File::writeText(workingDirectory / "Output" / "output.cara", "");
    Caracal::File::writeText(workingDirectory / "Source" / "Output" / "nested.cara", "");

    const auto relativePaths = RelativeSourcePaths(Caracal::Project{ "TempAndOutput", workingDirectory });

    CaraTest::areEqual(size_t(2), relativePaths.size());
    CaraTest::areEqual(std::string("Source/Output/nested.cara"), relativePaths.at(0));
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

static void CollectingSourcesSkipsFoldersFromCaracalJson()
{
    const auto workingDirectory = EmptyScratchDirectory("MovedFolders");
    Caracal::File::writeText(workingDirectory / "main.cara", "");
    for (const auto* folder : { "Lib", "Obj", "Out", "Temp" })
    {
        std::filesystem::create_directories(workingDirectory / folder);
        Caracal::File::writeText(workingDirectory / folder / "inside.cara", "");
    }

    Caracal::Project project{ "MovedFolders", workingDirectory };
    project.folders = { "Lib", "Obj", "Out" };
    const auto relativePaths = RelativeSourcePaths(project);

    CaraTest::areEqual(size_t(2), relativePaths.size());
    CaraTest::areEqual(std::string("Temp/inside.cara"), relativePaths.at(0));
    CaraTest::areEqual(std::string("main.cara"), relativePaths.at(1));
}

static void CreatingBuildCommandUsesTempAndOutputFoldersFromCaracalJson()
{
    const auto workingDirectory = EmptyScratchDirectory("BuildCommandFolders");
    Caracal::File::writeText(workingDirectory / "main.cara", "");

    Caracal::Project project{ "Hello", workingDirectory };
    project.folders = { "Libraries", "Obj", "Out" };
    const auto buildCommand = Caracal::createProjectBuildCommand(project);

    CaraTest::isTrue(buildCommand.has_value());
    CaraTest::areEqual(workingDirectory / "Obj" / "Debug" / "Hello.o", buildCommand->objectFilePath);
    CaraTest::areEqual(workingDirectory / "Out" / "Debug" / "Hello.exe", buildCommand->executablePath);
}

static void CreatingBuildCommandUsesProjectPreludeAndLibraries()
{
    const auto workingDirectory = EmptyScratchDirectory("BuildCommandLibraries");
    Caracal::File::writeText(workingDirectory / "main.cara", "");

    Caracal::Project project{ "Hello", workingDirectory };
    project.dependencies = { { "Prelude", true }, { "Core", false } };
    const auto buildCommand = Caracal::createProjectBuildCommand(project);

    CaraTest::isTrue(buildCommand.has_value());
    CaraTest::areEqual((workingDirectory / "Libraries" / "Prelude").string(), buildCommand->preludeDirectory.string());
    CaraTest::areEqual(size_t(1), buildCommand->libraryDirectories.size());
    CaraTest::areEqual((workingDirectory / "Libraries" / "Core").string(), buildCommand->libraryDirectories.at(0).string());
}

static void CreatingBuildCommandWithoutPreludeUsesCompilerPrelude()
{
    const auto workingDirectory = EmptyScratchDirectory("BuildCommandCompilerPrelude");
    Caracal::File::writeText(workingDirectory / "main.cara", "");

    Caracal::Project project{ "Hello", workingDirectory };
    project.dependencies = { { "Core", false } };
    const auto buildCommand = Caracal::createProjectBuildCommand(project);

    CaraTest::isTrue(buildCommand.has_value());
    CaraTest::areEqual((Caracal::compilerDirectory() / "Prelude").string(), buildCommand->preludeDirectory.string());
    CaraTest::areEqual(size_t(1), buildCommand->libraryDirectories.size());
    CaraTest::areEqual((workingDirectory / "Libraries" / "Core").string(), buildCommand->libraryDirectories.at(0).string());
}

static void CreatingBuildCommandWithoutDependenciesUsesCompilerLibraries()
{
    const auto workingDirectory = EmptyScratchDirectory("BuildCommandCompilerLibraries");
    Caracal::File::writeText(workingDirectory / "main.cara", "");

    const auto buildCommand = Caracal::createProjectBuildCommand(Caracal::Project{ "Hello", workingDirectory });

    CaraTest::isTrue(buildCommand.has_value());
    CaraTest::areEqual((Caracal::compilerDirectory() / "Prelude").string(), buildCommand->preludeDirectory.string());
    CaraTest::areEqual(size_t(1), buildCommand->libraryDirectories.size());
    CaraTest::areEqual((Caracal::compilerDirectory() / "Core").string(), buildCommand->libraryDirectories.at(0).string());
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
    CaraTest::addTest("creating a project copies the compiler's core into libraries", CreatingProjectCopiesCompilerCoreIntoLibraries),
    CaraTest::addTest("creating a project without core next to the compiler creates nothing", CreatingProjectWithoutCoreNextToCompilerCreatesNothing),
    CaraTest::addTest("creating a project without a name uses the name of the current folder", CreatingProjectWithoutNameUsesNameOfCurrentFolder),
    CaraTest::addTest("creating a project in a folder with a caracal.json changes nothing", CreatingProjectInFolderWithCaracalJsonChangesNothing),
    CaraTest::addTest("creating a project keeps the main.cara and .gitignore it finds", CreatingProjectKeepsMainCaraAndGitIgnoreItFinds),
    CaraTest::addTest("creating a project with a name that cannot be a file name creates no folder", CreatingProjectWithNameThatCannotBeFileNameCreatesNoFolder),
    CaraTest::addTest("finding a project does not look in the parent folder", FindingProjectDoesNotLookInParentFolder),
    CaraTest::addTest("finding a project from a path to a .json file loads that file", FindingProjectFromPathToJsonFileLoadsThatFile),
    CaraTest::addTest("loading a caracal.json with a name that cannot be a file name fails", LoadingCaracalJsonWithNameThatCannotBeFileNameFails),
    CaraTest::addTest("loading a caracal.json without a type or version fails", LoadingCaracalJsonWithoutRequiredFieldFails, LoadingCaracalJsonWithoutRequiredFieldFails_Data),
    CaraTest::addTest("loading a caracal.json with a version that is not a string fails", LoadingCaracalJsonWithVersionThatIsNotStringFails),
    CaraTest::addTest("loading a caracal.json fills in the folders it leaves out", LoadingCaracalJsonFillsInFoldersItLeavesOut),
    CaraTest::addTest("loading a caracal.json with a folder outside the project fails", LoadingCaracalJsonThatCannotWorkFails, LoadingCaracalJsonWithFolderOutsideProjectFails_Data),
    CaraTest::addTest("loading a caracal.json with two folders that are the same fails", LoadingCaracalJsonThatCannotWorkFails, LoadingCaracalJsonWithTwoFoldersThatAreTheSameFails_Data),
    CaraTest::addTest("loading a caracal.json with a folder inside another fails", LoadingCaracalJsonThatCannotWorkFails, LoadingCaracalJsonWithFolderInsideAnotherFails_Data),
    CaraTest::addTest("loading a caracal.json with folders of the wrong form fails", LoadingCaracalJsonThatCannotWorkFails, LoadingCaracalJsonWithFoldersOfWrongFormFails_Data),
    CaraTest::addTest("loading a caracal.json keeps dependency order and the prelude flag", LoadingCaracalJsonKeepsDependencyOrderAndPreludeFlag),
    CaraTest::addTest("loading a caracal.json with dependencies but no prelude succeeds", LoadingCaracalJsonWithDependenciesButNoPreludeSucceeds),
    CaraTest::addTest("loading a caracal.json without dependencies uses the compiler's libraries", LoadingCaracalJsonWithoutDependenciesUsesCompilerLibraries),
    CaraTest::addTest("loading a caracal.json with dependencies that cannot work fails", LoadingCaracalJsonThatCannotWorkFails, LoadingCaracalJsonWithDependenciesThatCannotWorkFails_Data),
    CaraTest::addTest("loading a caracal.json that is not valid JSON reports the line and column", LoadingCaracalJsonThatIsNotValidJsonReportsLineAndColumn),
    CaraTest::addTest("collecting sources skips a nested project", CollectingSourcesSkipsNestedProject),
    CaraTest::addTest("collecting sources skips the project's Temp and Output folders", CollectingSourcesSkipsProjectTempAndOutputFolders),
    CaraTest::addTest("collecting sources sorts them by relative path", CollectingSourcesSortsThemByRelativePath),
    CaraTest::addTest("collecting sources skips the folders from caracal.json", CollectingSourcesSkipsFoldersFromCaracalJson),
    CaraTest::addTest("creating a build command uses the temp and output folders from caracal.json", CreatingBuildCommandUsesTempAndOutputFoldersFromCaracalJson),
    CaraTest::addTest("creating a build command uses the project's prelude and libraries", CreatingBuildCommandUsesProjectPreludeAndLibraries),
    CaraTest::addTest("creating a build command without a prelude uses the compiler's prelude", CreatingBuildCommandWithoutPreludeUsesCompilerPrelude),
    CaraTest::addTest("creating a build command without dependencies uses the compiler's libraries", CreatingBuildCommandWithoutDependenciesUsesCompilerLibraries),
    CaraTest::addTest("checking a project name rejects names Windows cannot use as a file name", CheckingProjectNameRejectsNamesWindowsCannotUseAsFileName, CheckingProjectNameRejectsNamesWindowsCannotUseAsFileName_Data),
    CaraTest::addTest("checking a project name accepts names Windows can use as a file name", CheckingProjectNameAcceptsNamesWindowsCanUseAsFileName, CheckingProjectNameAcceptsNamesWindowsCanUseAsFileName_Data),
};
