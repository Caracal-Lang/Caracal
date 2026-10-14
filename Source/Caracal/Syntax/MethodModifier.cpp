#include <Caracal/Syntax/MethodModifier.h>

#include <Caracal/Defines.h>

namespace Caracal
{
    std::string stringify(MethodModifier modifier)
    {
        switch (modifier)
        {
            case MethodModifier::Public:
            {
                return std::string("Public");
            }
            case MethodModifier::Private:
            {
                return std::string("Private");
            }
            case MethodModifier::Static:
            {
                return std::string("Static");
            }
            default:
            {
                TODO("String for MethodModifier value was not defined yet");
                return std::string();
            }
        }
    }
}
