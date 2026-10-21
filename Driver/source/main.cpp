#include <Caracal/Compiler.h>
#include <Caracal/Version.h>
#include <iostream>
#include <string_view>

void printHelp()
{
    std::cout << "Caracal " << CARA_VERSION << "\n";
    std::cout << "\n";
    std::cout << "Usage:\n";
    std::cout << "  caracal <file.cara> [-ast] [-ir] [-llvm]       compiles and runs the resulting <file>.exe\n";
    std::cout << "  caracal help\n";
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

    std::filesystem::path parameterFilePath{};
    Caracal::PrintOptions printOptions{};
    for (int argumentIndex = 1; argumentIndex < argc; ++argumentIndex)
    {
        const auto argument = std::string_view(argv[argumentIndex]);
        if (argument == "-ast")
        {
            printOptions.printParseTree = true;
        }
        else if (argument == "-ir")
        {
            printOptions.printIR = true;
        }
        else if (argument == "-llvm")
        {
            printOptions.printLLVMIR = true;
        }
        else if (argument.starts_with("-"))
        {
            std::cout << "Unknown option " << argument << ". Use \"caracal help\" to see the available options.\n";
            return 1;
        }
        else if (!parameterFilePath.empty())
        {
            std::cout << "Only one file can be compiled at a time, got " << parameterFilePath.string() << " and " << argument << ".\n";
            return 1;
        }
        else
        {
            parameterFilePath = argument;
        }
    }

    if (parameterFilePath.empty())
    {
        std::cout << "No file to compile. Use \"caracal help\" to see the available commands.\n";
        return 1;
    }

    const auto absolutePath = std::filesystem::absolute(parameterFilePath);
    if (!std::filesystem::exists(absolutePath))
    {
        std::cout << "The parameter is not a valid file.\n";
        return 1;
    }

    return Caracal::compileFile(absolutePath, Caracal::DiagnosticOptions{}, printOptions);
}
