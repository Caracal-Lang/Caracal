#include <Caracal/Syntax/SpecialFunctionType.h>

#include <Caracal/Defines.h>

namespace Caracal
{
    std::string stringify(SpecialFunctionType specialFunctionType)
    {
        switch (specialFunctionType)
        {
            case SpecialFunctionType::None:
            {
                return std::string("None");
            }
            case SpecialFunctionType::Constructor:
            {
                return std::string("Constructor");
            }
            case SpecialFunctionType::Destructor:
            {
                return std::string("Destructor");
            }
            default:
            {
                TODO("String for SpecialFunctionType value was not defined yet");
                return std::string();
            }
        }
    }
}
