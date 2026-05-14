#pragma once

#include <hive/hive_config.h>
#include <hive/math/functions.h>
#include <hive/math/types.h>

namespace waggle::math
{
    struct Ray
    {
        hive::math::Float3 m_origin{};
        hive::math::Float3 m_direction{};
    };

    [[nodiscard]] HIVE_API Ray ScreenToRay(const hive::math::Mat4& view, const hive::math::Mat4& proj,
                                           float viewportWidth, float viewportHeight,
                                           float mouseX, float mouseY) noexcept;

    [[nodiscard]] HIVE_API bool RayVsCylinder(const Ray& ray, hive::math::Float3 origin,
                                              hive::math::Float3 axisDir, float length, float radius,
                                              float* outT) noexcept;

    [[nodiscard]] HIVE_API bool RayVsPlane(const Ray& ray, hive::math::Float3 planePoint,
                                           hive::math::Float3 planeNormal, float* outT) noexcept;

    [[nodiscard]] HIVE_API bool RayVsAabb(const Ray& ray, hive::math::Float3 boxMin,
                                          hive::math::Float3 boxMax, float* outT) noexcept;

    [[nodiscard]] HIVE_API bool ClosestPointsRayLine(hive::math::Float3 rayOrigin,
                                                     hive::math::Float3 rayDir,
                                                     hive::math::Float3 lineOrigin,
                                                     hive::math::Float3 lineDir, float* outTRay,
                                                     float* outTLine) noexcept;
} // namespace waggle::math
