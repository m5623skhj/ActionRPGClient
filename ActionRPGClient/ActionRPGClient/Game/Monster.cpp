#include "Game/Monster.h"
#include "Game/Camera.h"
#include "Graphics/D2DRenderer.h"
#include "Resources/AssetCatalog.h"

#include <nlohmann/json.hpp>
#include <d2d1_1helper.h>
#include <cmath>
#include <algorithm>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <utility>
#include <unordered_set>

namespace
{
    using Json = nlohmann::json;
    using Motion = ActionRPG::MonsterMotion;

    Json ReadJson(const std::filesystem::path& inPath)
    {
        std::ifstream file(inPath);
        if (!file) throw std::runtime_error("Cannot read monster visual data: " + inPath.string());
        return Json::parse(file);
    }

    float Number(const Json& inValue, const bool inPositive = false)
    {
        if (!inValue.is_number()) throw std::runtime_error("Invalid monster visual number.");
        const float value = inValue.get<float>();
        if (!std::isfinite(value) || value < 0.0f || value > 1000000.0f || (inPositive && value == 0.0f))
            throw std::runtime_error("Invalid monster visual number.");
        return value;
    }

    std::uint32_t Integer(const Json& inValue)
    {
        if (!inValue.is_number_integer()) throw std::runtime_error("Invalid monster visual integer.");
        const auto value = inValue.get<std::int64_t>();
        if (value < 0 || value > std::numeric_limits<std::uint32_t>::max())
            throw std::runtime_error("Invalid monster visual integer.");
        return static_cast<std::uint32_t>(value);
    }

    ActionRPG::SpriteFrame Frame(const Json& inValue)
    {
        const auto& rect = inValue.at("sourceRect");
        const float x = Number(rect.at("x"));
        const float y = Number(rect.at("y"));
        const float width = Number(rect.at("width"), true);
        const float height = Number(rect.at("height"), true);
        const auto& pivot = inValue.at("pivot");
        const float pivotX = Number(pivot.at("x"));
        const float pivotY = Number(pivot.at("y"));
        if (pivotX > width || pivotY > height) throw std::runtime_error("Invalid monster pivot.");
        return { D2D1::RectF(x, y, x + width, y + height), D2D1::Point2F(pivotX, pivotY) };
    }

    ActionRPG::MonsterClipDefinition ReadClip(const Json& inMotion, const std::string& inImageFolder,
        const float inBaseScale, const ActionRPG::AssetCatalog& inAssets)
    {
        ActionRPG::MonsterClipDefinition clip;
        const std::filesystem::path image = inMotion.at("image").get<std::string>();
        if (image.empty() || image.has_parent_path()) throw std::runtime_error("Expected a monster image filename.");
        clip.image = inImageFolder + image.generic_string();
        (void)inAssets.GetAssetPath(clip.image);
        const float fps = Number(inMotion.at("fps"), true);
        if (fps > 240.0f) throw std::runtime_error("Monster playback rate exceeds 240 fps.");
        clip.frameSeconds = 1.0f / fps;
        clip.scale = inBaseScale * Number(inMotion.at("scaleToMovement"), true);
        clip.loop = inMotion.at("loop").get<bool>();
        clip.holdLastFrame = inMotion.at("holdLastFrame").get<bool>();
        const auto count = Integer(inMotion.at("frameCount"));
        const float width = Number(inMotion.at("width"), true);
        const float height = Number(inMotion.at("height"), true);
        const auto& frames = inMotion.at("frames");
        if (!frames.is_array() || count == 0 || count > 4096 || frames.size() != count)
            throw std::runtime_error("Invalid monster frame count.");
        for (const auto& frame : frames)
        {
            if (Integer(frame.at("index")) != clip.frames.size()) throw std::runtime_error("Invalid monster frame order.");
            const auto parsed = Frame(frame);
            if (parsed.sourceRect.right > width || parsed.sourceRect.bottom > height)
                throw std::runtime_error("Monster frame exceeds the declared image size.");
            clip.frames.push_back(parsed);
        }
        return clip;
    }

    ActionRPG::MonsterClipDefinition Slice(const ActionRPG::MonsterClipDefinition& inClip, const Json& inRange)
    {
        if (!inRange.is_array() || inRange.size() != 2) throw std::runtime_error("Invalid monster motion phase.");
        const auto first = Integer(inRange.at(0));
        const auto last = Integer(inRange.at(1));
        if (first > last || last >= inClip.frames.size()) throw std::runtime_error("Invalid monster motion range.");
        auto clip = inClip;
        clip.frames.assign(inClip.frames.begin() + first, inClip.frames.begin() + last + 1);
        clip.loop = false;
        clip.holdLastFrame = true;
        return clip;
    }
}

namespace ActionRPG
{
    MonsterCatalog::MonsterCatalog(const AssetCatalog& inAssetCatalog)
    {
        const auto registry = ReadJson(inAssetCatalog.GetDataPath("Monsters"));
        const auto& monsters = registry.at("monsters");
        if (registry.at("version") != 1 || !monsters.is_array() || monsters.empty() || monsters.size() > 4096)
            throw std::runtime_error("Invalid client monster registry.");
        std::unordered_map<std::string, Json> metadataFiles;
        std::unordered_set<std::string> monsterIds;
        for (const auto& entry : monsters)
        {
            const auto dataId = Integer(entry.at("dataId"));
            MonsterVisualDefinition definition;
            definition.monsterId = entry.at("monsterId").get<std::string>();
            if (dataId == 0 || definition.monsterId.empty() || definition.monsterId.size() > 128
                || !monsterIds.insert(definition.monsterId).second || definitions.contains(dataId))
                throw std::runtime_error("Duplicate or invalid client monster ID.");
            if (entry.contains("idleAnimationSection"))
            {
                definition.idleAnimationSection = entry.at("idleAnimationSection").get<std::string>();
                if (definition.idleAnimationSection.empty()) throw std::runtime_error("Empty monster idle animation.");
            }
            else
            {
                const float renderHeight = Number(entry.at("renderHeight"), true);
                const auto metadataPath = entry.at("metadata").get<std::string>();
                if (!metadataFiles.contains(metadataPath))
                    metadataFiles.emplace(metadataPath, ReadJson(inAssetCatalog.GetAssetPath(metadataPath)));
                const auto& metadata = metadataFiles.at(metadataPath);
                if (metadata.at("version") != 1) throw std::runtime_error("Unsupported monster motion metadata.");
                const auto& character = metadata.at("characters").at(definition.monsterId);
                const auto& motions = character.at("motions");
                const float baseScale = renderHeight / Number(character.at("referenceStandingHeight"), true);
                const std::string folder = std::filesystem::path(metadataPath).parent_path().generic_string() + "/";
                for (const auto& [motion, name] : std::map<Motion, std::string>{
                    {Motion::Move, "move"}, {Motion::Hit, "hit"}, {Motion::Attack, "attack"}, {Motion::Death, "death"} })
                {
                    definition.clips.emplace(motion, ReadClip(motions.at(name), folder, baseScale, inAssetCatalog));
                    const auto& clip = definition.clips.at(motion);
                    if (clip.loop != (motion == Motion::Move)
                        || (motion == Motion::Death && !clip.holdLastFrame))
                        throw std::runtime_error("Invalid monster motion playback policy.");
                }
                const auto airborne = ReadClip(motions.at("airborne"), folder, baseScale, inAssetCatalog);
                if (airborne.loop) throw std::runtime_error("Airborne motion must not loop.");
                const auto& phases = motions.at("airborne").at("phases");
                for (const auto& [motion, name] : std::map<Motion, std::string>{
                    {Motion::AirborneLaunch, "launch"}, {Motion::AirborneHold, "airHold"},
                    {Motion::AirborneFall, "fall"}, {Motion::Knockdown, "knockdown"}, {Motion::GetUp, "getUp"} })
                    definition.clips.emplace(motion, Slice(airborne, phases.at(name)));
                // Launch ends in the annotated hold pose until authoritative flight state changes.
                auto& launch = definition.clips.at(Motion::AirborneLaunch);
                const auto& hold = definition.clips.at(Motion::AirborneHold);
                launch.frames.insert(launch.frames.end(), hold.frames.begin(), hold.frames.end());
                const auto& attack = motions.at("attack");
                for (const auto& name : { "start", "active", "end" })
                    (void)Slice(definition.clips.at(Motion::Attack), attack.at("phases").at(name));
                if (Integer(attack.at("suggestedImpactFrame")) >= definition.clips.at(Motion::Attack).frames.size())
                    throw std::runtime_error("Invalid monster impact frame.");
                const auto& idle = entry.at("idle");
                MonsterClipDefinition idleClip;
                idleClip.image = idle.at("image").get<std::string>();
                (void)inAssetCatalog.GetAssetPath(idleClip.image);
                idleClip.frames.push_back(Frame(idle));
                idleClip.scale = renderHeight / Number(idle.at("standingHeight"), true);
                idleClip.loop = true;
                definition.clips.emplace(Motion::Idle, std::move(idleClip));
            }
            definitions.emplace(dataId, std::move(definition));
        }
    }

    const MonsterVisualDefinition& MonsterCatalog::Get(const std::uint32_t inDataId) const
    {
        const auto found = definitions.find(inDataId);
        if (found == definitions.end()) throw std::runtime_error("Unregistered client monster ID: " + std::to_string(inDataId));
        return found->second;
    }

    Monster::Monster(const MonsterSpawn& inSpawn, const MonsterCatalog& inCatalog,
        const AssetCatalog& inAssetCatalog, D2DRenderer& inRenderer)
        : Character(inSpawn.position), instanceId(inSpawn.instanceId), dataId(inSpawn.dataId)
    {
        const auto& definition = inCatalog.Get(dataId);
        if (!definition.idleAnimationSection.empty())
        {
            const IniDocument animations(inAssetCatalog.GetDataPath("Animations"));
            const SpriteAnimation idle(inRenderer, inAssetCatalog, animations, definition.idleAnimationSection);
            for (const auto clipMotion : {Motion::Idle, Motion::Move, Motion::Hit, Motion::AirborneLaunch,
                Motion::AirborneHold, Motion::AirborneFall, Motion::Knockdown, Motion::GetUp, Motion::Attack, Motion::Death})
                clips.emplace(clipMotion, Clip{idle, clipMotion == Motion::Idle || clipMotion == Motion::Move, true});
        }
        else
        {
            std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID2D1Bitmap1>> bitmaps;
            for (const auto& [clipMotion, source] : definition.clips)
            {
                if (!bitmaps.contains(source.image))
                    bitmaps.emplace(source.image, inRenderer.LoadBitmap(inAssetCatalog.GetAssetPath(source.image)));
                clips.emplace(clipMotion, Clip{SpriteAnimation(bitmaps.at(source.image), source.frames,
                    source.frameSeconds, source.scale), source.loop, source.holdLastFrame});
            }
        }
        ConfigureSpawn(inSpawn);
    }

    Monster::Monster(const Monster& inTemplate, const MonsterSpawn& inSpawn)
        : Character(inTemplate), instanceId(inSpawn.instanceId), dataId(inSpawn.dataId), clips(inTemplate.clips)
    {
        if (dataId != inTemplate.dataId) throw std::runtime_error("Monster template ID mismatch.");
        ConfigureSpawn(inSpawn);
    }

    void Monster::ConfigureSpawn(const MonsterSpawn& inSpawn)
    {
        if (instanceId == 0 || inSpawn.maxHp == 0 || inSpawn.hp > inSpawn.maxHp
            || !std::isfinite(inSpawn.position.x) || !std::isfinite(inSpawn.position.y))
            throw std::runtime_error("Invalid monster spawn.");
        SetGroundPosition(inSpawn.position);
        SetFacingLeft(inSpawn.facingLeft);
        SetRunningEnabled(false);
        ResetActionState();
        if (inSpawn.hp == 0) PlayMotion(Motion::Death);
    }

    void Monster::ResetActionState()
    {
        Character::ResetActionState();
        serverState.reset(); serverPresentationSeconds = 0.0f;
        for (auto& entry : clips) entry.second.animation.Reset();
        motion = Motion::Idle;
    }

    void Monster::PlayMotion(const MonsterMotion inMotion)
    {
        if (!clips.contains(inMotion)) throw std::runtime_error("Unsupported monster motion.");
        if (motion == Motion::Death && inMotion != Motion::Death) return;
        motion = inMotion;
        clips.at(motion).animation.Reset();
    }

    void Monster::ApplyPresentationState(const MonsterMotion inMotion, const Vector2 inPosition,
        const bool inFacingLeft, const float inHeight)
    {
        if (!clips.contains(inMotion) || !std::isfinite(inPosition.x) || !std::isfinite(inPosition.y)
            || !std::isfinite(inHeight) || inHeight < 0.0f || inHeight > 1000000.0f)
            throw std::runtime_error("Invalid monster presentation state.");
        SetGroundPosition(inPosition);
        SetFacingLeft(inFacingLeft);
        SetPresentationHeight(inHeight);
        if (motion != inMotion) PlayMotion(inMotion);
    }

    void Monster::ApplyHit(const CharacterHitType inType)
    {
        PlayMotion(GetHeight() > 0.0f ? Motion::AirborneFall
            : inType == CharacterHitType::Airborne ? Motion::AirborneLaunch : Motion::Hit);
    }

    bool Monster::IsHitReacting() const
    {
        return motion == Motion::Hit || motion == Motion::AirborneLaunch
            || motion == Motion::AirborneHold || motion == Motion::AirborneFall
            || motion == Motion::Knockdown || motion == Motion::GetUp;
    }

    void Monster::Update(const float inDeltaSeconds)
    {
        if (!std::isfinite(inDeltaSeconds) || inDeltaSeconds < 0.0f)
            throw std::runtime_error("Invalid monster update interval.");
        auto& clip = clips.at(motion);
        if (serverState)
        {
            serverPresentationSeconds = std::min(0.25f, serverPresentationSeconds + inDeltaSeconds);
            const float blend = 1.0f - std::exp(-12.0f * inDeltaSeconds);
            auto position = GetGroundPosition();
            position.x += (serverState->position.x - position.x) * blend;
            position.y += (serverState->position.y - position.y) * blend;
            SetGroundPosition(position);
            float height = serverState->height;
            if (height > 0.0f && serverState->hp != 0)
                height = std::max(0.0f, height + serverState->verticalSpeed * serverPresentationSeconds
                    - 0.5f * serverRules.gravity * serverPresentationSeconds * serverPresentationSeconds);
            SetPresentationHeight(height);
            clip.animation.Update(inDeltaSeconds, clip.loop);
            return;
        }
        clip.animation.Update(inDeltaSeconds, clip.loop);
        if (!clip.loop && clip.animation.IsFinished()
            && (motion == Motion::Hit || motion == Motion::Attack || motion == Motion::GetUp || !clip.holdLastFrame))
            PlayMotion(Motion::Idle);
    }

    // HP/reaction precede AI presentation. Repeated snapshots do not restart death or attacks.
    void Monster::ApplyCombatState(const CombatMonsterState& inState, const CombatRules& inRules)
    {
        if (inState.instanceId != instanceId || inState.dataId != dataId)
            throw std::runtime_error("Combat monster identity mismatch.");
        MonsterMotion next = Motion::Idle;
        if (inState.hp == 0 || inState.reaction == CombatReaction::Dead) next = Motion::Death;
        else if (inState.reaction == CombatReaction::Hit) next = Motion::Hit;
        else if (inState.reaction == CombatReaction::Falling)
            next = inState.verticalSpeed > 0.0f ? Motion::AirborneLaunch : Motion::AirborneFall;
        else if (inState.reaction == CombatReaction::Down) next = Motion::Knockdown;
        else if (inState.reaction == CombatReaction::Rising) next = Motion::GetUp;
        else if (inState.actionStarted && !inState.actionComplete)
        {
            if (inState.actionType == "UseSkill")
            {
                if (inState.animationId != "attack") throw std::runtime_error("Unsupported monster skill animation.");
                next = Motion::Attack;
            }
            else if (inState.actionType == "PlayMotion")
            {
                if (inState.animationId == "move") next = Motion::Move;
                else if (inState.animationId == "attack") next = Motion::Attack;
                else if (inState.animationId == "hit") next = Motion::Hit;
                else if (inState.animationId == "airborne") next = Motion::AirborneLaunch;
                else if (inState.animationId != "idle") throw std::runtime_error("Unsupported monster animation ID.");
            }
            else if ((inState.actionType == "MoveToTarget" || inState.actionType == "ReturnToSpawn")
                && (!serverState || std::abs(serverState->position.x - inState.position.x) > 0.5f
                    || std::abs(serverState->position.y - inState.position.y) > 0.5f)) next = Motion::Move;
        }
        const bool newAction = serverState && (inState.aiNodeId != serverState->aiNodeId
            || inState.actionSeconds < serverState->actionSeconds || (!serverState->actionStarted && inState.actionStarted));
        if (next != motion || (next == Motion::Attack && newAction)) PlayMotion(next);
        SetFacingLeft(inState.facingLeft);
        SetPresentationHeight(inState.height);
        if (!serverState) SetGroundPosition(inState.position);
        if (next != Motion::Death)
        {
            auto& animation = clips.at(motion).animation;
            if (next == Motion::Attack || (inState.actionType == "PlayMotion" && inState.reaction == CombatReaction::None))
                animation.Seek(inState.actionSeconds, clips.at(motion).loop);
            else if (next == Motion::Hit || next == Motion::Knockdown || next == Motion::GetUp)
            {
                const float duration = next == Motion::Hit ? inRules.hitStunSeconds
                    : next == Motion::Knockdown ? inRules.downSeconds : inRules.riseSeconds;
                animation.Seek(std::max(0.0f, duration - inState.reactionSeconds) / duration * animation.GetDuration());
            }
        }
        serverState = inState; serverRules = inRules; serverPresentationSeconds = 0.0f;
    }

    void Monster::Render(D2DRenderer& inRenderer, const Camera& inCamera) const
    {
        const auto position = inCamera.WorldToScreen(GetGroundPosition());
        inRenderer.FillEllipse(position.x, position.y, WIDTH * 0.42f, 12.0f,
            D2D1::ColorF(0.02f, 0.03f, 0.05f, 0.45f));
        clips.at(motion).animation.Draw(inRenderer, position.x, position.y - GetHeight(), GetFacingLeft());
    }
}
