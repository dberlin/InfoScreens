#pragma once

#include "InfoScreenTypes.h"

class AFGBuildable;

/** Read-only, authority-side configured recipe rates for one placed blueprint's membership. */
namespace InfoBlueprintReader
{
    INFOSCREENS_API void Read(const TArray<AFGBuildable*>& Buildings, bool Outputs,
        FInfoScreenSnapshot& OutSnapshot);
}
