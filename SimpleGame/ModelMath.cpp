#include "stdafx.h"
#include "ModelMath.h"
#include <algorithm>
#include <cmath>

namespace ModelMath
{
    Matrix Multiply(const Matrix& a, const Matrix& b)
    {
        Matrix result;
        result.values.fill(0);
        for (int column = 0; column < 4; ++column)
        {
            for (int row = 0; row < 4; ++row)
            {
                for (int k = 0; k < 4; ++k)
                {
                    result.values[column * 4 + row] +=
                        a.values[k * 4 + row] * b.values[column * 4 + k];
                }
            }
        }
        return result;
    }

    Vec4 Normalize(const Vec4& value)
    {
        float length = 0;
        for (float component : value)
        {
            length += component * component;
        }
        if (length < 1e-12f)
        {
            return {0, 0, 0, 1};
        }
        const float inverse = 1 / std::sqrt(length);
        return {value[0] * inverse, value[1] * inverse, value[2] * inverse, value[3] * inverse};
    }

    Matrix Compose(const Vec3& t, const Vec4& rotation, const Vec3& s)
    {
        const auto q = Normalize(rotation);
        const float x = q[0], y = q[1], z = q[2], w = q[3];
        Matrix result;
        result.values = {(1 - 2 * (y * y + z * z)) * s[0],
                         2 * (x * y + z * w) * s[0],
                         2 * (x * z - y * w) * s[0],
                         0,
                         2 * (x * y - z * w) * s[1],
                         (1 - 2 * (x * x + z * z)) * s[1],
                         2 * (y * z + x * w) * s[1],
                         0,
                         2 * (x * z + y * w) * s[2],
                         2 * (y * z - x * w) * s[2],
                         (1 - 2 * (x * x + y * y)) * s[2],
                         0,
                         t[0],
                         t[1],
                         t[2],
                         1};
        return result;
    }

    Vec3 TransformPoint(const Matrix& matrix, const Vec3& point)
    {
        const auto& m = matrix.values;
        return {m[0] * point[0] + m[4] * point[1] + m[8] * point[2] + m[12],
                m[1] * point[0] + m[5] * point[1] + m[9] * point[2] + m[13],
                m[2] * point[0] + m[6] * point[1] + m[10] * point[2] + m[14]};
    }

    Vec4 Slerp(Vec4 a, Vec4 b, float amount)
    {
        a = Normalize(a);
        b = Normalize(b);
        float dot = 0;
        for (int i = 0; i < 4; ++i)
        {
            dot += a[i] * b[i];
        }
        if (dot < 0)
        {
            for (float& component : b)
            {
                component = -component;
            }
            dot = -dot;
        }
        float left = 1 - amount, right = amount;
        if (dot < .9995f)
        {
            const float angle = std::acos(std::clamp(dot, -1.f, 1.f));
            left = std::sin((1 - amount) * angle) / std::sin(angle);
            right = std::sin(amount * angle) / std::sin(angle);
        }
        Vec4 result{};
        for (int i = 0; i < 4; ++i)
        {
            result[i] = a[i] * left + b[i] * right;
        }
        return Normalize(result);
    }
} // namespace ModelMath
