#pragma once
#include <array>

namespace ModelMath
{
    using Vec3 = std::array<float, 3>;
    using Vec4 = std::array<float, 4>;

    struct Matrix
    {
        // Column major, matching glTF and GLSL. Identity by default.
        std::array<float, 16> values{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    };

    Matrix Multiply(const Matrix& a, const Matrix& b);
    Matrix Compose(const Vec3& translation, const Vec4& rotation, const Vec3& scale);
    Vec3 TransformPoint(const Matrix& matrix, const Vec3& point);
    Vec4 Normalize(const Vec4& value);
    Vec4 Slerp(Vec4 a, Vec4 b, float amount);
} // namespace ModelMath
