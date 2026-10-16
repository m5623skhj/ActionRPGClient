#include "Game/PlayerSkillPresentation.h"
#include "Game/InputCommandQueue.h"
#include "Graphics/D2DRenderer.h"
#include "Resources/AssetCatalog.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <stdexcept>
#include <utility>

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
                const auto count = timeline.at("frameCount").get<std::uint32_t>();
                for (std::uint32_t index = 0; index <= count; ++index)
                {
                    const double expected = Contract::FrameStartSeconds(timeline, index);
                    const float actual = index == count ? output.motion->animation.GetDuration()
                        : output.motion->animation.GetFrameStartSeconds(index);
                    Contract::Require(std::abs(Contract::FrameStartSeconds(output.motion->definition, index) - expected) <= 0.0001
                        && std::abs(actual - expected) <= 0.0001, "Visual frame timeline differs from gameplay.");
                }
                if (!variant.at("effect").is_null())
                {
                    const auto& effect = variant.at("effect");
                    Contract::Keys(effect, { "motion", "eventFrame", "offset", "loop" });
                    output.effect = LoadMotion(effect.at("motion"), inAssets, inRenderer);
                    const auto index = Contract::Integer(effect.at("eventFrame"), 0, timeline.at("frameCount").get<std::uint32_t>() - 1);
                    output.effectSeconds = static_cast<float>(Contract::FrameStartSeconds(timeline, index));
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
        if (inMotion.contains("frameDurationsSeconds"))
            Contract::Require(inMotion.at("frameDurationsSeconds").is_array()
                && inMotion.at("frameDurationsSeconds").size() == count, "Visual frame duration count mismatch.");
        const double total = Contract::MotionDurationSeconds(inMotion);
        Contract::Require(std::isfinite(total) && total > 0.0 && total <= 60.0, "Visual duration exceeds limit.");
        if (inMotion.contains("durationSeconds"))
            Contract::Require(std::abs(Contract::Number(inMotion.at("durationSeconds"), 0.001, 60) - total) <= 0.0001,
                "Visual duration differs from timeline.");
        std::vector<float> durations;
        std::vector<SpriteFrame> frames(count);
        double previous{};
        for (std::uint32_t index = 0; index < count; ++index)
        {
            const double duration = Contract::FrameDurationSeconds(inMotion, index);
            const float value = static_cast<float>(duration);
            const double next = previous + duration;
            Contract::Require(std::isfinite(duration) && value > 0.0f && std::isfinite(value)
                && std::isfinite(next) && next > previous
                && static_cast<float>(next) > static_cast<float>(previous), "Invalid visual frame duration.");
            durations.push_back(value);
            previous = next;
        }
        std::unordered_set<std::uint32_t> indexes;
        for (const auto& frame : inMotion.at("frames"))
        {
            Contract::Require(indexes.insert(Contract::Integer(frame.at("index"), 0, count - 1)).second, "Duplicate visual frame.");
            const auto& rectangle = frame.at("sourceRect"), pivot = frame.at("pivot");
            const double x = Contract::Number(rectangle.at("x"), 0, width), y = Contract::Number(rectangle.at("y"), 0, height);
            const double w = Contract::Number(rectangle.at("width"), 0.01, width), h = Contract::Number(rectangle.at("height"), 0.01, height);
            Contract::Require(x + w <= width + 0.001 && y + h <= height + 0.001, "Visual frame exceeds image.");
            if (frame.contains("muzzles"))
            {
                const auto& muzzles = frame.at("muzzles");
                Contract::Require(muzzles.is_array() && muzzles.size() <= 2, "Invalid muzzle anchors.");
                for (const auto& muzzle : muzzles)
                {
                    Contract::Keys(muzzle, { "x", "y", "directionX", "directionY" });
                    Contract::Number(muzzle.at("x"), 0, w); Contract::Number(muzzle.at("y"), 0, h);
                    const double dx = Contract::Number(muzzle.at("directionX"), -1, 1);
                    const double dy = Contract::Number(muzzle.at("directionY"), -1, 1);
                    Contract::Require(std::hypot(dx, dy) >= 0.001, "Zero muzzle direction.");
                }
            }
            const float pivotX = static_cast<float>(Contract::Number(pivot.at("x"), 0, w));
            const float pivotY = static_cast<float>(Contract::Number(pivot.at("y"), 0, h));
            frames[frame.at("index").get<std::uint32_t>()] = {
                D2D1::RectF(static_cast<float>(x), static_cast<float>(y), static_cast<float>(x + w), static_cast<float>(y + h)),
                D2D1::Point2F(pivotX, pivotY)};
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
        result->animation = SpriteAnimation(bitmap->second, std::move(frames),
            1.0f / inMotion.at("fps").get<float>(), 1.0f, std::move(durations));
        return result;
    }
    void PlayerSkillPresentation::ValidateServer(const PlayerSkills::Catalog& inCatalog) const
    {
        Contract::Require(inCatalog.source == catalog.source, "Server/client skill catalogs differ. Install the same package on both ends.");
    }
    std::string PlayerSkillPresentation::TryCommand(InputCommandQueue& inQueue, std::uint32_t inCharacterId,
        const std::unordered_map<std::string,std::uint32_t>& inSkillLevels,bool& outMatched) const
    {
        outMatched=false;
        for (const auto& id : commandOrder)
        {
            const auto& skill = catalog.skills.at(id);
            if (catalog.characterIds.at(skill.at("characterId").get<std::string>()) != inCharacterId) continue;
            std::vector<InputKey> keys;
            for (const auto& key : skill.at("input").at("command")) keys.push_back(Key(key.get<std::string>()));
            if (inQueue.TryConsume(keys, skill.at("input").at("maxStepSeconds").get<double>()))
            {
                outMatched=true; // Always consume a matched command, even if it cannot be used now.
                const auto learned=inSkillLevels.find(id);
                return learned!=inSkillLevels.end() && learned->second>0 ? id : std::string{};
            }
        }
        return {};
    }
    void PlayerSkillPresentation::Draw(const Motion& inMotion, float inSeconds, bool inLoop, float inX,
        float inY, bool inFacingLeft, D2DRenderer& inRenderer)
    {
        const auto& motion = inMotion.definition;
        const auto index = inMotion.animation.GetFrameAt(std::max(0.0f, inSeconds), inLoop);
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
        float inSeconds, D2DRenderer& inRenderer, const Camera& inCamera) const
    {
        const auto found = visuals.find(inState.skillId);
        if (found == visuals.end() || inState.hp == 0 || inState.reaction != CombatReaction::None) return false;
        const auto& variant = inState.skillAirborne ? found->second.air : found->second.ground;
        if (!variant.motion) return false;
        const float seconds = inSeconds; // Player supplies the monotonic clock for this cast.
        const auto position = inCamera.WorldToScreen(inGround);
        const bool showMotion = seconds < variant.motion->animation.GetDuration();
        if (showMotion)
        {
            inRenderer.FillEllipse(position.x, position.y, 24, 8, D2D1::ColorF(0.0f, 0.0f, 0.0f, .3f));
            Draw(*variant.motion, seconds, false, position.x, position.y - inHeight, inState.facingLeft, inRenderer);
            const auto& definition = catalog.skills.at(inState.skillId);
            if (definition.at("type") == "area")
            {
                const auto& motion = variant.motion->definition;
                const auto index = variant.motion->animation.GetFrameAt(std::max(0.0f, seconds), false);
                const auto frame = std::find_if(motion.at("frames").begin(), motion.at("frames").end(),
                    [index](const auto& value) { return value.at("index") == index; });
                if (frame->contains("muzzles"))
                {
                    const auto& timeline = definition.at(inState.skillAirborne ? "air" : "ground");
                    const double start = Contract::FrameStartSeconds(timeline, timeline.at("eventFrame").get<std::uint32_t>());
                    const double end = Contract::FrameStartSeconds(timeline, timeline.at("endFrame").get<std::uint32_t>() + 1);
                    if (seconds >= start && seconds < end)
                    {
                        const float interval = static_cast<float>((end - start) / definition.at("execution").at("hitCount").get<float>());
                        const float phase = std::fmod(static_cast<float>(seconds - start), interval) / interval;
                        const auto& rect = frame->at("sourceRect");
                        const auto& pivot = frame->at("pivot");
                        const float sx = motion.contains("renderSize") ? motion.at("renderSize").at("width").get<float>()
                            / rect.at("width").get<float>() : motion.value("scaleToMovement", 1.0f);
                        const float sy = motion.contains("renderSize") ? motion.at("renderSize").at("height").get<float>()
                            / rect.at("height").get<float>() : sx;
                        const float sign = inState.facingLeft ? -1.0f : 1.0f;
                        std::size_t muzzleIndex{};
                        for (const auto& muzzle : frame->at("muzzles"))
                        {
                            // Offset the second pistol within the same pulse; damage still uses the server's 20 hits.
                            const float delay = muzzleIndex++ == 0 ? 0.0f : .22f;
                            const float shotPhase = (phase - delay) / (1.0f - delay);
                            if (shotPhase < 0.0f) continue;
                            const float x = position.x + sign * (muzzle.at("x").get<float>() - pivot.at("x").get<float>()) * sx;
                            const float y = position.y - inHeight + (muzzle.at("y").get<float>() - pivot.at("y").get<float>()) * sy;
                            float dx = sign * muzzle.at("directionX").get<float>(), dy = muzzle.at("directionY").get<float>();
                            const float length = std::hypot(dx, dy); dx /= length; dy /= length;
                            const float travel = 180.0f * shotPhase;
                            inRenderer.DrawLuminousProjectile(x + dx * travel, y + dy * travel, dx, dy, 2.4f, travel,
                                D2D1::ColorF(.2f, .65f, 1.0f, 1.0f - shotPhase),
                                D2D1::ColorF(.9f, .98f, 1.0f, 1.0f - shotPhase));
                            if (shotPhase < .55f)
                            {
                                const float alpha = 1.0f - shotPhase / .55f;
                                const float flameLength = 14.0f + 12.0f * alpha;
                                const float sideX = -dy, sideY = dx;
                                inRenderer.FillEllipse(x + dx * 5, y + dy * 5, 11, 9, D2D1::ColorF(1.0f, .48f, .08f, .4f * alpha));
                                inRenderer.DrawLine(x, y, x + dx * flameLength, y + dy * flameLength,
                                    D2D1::ColorF(1.0f, .65f, .12f, alpha), 6.0f * alpha + 1.0f);
                                inRenderer.DrawLine(x, y, x + dx * flameLength * .8f, y + dy * flameLength * .8f,
                                    D2D1::ColorF(1.0f, .98f, .8f, alpha), 2.0f * alpha + 1.0f);
                                for (const float side : { -1.0f, 1.0f })
                                {
                                    inRenderer.DrawLine(x + dx * 4, y + dy * 4,
                                        x + dx * 12 + sideX * side * 8 * alpha,
                                        y + dy * 12 + sideY * side * 8 * alpha,
                                        D2D1::ColorF(1.0f, .8f, .25f, alpha), 2.0f * alpha);
                                    const float sparkX = x + dx * (12 + 22 * shotPhase) + sideX * side * (5 + 14 * shotPhase);
                                    const float sparkY = y + dy * (12 + 22 * shotPhase) + sideY * side * (5 + 14 * shotPhase) + 10 * shotPhase * shotPhase;
                                    inRenderer.DrawLine(sparkX, sparkY, sparkX - dx * 4, sparkY - dy * 4,
                                        D2D1::ColorF(1.0f, .65f, .15f, alpha), 1.4f);
                                }
                            }
                        }
                    }
                }
            }

        }
        if (variant.effect && seconds >= variant.effectSeconds)
        {
            const float effectTime = seconds - variant.effectSeconds;
            if ((variant.loop && showMotion) || (!variant.loop && effectTime < variant.effect->animation.GetDuration()))
            {
                const auto effectPosition = inCamera.WorldToScreen({ inGround.x + (inState.facingLeft ? -variant.offsetX : variant.offsetX), inGround.y + variant.offsetY });
                Draw(*variant.effect, effectTime, variant.loop, effectPosition.x, effectPosition.y - inHeight - variant.offsetHeight, inState.facingLeft, inRenderer);
            }
        }
        return showMotion;
    }
}
