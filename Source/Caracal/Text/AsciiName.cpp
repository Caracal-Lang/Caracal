#include "AsciiName.h"

namespace Caracal
{
    [[nodiscard]] static bool IsAsciiCharacter(char character) noexcept
    {
        return (character >= 'a' && character <= 'z')
            || (character >= 'A' && character <= 'Z')
            || (character >= '0' && character <= '9')
            || character == '_';
    }

    void replaceNonAsciiCharacters(std::string& text)
    {
        for (auto& character : text)
        {
            if (!IsAsciiCharacter(character))
            {
                character = '_';
            }
        }
    }
}
