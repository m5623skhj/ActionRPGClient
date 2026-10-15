#include "Platform/ShutdownTrace.h"
#include "Platform/AudioMixer.h"

#include <Windows.h>
#include <xaudio2.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <fstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
    struct WaveData
    {
        WAVEFORMATEX format{};
        std::vector<BYTE> samples;
    };

    // Read only bounded RIFF chunks. PCM16 mono/stereo needs no decoder or external package.
    std::shared_ptr<WaveData> ReadWave(const std::filesystem::path& inPath)
    {
        std::ifstream input(inPath, std::ios::binary | std::ios::ate);
        const auto size = static_cast<std::streamoff>(input.tellg());
        constexpr std::streamoff MAX_WAVE_BYTES = 64 * 1024 * 1024;
        if (!input || size < 44 || size > MAX_WAVE_BYTES) throw std::runtime_error("Invalid WAV size.");
        std::vector<BYTE> bytes(static_cast<std::size_t>(size));
        input.seekg(0);
        if (!input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
            throw std::runtime_error("Unable to read WAV.");
        const auto word = [&](std::size_t offset)
        { return static_cast<std::uint16_t>(bytes[offset] | (static_cast<std::uint16_t>(bytes[offset + 1]) << 8)); };
        const auto dword = [&](std::size_t offset)
        { return static_cast<std::uint32_t>(word(offset)) | (static_cast<std::uint32_t>(word(offset + 2)) << 16); };
        const auto tag = [&](std::size_t offset, const char* value)
        { return std::equal(bytes.begin() + offset, bytes.begin() + offset + 4, value); };
        if (!tag(0, "RIFF") || !tag(8, "WAVE")) throw std::runtime_error("Expected RIFF WAV.");
        const std::uint64_t end = static_cast<std::uint64_t>(dword(4)) + 8;
        if (end < 12 || end > bytes.size()) throw std::runtime_error("Invalid RIFF length.");
        auto result = std::make_shared<WaveData>();
        bool hasFormat = false, hasData = false;
        for (std::uint64_t offset = 12; offset < end;)
        {
            if (end - offset < 8) throw std::runtime_error("Incomplete WAV chunk.");
            const auto count = dword(static_cast<std::size_t>(offset + 4));
            const auto data = static_cast<std::size_t>(offset + 8);
            if (count > end - data) throw std::runtime_error("WAV chunk exceeds RIFF length.");
            if (tag(static_cast<std::size_t>(offset), "fmt "))
            {
                if (hasFormat || count < 16) throw std::runtime_error("Invalid WAV format chunk.");
                auto& format = result->format;
                format.wFormatTag = word(data);
                format.nChannels = word(data + 2);
                format.nSamplesPerSec = dword(data + 4);
                format.nAvgBytesPerSec = dword(data + 8);
                format.nBlockAlign = word(data + 12);
                format.wBitsPerSample = word(data + 14);
                if (format.wFormatTag != WAVE_FORMAT_PCM || (format.nChannels != 1 && format.nChannels != 2)
                    || format.wBitsPerSample != 16 || format.nSamplesPerSec < 8000 || format.nSamplesPerSec > 192000
                    || format.nBlockAlign != format.nChannels * 2
                    || format.nAvgBytesPerSec != format.nSamplesPerSec * format.nBlockAlign)
                    throw std::runtime_error("Expected PCM16 mono/stereo WAV.");
                hasFormat = true;
            }
            else if (tag(static_cast<std::size_t>(offset), "data"))
            {
                if (hasData || count == 0) throw std::runtime_error("Invalid WAV data chunk.");
                result->samples.assign(bytes.begin() + data, bytes.begin() + data + count);
                hasData = true;
            }
            offset = static_cast<std::uint64_t>(data) + count + (count & 1);
            if (offset > end) throw std::runtime_error("Missing WAV chunk padding.");
        }
        if (!hasFormat || !hasData || result->samples.size() % result->format.nBlockAlign != 0)
            throw std::runtime_error("Incomplete PCM samples.");
        return result;
    }
    void CheckAudioResult(const HRESULT inResult)
    {
        if (FAILED(inResult)) throw std::runtime_error("Audio decoding/playback failed.");
    }

    // Decode local MP3/WAV samples to PCM16. COM is owned by wWinMain; the reader stays
    // on the game thread and is released before MFShutdown/CoUninitialize.
    class AudioReader final
    {
    public:
        explicit AudioReader(const std::filesystem::path& inPath)
        {
            CheckAudioResult(MFCreateSourceReaderFromURL(inPath.c_str(), nullptr, reader.GetAddressOf()));
            CheckAudioResult(reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE));
            CheckAudioResult(reader->SetStreamSelection(MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE));
            Microsoft::WRL::ComPtr<IMFMediaType> requested, actual;
            CheckAudioResult(MFCreateMediaType(requested.GetAddressOf()));
            CheckAudioResult(requested->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio));
            CheckAudioResult(requested->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM));
            CheckAudioResult(requested->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16));
            CheckAudioResult(reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, requested.Get()));
            CheckAudioResult(reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, actual.GetAddressOf()));
            WAVEFORMATEX* waveFormat{};
            UINT32 formatBytes{};
            const HRESULT result = MFCreateWaveFormatExFromMFMediaType(actual.Get(), &waveFormat, &formatBytes);
            const std::unique_ptr<WAVEFORMATEX, decltype(&CoTaskMemFree)> ownedFormat(waveFormat, CoTaskMemFree);
            CheckAudioResult(result);
            if (!waveFormat || formatBytes < sizeof(WAVEFORMATEX)) throw std::runtime_error("Missing audio format.");
            format = *waveFormat;
            if (format.wFormatTag != WAVE_FORMAT_PCM || (format.nChannels != 1 && format.nChannels != 2)
                || format.wBitsPerSample != 16 || format.nSamplesPerSec < 8000 || format.nSamplesPerSec > 192000
                || format.nBlockAlign != format.nChannels * 2
                || format.nAvgBytesPerSec != format.nSamplesPerSec * format.nBlockAlign)
                throw std::runtime_error("Expected decoded PCM16 mono/stereo audio.");
            format.cbSize = 0;
        }
        [[nodiscard]] const WAVEFORMATEX& GetFormat() const { return format; }
        [[nodiscard]] std::vector<BYTE> ReadChunk(bool& outEnd)
        {
            DWORD flags{};
            Microsoft::WRL::ComPtr<IMFSample> sample;
            CheckAudioResult(reader->ReadSample(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0,
                nullptr, &flags, nullptr, sample.GetAddressOf()));
            if (flags & MF_SOURCE_READERF_ERROR) throw std::runtime_error("Audio stream failed.");
            if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED)
            {
                Microsoft::WRL::ComPtr<IMFMediaType> actual;
                CheckAudioResult(reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, actual.GetAddressOf()));
                UINT32 channels{}, rate{}, bits{};
                GUID subtype{};
                CheckAudioResult(actual->GetGUID(MF_MT_SUBTYPE, &subtype));
                CheckAudioResult(actual->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels));
                CheckAudioResult(actual->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate));
                CheckAudioResult(actual->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, &bits));
                if (!IsEqualGUID(subtype, MFAudioFormat_PCM) || channels != format.nChannels
                    || rate != format.nSamplesPerSec || bits != format.wBitsPerSample)
                    throw std::runtime_error("Audio stream format changed.");
            }
            outEnd = (flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0;
            if (!sample) return {};
            Microsoft::WRL::ComPtr<IMFMediaBuffer> buffer;
            CheckAudioResult(sample->ConvertToContiguousBuffer(buffer.GetAddressOf()));
            DWORD length{};
            CheckAudioResult(buffer->GetCurrentLength(&length));
            constexpr DWORD MAX_SAMPLE_BYTES = 4 * 1024 * 1024;
            if (length > MAX_SAMPLE_BYTES || length % format.nBlockAlign != 0)
                throw std::runtime_error("Invalid decoded audio sample size.");
            std::vector<BYTE> bytes(length);
            if (length != 0)
            {
                BYTE* source{};
                CheckAudioResult(buffer->Lock(&source, nullptr, nullptr));
                std::memcpy(bytes.data(), source, length);
                CheckAudioResult(buffer->Unlock());
            }
            return bytes;
        }
        void Rewind()
        {
            PROPVARIANT position{};
            position.vt = VT_I8;
            position.hVal.QuadPart = 0;
            CheckAudioResult(reader->SetCurrentPosition(GUID_NULL, position));
        }
    private:
        Microsoft::WRL::ComPtr<IMFSourceReader> reader;
        WAVEFORMATEX format{};
    };

    std::shared_ptr<WaveData> ReadEffect(const std::filesystem::path& inPath)
    {
        if (inPath.extension() == L".wav") return ReadWave(inPath);
        AudioReader reader(inPath);
        auto result = std::make_shared<WaveData>();
        result->format = reader.GetFormat();
        constexpr std::size_t MAX_EFFECT_BYTES = 64 * 1024 * 1024;
        unsigned emptyReads{};
        for (;;)
        {
            bool end{};
            const auto chunk = reader.ReadChunk(end);
            if (chunk.size() > MAX_EFFECT_BYTES - result->samples.size())
                throw std::runtime_error("Effect exceeds decoded audio size limit.");
            result->samples.insert(result->samples.end(), chunk.begin(), chunk.end());
            if (end) break;
            emptyReads = chunk.empty() ? emptyReads + 1 : 0;
            if (emptyReads >= 128) throw std::runtime_error("Empty audio stream.");
        }
        if (result->samples.empty()) throw std::runtime_error("Effect has no audio samples.");
        return result;
    }

    struct MusicPlayback final
    {
        MusicPlayback(IXAudio2& inEngine, IXAudio2SubmixVoice* inBus, const std::filesystem::path& inPath)
            : decoder(inPath), path(inPath)
        {
            XAUDIO2_SEND_DESCRIPTOR send{0, inBus};
            XAUDIO2_VOICE_SENDS sends{1, &send};
            CheckAudioResult(inEngine.CreateSourceVoice(&voice, &decoder.GetFormat(), 0,
                XAUDIO2_DEFAULT_FREQ_RATIO, nullptr, &sends));
        }
        ~MusicPlayback() { if (voice) voice->DestroyVoice(); }
        MusicPlayback(const MusicPlayback&) = delete;
        MusicPlayback& operator=(const MusicPlayback&) = delete;

        // Retain every submitted buffer until XAudio2 consumes it. Decode a bounded
        // queue each update instead of loading an entire BGM or creating a worker.
        void Fill()
        {
            XAUDIO2_VOICE_STATE state{};
            voice->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
            while (buffers.size() > state.BuffersQueued) buffers.pop_front();
            constexpr std::size_t TARGET_BUFFERS = 24;
            constexpr unsigned MAX_READS_PER_UPDATE = 32;
            for (unsigned read = 0; read < MAX_READS_PER_UPDATE && buffers.size() < TARGET_BUFFERS; ++read)
            {
                bool end{};
                auto chunk = decoder.ReadChunk(end);
                if (!chunk.empty())
                {
                    buffers.push_back(std::move(chunk));
                    XAUDIO2_BUFFER buffer{};
                    buffer.AudioBytes = static_cast<UINT32>(buffers.back().size());
                    buffer.pAudioData = buffers.back().data();
                    CheckAudioResult(voice->SubmitSourceBuffer(&buffer));
                }
                if (end) decoder.Rewind();
            }
            if (buffers.empty()) throw std::runtime_error("Music has no playable samples.");
        }
        AudioReader decoder;
        std::filesystem::path path;
        IXAudio2SourceVoice* voice{};
        std::deque<std::vector<BYTE>> buffers;
    };
}

namespace ActionRPG
{
    struct AudioMixer::Impl
    {
        struct Playback
        {
            Playback() = default;
            Playback(const Playback&) = delete;
            Playback& operator=(const Playback&) = delete;
            IXAudio2SourceVoice* voice{};
            std::shared_ptr<WaveData> data;
            ~Playback() { if (voice) voice->DestroyVoice(); }
        };
        Microsoft::WRL::ComPtr<IXAudio2> engine;
        IXAudio2MasteringVoice* mastering{};
        IXAudio2SubmixVoice* effectsBus{};
        IXAudio2SubmixVoice* musicBus{};
        std::array<float, 3> volumes{1.0f, 1.0f, 1.0f};
        std::unique_ptr<MusicPlayback> music;
        std::unordered_map<std::string, std::shared_ptr<WaveData>> loadedEffects;
        std::vector<std::unique_ptr<Playback>> effects;
        std::wstring notice;
        bool available{}, mediaStarted{};

        Impl()
        {
            if (FAILED(XAudio2Create(engine.GetAddressOf()))
                || FAILED(engine->CreateMasteringVoice(&mastering))
                || FAILED(engine->CreateSubmixVoice(&effectsBus, 2, 48000))
                || FAILED(engine->CreateSubmixVoice(&musicBus, 2, 48000)))
            { notice = L"오디오 장치를 사용할 수 없습니다."; return; }
            available = true;
            mediaStarted = SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET));
        }
        ~Impl()
        {
            TraceShutdown("ActionRPGClient: stopping audio engine\n");
            effects.clear(); music.reset(); loadedEffects.clear();
            if (engine) engine->StopEngine();
            if (effectsBus) effectsBus->DestroyVoice();
            if (musicBus) musicBus->DestroyVoice();
            if (mastering) mastering->DestroyVoice();
            engine.Reset();
            if (mediaStarted) (void)MFShutdown();
            TraceShutdown("ActionRPGClient: audio engine stopped\n");
        }
        std::unique_ptr<Playback> Start(std::shared_ptr<WaveData> inData)
        {
            auto playback = std::make_unique<Playback>();
            playback->data = std::move(inData);
            XAUDIO2_SEND_DESCRIPTOR send{0, effectsBus};
            XAUDIO2_VOICE_SENDS sends{1, &send};
            if (FAILED(engine->CreateSourceVoice(&playback->voice, &playback->data->format, 0,
                XAUDIO2_DEFAULT_FREQ_RATIO, nullptr, &sends))) return {};
            XAUDIO2_BUFFER buffer{};
            buffer.Flags = XAUDIO2_END_OF_STREAM;
            buffer.AudioBytes = static_cast<UINT32>(playback->data->samples.size());
            buffer.pAudioData = playback->data->samples.data();
            if (FAILED(playback->voice->SubmitSourceBuffer(&buffer)) || FAILED(playback->voice->Start())) return {};
            return playback;
        }
    };

    AudioMixer::AudioMixer() : impl(std::make_unique<Impl>()) {}
    AudioMixer::~AudioMixer() = default;
    bool AudioMixer::IsAvailable() const { return impl->available; }
    float AudioMixer::GetVolume(AudioChannel inChannel) const
    {
        const auto index = static_cast<std::size_t>(inChannel);
        return index < impl->volumes.size() ? impl->volumes[index] : 0.0f;
    }
    bool AudioMixer::SetVolume(AudioChannel inChannel, float inVolume)
    {
        const auto index = static_cast<std::size_t>(inChannel);
        if (!impl->available || index >= impl->volumes.size() || !std::isfinite(inVolume)) return false;
        const float volume = std::clamp(inVolume, 0.0f, 1.0f);
        IXAudio2Voice* voice = inChannel == AudioChannel::Master ? static_cast<IXAudio2Voice*>(impl->mastering)
            : inChannel == AudioChannel::Effects ? static_cast<IXAudio2Voice*>(impl->effectsBus)
            : static_cast<IXAudio2Voice*>(impl->musicBus);
        if (FAILED(voice->SetVolume(volume))) { impl->notice = L"볼륨을 적용하지 못했습니다."; return false; }
        impl->volumes[index] = volume;
        return true;
    }
    bool AudioMixer::PlayMusic(const std::filesystem::path& inPath)
    {
        if (!impl->available) return false;
        try
        {
            if (impl->music && impl->music->path == inPath) return true;
            if (!impl->mediaStarted) throw std::runtime_error("Media Foundation is unavailable.");
            auto music = std::make_unique<MusicPlayback>(*impl->engine.Get(), impl->musicBus, inPath);
            music->Fill();
            CheckAudioResult(music->voice->Start());
            impl->music = std::move(music);
            return true;
        }
        catch (const std::exception&) { impl->notice = L"배경음악을 읽거나 재생하지 못했습니다. MP3/WAV를 확인해 주세요."; return false; }
    }
    bool AudioMixer::LoadEffect(const std::filesystem::path& inPath, const std::string_view inId)
    {
        if (!impl->available) return false;
        try { impl->loadedEffects[std::string(inId)] = ReadEffect(inPath); return true; }
        catch (const std::exception&) { impl->notice = L"효과음을 읽지 못했습니다. MP3/WAV를 확인해 주세요."; return false; }
    }
    bool AudioMixer::PlayEffect(const std::string_view inId)
    {
        if (!impl->available) return false;
        const auto effect = impl->loadedEffects.find(std::string(inId));
        if (effect == impl->loadedEffects.end()) return false;
        Update();
        constexpr std::size_t MAX_EFFECT_VOICES = 16;
        if (impl->effects.size() >= MAX_EFFECT_VOICES) return false;
        auto playback = impl->Start(effect->second);
        if (!playback) { impl->notice = L"효과음을 재생하지 못했습니다."; return false; }
        impl->effects.push_back(std::move(playback));
        return true;
    }
    bool AudioMixer::HasSounds() const { return impl->music != nullptr || !impl->loadedEffects.empty(); }
    std::wstring_view AudioMixer::GetNotice() const { return impl->notice; }
    void AudioMixer::Update()
    {
        if (impl->music)
        {
            try { impl->music->Fill(); }
            catch (const std::exception&)
            {
                impl->music.reset();
                impl->notice = L"배경음악 재생을 계속하지 못했습니다.";
            }
        }
        // Poll completed voices on their owner thread; audio callbacks never touch game/UI state.
        std::erase_if(impl->effects, [](const auto& playback)
        {
            XAUDIO2_VOICE_STATE state{};
            playback->voice->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
            return state.BuffersQueued == 0;
        });
    }
    void AudioMixer::Stop()
    {
        impl->effects.clear(); impl->music.reset(); impl->loadedEffects.clear();
        if (impl->available) impl->notice.clear();
    }
}
