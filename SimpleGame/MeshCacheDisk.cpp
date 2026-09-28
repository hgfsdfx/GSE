#include "stdafx.h"
#include "MeshCache.h"
#include "ProfileMetrics.h"
#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace
{
    constexpr uint32_t Magic = 0x47534d48, Version = 3;
    constexpr size_t MaxFileVertices = 16 * 1024 * 1024 / sizeof(MeshVertex);
    static_assert(sizeof(MeshVertex) == 7 * sizeof(float));

    uint64_t Hash(const void* bytes, size_t count, uint64_t hash = 14695981039346656037ull)
    {
        const auto data = static_cast<const unsigned char*>(bytes);
        for (size_t i = 0; i < count; ++i)
        {
            hash = (hash ^ data[i]) * 1099511628211ull;
        }
        return hash;
    }

    uint64_t Checksum(const std::string& key,
                      const std::vector<MeshVertex>& vertices,
                      float advance)
    {
        auto hash = Hash(key.data(), key.size());
        hash = Hash(&advance, sizeof(advance), hash);
        return Hash(vertices.data(), vertices.size() * sizeof(MeshVertex), hash);
    }

    template <typename T> void Read(std::istream& stream, T& value)
    {
        stream.read(reinterpret_cast<char*>(&value), sizeof(value));
    }

    template <typename T> void Write(std::ostream& stream, const T& value)
    {
        stream.write(reinterpret_cast<const char*>(&value), sizeof(value));
    }
} // namespace

std::filesystem::path MeshCache::FilePath(const std::string& key) const
{
    std::ostringstream name;
    name << std::hex << std::setw(16) << std::setfill('0') << Hash(key.data(), key.size())
         << ".gmesh";
    return directory_ / name.str();
}

bool MeshCache::LoadFile(const std::string& key, std::vector<MeshVertex>& vertices, float& advance)
{
    const CpuProfileScope profile(CpuMetric::CacheRead);
    if (!diskEnabled_)
    {
        return false;
    }
    std::ifstream file(FilePath(key), std::ios::binary);
    if (!file)
    {
        std::error_code error;
        if (std::filesystem::exists(FilePath(key), error) || error)
        {
            ++stats_.fileErrors;
        }
        else
        {
            ++stats_.fileMissing;
        }
        return false;
    }
    ++stats_.fileInvalid;
    uint32_t magic = 0, version = 0, keySize = 0, count = 0;
    uint64_t checksum = 0;
    Read(file, magic);
    Read(file, version);
    Read(file, keySize);
    Read(file, count);
    Read(file, advance);
    Read(file, checksum);
    if (!file || magic != Magic || version != Version || keySize != key.size() || keySize > 4096
        || count > MaxFileVertices || count % 3 != 0 || !std::isfinite(advance) || advance < 0
        || advance > 100)
    {
        return false;
    }
    std::string storedKey(keySize, '\0');
    file.read(storedKey.data(), keySize);
    if (!file || storedKey != key)
    {
        return false;
    }
    vertices.resize(count);
    if (count)
    {
        file.read(reinterpret_cast<char*>(vertices.data()), count * sizeof(MeshVertex));
    }
    if (!file || file.peek() != std::char_traits<char>::eof()
        || checksum != Checksum(key, vertices, advance))
    {
        return false;
    }
    for (const auto& v : vertices)
    {
        for (float value : {v.x, v.y, v.r, v.g, v.b, v.a, v.emission})
        {
            if (!std::isfinite(value))
            {
                return false;
            }
        }
    }
    --stats_.fileInvalid;
    stats_.readBytes += 28 + key.size() + vertices.size() * sizeof(MeshVertex);
    return true;
}

void MeshCache::SaveFile(const std::string& key,
                         const std::vector<MeshVertex>& vertices,
                         float advance)
{
    const CpuProfileScope profile(CpuMetric::CacheWrite);
    if (!diskEnabled_ || key.size() > 4096 || vertices.size() > MaxFileVertices
        || vertices.size() % 3 != 0)
    {
        ++stats_.fileSkipped;
        return;
    }
    const auto path = FilePath(key);
    std::error_code error;
    const auto oldSize = std::filesystem::file_size(path, error);
    const uint64_t replaced = error ? 0 : oldSize;
    const uint64_t bytes = 28 + key.size() + vertices.size() * sizeof(MeshVertex);
    if (diskBytes_ - std::min(diskBytes_, replaced) + bytes > DiskBudget
        || (replaced == 0 && diskFiles_ >= MaxEntries))
    {
        ++stats_.fileSkipped;
        return;
    }
    auto temporary = path;
    temporary += L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp";
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    const auto keySize = static_cast<uint32_t>(key.size());
    const auto count = static_cast<uint32_t>(vertices.size());
    const auto checksum = Checksum(key, vertices, advance);
    Write(file, Magic);
    Write(file, Version);
    Write(file, keySize);
    Write(file, count);
    Write(file, advance);
    Write(file, checksum);
    file.write(key.data(), key.size());
    if (count)
    {
        file.write(reinterpret_cast<const char*>(vertices.data()), count * sizeof(MeshVertex));
    }
    file.close();
    if (!file || !MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING))
    {
        std::filesystem::remove(temporary, error);
        diskEnabled_ = false;
        ++stats_.fileErrors;
        return;
    }
    diskBytes_ = diskBytes_ - std::min(diskBytes_, replaced) + bytes;
    diskFiles_ += replaced == 0 ? 1 : 0;
    ++stats_.fileSaved;
    stats_.writtenBytes += bytes;
}
