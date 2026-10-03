#include "Game/PlayerSkillPresentation.h"
#include "Game/InputCommandQueue.h"
#include "Graphics/D2DRenderer.h"
#include "Resources/AssetCatalog.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>

namespace ActionRPG
{
    namespace
    {
        using Json = nlohmann::json;
        using Contract = PlayerSkills::Catalog;
        InputKey Key(const std::string& inName)
        {
            if (inName == "Left") return InputKey::MoveLeft;
            if (inName == "Right") return InputKey::MoveRight;
            if (inName == "Up") return InputKey::MoveUp;
            if (inName == "Down") return InputKey::MoveDown;
            if (inName == "Z") return InputKey::ActionZ;
            if (inName == "X") return InputKey::ActionX;
            if (inName == "C") return InputKey::ActionC;
            return InputKey::ActionV;
        }
    }
    PlayerSkillPresentation::PlayerSkillPresentation(const AssetCatalog& inAssets, D2DRenderer& inRenderer)
        : catalog(Contract::Load(inAssets.GetDataPath("PlayerSkills")))
    {
        const auto path = inAssets.GetDataPath("PlayerSkillVisuals");
        Contract::Require(std::filesystem::file_size(path) <= 16 * 1024 * 1024, "Visual file size exceeded.");
        std::ifstream file(path, std::ios::binary); Json source; file >> source;
        Contract::Keys(source, { "format", "schemaVersion", "skills" });
        Contract::Require(source.at("format") == "PlayerSkillVisuals" && source.at("schemaVersion") == 1
            && source.at("skills").is_array() && source.at("skills").size() == catalog.skills.size(), "Visual catalog mismatch.");
        for (const auto& skill : source.at("skills"))
        {
            Contract::Keys(skill, { "id", "characterId", "ground", "air" });
            const auto id = skill.at("id").get<std::string>();
            Contract::Require(catalog.skills.contains(id) && !visuals.contains(id), "Visual skill ID mismatch.");
            const auto& shared = catalog.skills.at(id);
            Contract::Require(skill.at("characterId") == shared.at("characterId"), "Visual character mismatch.");
            Visual value; value.id = id;
            for (const char* mode : { "ground", "air" })
            {
                const auto& variant = skill.at(mode); const auto& timeline = shared.at(mode);
                Contract::Require(variant.is_null() == timeline.is_null(), "Visual mode mismatch.");
                if (variant.is_null()) continue;
                Contract::Keys(variant, { "motion", "effect" });
                auto& output = std::string(mode) == "ground" ? value.ground : value.air;
                output.motion = LoadMotion(variant.at("motion"), inAssets, inRenderer);
                Contract::Require(output.motion->definition.at("frameCount") == timeline.at("frameCount")
                    && output.motion->definition.at("fps") == timeline.at("fps"), "Visual timing mismatch.");
                if (!variant.at("effect").is_null())
                {
                    const auto& effect = variant.at("effect");
                    Contract::Keys(effect, { "motion", "eventFrame", "offset", "loop" });
                    output.effect = LoadMotion(effect.at("motion"), inAssets, inRenderer);
                    const auto index = Contract::Integer(effect.at("eventFrame"), 0, timeline.at("frameCount").get<std::uint32_t>() - 1);
                    output.effectSeconds = index / timeline.at("fps").get<float>();
                    Contract::Point(effect.at("offset"));
                    output.offsetX = effect.at("offset").at("x").get<float>();
                    output.offsetY = effect.at("offset").at("y").get<float>();
                    output.offsetHeight = effect.at("offset").at("height").get<float>();
                    output.loop = effect.at("loop").get<bool>();
                }
            }
            visuals.emplace(id, std::move(value)); commandOrder.push_back(id);
        }
        std::sort(commandOrder.begin(), commandOrder.end(), [this](const auto& first, const auto& second)
        {
            const auto left = catalog.skills.at(first).at("input").at("command").size();
            const auto right = catalog.skills.at(second).at("input").at("command").size();
            return left != right ? left > right : first < second;
        });
    }
    std::shared_ptr<const PlayerSkillPresentation::Motion> PlayerSkillPresentation::LoadMotion(
        const Json& inMotion, const AssetCatalog& inAssets, D2DRenderer& inRenderer)
    {
        const auto path = inMotion.at("image").get<std::string>();
        Contract::Require(path.size() <= 240 && !path.empty() && path.find('\\') == std::string::npos
            && path.find(':') == std::string::npos && path.front() != '/', "Invalid visual image path.");
        const auto width = Contract::Integer(inMotion.at("width"), 1, 8192), height = Contract::Integer(inMotion.at("height"), 1, 8192);
        const auto count = Contract::Integer(inMotion.at("frameCount"), 1, 512);
        Contract::Number(inMotion.at("fps"), 0.001, 240);
        Contract::Require(inMotion.at("frames").is_array() && inMotion.at("frames").size() == count, "Visual frame count mismatch.");
        if (inMotion.contains("renderSize"))
        {
            Contract::Number(inMotion.at("renderSize").at("width"), 0.01, 10000);
            Contract::Number(inMotion.at("renderSize").at("height"), 0.01, 10000);
        }
        else Contract::Number(inMotion.value("scaleToMovement", 1.0), 0.01, 20);
        std::unordered_set<std::uint32_t> indexes;
        for (const auto& frame : inMotion.at("frames"))
        {
            Contract::Require(indexes.insert(Contract::Integer(frame.at("index"), 0, count - 1)).second, "Duplicate visual frame.");
            const auto& rectangle = frame.at("sourceRect"), pivot = frame.at("pivot");
            const double x = Contract::Number(rectangle.at("x"), 0, width), y = Contract::Number(rectangle.at("y"), 0, height);
            const double w = Contract::Number(rectangle.at("width"), 0.01, width), h = Contract::Number(rectangle.at("height"), 0.01, height);
            Contract::Require(x + w <= width + 0.001 && y + h <= height + 0.001, "Visual frame exceeds image.");
            Contract::Number(pivot.at("x"), 0, w); Contract::Number(pivot.at("y"), 0, h);
        }
        auto bitmap = bitmaps.find(path);
        if (bitmap == bitmaps.end())
        {
            const auto imagePath = inAssets.GetAssetPath(path);
            const auto pixels = static_cast<std::uint64_t>(width) * height;
            Contract::Require(decodedPixels + pixels <= 64 * 1024 * 1024
                && std::filesystem::file_size(imagePath) <= 15 * 1024 * 1024, "Visual image memory/file budget exceeded.");
            bitmap = bitmaps.emplace(path, inRenderer.LoadBitmap(imagePath)).first; decodedPixels += pixels;
        }
        const auto size = bitmap->second->GetPixelSize();
        Contract::Require(size.width == width && size.height == height, "Actual image dimensions differ.");
        auto result = std::make_shared<Motion>(); result->definition = inMotion; result->bitmap = bitmap->second;
        return result;
    }
    void PlayerSkillPresentation::ValidateServer(const PlayerSkills::Catalog& inCatalog) const
    {
        Contract::Require(inCatalog.source == catalog.source, "Server/client skill catalogs differ. Install the same package on both ends.");
    }
    std::string PlayerSkillPresentation::TryCommand(InputCommandQueue& inQueue, std::uint32_t inCharacterId, bool inAirborne) const
    {
        for (const auto& id : commandOrder)
        {
            const auto& skill = catalog.skills.at(id);
            if (catalog.characterIds.at(skill.at("characterId").get<std::string>()) != inCharacterId
                || skill.at(inAirborne ? "air" : "ground").is_null()) continue;
            std::vector<InputKey> keys;
            for (const auto& key : skill.at("input").at("command")) keys.push_back(Key(key.get<std::string>()));
            if (inQueue.TryConsume(keys, skill.at("input").at("maxStepSeconds").get<double>())) return id;
        }
        return {};
    }
    void PlayerSkillPresentation::Draw(const Motion& inMotion, float inSeconds, bool inLoop, float inX,
        float inY, bool inFacingLeft, D2DRenderer& inRenderer)
    {
        const auto& motion = inMotion.definition;
        const auto count = motion.at("frameCount").get<std::uint32_t>();
        auto index = static_cast<std::uint32_t>(std::max(0.0f, inSeconds) * motion.at("fps").get<float>());
        index = inLoop ? index % count : std::min(index, count - 1);
        const auto frame = std::find_if(motion.at("frames").begin(), motion.at("frames").end(), [index](const auto& value) { return value.at("index") == index; });
        const auto& rectangle = frame->at("sourceRect"), pivot = frame->at("pivot");
        const float x = rectangle.at("x").get<float>(), y = rectangle.at("y").get<float>();
        const float w = rectangle.at("width").get<float>(), h = rectangle.at("height").get<float>();
        const float scale = motion.value("scaleToMovement", 1.0f);
        const float width = motion.contains("renderSize") ? motion.at("renderSize").at("width").get<float>() : w * scale;
        const float height = motion.contains("renderSize") ? motion.at("renderSize").at("height").get<float>() : h * scale;
        const float anchorX = pivot.at("x").get<float>() / w, anchorY = pivot.at("y").get<float>() / h;
        const float left = inX - width * (inFacingLeft ? 1 - anchorX : anchorX), top = inY - height * anchorY;
        inRenderer.DrawBitmap(inMotion.bitmap.Get(), D2D1::RectF(x, y, x + w, y + h),
            D2D1::RectF(left, top, left + width, top + height), inFacingLeft);
    }
    bool PlayerSkillPresentation::Render(const CombatPlayerState& inState, Vector2 inGround, float inHeight,
        float inElapsed, D2DRenderer& inRenderer, const Camera& inCamera) const
    {
        const auto found = visuals.find(inState.skillId);
        if (found == visuals.end() || inState.hp == 0 || inState.reaction != CombatReaction::None) return false;
        const auto& variant = inState.skillAirborne ? found->second.air : found->second.ground;
        if (!variant.motion) return false;
        const float seconds = inState.skillSeconds + inElapsed;
        const auto position = inCamera.WorldToScreen(inGround);
        const auto& motion = variant.motion->definition;
        const bool showMotion = seconds < motion.at("frameCount").get<float>() / motion.at("fps").get<float>();
        if (showMotion)
        {
            inRenderer.FillEllipse(position.x, position.y, 24, 8, D2D1::ColorF(0.0f, 0.0f, 0.0f, .3f));
            Draw(*variant.motion, seconds, false, position.x, position.y - inHeight, inState.facingLeft, inRenderer);
        }
        if (variant.effect && seconds >= variant.effectSeconds)
        {
            const auto& effect = variant.effect->definition;
            const float effectTime = seconds - variant.effectSeconds;
            if ((variant.loop && showMotion) || (!variant.loop && effectTime < effect.at("frameCount").get<float>() / effect.at("fps").get<float>()))
            {
                const auto effectPosition = inCamera.WorldToScreen({ inGround.x + (inState.facingLeft ? -variant.offsetX : variant.offsetX), inGround.y + variant.offsetY });
                Draw(*variant.effect, effectTime, variant.loop, effectPosition.x, effectPosition.y - inHeight - variant.offsetHeight, inState.facingLeft, inRenderer);
            }
        }
        return showMotion;
    }
}
