#pragma once

#include <filesystem>
#include <memory>
#include <string_view>

namespace ActionRPG
{
    enum class AudioChannel { Master, Effects, Music };

    // Owned by Application; all public operations run on the window/game thread.
    class AudioMixer final
    {
    public:
        AudioMixer();
        ~AudioMixer();
        AudioMixer(const AudioMixer&) = delete;
        AudioMixer& operator=(const AudioMixer&) = delete;

        [[nodiscard]] bool IsAvailable() const;
        [[nodiscard]] float GetVolume(AudioChannel inChannel) const;
        [[nodiscard]] bool SetVolume(AudioChannel inChannel, float inVolume);
        [[nodiscard]] bool PlayMusic(const std::filesystem::path& inPath);
        [[nodiscard]] bool LoadEffect(const std::filesystem::path& inPath, std::string_view inId = {});
        [[nodiscard]] bool PlayEffect(std::string_view inId = {});
        [[nodiscard]] bool HasSounds() const;
        [[nodiscard]] std::wstring_view GetNotice() const;
        void Update();
        void Stop();

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
}
