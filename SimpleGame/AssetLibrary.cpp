#include "stdafx.h"
#include "AssetLibrary.h"
#pragma warning(push, 0)
#include "Dependencies/json.hpp"
#pragma warning(pop)
#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>

namespace
{
    using Json = nlohmann::json;
    const char* Slots[] = {"Head", "Hair", "Torso", "ArmL", "ArmR", "Legs", "Feet", "Accessory"};

    Json ReadJson(const std::filesystem::path& file)
    {
        if (std::filesystem::file_size(file) > 4 * 1024 * 1024)
        {
            throw std::runtime_error("catalog exceeds 4 MiB");
        }
        std::ifstream stream(file);
        auto json = Json::parse(stream);
        const int schema = json.at("schema_version").get<int>();
        if (schema != 1 && schema != 2)
        {
            throw std::runtime_error("unsupported catalog schema");
        }
        return json;
    }

    std::filesystem::path FindRoot()
    {
        wchar_t configured[32768]{};
        const auto size = GetEnvironmentVariableW(L"GSE_ASSET_ROOT", configured, 32768);
        if (size > 0 && size < 32768)
        {
            const std::filesystem::path path(configured);
            if (!path.is_absolute())
            {
                throw std::runtime_error("GSE_ASSET_ROOT must be absolute");
            }
            return std::filesystem::weakly_canonical(path);
        }
        wchar_t executable[32768]{};
        const auto length = GetModuleFileNameW(nullptr, executable, 32768);
        if (!length || length >= 32768)
        {
            return {};
        }
        auto directory = std::filesystem::path(executable).parent_path();
        for (int i = 0; i < 6; ++i)
        {
            if (std::filesystem::exists(directory / L"assets" / L"nox_lowpoly"
                                        / L"mesh_catalog.json"))
            {
                return std::filesystem::weakly_canonical(directory / L"assets");
            }
            const auto parent = directory.parent_path();
            if (parent == directory)
            {
                break;
            }
            directory = parent;
        }
        return {};
    }

    void Bounds(ModelAppearance& appearance)
    {
        appearance.minimum.fill(std::numeric_limits<float>::max());
        appearance.maximum.fill(std::numeric_limits<float>::lowest());
        for (const auto& part : appearance.parts)
        {
            for (int axis = 0; axis < 3; ++axis)
            {
                appearance.minimum[axis] =
                    std::min(appearance.minimum[axis], part.asset->minimum[axis]);
                appearance.maximum[axis] =
                    std::max(appearance.maximum[axis], part.asset->maximum[axis]);
            }
        }
    }
} // namespace

bool AssetLibrary::Initialize()
{
    try
    {
        root_ = FindRoot();
        if (root_.empty())
        {
            throw std::runtime_error("assets/nox_lowpoly/mesh_catalog.json not found");
        }
        const auto base = root_ / L"nox_lowpoly";
        const auto catalog = ReadJson(base / L"mesh_catalog.json");
        for (const auto& item : catalog.at("assets"))
        {
            const auto key = item.at("slot").get<std::string>() + ":"
                             + std::to_string(item.at("index").get<int>());
            if (item.at("index").get<int>() <= 0)
            {
                throw std::runtime_error("invalid slot index");
            }
            Entry entry{ResolveAssetPath(root_, base, item.at("file").get<std::string>()), ""};
            auto& entries = item.at("category") == "npc" ? staticParts_ : environment_;
            if (!entries.emplace(key, std::move(entry)).second)
            {
                throw std::runtime_error("duplicate slot index");
            }
        }
        for (const auto& item : catalog.at("palettes"))
        {
            std::array<ModelMath::Vec4, 4> palette{};
            const char* names[] = {"cloth", "hair", "skin", "accent"};
            for (int i = 0; i < 4; ++i)
            {
                const auto rgb = item.at(names[i]).get<std::vector<float>>();
                if (rgb.size() != 3)
                {
                    throw std::runtime_error("palette requires three linear RGB values");
                }
                for (float value : rgb)
                {
                    if (!std::isfinite(value) || value < 0 || value > 1)
                    {
                        throw std::runtime_error("invalid palette color");
                    }
                }
                palette[i] = {rgb[0], rgb[1], rgb[2], 1};
            }
            if (!palettes_.emplace(item.at("index").get<int>(), palette).second)
            {
                throw std::runtime_error("duplicate palette index");
            }
        }
        auto presetFile = base / L"npc_presets.json";
        const auto animated = root_ / L"nox_animated";
        if (std::filesystem::exists(animated / L"rig_catalog.json"))
        {
            try
            {
                const auto rigs = ReadJson(animated / L"rig_catalog.json");
                const auto& npc = rigs.at("npc");
                npcAnimation_ = {ResolveAssetPath(root_,
                                                  animated,
                                                  npc.at("animated_example").get<std::string>()),
                                 npc.at("skeleton_id").get<std::string>()};
                for (const auto& item : npc.at("parts"))
                {
                    const int index = item.at("index").get<int>();
                    const auto key =
                        item.at("slot").get<std::string>() + ":" + std::to_string(index);
                    if (index <= 0
                        || !animatedParts_
                                .emplace(key,
                                         Entry{ResolveAssetPath(root_,
                                                                animated,
                                                                item.at("file").get<std::string>()),
                                               item.at("skeleton_id").get<std::string>()})
                                .second)
                    {
                        throw std::runtime_error("invalid animated slot index");
                    }
                }
                for (const auto& item : rigs.at("robots"))
                {
                    if (!robots_
                             .emplace(item.at("id").get<std::string>(),
                                      Entry{ResolveAssetPath(root_,
                                                             animated,
                                                             item.at("file").get<std::string>()),
                                            item.at("skeleton_id").get<std::string>()})
                             .second)
                    {
                        throw std::runtime_error("duplicate robot id");
                    }
                }
                presetFile =
                    ResolveAssetPath(root_, animated, npc.at("preset_file").get<std::string>());
            }
            catch (const std::exception& error)
            {
                std::cerr << "[Asset] Rig catalog rejected; static fallback: " << error.what()
                          << '\n';
                animatedParts_.clear();
                robots_.clear();
                npcAnimation_ = {};
            }
        }
        const auto presets = ReadJson(presetFile);
        for (const auto& item : presets.at("npcs"))
        {
            Preset preset;
            preset.palette = item.at("palette_index").get<int>();
            if (palettes_.count(preset.palette) == 0)
            {
                throw std::runtime_error("unknown palette index");
            }
            for (const auto* slot : Slots)
            {
                const int index = item.at("parts").at(slot).get<int>();
                if (index == 0 && (std::string(slot) == "Hair" || std::string(slot) == "Accessory"))
                {
                    continue;
                }
                if (index <= 0
                    || (staticParts_.count(std::string(slot) + ":" + std::to_string(index)) == 0
                        && animatedParts_.count(std::string(slot) + ":" + std::to_string(index))
                               == 0))
                {
                    throw std::runtime_error("preset references unknown slot index");
                }
                preset.parts.emplace(slot, index);
            }
            if (!presets_.emplace(item.at("npc_id").get<std::string>(), std::move(preset)).second)
            {
                throw std::runtime_error("duplicate NPC preset");
            }
        }
        // Decode once before gameplay. GPU uploads remain lazy, once per shared mesh.
        for (const auto& preset : presets_)
        {
            Npc(preset.first);
        }
        for (const auto& robot : robots_)
        {
            Robot(robot.first);
        }
        for (const auto& entry : environment_)
        {
            Single("env:" + entry.first, entry.second, false);
        }
        std::cout << "[Asset] Root=" << root_.u8string() << " models=" << models_.size()
                  << " appearances=" << appearances_.size() << " vertexBytes=" << decodedBytes_
                  << '\n';
        return true;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[Asset] Catalog unavailable; procedural visuals retained: " << error.what()
                  << '\n';
        appearances_.clear();
        presets_.clear();
        environment_.clear();
        robots_.clear();
        return false;
    }
}

std::shared_ptr<ModelAsset> AssetLibrary::Load(const Entry& entry)
{
    const auto found = models_.find(entry.file);
    if (found != models_.end())
    {
        return found->second;
    }
    auto model = ModelAsset::Load(entry.file, root_);
    if (model)
    {
        size_t bytes = 0;
        for (const auto& surface : model->surfaces)
        {
            bytes += surface.vertices.size() * sizeof(ModelVertex);
        }
        if (bytes > 64 * 1024 * 1024 - decodedBytes_)
        {
            std::cerr << "[Asset] CPU geometry budget exceeded: " << entry.file.u8string() << '\n';
            model.reset();
        }
        else
        {
            decodedBytes_ += bytes;
        }
    }
    models_.emplace(entry.file, model); // Cache failures too; no retries or log spam per frame.
    return model;
}

bool AssetLibrary::Bind(ModelAppearance& appearance,
                        const std::vector<Entry>& entries,
                        const Entry* animation)
{
    appearance.parts.clear();
    appearance.animation = animation ? Load(*animation) : nullptr;
    if (animation && !appearance.animation)
    {
        return false;
    }
    std::map<std::string, int> sourceNodes;
    if (appearance.animation)
    {
        for (size_t i = 0; i < appearance.animation->nodes.size(); ++i)
        {
            const auto& name = appearance.animation->nodes[i].name;
            if (name.empty())
            {
                continue;
            }
            if (!sourceNodes.emplace(name, int(i)).second)
            {
                sourceNodes[name] = -1;
            }
        }
    }
    for (const auto& entry : entries)
    {
        ModelPart part;
        part.asset = Load(entry);
        if (!part.asset)
        {
            return false;
        }
        part.animationNodes.resize(part.asset->nodes.size(), -1);
        if (appearance.animation)
        {
            if (animation->skeleton != entry.skeleton)
            {
                return false;
            }
            for (size_t i = 0; i < part.asset->nodes.size(); ++i)
            {
                const auto found = sourceNodes.find(part.asset->nodes[i].name);
                if (found != sourceNodes.end())
                {
                    part.animationNodes[i] = found->second;
                }
            }
            for (const auto& skin : part.asset->skins)
            {
                for (const int joint : skin.joints)
                {
                    const int source = part.animationNodes[joint];
                    if (source < 0)
                    {
                        std::cerr << "[Asset] Missing/ambiguous animation joint: "
                                  << part.asset->nodes[joint].name << '\n';
                        return false;
                    }
                    const int parent = part.asset->nodes[joint].parent;
                    const int sourceParent = appearance.animation->nodes[source].parent;
                    const std::string parentName = parent < 0 ? "" : part.asset->nodes[parent].name;
                    const std::string sourceParentName =
                        sourceParent < 0 ? "" : appearance.animation->nodes[sourceParent].name;
                    if (parentName != sourceParentName)
                    {
                        std::cerr << "[Asset] Incompatible joint hierarchy: "
                                  << entry.file.u8string() << '\n';
                        return false;
                    }
                    const auto& original = part.asset->nodes[joint].bindWorld.values;
                    const auto& reference = appearance.animation->nodes[source].bindWorld.values;
                    for (int i = 0; i < 16; ++i)
                    {
                        if (std::abs(original[i] - reference[i]) > .002f)
                        {
                            std::cerr << "[Asset] Incompatible bind pose: " << entry.file.u8string()
                                      << '\n';
                            return false;
                        }
                    }
                }
            }
        }
        appearance.parts.push_back(std::move(part));
    }
    Bounds(appearance);
    return !appearance.parts.empty();
}

const ModelAppearance* AssetLibrary::Npc(const std::string& preset)
{
    const auto key = "npc:" + preset;
    const auto found = appearances_.find(key);
    if (found != appearances_.end())
    {
        return found->second.parts.empty() ? nullptr : &found->second;
    }
    const auto definition = presets_.find(preset);
    if (definition == presets_.end())
    {
        return nullptr;
    }
    ModelAppearance appearance;
    appearance.palette = palettes_.at(definition->second.palette);
    std::vector<Entry> animated, fallback;
    bool hasAnimated = !npcAnimation_.file.empty();
    bool hasStatic = true;
    for (const auto& part : definition->second.parts)
    {
        const auto partKey = part.first + ":" + std::to_string(part.second);
        const auto staticPart = staticParts_.find(partKey);
        hasStatic = hasStatic && staticPart != staticParts_.end();
        if (staticPart != staticParts_.end())
        {
            fallback.push_back(staticPart->second);
        }
        const auto rig = animatedParts_.find(partKey);
        hasAnimated = hasAnimated && rig != animatedParts_.end();
        if (rig != animatedParts_.end())
        {
            animated.push_back(rig->second);
        }
    }
    if (!hasAnimated || !Bind(appearance, animated, &npcAnimation_))
    {
        if (!hasStatic || !Bind(appearance, fallback, nullptr))
        {
            appearance.parts.clear();
        }
    }
    const auto stored = appearances_.emplace(key, std::move(appearance));
    return stored.first->second.parts.empty() ? nullptr : &stored.first->second;
}

const ModelAppearance* AssetLibrary::Single(const std::string& key,
                                            const Entry& entry,
                                            bool animated)
{
    const auto found = appearances_.find(key);
    if (found != appearances_.end())
    {
        return found->second.parts.empty() ? nullptr : &found->second;
    }
    ModelAppearance appearance;
    if (!Bind(appearance, {entry}, animated ? &entry : nullptr))
    {
        appearance.parts.clear();
    }
    const auto stored = appearances_.emplace(key, std::move(appearance));
    return stored.first->second.parts.empty() ? nullptr : &stored.first->second;
}

const ModelAppearance* AssetLibrary::Environment(const std::string& slot, int index)
{
    const auto key = slot + ":" + std::to_string(index);
    const auto found = environment_.find(key);
    return found == environment_.end() ? nullptr : Single("env:" + key, found->second, false);
}

const ModelAppearance* AssetLibrary::Robot(const std::string& id)
{
    const auto found = robots_.find(id);
    return found == robots_.end() ? nullptr : Single("robot:" + id, found->second, true);
}

const std::filesystem::path& AssetLibrary::Root() const
{
    return root_;
}
