#pragma once

#include <hive/core/log.h>

#include <utility>

namespace propolis
{
    inline const hive::LogCategory LOG_SCRIPT{"Propolis.Script"};

    template <typename... Args>
    void ScriptLog(fmt::format_string<Args...> format, Args&&... args)
    {
        hive::LogInfo(LOG_SCRIPT, format, std::forward<Args>(args)...);
    }
} // namespace propolis
