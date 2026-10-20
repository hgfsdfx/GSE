#pragma once
#include "ModelMath.h"
#include <filesystem>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

// Six RGBA32F texels. Independent of the existing, smaller 2D mesh atlas.
struct ModelVertex
{
    // xyz, palette role (0 unchanged, 1 cloth, 2 hair, 3 skin, 4 accent)
    ModelMath::Vec4 position{};
    ModelMath::Vec4 color{1, 1, 1, 1};
    ModelMath::Vec4 normal{0, 1, 0, 0};
    ModelMath::Vec4 joints{};
    ModelMath::Vec4 weights{1, 0, 0, 0};
    ModelMath::Vec4 emissive{}; // linear RGB, alpha cutoff
};

struct ModelNode
{
    std::string name;
    int parent = -1;
    ModelMath::Vec3 translation{0, 0, 0}, scale{1, 1, 1};
    ModelMath::Vec4 rotation{0, 0, 0, 1};
    ModelMath::Matrix local, bindWorld;
    bool matrixAuthored = false;
};

struct ModelSkin
{
    std::vector<int> joints;
    std::vector<ModelMath::Matrix> inverseBind;
};

struct ModelSurface
{
    std::string key;
    int node = -1, skin = -1;
    std::vector<ModelVertex> vertices;
};

struct ModelChannel
{
    enum class Path
    {
        Translation,
        Rotation,
        Scale
    };
    enum class Interpolation
    {
        Linear,
        Step,
        Cubic
    };
    int node = -1;
    Path path = Path::Translation;
    Interpolation interpolation = Interpolation::Linear;
    std::vector<float> times;
    std::vector<ModelMath::Vec4> values;
};

struct ModelClip
{
    std::string name;
    float duration = 0;
    std::vector<ModelChannel> channels;
};

class ModelAsset
{
  public:

    static std::shared_ptr<ModelAsset> Load(const std::filesystem::path& file,
                                            const std::filesystem::path& assetRoot);
    std::vector<ModelNode> nodes;
    std::vector<int> hierarchy;
    std::vector<ModelSkin> skins;
    std::vector<ModelSurface> surfaces;
    std::map<std::string, ModelClip> clips;
    ModelMath::Vec3 minimum{}, maximum{};
    std::string identity;

    // Quantized sampling shares poses between actors, never regenerates geometry.
    const std::vector<ModelMath::Matrix>& Pose(const std::string& clip,
                                               float seconds,
                                               bool loop = true) const;

  private:

    mutable std::map<std::string, std::vector<ModelMath::Matrix>> poses_;
};

struct ModelPart
{
    std::shared_ptr<ModelAsset> asset;
    // Part node -> animation-source node, matched by name and validated bind pose.
    std::vector<int> animationNodes;
};

struct ModelAppearance
{
    std::vector<ModelPart> parts;
    std::shared_ptr<ModelAsset> animation;
    // A negative red component means retain the material's own color.
    std::array<ModelMath::Vec4, 4> palette{
        {{-1, 0, 0, 1}, {-1, 0, 0, 1}, {-1, 0, 0, 1}, {-1, 0, 0, 1}}};
    ModelMath::Vec3 minimum{}, maximum{};
};

// Reusable path guard for catalog entries and glTF external buffers.
std::filesystem::path ResolveAssetPath(const std::filesystem::path& root,
                                       const std::filesystem::path& base,
                                       const std::string& relative);
