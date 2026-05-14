#include <waggle/math/ray_intersect.h>

#include <hive/math/transforms.h>

namespace waggle::math
{
    namespace
    {
        using hive::math::Float3;
        using hive::math::Float4;
        using hive::math::kEpsilon;
        using hive::math::Mat4;
    } // namespace

    Ray ScreenToRay(const Mat4& view, const Mat4& proj, float viewportWidth, float viewportHeight,
                    float mouseX, float mouseY) noexcept
    {
        const float ndcX = (2.0f * mouseX / viewportWidth) - 1.0f;
        const float ndcY = 1.0f - (2.0f * mouseY / viewportHeight);

        const Mat4 invProj = hive::math::Inverse(proj);
        const Mat4 invView = hive::math::Inverse(view);

        // Hive uses RH-ZO non-reversed: near at z=0, far at z=1.
        const Float4 nearClip{ndcX, ndcY, 0.0f, 1.0f};
        const Float4 farClip{ndcX, ndcY, 1.0f, 1.0f};

        const Float4 nearView4 = invProj * nearClip;
        const Float4 farView4 = invProj * farClip;
        const Float3 nearView{nearView4.m_x / nearView4.m_w, nearView4.m_y / nearView4.m_w,
                              nearView4.m_z / nearView4.m_w};
        const Float3 farView{farView4.m_x / farView4.m_w, farView4.m_y / farView4.m_w,
                             farView4.m_z / farView4.m_w};

        const Float4 nearWorld4 = invView * Float4{nearView.m_x, nearView.m_y, nearView.m_z, 1.0f};
        const Float4 farWorld4 = invView * Float4{farView.m_x, farView.m_y, farView.m_z, 1.0f};
        const Float3 nearWorld{nearWorld4.m_x, nearWorld4.m_y, nearWorld4.m_z};
        const Float3 farWorld{farWorld4.m_x, farWorld4.m_y, farWorld4.m_z};

        return Ray{nearWorld, hive::math::Normalize(farWorld - nearWorld)};
    }

    bool RayVsCylinder(const Ray& ray, Float3 origin, Float3 axisDir, float length, float radius,
                       float* outT) noexcept
    {
        const Float3 d = ray.m_direction;
        const Float3 m = ray.m_origin - origin;
        const float dDotA = hive::math::Dot(d, axisDir);
        const float mDotA = hive::math::Dot(m, axisDir);

        const Float3 dPerp{d.m_x - dDotA * axisDir.m_x, d.m_y - dDotA * axisDir.m_y,
                           d.m_z - dDotA * axisDir.m_z};
        const Float3 mPerp{m.m_x - mDotA * axisDir.m_x, m.m_y - mDotA * axisDir.m_y,
                           m.m_z - mDotA * axisDir.m_z};

        const float a = hive::math::Dot(dPerp, dPerp);
        const float b = 2.0f * hive::math::Dot(dPerp, mPerp);
        const float c = hive::math::Dot(mPerp, mPerp) - radius * radius;

        if (a < kEpsilon)
        {
            return false;
        }

        const float disc = b * b - 4.0f * a * c;
        if (disc < 0.0f)
        {
            return false;
        }

        const float sq = hive::math::Sqrt(disc);
        const float t0 = (-b - sq) / (2.0f * a);
        const float t1 = (-b + sq) / (2.0f * a);
        const float tHit = t0 >= 0.0f ? t0 : t1;
        if (tHit < 0.0f)
        {
            return false;
        }

        const float along = mDotA + tHit * dDotA;
        if (along < 0.0f || along > length)
        {
            return false;
        }

        if (outT != nullptr)
        {
            *outT = tHit;
        }
        return true;
    }

    bool RayVsPlane(const Ray& ray, Float3 planePoint, Float3 planeNormal, float* outT) noexcept
    {
        const float denom = hive::math::Dot(planeNormal, ray.m_direction);
        if (denom > -kEpsilon && denom < kEpsilon)
        {
            return false;
        }
        const Float3 diff = planePoint - ray.m_origin;
        const float t = hive::math::Dot(diff, planeNormal) / denom;
        if (t < 0.0f)
        {
            return false;
        }
        if (outT != nullptr)
        {
            *outT = t;
        }
        return true;
    }

    bool RayVsAabb(const Ray& ray, Float3 boxMin, Float3 boxMax, float* outT) noexcept
    {
        float tMin = 0.0f;
        float tMax = 1.0e30f;

        const float* origin = &ray.m_origin.m_x;
        const float* direction = &ray.m_direction.m_x;
        const float* minPtr = &boxMin.m_x;
        const float* maxPtr = &boxMax.m_x;

        for (int axis = 0; axis < 3; ++axis)
        {
            if (direction[axis] > -kEpsilon && direction[axis] < kEpsilon)
            {
                if (origin[axis] < minPtr[axis] || origin[axis] > maxPtr[axis])
                {
                    return false;
                }
            }
            else
            {
                const float inv = 1.0f / direction[axis];
                float t0 = (minPtr[axis] - origin[axis]) * inv;
                float t1 = (maxPtr[axis] - origin[axis]) * inv;
                if (t0 > t1)
                {
                    const float tmp = t0;
                    t0 = t1;
                    t1 = tmp;
                }
                if (t0 > tMin)
                {
                    tMin = t0;
                }
                if (t1 < tMax)
                {
                    tMax = t1;
                }
                if (tMin > tMax)
                {
                    return false;
                }
            }
        }

        if (outT != nullptr)
        {
            *outT = tMin;
        }
        return true;
    }

    bool ClosestPointsRayLine(Float3 rayOrigin, Float3 rayDir, Float3 lineOrigin, Float3 lineDir,
                              float* outTRay, float* outTLine) noexcept
    {
        const Float3 w = rayOrigin - lineOrigin;
        const float a = hive::math::Dot(rayDir, rayDir);
        const float b = hive::math::Dot(rayDir, lineDir);
        const float c = hive::math::Dot(lineDir, lineDir);
        const float d = hive::math::Dot(rayDir, w);
        const float e = hive::math::Dot(lineDir, w);
        const float denom = a * c - b * b;
        if (denom < kEpsilon)
        {
            return false;
        }
        const float tRay = (b * e - c * d) / denom;
        const float tLine = (a * e - b * d) / denom;
        if (tRay < 0.0f)
        {
            return false;
        }
        if (outTRay != nullptr)
        {
            *outTRay = tRay;
        }
        if (outTLine != nullptr)
        {
            *outTLine = tLine;
        }
        return true;
    }
} // namespace waggle::math
