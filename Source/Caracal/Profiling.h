#pragma once

#if defined(CARACAL_TRACY)
#include <tracy/Tracy.hpp>
#define CARACAL_ZONE ZoneScoped
#define CARACAL_ZONE_NAMED(name) ZoneScopedN(name)
#define CARACAL_ZONE_NAMED_VAR(varname, name) ZoneNamedN(varname, name, true)
#else
#define CARACAL_ZONE
#define CARACAL_ZONE_NAMED(name)
#define CARACAL_ZONE_NAMED_VAR(varname, name)
#endif

#if defined(CARACAL_TRACY) && defined(CARACAL_TRACY_DETAIL)
#define CARACAL_ZONE_DETAIL(name) ZoneScopedN(name)
#else
#define CARACAL_ZONE_DETAIL(name)
#endif
