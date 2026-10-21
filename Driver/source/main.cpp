#include <Caracal/Compiler.h>
#include <Caracal/Version.h>
#include <iostream>

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        std::cout << "Caracal " << CARA_VERSION << "\n";
        std::cout << "Please provide a file path as a parameter.\n";
        return 1;
    }

    const auto parameterFilePath = std::filesystem::path(argv[1]);
    const auto absolutePath = std::filesystem::absolute(parameterFilePath);
    if (!std::filesystem::exists(absolutePath))
    {
        std::cout << "The parameter is not a valid file.\n";
        return 1;
    }

    return Caracal::compileFile(absolutePath, Caracal::DiagnosticOptions{});
}
