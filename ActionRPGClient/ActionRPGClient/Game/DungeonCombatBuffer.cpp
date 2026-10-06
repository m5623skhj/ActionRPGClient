#include "Game/DungeonCombatBuffer.h"
#include <algorithm>
#include <cmath>

namespace ActionRPG
{
    namespace
    {
        constexpr double INTERPOLATION_DELAY_MS = 125.0;
        constexpr double MAX_EXTRAPOLATION_MS = 100.0;
        constexpr float TELEPORT_DISTANCE = 200.0f;
        double NowMs()
        {
            return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
        }
        template<class T, class Predicate>
        const T* Find(const std::vector<T>& inValues, Predicate inPredicate)
        {
            const auto found = std::find_if(inValues.begin(), inValues.end(), inPredicate);
            return found == inValues.end() ? nullptr : &*found;
        }
        bool Continuous(const CombatActorState& inA, const CombatActorState& inB)
        {
            const float dx = inB.position.x - inA.position.x, dy = inB.position.y - inA.position.y;
            return inA.hp != 0 && inB.hp != 0 && dx * dx + dy * dy < TELEPORT_DISTANCE * TELEPORT_DISTANCE;
        }
        bool ContinuousPlayer(const CombatPlayerState& inA, const CombatPlayerState& inB, const float inSeconds)
        {
            if (Continuous(inA, inB)) return true;
            if (inA.hp == 0 || inB.hp == 0 || inA.slideSequence == 0 || inA.slideSequence != inB.slideSequence
                || (!inA.slideActive && !inB.slideActive) || inSeconds <= 0.0f || inSeconds > 0.5f) return false;
            // A fast slide may exceed the ordinary teleport threshold within one valid snapshot interval.
            const float duration = std::max(inA.slideDurationSeconds, inB.slideDurationSeconds);
            const float limit = std::max(TELEPORT_DISTANCE,
                std::max(inA.slideSpeed, inB.slideSpeed) * std::min(inSeconds, duration) + 16.0f);
            const float dx = inB.position.x - inA.position.x, dy = inB.position.y - inA.position.y;
            return dx * dx + dy * dy <= limit * limit;
        }
        void Pose(CombatActorState& outState, const CombatActorState& inA, const CombatActorState& inB,
            const float inRatio, const float inSeconds)
        {
            outState.position = {std::lerp(inA.position.x, inB.position.x, inRatio), std::lerp(inA.position.y, inB.position.y, inRatio)};
            outState.height = std::max(0.0f, std::lerp(inA.height, inB.height, inRatio));
            outState.verticalSpeed = std::lerp(inA.verticalSpeed, inB.verticalSpeed, std::min(inRatio, 1.0f));
            const float dx = inB.position.x - inA.position.x, dy = inB.position.y - inA.position.y;
            outState.presentationSpeed = std::sqrt(dx * dx + dy * dy) / std::max(0.001f, inSeconds);
            outState.presentationMoving = outState.presentationSpeed > 1.0f;
        }
        void Advance(CombatActorState& outState, const float inSeconds)
        {
            outState.reactionSeconds = std::max(0.0f, outState.reactionSeconds - inSeconds);
        }
    }

    void DungeonCombatBuffer::Clear()
    {
        frames.clear(); lastPresentation.reset(); clockOffsetMs = lastDisplayTimeMs = 0; lastArrival = {};
    }

    void DungeonCombatBuffer::Push(const DungeonCombatSnapshot& inSnapshot)
    {
        const auto now = std::chrono::steady_clock::now();
        if (!frames.empty() && (frames.back().roomId != inSnapshot.roomId || frames.back().mapEpoch != inSnapshot.mapEpoch
            || frames.back().mapId != inSnapshot.mapId || frames.back().realtime != inSnapshot.realtime
            || now - lastArrival > std::chrono::milliseconds(500))) Clear();
        if (!frames.empty() && (inSnapshot.serverTimeMs < frames.back().serverTimeMs
            || (inSnapshot.realtime && inSnapshot.snapshotSequence <= frames.back().snapshotSequence))) return;
        const double offset = NowMs() - static_cast<double>(inSnapshot.serverTimeMs);
        if (frames.empty()) clockOffsetMs = offset;
        else clockOffsetMs += std::clamp(offset - clockOffsetMs, -2.0, 0.25); // Slow clock correction cannot rewind display time.
        lastArrival = now;
        if (!frames.empty() && frames.back().serverTimeMs == inSnapshot.serverTimeMs) frames.back() = inSnapshot;
        else frames.push_back(inSnapshot);
        while (frames.size() > 8) frames.pop_front();
    }

    bool DungeonCombatBuffer::HasFreshRealtime() const
    {
        return !frames.empty() && frames.back().realtime
            && std::chrono::steady_clock::now() - lastArrival < std::chrono::milliseconds(500);
    }

    // Interpolate matching living actors only. Latest complete membership/HP is authoritative.
    // Beyond the last sample, at most 100ms of measured velocity is used; >500ms stalls hold the last pose.
    std::optional<DungeonCombatSnapshot> DungeonCombatBuffer::Sample()
    {
        if (frames.empty()) return std::nullopt;
        const bool stalled = std::chrono::steady_clock::now() - lastArrival > std::chrono::milliseconds(500);
        if (stalled && lastPresentation)
        {
            auto result = *lastPresentation;
            for (auto& actor : result.players) actor.presentationMoving = false;
            for (auto& actor : result.monsters) actor.presentationMoving = false;
            return result;
        }
        const auto& latest = frames.back();
        double target = NowMs() - clockOffsetMs - INTERPOLATION_DELAY_MS;
        target = std::max(lastDisplayTimeMs, std::min(target, static_cast<double>(latest.serverTimeMs) + MAX_EXTRAPOLATION_MS));
        lastDisplayTimeMs = target;
        const DungeonCombatSnapshot* a = &frames.front();
        const DungeonCombatSnapshot* b = a;
        for (const auto& frame : frames)
        {
            if (static_cast<double>(frame.serverTimeMs) <= target) { a = &frame; b = a; }
            else { b = &frame; break; }
        }
        if (a == b && a == &latest && frames.size() >= 2)
        { a = &frames[frames.size() - 2]; b = &latest; }
        const double span = static_cast<double>(b->serverTimeMs - a->serverTimeMs);
        const float ratio = span > 0 ? static_cast<float>(std::clamp((target - static_cast<double>(a->serverTimeMs)) / span,
            0.0, 1.0 + MAX_EXTRAPOLATION_MS / span)) : 0.0f;
        const float boundedRatio = latest.cleared || stalled ? std::min(ratio, 1.0f) : ratio;
        const bool useB = target >= static_cast<double>(b->serverTimeMs);
        const float age = static_cast<float>(std::clamp((target - static_cast<double>(useB ? b->serverTimeMs : a->serverTimeMs)) / 1000.0, 0.0, 0.5));
        DungeonCombatSnapshot result = latest;
        for (auto& player : result.players)
        {
            const auto pa = Find(a->players, [&player](const auto& value) { return value.playerId == player.playerId; });
            const auto pb = Find(b->players, [&player](const auto& value) { return value.playerId == player.playerId; });
            if (!pa || !pb || !ContinuousPlayer(*pa, *pb, static_cast<float>(span / 1000.0)) || player.hp == 0) continue;
            const auto hp = player.hp, maxHp = player.maxHp;
            player = useB ? *pb : *pa;
            float playerRatio = boundedRatio;
            if (playerRatio > 1.0f && (pa->slideActive || pb->slideActive))
            {
                // A completed slide cannot coast past its endpoint during a missing frame.
                playerRatio = !pb->slideActive || span <= 0.0 ? 1.0f
                    : std::min(playerRatio, 1.0f + std::max(0.0f, pb->slideDurationSeconds - pb->slideSeconds)
                        / static_cast<float>(span / 1000.0));
            }
            Pose(player, *pa, *pb, playerRatio, static_cast<float>(span / 1000.0));
            Advance(player, age); player.shotSeconds += age; player.jumpSeconds += age;
            if (player.slideActive)
            {
                player.slideSeconds = std::min(player.slideDurationSeconds, player.slideSeconds + age);
                if (player.slideSeconds >= player.slideDurationSeconds) player.slideActive = false;
            }
            if (playerRatio < boundedRatio)
            {
                player.presentationMoving = false;
                player.presentationSpeed = 0.0f;
            }
            if (!player.skillId.empty()) player.skillSeconds += age;
            for (auto& buff : player.buffs) buff.remainingSeconds = std::max(0.0f, buff.remainingSeconds - age);
            player.hp = hp; player.maxHp = maxHp;
        }
        for (auto& monster : result.monsters)
        {
            const auto ma = Find(a->monsters, [&monster](const auto& value) { return value.instanceId == monster.instanceId; });
            const auto mb = Find(b->monsters, [&monster](const auto& value) { return value.instanceId == monster.instanceId; });
            if (!ma || !mb || !Continuous(*ma, *mb) || monster.hp == 0) continue;
            const auto hp = monster.hp, maxHp = monster.maxHp;
            monster = useB ? *mb : *ma;
            Pose(monster, *ma, *mb, boundedRatio, static_cast<float>(span / 1000.0));
            Advance(monster, age); monster.actionSeconds += age;
            monster.hp = hp; monster.maxHp = maxHp;
        }
        for (auto& projectile : result.projectiles)
        {
            const auto pa = Find(a->projectiles, [&projectile](const auto& value) { return value.id == projectile.id; });
            const auto pb = Find(b->projectiles, [&projectile](const auto& value) { return value.id == projectile.id; });
            if (!pa || !pb) continue;
            projectile.position = {std::lerp(pa->position.x, pb->position.x, boundedRatio), std::lerp(pa->position.y, pb->position.y, boundedRatio)};
            projectile.height = std::max(0.0f, std::lerp(pa->height, pb->height, boundedRatio));
        }
        lastPresentation = result;
        return result;
    }
}
