#include <Caracal/Syntax/StringEscapes.h>

namespace Caracal
{
    std::string replaceEscapeSequences(std::string_view input)
    {
        std::string result;
        result.reserve(input.size());

        for (size_t index = 0; index < input.size(); index++)
        {
            const auto character = input[index];
            if (character != '\\' || index + 1 == input.size())
            {
                result.push_back(character);
                continue;
            }

            index++;
            const auto escapedCharacter = input[index];
            switch (escapedCharacter)
            {
                case '\'':
                {
                    result.push_back('\'');
                    break;
                }
                case '\"':
                {
                    result.push_back('\"');
                    break;
                }
                case 'a':
                {
                    result.push_back('\a');
                    break;
                }
                case 'b':
                {
                    result.push_back('\b');
                    break;
                }
                case 'f':
                {
                    result.push_back('\f');
                    break;
                }
                case 'n':
                {
                    result.push_back('\n');
                    break;
                }
                case 'r':
                {
                    result.push_back('\r');
                    break;
                }
                case 't':
                {
                    result.push_back('\t');
                    break;
                }
                case 'v':
                {
                    result.push_back('\v');
                    break;
                }
                case '\\':
                {
                    result.push_back('\\');
                    break;
                }
                default:
                {
                    // unknown escapes stay verbatim
                    result.push_back('\\');
                    result.push_back(escapedCharacter);
                    break;
                }
            }
        }

        return result;
    }

    std::string escapeStringLiteral(std::string_view text)
    {
        std::string result;
        result.reserve(text.size());

        for (const auto character : text)
        {
            switch (character)
            {
                case '\"':
                {
                    result += "\\\"";
                    break;
                }
                case '\\':
                {
                    result += "\\\\";
                    break;
                }
                case '\a':
                {
                    result += "\\a";
                    break;
                }
                case '\b':
                {
                    result += "\\b";
                    break;
                }
                case '\f':
                {
                    result += "\\f";
                    break;
                }
                case '\n':
                {
                    result += "\\n";
                    break;
                }
                case '\r':
                {
                    result += "\\r";
                    break;
                }
                case '\t':
                {
                    result += "\\t";
                    break;
                }
                case '\v':
                {
                    result += "\\v";
                    break;
                }
                default:
                {
                    result.push_back(character);
                    break;
                }
            }
        }

        return result;
    }
}
