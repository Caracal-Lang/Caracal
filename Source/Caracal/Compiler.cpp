#include <Caracal/Compiler.h>
#include <Caracal/Compilation.h>
#include <Caracal/CompilationContext.h>
#include <Caracal/Profiling.h>
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
#include <llvm/MC/TargetRegistry.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <iostream>

namespace Caracal
{
    static int ExecuteCommand(const std::string& command)
    {
        return std::system(command.c_str());
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

        return caraFilePaths;
    }

    static std::filesystem::path ResolveCoreDirectory()
    {
        const auto executablePath = llvm::sys::fs::getMainExecutable(nullptr, nullptr);
        if (executablePath.empty())
        {
            std::cout << "Warning: could not determine the compiler location, compiling without the Core directory\n";
            return {};
        }

        auto coreDirectory = std::filesystem::path(executablePath).parent_path() / "Core";
        if (!std::filesystem::exists(coreDirectory))
        {
            std::cout << "Warning: no Core directory found next to the compiler, compiling without it\n";
        }

        return coreDirectory;
    }

    static std::filesystem::path ResolvePreludeDirectory()
    {
        const auto executablePath = llvm::sys::fs::getMainExecutable(nullptr, nullptr);
        if (executablePath.empty())
        {
            std::cout << "Warning: could not determine the compiler location, compiling without the Prelude directory\n";
            return {};
        }

        auto preludeDirectory = std::filesystem::path(executablePath).parent_path() / "Prelude";
        if (!std::filesystem::exists(preludeDirectory))
        {
            std::cout << "Warning: no Prelude directory found next to the compiler, compiling without it\n";
        }

        return preludeDirectory;
    }

    static bool GenerateModule(Module& irModule, llvm::Module& llvmModule)
    {
        LLVMCodeGenerator codeGenerator{ irModule, llvmModule };
        if (!codeGenerator.generate())
            return false;

        // catch malformed IR before it is emitted, verifyModule returns true when broken
        CARACAL_ZONE_NAMED("verify");
        if (llvm::verifyModule(llvmModule, &llvm::errs()))
            return false;

        return true;
    }

    int compileFile(
        const std::filesystem::path& filePath,
        const DiagnosticOptions& diagnosticOptions)
    {
        CARACAL_ZONE_NAMED("compileFile");
        const auto coreDirectoryPath = ResolveCoreDirectory();
        auto caraFiles = CollectCaraFiles(coreDirectoryPath);

        Caracal::DiagnosticsBag diagnostics;
        Caracal::CompilationContext compilationContext;
        for (const auto& caraFilePath : caraFiles)
        {
            auto content = Caracal::File::readText(caraFilePath);
            if (!content.has_value())
            {
                std::cout << "Failed to read file: " << caraFilePath << "\n";
                continue;
            }

            compilationContext.addSource(std::move(content.value()), caraFilePath, Caracal::UnitOrigin::Core);
        }

        auto fileContent = Caracal::File::readText(filePath);
        if (!fileContent.has_value())
        {
            std::cout << "Failed to read the file content.\n";
            return 1;
        }

        compilationContext.addSource(std::move(fileContent.value()), filePath, Caracal::UnitOrigin::User);
        Caracal::lexAndParse(compilationContext, diagnostics);

        if (diagnostics.hasErrors())
        {
            Caracal::writeDiagnostics(std::cout, diagnostics, diagnosticOptions);
            return 1;
        }

        auto preludeSources = Caracal::collectPreludeSources(ResolvePreludeDirectory());
        if (preludeSources.empty())
            std::cout << "Warning: no prelude found!! operators will not type check!!\n";

        Caracal::loadPrelude(compilationContext, preludeSources);

        Module irModule{};
        auto wasSuccessful = typeCheck(compilationContext, diagnostics, irModule);
        if (!wasSuccessful)
        {
            if (diagnostics.diagnostics().empty())
            {
                std::cout << "internal error: this program type checks but the compiler could not lower it\n";
            }
            Caracal::writeDiagnostics(std::cout, diagnostics, diagnosticOptions);
            return 1;
        }

        optimize(irModule);

        if (!diagnostics.diagnostics().empty())
        {
            Caracal::writeDiagnostics(std::cout, diagnostics, diagnosticOptions);
        }

        llvm::InitializeNativeTarget();
        llvm::InitializeNativeTargetAsmPrinter();

        llvm::LLVMContext context;
        auto llvmModule = std::make_unique<llvm::Module>("CaracalModule", context);

        auto defaultTargetTriple = llvm::sys::getDefaultTargetTriple();
        llvm::outs() << "TargetTriple: " << defaultTargetTriple << '\n';
        std::string targetError;
        auto target = llvm::TargetRegistry::lookupTarget(defaultTargetTriple, targetError);
        auto triple = llvm::Triple(defaultTargetTriple);
        if (!target)
        {
            llvm::errs() << "Error: " << targetError << "\n";
            return 1;
        }

        auto CPU = "generic";
        auto features = "";
        llvm::TargetOptions targetOptions{};
        auto relocModel = std::optional<llvm::Reloc::Model>();
        auto targetMachine = target->createTargetMachine(triple, CPU, features, targetOptions, relocModel);
        llvmModule->setDataLayout(targetMachine->createDataLayout());
        llvmModule->setTargetTriple(triple);

        wasSuccessful = GenerateModule(irModule, *llvmModule);
        if (!wasSuccessful)
        {
            std::cout << "Module not generated!";
            return 1;
        }

        std::string irOutput;
        llvm::raw_string_ostream irStream(irOutput);
        llvmModule->print(irStream, nullptr);
        irStream.flush();
        llvm::outs() << "Module IR:\n" << irOutput << "\n";

        const auto inputFileName = filePath.stem().string();
        const auto objectFileName = inputFileName + ".o";
        std::error_code errorCode;
        llvm::raw_fd_ostream objectFile(objectFileName, errorCode, llvm::sys::fs::OF_None);
        if (errorCode)
        {
            llvm::errs() << "Couldnt create object file: " << errorCode.message() << "\n";
            return 1;
        }

        llvm::legacy::PassManager passManager;
        if (targetMachine->addPassesToEmitFile(passManager, objectFile, nullptr, llvm::CodeGenFileType::ObjectFile))
        {
            llvm::errs() << "TargetMachine cant emit a file of this type\n";
            return 1;
        }
        {
            CARACAL_ZONE_NAMED("emitObject");
            passManager.run(*llvmModule);
        }
        objectFile.flush();

        const auto lldPathOrError = llvm::sys::findProgramByName("lld-link");
        if (!lldPathOrError)
        {
            llvm::errs() << "Error: lld-link not found in PATH.\n";
            return 1;
        }
        const auto& lldPath = lldPathOrError.get();

        // TODO support linux
        std::string linkError;
        const auto exeFileName = inputFileName + ".exe";
        CARACAL_ZONE_NAMED_VAR(linkZone, "link");
        const auto linkingResult = llvm::sys::ExecuteAndWait(
            lldPath,
            { "lld-link", "-flavor", "link", "/out:" + exeFileName, objectFileName, "/subsystem:console", "/defaultlib:msvcrt.lib", "/defaultlib:legacy_stdio_definitions.lib","/defaultlib:ucrt.lib" },
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
            return 1;
        }
        else
        {
            llvm::outs() << "Executable generated: " << exeFileName << "\n\n";
        }

        const auto exeResult = ExecuteCommand(exeFileName);
        return exeResult;
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
