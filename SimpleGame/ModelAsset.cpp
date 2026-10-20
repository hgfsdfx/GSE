#include "stdafx.h"
#include "ModelAsset.h"
#pragma warning(push, 0)
// stdafx.h already includes the CRT, so cgltf's own CRT opt-out is too late.
// Keep the legacy CRT warning suppression local to the vendored implementation.
#pragma warning(disable : 4996)
#define CGLTF_IMPLEMENTATION
#include "Dependencies/cgltf.h"
#pragma warning(pop)
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace
{
    using namespace ModelMath;
    constexpr size_t MaxFileBytes = 64 * 1024 * 1024;
    constexpr size_t MaxElements = 1024 * 1024;

    void Require(bool condition, const char* reason)
    {
        if (!condition)
        {
            throw std::runtime_error(reason);
        }
    }

    uint64_t Hash(const void* bytes, size_t count, uint64_t hash)
    {
        const auto* data = static_cast<const unsigned char*>(bytes);
        for (size_t i = 0; i < count; ++i)
        {
            hash = (hash ^ data[i]) * 1099511628211ull;
        }
        return hash;
    }

    std::vector<char> ReadFile(const std::filesystem::path& path)
    {
        const auto size = std::filesystem::file_size(path);
        Require(size > 0 && size <= MaxFileBytes, "empty or oversized model/buffer");
        std::vector<char> bytes(static_cast<size_t>(size));
        std::ifstream file(path, std::ios::binary);
        Require(bool(file.read(bytes.data(), bytes.size())), "cannot read model/buffer");
        return bytes;
    }

    struct FileContext
    {
        std::filesystem::path root;
        uint64_t hash = 14695981039346656037ull;
        size_t bytes = 0;
    };

    cgltf_result ReadBuffer(const cgltf_memory_options*,
                            const cgltf_file_options* options,
                            const char* path,
                            cgltf_size* size,
                            void** output)
    {
        try
        {
            auto& context = *static_cast<FileContext*>(options->user_data);
            const auto candidate = std::filesystem::weakly_canonical(std::filesystem::u8path(path));
            const auto relative = candidate.lexically_relative(context.root).generic_u8string();
            const auto bytes = ReadFile(ResolveAssetPath(context.root, context.root, relative));
            Require(bytes.size() >= *size, "external buffer shorter than declared byteLength");
            Require(context.bytes + bytes.size() <= MaxFileBytes, "buffer budget exceeded");
            void* memory = std::malloc(bytes.size());
            if (!memory)
            {
                return cgltf_result_out_of_memory;
            }
            std::memcpy(memory, bytes.data(), bytes.size());
            context.hash = Hash(bytes.data(), bytes.size(), context.hash);
            context.bytes += bytes.size();
            *size = bytes.size();
            *output = memory;
            return cgltf_result_success;
        }
        catch (...)
        {
            return cgltf_result_io_error;
        }
    }

    void ReleaseBuffer(const cgltf_memory_options*, const cgltf_file_options*, void* data)
    {
        std::free(data);
    }

    std::vector<float> Floats(const cgltf_accessor* accessor, size_t components)
    {
        Require(accessor && accessor->count > 0 && accessor->count <= MaxElements,
                "invalid accessor size");
        Require(cgltf_num_components(accessor->type) == components, "accessor type mismatch");
        std::vector<float> result(accessor->count * components);
        Require(cgltf_accessor_unpack_floats(accessor, result.data(), result.size())
                    == result.size(),
                "cannot decode accessor");
        for (float value : result)
        {
            Require(std::isfinite(value) && std::abs(value) < 1e12f, "invalid accessor value");
        }
        return result;
    }

    const cgltf_accessor* Attribute(const cgltf_primitive& primitive, cgltf_attribute_type type)
    {
        for (size_t i = 0; i < primitive.attributes_count; ++i)
        {
            const auto& attribute = primitive.attributes[i];
            if (attribute.type == type && attribute.index == 0)
            {
                return attribute.data;
            }
        }
        return nullptr;
    }

    float PaletteRole(const char* name)
    {
        const std::string value = name ? name : "";
        if (value == "MAT_Cloth_Navy")
        {
            return 1;
        }
        if (value == "MAT_Hair_Base")
        {
            return 2;
        }
        if (value == "MAT_Skin_Base")
        {
            return 3;
        }
        if (value == "MAT_Accent_Cyan" || value == "MAT_Accent_Magenta"
            || value == "MAT_Accent_Amber")
        {
            return 4;
        }
        return 0;
    }

    Vec3 FaceNormal(const ModelVertex& a, const ModelVertex& b, const ModelVertex& c)
    {
        Vec3 u{}, v{}, normal{};
        for (int i = 0; i < 3; ++i)
        {
            u[i] = b.position[i] - a.position[i];
            v[i] = c.position[i] - a.position[i];
        }
        normal = {u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]};
        const float length =
            std::sqrt(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
        if (length > 1e-8f)
        {
            for (float& value : normal)
            {
                value /= length;
            }
        }
        return normal;
    }

    void LoadPrimitive(const cgltf_primitive& primitive,
                       const ModelAsset& asset,
                       ModelSurface& surface)
    {
        Require(primitive.type == cgltf_primitive_type_triangles, "only triangle meshes supported");
        Require(!primitive.has_draco_mesh_compression && primitive.targets_count == 0,
                "compressed/morph geometry needs a separate decoder");
        for (size_t i = 0; i < primitive.attributes_count; ++i)
        {
            const auto& a = primitive.attributes[i];
            Require(
                !((a.type == cgltf_attribute_type_joints || a.type == cgltf_attribute_type_weights)
                  && a.index != 0),
                "more than four skin influences are unsupported");
        }
        const auto* position = Attribute(primitive, cgltf_attribute_type_position);
        const auto positions = Floats(position, 3);
        const auto* normal = Attribute(primitive, cgltf_attribute_type_normal);
        const auto normals = normal ? Floats(normal, 3) : std::vector<float>{};
        const auto* joints = Attribute(primitive, cgltf_attribute_type_joints);
        const auto* weights = Attribute(primitive, cgltf_attribute_type_weights);
        const bool skinned = surface.skin >= 0;
        Require(!skinned || (joints && weights), "skin requires JOINTS_0 and WEIGHTS_0");
        const auto jointValues = skinned ? Floats(joints, 4) : std::vector<float>{};
        const auto weightValues = skinned ? Floats(weights, 4) : std::vector<float>{};
        Require(!normal || normal->count == position->count, "normal count mismatch");
        Require(!skinned || (joints->count == position->count && weights->count == position->count),
                "skin attribute count mismatch");

        ModelVertex material;
        if (const auto* m = primitive.material)
        {
            const auto& pbr = m->pbr_metallic_roughness;
            Require(!pbr.base_color_texture.texture && !pbr.metallic_roughness_texture.texture
                        && !m->normal_texture.texture && !m->emissive_texture.texture
                        && !m->occlusion_texture.texture,
                    "textured material is not yet supported");
            Require(m->alpha_mode != cgltf_alpha_mode_blend,
                    "transparent glTF material unsupported");
            if (m->has_pbr_metallic_roughness)
            {
                std::copy_n(pbr.base_color_factor, 4, material.color.begin());
            }
            material.position[3] = PaletteRole(m->name);
            const float strength =
                m->has_emissive_strength ? m->emissive_strength.emissive_strength : 1;
            for (int i = 0; i < 3; ++i)
            {
                Require(std::isfinite(m->emissive_factor[i]) && std::isfinite(strength)
                            && m->emissive_factor[i] >= 0 && strength >= 0,
                        "invalid emissive material");
                material.emissive[i] = std::min(32.f, m->emissive_factor[i] * strength);
            }
            material.emissive[3] = m->alpha_mode == cgltf_alpha_mode_mask ? m->alpha_cutoff : 0;
            material.normal[3] = m->unlit ? 1.f : 0.f;
        }
        for (float value : material.color)
        {
            Require(std::isfinite(value) && value >= 0 && value <= 1, "invalid base color");
        }
        Require(std::isfinite(material.emissive[3]), "invalid alpha cutoff");

        const auto indices =
            primitive.indices ? Floats(primitive.indices, 1) : std::vector<float>{};
        const size_t count = primitive.indices ? indices.size() : position->count;
        Require(count % 3 == 0 && surface.vertices.size() + count <= MaxElements,
                "invalid triangle count");
        const auto first = surface.vertices.size();
        surface.vertices.reserve(first + count);
        for (size_t i = 0; i < count; ++i)
        {
            const float raw = primitive.indices ? indices[i] : float(i);
            Require(raw >= 0 && raw < float(position->count) && std::floor(raw) == raw,
                    "index outside vertex buffer");
            const size_t index = static_cast<size_t>(raw);
            auto vertex = material;
            for (int axis = 0; axis < 3; ++axis)
            {
                vertex.position[axis] = positions[index * 3 + axis];
                if (normal)
                {
                    vertex.normal[axis] = normals[index * 3 + axis];
                }
            }
            if (skinned)
            {
                float total = 0;
                for (int influence = 0; influence < 4; ++influence)
                {
                    const float joint = jointValues[index * 4 + influence];
                    const float weight = weightValues[index * 4 + influence];
                    Require(joint >= 0 && std::floor(joint) == joint
                                && joint < float(asset.skins[surface.skin].joints.size()),
                            "joint outside skin");
                    Require(weight >= 0, "negative skin weight");
                    vertex.joints[influence] = joint;
                    vertex.weights[influence] = weight;
                    total += weight;
                }
                Require(total > 1e-8f, "vertex has no skin weights");
                for (float& weight : vertex.weights)
                {
                    weight /= total;
                }
            }
            surface.vertices.push_back(vertex);
        }
        if (!normal)
        {
            for (size_t i = first; i < surface.vertices.size(); i += 3)
            {
                const auto n = FaceNormal(surface.vertices[i],
                                          surface.vertices[i + 1],
                                          surface.vertices[i + 2]);
                for (int k = 0; k < 3; ++k)
                {
                    std::copy(n.begin(), n.end(), surface.vertices[i + k].normal.begin());
                }
            }
        }
    }

    Vec4 Sample(const ModelChannel& channel, float time)
    {
        const auto& times = channel.times;
        const bool cubic = channel.interpolation == ModelChannel::Interpolation::Cubic;
        const auto value = [&](size_t frame)
        {
            return channel.values[frame * (cubic ? 3 : 1) + (cubic ? 1 : 0)];
        };
        if (time <= times.front())
        {
            return value(0);
        }
        if (time >= times.back())
        {
            return value(times.size() - 1);
        }
        const size_t next = std::upper_bound(times.begin(), times.end(), time) - times.begin();
        const size_t previous = next - 1;
        if (channel.interpolation == ModelChannel::Interpolation::Step)
        {
            return value(previous);
        }
        const float span = times[next] - times[previous];
        const float t = (time - times[previous]) / span;
        const auto a = value(previous), b = value(next);
        if (!cubic && channel.path == ModelChannel::Path::Rotation)
        {
            return Slerp(a, b, t);
        }
        Vec4 result{};
        for (int i = 0; i < 4; ++i)
        {
            if (cubic)
            {
                const float t2 = t * t, t3 = t2 * t;
                result[i] = (2 * t3 - 3 * t2 + 1) * a[i] + (-2 * t3 + 3 * t2) * b[i]
                            + (t3 - 2 * t2 + t) * span * channel.values[previous * 3 + 2][i]
                            + (t3 - t2) * span * channel.values[next * 3][i];
            }
            else
            {
                result[i] = a[i] * (1 - t) + b[i] * t;
            }
        }
        return channel.path == ModelChannel::Path::Rotation ? Normalize(result) : result;
    }
} // namespace

std::filesystem::path ResolveAssetPath(const std::filesystem::path& root,
                                       const std::filesystem::path& base,
                                       const std::string& relative)
{
    const auto path = std::filesystem::u8path(relative);
    Require(!relative.empty() && !path.is_absolute() && !path.has_root_name()
                && relative.find(':') == std::string::npos,
            "asset path must be relative");
    const auto canonicalRoot = std::filesystem::weakly_canonical(root);
    const auto result = std::filesystem::weakly_canonical(base / path);
    const auto contained = result.lexically_relative(canonicalRoot);
    Require(!contained.empty() && *contained.begin() != L"..", "asset escapes its root directory");
    return result;
}

std::shared_ptr<ModelAsset> ModelAsset::Load(const std::filesystem::path& file,
                                             const std::filesystem::path& assetRoot)
{
    try
    {
        FileContext context{std::filesystem::weakly_canonical(assetRoot)};
        const auto bytes = ReadFile(file);
        context.hash = Hash(bytes.data(), bytes.size(), context.hash);
        context.bytes = bytes.size();
        cgltf_options options{};
        options.file.read = ReadBuffer;
        options.file.release = ReleaseBuffer;
        options.file.user_data = &context;
        cgltf_data* parsed = nullptr;
        const auto result = cgltf_parse(&options, bytes.data(), bytes.size(), &parsed);
        const std::unique_ptr<cgltf_data, decltype(&cgltf_free)> data(parsed, cgltf_free);
        Require(result == cgltf_result_success && parsed, "glTF parse failed");
        Require(data->nodes_count <= 4096 && data->skins_count <= 256
                    && data->animations_count <= 256 && data->accessors_count <= 16384,
                "glTF structure budget exceeded");
        for (size_t i = 0; i < data->extensions_required_count; ++i)
        {
            const std::string extension = data->extensions_required[i];
            Require(extension == "KHR_materials_unlit"
                        || extension == "KHR_materials_emissive_strength",
                    "required glTF extension is unsupported");
        }
        for (size_t i = 0; i < data->buffer_views_count; ++i)
        {
            Require(!data->buffer_views[i].has_meshopt_compression, "meshopt decoder unavailable");
        }
        size_t bufferBytes = 0;
        for (size_t i = 0; i < data->buffers_count; ++i)
        {
            Require(data->buffers[i].size <= MaxFileBytes - bufferBytes, "buffer budget exceeded");
            bufferBytes += data->buffers[i].size;
        }
        const auto utf8 = file.u8string();
        Require(cgltf_load_buffers(&options, data.get(), utf8.c_str()) == cgltf_result_success,
                "glTF buffer load failed");
        Require(cgltf_validate(data.get()) == cgltf_result_success, "glTF validation failed");
        auto asset = std::make_shared<ModelAsset>();
        std::ostringstream identity;
        identity << std::hex << context.hash;
        asset->identity = identity.str();
        asset->nodes.resize(data->nodes_count);
        for (size_t i = 0; i < data->nodes_count; ++i)
        {
            const auto& input = data->nodes[i];
            auto& node = asset->nodes[i];
            node.name = input.name ? input.name : "";
            node.parent = input.parent ? int(input.parent - data->nodes) : -1;
            node.matrixAuthored = input.has_matrix != 0;
            if (input.has_translation)
            {
                std::copy_n(input.translation, 3, node.translation.begin());
            }
            if (input.has_rotation)
            {
                std::copy_n(input.rotation, 4, node.rotation.begin());
            }
            if (input.has_scale)
            {
                std::copy_n(input.scale, 3, node.scale.begin());
            }
            cgltf_node_transform_local(&input, node.local.values.data());
            for (float value : node.local.values)
            {
                Require(std::isfinite(value), "invalid node matrix");
            }
        }
        std::vector<int> visited(data->nodes_count, 0);
        std::function<void(int, size_t)> visit = [&](int index, size_t depth)
        {
            Require(depth <= 256, "node hierarchy exceeds 256 levels");
            Require(index >= 0 && size_t(index) < asset->nodes.size(), "invalid node parent");
            Require(visited[index] != 1, "cyclic node hierarchy");
            if (visited[index] == 2)
            {
                return;
            }
            visited[index] = 1;
            auto& node = asset->nodes[index];
            if (node.parent >= 0)
            {
                visit(node.parent, depth + 1);
            }
            node.bindWorld = node.parent < 0
                                 ? node.local
                                 : Multiply(asset->nodes[node.parent].bindWorld, node.local);
            asset->hierarchy.push_back(index);
            visited[index] = 2;
        };
        for (size_t i = 0; i < asset->nodes.size(); ++i)
        {
            visit(int(i), 0);
        }

        for (size_t i = 0; i < data->skins_count; ++i)
        {
            const auto& input = data->skins[i];
            Require(input.joints_count > 0 && input.joints_count <= 256, "unsupported joint count");
            ModelSkin skin;
            skin.inverseBind.resize(input.joints_count);
            for (size_t j = 0; j < input.joints_count; ++j)
            {
                skin.joints.push_back(int(input.joints[j] - data->nodes));
            }
            if (input.inverse_bind_matrices)
            {
                const auto matrices = Floats(input.inverse_bind_matrices, 16);
                Require(matrices.size() == input.joints_count * 16, "inverse bind count mismatch");
                for (size_t j = 0; j < input.joints_count; ++j)
                {
                    std::copy_n(matrices.data() + j * 16, 16, skin.inverseBind[j].values.begin());
                }
            }
            asset->skins.push_back(std::move(skin));
        }
        const cgltf_scene* scene =
            data->scene ? data->scene : (data->scenes_count ? data->scenes : nullptr);
        std::set<int> sceneRoots;
        if (scene)
        {
            for (size_t i = 0; i < scene->nodes_count; ++i)
            {
                sceneRoots.insert(int(scene->nodes[i] - data->nodes));
            }
        }
        size_t totalVertices = 0;
        for (size_t i = 0; i < data->nodes_count; ++i)
        {
            const auto& input = data->nodes[i];
            if (!input.mesh)
            {
                continue;
            }
            int root = int(i);
            while (asset->nodes[root].parent >= 0)
            {
                root = asset->nodes[root].parent;
            }
            if (scene && sceneRoots.count(root) == 0)
            {
                continue;
            }
            ModelSurface surface;
            surface.node = int(i);
            surface.skin = input.skin ? int(input.skin - data->skins) : -1;
            surface.key = "model:v1:" + asset->identity
                          + ":mesh:" + std::to_string(input.mesh - data->meshes)
                          + ":skin:" + std::to_string(surface.skin);
            for (size_t p = 0; p < input.mesh->primitives_count; ++p)
            {
                LoadPrimitive(input.mesh->primitives[p], *asset, surface);
            }
            totalVertices += surface.vertices.size();
            Require(totalVertices <= MaxElements, "model vertex budget exceeded");
            if (!surface.vertices.empty())
            {
                asset->surfaces.push_back(std::move(surface));
            }
        }
        Require(!asset->surfaces.empty(), "no visible triangle meshes in default scene");
        asset->minimum.fill(std::numeric_limits<float>::max());
        asset->maximum.fill(std::numeric_limits<float>::lowest());
        for (const auto& surface : asset->surfaces)
        {
            for (const auto& vertex : surface.vertices)
            {
                Vec3 point{};
                const Vec3 local{vertex.position[0], vertex.position[1], vertex.position[2]};
                if (surface.skin < 0)
                {
                    point = TransformPoint(asset->nodes[surface.node].bindWorld, local);
                }
                else
                {
                    const auto& skin = asset->skins[surface.skin];
                    for (int w = 0; w < 4; ++w)
                    {
                        const size_t j = size_t(vertex.joints[w]);
                        const auto p = TransformPoint(
                            Multiply(asset->nodes[skin.joints[j]].bindWorld, skin.inverseBind[j]),
                            local);
                        for (int axis = 0; axis < 3; ++axis)
                        {
                            point[axis] += p[axis] * vertex.weights[w];
                        }
                    }
                }
                for (int axis = 0; axis < 3; ++axis)
                {
                    Require(std::isfinite(point[axis]) && std::abs(point[axis]) < 1e8f,
                            "invalid model bounds");
                    asset->minimum[axis] = std::min(asset->minimum[axis], point[axis]);
                    asset->maximum[axis] = std::max(asset->maximum[axis], point[axis]);
                }
            }
        }

        for (size_t a = 0; a < data->animations_count; ++a)
        {
            const auto& input = data->animations[a];
            ModelClip clip;
            clip.name = input.name ? input.name : "Animation_" + std::to_string(a);
            Require(input.channels_count <= 16384, "too many animation channels");
            std::set<std::pair<int, int>> targets;
            for (size_t c = 0; c < input.channels_count; ++c)
            {
                const auto& channel = input.channels[c];
                Require(channel.target_node && channel.sampler, "invalid animation channel");
                ModelChannel track;
                track.node = int(channel.target_node - data->nodes);
                Require(!asset->nodes[track.node].matrixAuthored,
                        "animated matrix node needs TRS export");
                Require(targets.emplace(track.node, int(channel.target_path)).second,
                        "duplicate animation channel");
                size_t components = 3;
                switch (channel.target_path)
                {
                    case cgltf_animation_path_type_translation:
                        track.path = ModelChannel::Path::Translation;
                        break;
                    case cgltf_animation_path_type_rotation:
                        track.path = ModelChannel::Path::Rotation;
                        components = 4;
                        break;
                    case cgltf_animation_path_type_scale:
                        track.path = ModelChannel::Path::Scale;
                        break;
                    default:
                        throw std::runtime_error("morph animation unsupported");
                }
                const auto& sampler = *channel.sampler;
                track.times = Floats(sampler.input, 1);
                for (size_t t = 0; t < track.times.size(); ++t)
                {
                    Require(track.times[t] >= 0 && track.times[t] <= 3600
                                && (t == 0 || track.times[t] > track.times[t - 1]),
                            "invalid key times");
                }
                const bool cubic = sampler.interpolation == cgltf_interpolation_type_cubic_spline;
                track.interpolation = cubic ? ModelChannel::Interpolation::Cubic
                                      : sampler.interpolation == cgltf_interpolation_type_step
                                          ? ModelChannel::Interpolation::Step
                                          : ModelChannel::Interpolation::Linear;
                const auto values = Floats(sampler.output, components);
                Require(values.size() == track.times.size() * components * (cubic ? 3 : 1),
                        "animation key count mismatch");
                track.values.resize(values.size() / components);
                for (size_t t = 0; t < track.values.size(); ++t)
                {
                    std::copy_n(values.data() + t * components,
                                components,
                                track.values[t].begin());
                }
                clip.duration = std::max(clip.duration, track.times.back());
                clip.channels.push_back(std::move(track));
            }
            Require(asset->clips.emplace(clip.name, std::move(clip)).second, "duplicate clip name");
        }
        std::cout << "[Asset] Loaded " << file.filename().u8string()
                  << " nodes=" << asset->nodes.size() << " surfaces=" << asset->surfaces.size()
                  << " vertices=" << totalVertices << " skins=" << asset->skins.size()
                  << " clips=" << asset->clips.size() << '\n';
        return asset;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[Asset] Load failed: " << file.u8string() << " | " << error.what() << '\n';
        return nullptr;
    }
}

const std::vector<ModelMath::Matrix>& ModelAsset::Pose(const std::string& name,
                                                       float seconds,
                                                       bool loop) const
{
    const auto found = clips.find(name);
    const ModelClip* clip = found == clips.end() ? nullptr : &found->second;
    float time = std::isfinite(seconds) ? std::max(0.f, seconds) : 0;
    if (clip && clip->duration > 0)
    {
        time = loop ? std::fmod(time, clip->duration) : std::min(time, clip->duration);
    }
    else
    {
        time = 0;
    }
    const bool ended = clip && !loop && time >= clip->duration;
    const auto frame = static_cast<unsigned>(std::floor(time * 30.f));
    // Keep the exact end pose for non-looping clips.
    time = ended ? clip->duration : frame / 30.f;
    const auto key = (clip ? name : std::string("bind")) + ":" + std::to_string(frame)
                     + (ended  ? ":end"
                        : loop ? ":loop"
                               : ":once");
    const auto cached = poses_.find(key);
    if (cached != poses_.end())
    {
        return cached->second;
    }
    const size_t poseLimit =
        std::min(size_t(256),
                 std::max(size_t(1),
                          size_t(8 * 1024 * 1024)
                              / std::max(size_t(1), nodes.size() * sizeof(ModelMath::Matrix))));
    if (poses_.size() >= poseLimit)
    {
        poses_.clear();
    }
    std::vector<ModelMath::Vec3> translations, scales;
    std::vector<ModelMath::Vec4> rotations;
    for (const auto& node : nodes)
    {
        translations.push_back(node.translation);
        scales.push_back(node.scale);
        rotations.push_back(node.rotation);
    }
    if (clip)
    {
        for (const auto& channel : clip->channels)
        {
            const auto value = Sample(channel, time);
            if (channel.path == ModelChannel::Path::Rotation)
            {
                rotations[channel.node] = value;
            }
            else
            {
                auto& output = channel.path == ModelChannel::Path::Translation
                                   ? translations[channel.node]
                                   : scales[channel.node];
                std::copy_n(value.begin(), 3, output.begin());
            }
        }
    }
    std::vector<ModelMath::Matrix> pose(nodes.size());
    for (const int index : hierarchy)
    {
        const auto& node = nodes[index];
        const auto local =
            node.matrixAuthored
                ? node.local
                : ModelMath::Compose(translations[index], rotations[index], scales[index]);
        pose[index] = node.parent < 0 ? local : ModelMath::Multiply(pose[node.parent], local);
    }
    return poses_.emplace(key, std::move(pose)).first->second;
}
