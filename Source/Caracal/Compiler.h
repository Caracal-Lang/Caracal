#include <Caracal/API.h>
#include <Caracal/Compilation/PrintOptions.h>
#include <Caracal/Diagnostics/DiagnosticOptions.h>
#include <Caracal/IR/Module.h>
#include <Caracal/Semantic/SemanticContext.h>
#include <filesystem>
#include <string>

namespace Caracal
{
    CARACAL_API int compileFile(
        const std::filesystem::path& filePath,
        const DiagnosticOptions& diagnosticOptions = {},
        const PrintOptions& printOptions = {});

    CARACAL_API std::pair<bool, std::string> generateLLVMIRFromModule(Module& irModule, std::string targetTriple);
};
