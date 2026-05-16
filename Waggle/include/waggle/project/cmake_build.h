#pragma once

#include <hive/hive_config.h>

#include <wax/containers/string_view.h>

namespace waggle
{
    struct CMakeBuildRequest
    {
        wax::StringView m_sourceDir{};
        wax::StringView m_binaryDir{};
        wax::StringView m_config{};
        wax::StringView m_target{};
        wax::StringView m_presetName{};
        wax::StringView m_generator{};
    };

    using CMakeLogCallback = void (*)(const char* line, bool isError, void* userdata);

    HIVE_API int RunCMakeConfigure(const CMakeBuildRequest& request, CMakeLogCallback callback, void* userdata);

    HIVE_API int RunCMakeBuild(const CMakeBuildRequest& request, CMakeLogCallback callback, void* userdata);
} // namespace waggle
