#pragma once
#include "ModelAsset.h"
#include <map>

// Immutable model data is shared by every actor. Catalog indices, never file order, are identity.
class AssetLibrary
{
  public:

    bool Initialize();
    const ModelAppearance* Npc(const std::string& preset);
    const ModelAppearance* Environment(const std::string& slot, int index);
    const ModelAppearance* Robot(const std::string& id);
    const std::filesystem::path& Root() const;

  private:

    struct Entry
    {
        std::filesystem::path file;
        std::string skeleton;
    };

    struct Preset
    {
        std::map<std::string, int> parts;
        int palette = 0;
    };

    std::shared_ptr<ModelAsset> Load(const Entry& entry);
    bool Bind(ModelAppearance& appearance,
              const std::vector<Entry>& entries,
              const Entry* animation);
    const ModelAppearance* Single(const std::string& key, const Entry& entry, bool animated);
    std::filesystem::path root_;
    Entry npcAnimation_;
    std::map<std::string, Entry> staticParts_, animatedParts_, environment_, robots_;
    std::map<std::string, Preset> presets_;
    std::map<int, std::array<ModelMath::Vec4, 4>> palettes_;
    std::map<std::filesystem::path, std::shared_ptr<ModelAsset>> models_;
    std::map<std::string, ModelAppearance> appearances_;
    size_t decodedBytes_ = 0;
};
