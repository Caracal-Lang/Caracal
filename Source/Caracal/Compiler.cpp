#include <Caracal/Compiler.h>
#include <Caracal/Compilation.h>
#include <Caracal/Compilation/BuildCommand.h>
#include <Caracal/CompilationContext.h>
#include <Caracal/Debug/IRPrinter.h>
#include <Caracal/Debug/ParseTreePrinter.h>
#include <Caracal/Profiling.h>
#include <Caracal/Project/ProjectSources.h>
#include <Caracal/Semantic/TypeCheckerOptions.h>
#include <Caracal/Text/File.h>
#include <Caracal/Diagnostics/DiagnosticsBag.h>
#include <Caracal/Diagnostics/DiagnosticPrinter.h>
#include <Caracal/CodeGen/LLVMCodeGenerator.h>

#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Program.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/xxhash.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <format>
#include <iostream>

namespace Caracal
{
    static int RunExecutable(const std::filesystem::path& executablePath)
    {
        const auto absolutePath = std::filesystem::absolute(executablePath).string();

        llvm::outs().flush();
        std::cout.flush();

        std::string runError;
        const auto exitCode = llvm::sys::ExecuteAndWait(absolutePath, { absolutePath }, std::nullopt, {}, 0, 0, &runError);
        if (!runError.empty())
        {
            std::cout << "Could not run " << absolutePath << ", " << runError << "\n";
            return 1;
        }

        return exitCode;
    }

    static std::vector<std::filesystem::path> CollectCaraFiles(
        const std::filesystem::path& directoryPath) noexcept
    {
        std::vector<std::filesystem::path> caraFilePaths{};

        if (!std::filesystem::exists(directoryPath) || !std::filesystem::is_directory(directoryPath))
            return caraFilePaths;

        for (const auto& file : std::filesystem::recursive_directory_iterator(directoryPath))
        {
            if (!file.is_regular_file() || file.path().extension() != ".cara")
                continue;

            // the prelude is loaded separately through loadPrelude
            if (file.path().parent_path().filename() == "Prelude")
                continue;

            caraFilePaths.push_back(file.path());
        }

        std::ranges::sort(caraFilePaths);
        return caraFilePaths;
    }

    std::filesystem::path compilerDirectory()
    {
        const auto executablePath = llvm::sys::fs::getMainExecutable(nullptr, nullptr);
        if (executablePath.empty())
            return {};

        return std::filesystem::path(executablePath).parent_path();
    }

    static std::filesystem::path ResolveCoreDirectory()
    {
        const auto directory = compilerDirectory();
        if (directory.empty())
        {
            std::cout << "Warning: could not determine the compiler location, compiling without the Core directory\n";
            return {};
        }

        auto coreDirectory = directory / "Core";
        if (!std::filesystem::exists(coreDirectory))
        {
            std::cout << "Warning: no Core directory found next to the compiler, compiling without it\n";
        }

        return coreDirectory;
    }

    static std::filesystem::path ResolvePreludeDirectory()
    {
        const auto directory = compilerDirectory();
        if (directory.empty())
        {
            std::cout << "Warning: could not determine the compiler location, compiling without the Prelude directory\n";
            return {};
        }

        auto preludeDirectory = directory / "Prelude";
        if (!std::filesystem::exists(preludeDirectory))
        {
            std::cout << "Warning: no Prelude directory found next to the compiler, compiling without it\n";
        }

        return preludeDirectory;
    }

    // catches malformed IR before it is emitted, verifyModule returns true when broken
    static bool VerifyModule(llvm::Module& llvmModule)
    {
        CARACAL_ZONE_NAMED("verify");
        return !llvm::verifyModule(llvmModule, &llvm::errs());
    }

    static bool GenerateModule(Module& irModule, llvm::Module& llvmModule)
    {
        LLVMCodeGenerator codeGenerator{ irModule, llvmModule };
        if (!codeGenerator.generate())
            return false;

        return VerifyModule(llvmModule);
    }

    static bool RunFrontEnd(
        const BuildCommand& buildCommand,
        CompilationContext& compilationContext,
        Module& irModule)
    {
        Caracal::DiagnosticsBag diagnostics;
        for (const auto& libraryDirectory : buildCommand.libraryDirectories)
        {
            for (const auto& caraFilePath : CollectCaraFiles(libraryDirectory))
            {
                auto content = Caracal::File::readText(caraFilePath);
                if (!content.has_value())
                {
                    std::cout << "Failed to read file: " << caraFilePath << "\n";
                    continue;
                }

                compilationContext.addSource(std::move(content.value()), caraFilePath, Caracal::UnitOrigin::Core);
            }
        }

        for (const auto& sourceFilePath : buildCommand.sourceFiles)
        {
            auto fileContent = Caracal::File::readText(sourceFilePath);
            if (!fileContent.has_value())
            {
                std::cout << "Failed to read the file content.\n";
                return false;
            }

            compilationContext.addSource(std::move(fileContent.value()), sourceFilePath, Caracal::UnitOrigin::User);
        }

        Caracal::lexAndParse(compilationContext, diagnostics);

        if (diagnostics.hasErrors())
        {
            Caracal::writeDiagnostics(std::cout, diagnostics, buildCommand.diagnosticOptions);
            return false;
        }

        auto preludeSources = Caracal::collectPreludeSources(buildCommand.preludeDirectory);
        if (preludeSources.empty())
            std::cout << "Warning: no prelude found!! operators will not type check!!\n";

        Caracal::loadPrelude(compilationContext, preludeSources);

        auto wasSuccessful = typeCheck(compilationContext, diagnostics, irModule);
        if (!wasSuccessful)
        {
            if (diagnostics.diagnostics().empty())
            {
                std::cout << "internal error: this program type checks but the compiler could not lower it\n";
            }
            Caracal::writeDiagnostics(std::cout, diagnostics, buildCommand.diagnosticOptions);
            return false;
        }

        optimize(irModule);

        if (!diagnostics.diagnostics().empty())
        {
            Caracal::writeDiagnostics(std::cout, diagnostics, buildCommand.diagnosticOptions);
        }

        return true;
    }

    static std::unique_ptr<llvm::TargetMachine> CreateTargetMachine()
    {
        auto defaultTargetTriple = llvm::sys::getDefaultTargetTriple();
        std::string targetError;
        auto target = llvm::TargetRegistry::lookupTarget(defaultTargetTriple, targetError);
        auto triple = llvm::Triple(defaultTargetTriple);
        if (!target)
        {
            llvm::errs() << "Error: " << targetError << "\n";
            return nullptr;
        }

        auto CPU = "generic";
        auto features = "";
        llvm::TargetOptions targetOptions{};
        auto relocModel = std::optional<llvm::Reloc::Model>();
        return std::unique_ptr<llvm::TargetMachine>(
            target->createTargetMachine(triple, CPU, features, targetOptions, relocModel, std::nullopt, llvm::CodeGenOptLevel::None));
    }

    static bool GenerateLLVMModule(Module& irModule, const llvm::TargetMachine& targetMachine, llvm::Module& llvmModule)
    {
        llvmModule.setDataLayout(targetMachine.createDataLayout());
        llvmModule.setTargetTriple(targetMachine.getTargetTriple());

        if (!GenerateModule(irModule, llvmModule))
        {
            std::cout << "Module not generated!";
            return false;
        }

        return true;
    }

    static void PrintParseTrees(CompilationContext& compilationContext)
    {
        for (const auto& unit : compilationContext.units())
        {
            if (unit.origin != UnitOrigin::User)
                continue;

            std::cout << prettyPrint(
                unit.nodeBuffer,
                compilationContext.tokenBuffer(unit.fileId),
                &compilationContext.semanticContext(),
                unit.fileId);
        }
    }

    static void PrintLLVMModule(const llvm::Module& llvmModule)
    {
        std::string llvmIR;
        llvm::raw_string_ostream llvmIRStream(llvmIR);
        llvmModule.print(llvmIRStream, nullptr);
        llvmIRStream.flush();
        std::cout << llvmIR;
    }

    static bool EmitObjectFile(const BuildCommand& buildCommand, llvm::TargetMachine& targetMachine, llvm::Module& llvmModule)
    {
        std::error_code errorCode;
        llvm::raw_fd_ostream objectFile(buildCommand.objectFilePath.string(), errorCode, llvm::sys::fs::OF_None);
        if (errorCode)
        {
            llvm::errs() << "Couldnt create object file: " << errorCode.message() << "\n";
            return false;
        }

        llvm::legacy::PassManager passManager;
        if (targetMachine.addPassesToEmitFile(passManager, objectFile, nullptr, llvm::CodeGenFileType::ObjectFile))
        {
            llvm::errs() << "TargetMachine cant emit a file of this type\n";
            return false;
        }
        {
            CARACAL_ZONE_NAMED("emitObject");
            passManager.run(llvmModule);
        }
        objectFile.flush();
        return true;
    }

    static llvm::ErrorOr<std::string> FindLinker()
    {
        const auto executablePath = llvm::sys::fs::getMainExecutable(nullptr, nullptr);
        if (!executablePath.empty())
        {
            const auto compilerDirectory = std::filesystem::path(executablePath).parent_path().string();
            auto bundledLinker = llvm::sys::findProgramByName("lld-link", { compilerDirectory });
            if (bundledLinker)
                return bundledLinker;
        }

        return llvm::sys::findProgramByName("lld-link");
    }

    static bool LinkExecutable(const BuildCommand& buildCommand)
    {
        const auto lldPathOrError = FindLinker();
        if (!lldPathOrError)
        {
            llvm::errs() << "Error: lld-link not found next to the compiler or in PATH.\n";
            return false;
        }
        const auto& lldPath = lldPathOrError.get();

        // TODO support linux
        std::string linkError;
        const auto exeFileName = buildCommand.executablePath.string();
        const auto objectFileName = buildCommand.objectFilePath.string();
        CARACAL_ZONE_NAMED_VAR(linkZone, "link");
        const auto linkingResult = llvm::sys::ExecuteAndWait(
            lldPath,
            { "lld-link", "/out:" + exeFileName, objectFileName, "/defaultlib:msvcrt.lib", "/defaultlib:legacy_stdio_definitions.lib", "/defaultlib:ucrt.lib" },
            std::nullopt,
            {},
            0,
            0,
            &linkError
        );

        if (linkingResult != 0)
        {
            llvm::errs() << "Linking failed with error code: " << linkingResult << "\n";
            llvm::errs() << "Error message: " << linkError << "\n";
            return false;
        }
        else
        {
            llvm::outs() << "Executable generated: " << exeFileName << "\n\n";
        }

        return true;
    }

    static bool CreateOutputDirectories(const BuildCommand& buildCommand)
    {
        for (const auto& outputPath : { buildCommand.objectFilePath, buildCommand.executablePath })
        {
            const auto directory = outputPath.parent_path();
            if (directory.empty())
                continue;

            std::error_code errorCode;
            std::filesystem::create_directories(directory, errorCode);
            if (errorCode)
            {
                std::cout << "Could not create " << directory.string() << ", " << errorCode.message() << "\n";
                return false;
            }
        }

        return true;
    }

    static int Build(const BuildCommand& buildCommand)
    {
        Caracal::CompilationContext compilationContext{ buildCommand.typeCheckerOptions };
        Module irModule{};
        if (!RunFrontEnd(buildCommand, compilationContext, irModule))
            return 1;

        const auto& printOptions = buildCommand.printOptions;
        if (printOptions.printParseTree)
            PrintParseTrees(compilationContext);

        if (printOptions.printIR)
        {
            IRPrinter printer{ irModule };
            std::cout << printer.prettyPrint();
        }

        const auto isPrintOnly = printOptions.printParseTree || printOptions.printIR || printOptions.printLLVMIR;
        if (isPrintOnly && !printOptions.printLLVMIR)
            return 0;

        llvm::InitializeNativeTarget();
        llvm::InitializeNativeTargetAsmPrinter();

        llvm::LLVMContext context;
        auto llvmModule = std::make_unique<llvm::Module>("CaracalModule", context);

        if (!isPrintOnly)
            llvm::outs() << "TargetTriple: " << llvm::sys::getDefaultTargetTriple() << '\n';

        const auto targetMachine = CreateTargetMachine();
        if (!targetMachine)
            return 1;

        if (!GenerateLLVMModule(irModule, *targetMachine, *llvmModule))
            return 1;

        if (printOptions.printLLVMIR)
        {
            PrintLLVMModule(*llvmModule);
            return 0;
        }

        if (!CreateOutputDirectories(buildCommand))
            return 1;

        if (!EmitObjectFile(buildCommand, *targetMachine, *llvmModule))
            return 1;

        if (!LinkExecutable(buildCommand))
            return 1;

        if (!buildCommand.shouldRunAfterBuild)
            return 0;

        return RunExecutable(buildCommand.executablePath);
    }

    static BuildCommand CreateBuildCommand(
        std::vector<std::filesystem::path> sourceFiles,
        std::filesystem::path preludeDirectory,
        std::vector<std::filesystem::path> libraryDirectories,
        std::filesystem::path objectFilePath,
        std::filesystem::path executablePath,
        const DiagnosticOptions& diagnosticOptions,
        const PrintOptions& printOptions,
        bool shouldRunAfterBuild)
    {
        return BuildCommand{
            .sourceFiles = std::move(sourceFiles),
            .preludeDirectory = std::move(preludeDirectory),
            .libraryDirectories = std::move(libraryDirectories),
            .diagnosticOptions = diagnosticOptions,
            .printOptions = printOptions,
            .objectFilePath = std::move(objectFilePath),
            .executablePath = std::move(executablePath),
            .shouldRunAfterBuild = shouldRunAfterBuild,
        };
    }

    static std::filesystem::path ResolveTemporaryDirectory(const std::filesystem::path& filePath)
    {
        std::error_code errorCode;
        const auto temporaryDirectory = std::filesystem::temp_directory_path(errorCode);
        if (errorCode)
        {
            std::cout << "Warning: could not find the temp folder, writing the object file to the current folder\n";
            return {};
        }

        const auto absolutePath = std::filesystem::absolute(filePath).generic_string();
        const auto pathHash = llvm::xxh3_64bits(llvm::StringRef(absolutePath));
        return temporaryDirectory / "Caracal" / std::format("{}-{:016x}", filePath.stem().string(), pathHash);
    }

    int compileFile(
        const std::filesystem::path& filePath,
        const DiagnosticOptions& diagnosticOptions,
        const PrintOptions& printOptions)
    {
        CARACAL_ZONE_NAMED("compileFile");
        const auto inputFileName = filePath.stem().string();
        const auto buildCommand = CreateBuildCommand(
            { filePath },
            ResolvePreludeDirectory(),
            { ResolveCoreDirectory() },
            ResolveTemporaryDirectory(filePath) / (inputFileName + ".o"),
            inputFileName + ".exe",
            diagnosticOptions,
            printOptions,
            true);
        return Build(buildCommand);
    }

    std::optional<BuildCommand> createProjectBuildCommand(
        const Project& project,
        const DiagnosticOptions& diagnosticOptions,
        bool shouldRunAfterBuild)
    {
        auto sources = collectProjectSources(project);
        if (!sources.error.empty())
        {
            std::cout << sources.error << "\n";
            return std::nullopt;
        }

        if (sources.sourceFiles.empty())
        {
            std::cout << "No .cara files in " << project.rootDirectory.string() << "\n";
            return std::nullopt;
        }

        const auto& folders = project.folders;
        if (project.dependencies.empty())
        {
            return CreateBuildCommand(
                std::move(sources.sourceFiles),
                ResolvePreludeDirectory(),
                { ResolveCoreDirectory() },
                project.rootDirectory / folders.temp / "Debug" / (project.name + ".o"),
                project.rootDirectory / folders.output / "Debug" / (project.name + ".exe"),
                diagnosticOptions,
                {},
                shouldRunAfterBuild);
        }

        const auto librariesDirectory = project.rootDirectory / folders.libraries;
        std::filesystem::path preludeDirectory{};
        std::vector<std::filesystem::path> libraryDirectories{};
        for (const auto& dependency : project.dependencies)
        {
            if (dependency.isPrelude)
            {
                preludeDirectory = librariesDirectory / dependency.path;
                continue;
            }

            libraryDirectories.push_back(librariesDirectory / dependency.path);
        }

        if (preludeDirectory.empty())
            preludeDirectory = ResolvePreludeDirectory();

        return CreateBuildCommand(
            std::move(sources.sourceFiles),
            std::move(preludeDirectory),
            std::move(libraryDirectories),
            project.rootDirectory / folders.temp / "Debug" / (project.name + ".o"),
            project.rootDirectory / folders.output / "Debug" / (project.name + ".exe"),
            diagnosticOptions,
            {},
            shouldRunAfterBuild);
    }

    int buildProject(
        const Project& project,
        const DiagnosticOptions& diagnosticOptions)
    {
        CARACAL_ZONE_NAMED("buildProject");
        const auto buildCommand = createProjectBuildCommand(project, diagnosticOptions);
        if (!buildCommand.has_value())
            return 1;

        return Build(buildCommand.value());
    }

    int runProject(
        const Project& project,
        const DiagnosticOptions& diagnosticOptions)
    {
        CARACAL_ZONE_NAMED("runProject");
        const auto buildCommand = createProjectBuildCommand(project, diagnosticOptions, true);
        if (!buildCommand.has_value())
            return 1;

        return Build(buildCommand.value());
    }

    std::pair<bool, std::string> generateLLVMIRFromModule(Module& irModule, std::string targetTriple)
    {
        llvm::LLVMContext context;
        auto llvmModule = std::make_unique<llvm::Module>("CaracalModule", context);
        llvmModule->setTargetTriple(llvm::Triple(targetTriple));

        if (!GenerateModule(irModule, *llvmModule))
            return std::make_pair(false, std::string{});

        std::string irOutput;
        llvm::raw_string_ostream irStream(irOutput);
        llvmModule->print(irStream, nullptr, true, true);
        irStream.flush();

        return std::make_pair(true, irOutput);
    }
}
