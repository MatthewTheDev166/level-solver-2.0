#pragma once

#include <Geode/Geode.hpp>
#include <Geode/loader/Dispatch.hpp>

#define MY_MOD_ID "legowiifun.cheat_api"

namespace cheatAPIEvents {
    inline void setCheatingAll() GEODE_EVENT_EXPORT_NORES(&setCheatingAll, ());
    inline void endCheatingAll() GEODE_EVENT_EXPORT_NORES(&endCheatingAll, ());
}

