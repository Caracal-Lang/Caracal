#include <Caracal/Compiler.h>
#include <Caracal/Version.h>
#include <iostream>
#include <string_view>

void printHelp()
{
    std::cout << "Caracal " << CARA_VERSION << "\n";
    std::cout << "\n";
    std::cout << "Usage:\n";
    std::cout << "  caracal help          shows this help\n";
    std::cout << "  caracal <file.cara>   compiles and runs the resulting <file>.exe\n";
}

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        std::cout << "Caracal " << CARA_VERSION << "\n";
        std::cout << "Use \"caracal help\" to see the available commands.\n";
        return 1;
    }

    if (std::string_view(argv[1]) == "help")
    {
        printHelp();
        return 0;
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
