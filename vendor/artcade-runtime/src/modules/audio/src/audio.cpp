#include "../include/audio.h"
#include <raylib.h>
#include <memory>
#include <cmath>
#include <unordered_map>
#include <string>
#include <vector>
#include <functional>

namespace ArtCade::Modules {

// ------------------------------------------------------------------ Pimpl

struct Audio::Impl {
    struct MemoryAudio {
        std::vector<unsigned char> bytes;
        std::string ext;
    };
    float masterVolume = 1.f;
    float musicVolume  = 1.f;
    float sfxVolume    = 1.f;

    bool  musicLoaded  = false;
    bool  deviceOpen   = false;
    // Raylib's audio device is a process-global miniaudio singleton. When a
    // host (the editor) already opened it, a second InitAudioDevice() would
    // re-init the global ma_device under the live WASAPI worker thread and
    // corrupt it; likewise CloseAudioDevice() here would tear down a device
    // the host still uses. Only own the device if we actually opened it.
    bool  ownsDevice   = false;
    Music currentMusic = {};
    std::string currentMusicPath;

    // Music fade state (advanced once per frame in update()).
    bool  fadeActive        = false;
    float fadeTarget        = 0.f;
    float fadeRate          = 0.f;   // volume units per second
    float fadeRestoreVolume = 1.f;   // musicVolume before a fade-out started

    std::unordered_map<std::string, Sound> soundCache;
    std::unordered_map<std::string, MemoryAudio> memoryAudio;
    std::unordered_map<AssetId, AudioAssetDef> runtimeAssets;
    std::function<std::string(const std::string&)> assetPathResolver;
};

// ------------------------------------------------------------------ lifecycle

Audio::Audio()  : impl_(std::make_unique<Impl>()) {}
Audio::~Audio() = default;

bool Audio::init() {
    // Idempotent: a second init() must not re-evaluate ownership (it would
    // observe the device we opened ourselves and silently drop ownership).
    if (impl_->deviceOpen) return true;
    const bool deviceWasReady = IsAudioDeviceReady();
    if (!deviceWasReady) InitAudioDevice();
    // InitAudioDevice() returns void: re-check instead of assuming success,
    // so we never claim ownership of (or use) a device that failed to open.
    if (!IsAudioDeviceReady()) {
        impl_->ownsDevice = false;
        return false;
    }
    impl_->ownsDevice = !deviceWasReady;
    impl_->deviceOpen = true;
    // Master volume is host-owned: when borrowing the host's device (editor
    // Play), don't override the process-global volume the host configured.
    if (impl_->ownsDevice) SetMasterVolume(impl_->masterVolume);
    return true;
}

void Audio::shutdown() {
    if (!impl_->deviceOpen) return;
    stopAll();
    for (auto& [path, snd] : impl_->soundCache)
        UnloadSound(snd);
    impl_->soundCache.clear();
    if (impl_->musicLoaded) {
        UnloadMusicStream(impl_->currentMusic);
        impl_->musicLoaded = false;
    }
    impl_->memoryAudio.clear();
    impl_->runtimeAssets.clear();
    // IsAudioDeviceReady() guard: tolerate partial teardown (e.g. the host
    // already closed the device during process exit) without a double close.
    if (impl_->ownsDevice && IsAudioDeviceReady()) {
        CloseAudioDevice();
    }
    impl_->ownsDevice = false;
    impl_->deviceOpen = false;
}

// ------------------------------------------------------------------ SFX

bool Audio::registerSoundFromMemory(const std::string& path,
                                    const unsigned char* data, int len,
                                    const std::string& ext) {
    if (!impl_->deviceOpen || !data || len <= 0) return false;
    invalidateSound(path);
    const char* hint = ext.empty() ? ".wav" : ext.c_str();
    Wave wave = LoadWaveFromMemory(hint, data, len);
    if (wave.data == nullptr) return false;
    Sound snd = LoadSoundFromWave(wave);
    UnloadWave(wave);
    if (snd.frameCount <= 0) {
        UnloadSound(snd);
        return false;
    }
    impl_->soundCache[path] = snd;
    impl_->memoryAudio[path] = Impl::MemoryAudio{
        std::vector<unsigned char>(data, data + len), hint
    };
    return true;
}

void Audio::setAssetPathResolver(
    std::function<std::string(const std::string&)> resolver) {
    impl_->assetPathResolver = std::move(resolver);
}

void Audio::invalidateSound(const std::string& path) {
    if (impl_->musicLoaded && impl_->currentMusicPath == path) {
        StopMusicStream(impl_->currentMusic);
        UnloadMusicStream(impl_->currentMusic);
        impl_->musicLoaded = false;
        impl_->currentMusicPath.clear();
    }
    auto it = impl_->soundCache.find(path);
    if (it != impl_->soundCache.end()) {
        UnloadSound(it->second);
        impl_->soundCache.erase(it);
    }
    impl_->memoryAudio.erase(path);
}

void Audio::evictSoundCache() {
    if (impl_->musicLoaded) {
        StopMusicStream(impl_->currentMusic);
        UnloadMusicStream(impl_->currentMusic);
        impl_->musicLoaded = false;
        impl_->currentMusicPath.clear();
    }
    for (auto& [path, snd] : impl_->soundCache)
        UnloadSound(snd);
    impl_->soundCache.clear();
    impl_->memoryAudio.clear();
}

bool Audio::playSound(const std::string& path, float volume, float pitch) {
    if (!impl_->deviceOpen || path.empty() || !std::isfinite(volume)
        || volume < 0.f || volume > 1.f || !std::isfinite(pitch) || pitch <= 0.f) {
        return false;
    }

    auto it = impl_->soundCache.find(path);
    if (it == impl_->soundCache.end()) {
        const std::string resolved = impl_->assetPathResolver
            ? impl_->assetPathResolver(path)
            : path;
        if (resolved.empty() || !FileExists(resolved.c_str())) return false;
        Sound loaded = LoadSound(resolved.c_str());
        if (loaded.frameCount <= 0) {
            UnloadSound(loaded);
            return false;
        }
        impl_->soundCache[path] = loaded;
        it = impl_->soundCache.find(path);
    }
    Sound& snd = it->second;
    SetSoundVolume(snd, volume * impl_->sfxVolume);
    SetSoundPitch (snd, pitch);
    PlaySound(snd);
    return true;
}

void Audio::setRuntimeAssetCatalog(const std::vector<AudioAssetDef>& assets) {
    impl_->runtimeAssets.clear();
    for (const AudioAssetDef& asset : assets) {
        if (!asset.assetId.empty()) impl_->runtimeAssets[asset.assetId] = asset;
    }
}

bool Audio::playResolvedAsset(const AssetId& audioAssetId, float volume) {
    if (audioAssetId.empty() || !std::isfinite(volume) || volume < 0.f || volume > 1.f)
        return false;
    const auto it = impl_->runtimeAssets.find(audioAssetId);
    if (it == impl_->runtimeAssets.end() || it->second.sourcePath.empty()
        || it->second.loadMode != AudioLoadMode::StaticSound) return false;
    return playSound(it->second.sourcePath, volume);
}

// ------------------------------------------------------------------ Music

void Audio::playMusic(const std::string& path, bool loop) {
    if (!impl_->deviceOpen) return;

    const std::string resolved = impl_->assetPathResolver
        ? impl_->assetPathResolver(path)
        : path;
    auto memoryIt = impl_->memoryAudio.find(path);
    if (memoryIt == impl_->memoryAudio.end() && resolved != path)
        memoryIt = impl_->memoryAudio.find(resolved);
    if (memoryIt == impl_->memoryAudio.end() &&
        (resolved.empty() || !FileExists(resolved.c_str()))) return;

    if (impl_->musicLoaded) {
        StopMusicStream(impl_->currentMusic);
        UnloadMusicStream(impl_->currentMusic);
        impl_->musicLoaded = false;
    }

    if (memoryIt != impl_->memoryAudio.end()) {
        const auto& source = memoryIt->second;
        impl_->currentMusic = LoadMusicStreamFromMemory(
            source.ext.c_str(), source.bytes.data(),
            static_cast<int>(source.bytes.size()));
    } else {
        impl_->currentMusic = LoadMusicStream(resolved.c_str());
    }
    if (impl_->currentMusic.ctxData == nullptr) return;
    impl_->currentMusicPath = memoryIt != impl_->memoryAudio.end()
        ? memoryIt->first
        : path;
    impl_->musicLoaded      = true;

    impl_->currentMusic.looping = loop;
    impl_->fadeActive = false;
    SetMusicVolume(impl_->currentMusic, impl_->musicVolume);
    PlayMusicStream(impl_->currentMusic);
}

void Audio::stopMusic() {
    impl_->fadeActive = false;
    if (impl_->musicLoaded)
        StopMusicStream(impl_->currentMusic);
}

void Audio::pauseMusic() {
    if (impl_->musicLoaded)
        PauseMusicStream(impl_->currentMusic);
}

void Audio::resumeMusic() {
    if (impl_->musicLoaded)
        ResumeMusicStream(impl_->currentMusic);
}

bool Audio::isMusicPlaying() const {
    return impl_->musicLoaded && IsMusicStreamPlaying(impl_->currentMusic);
}

void Audio::fadeMusicTo(float target, float seconds) {
    if (target < 0.f) target = 0.f;
    if (target > 1.f) target = 1.f;
    if (seconds <= 0.f) {
        impl_->fadeActive = false;
        if (target <= 0.001f) {
            stopMusic();
        } else {
            setMusicVolume(target);
        }
        return;
    }
    impl_->fadeActive        = true;
    impl_->fadeTarget        = target;
    impl_->fadeRestoreVolume = impl_->musicVolume;
    float delta = target - impl_->musicVolume;
    if (delta < 0.f) delta = -delta;
    impl_->fadeRate = delta / seconds;
}

// ------------------------------------------------------------------ mixer

void Audio::setMasterVolume(float v) {
    impl_->masterVolume = v;
    if (impl_->deviceOpen) SetMasterVolume(v);
}

void Audio::setMusicVolume(float v) {
    impl_->musicVolume = v;
    if (impl_->musicLoaded)
        SetMusicVolume(impl_->currentMusic, v);
}

void Audio::setSFXVolume(float v) {
    impl_->sfxVolume = v;
}

// ------------------------------------------------------------------ frame

void Audio::stopAll() {
    stopMusic();
    for (auto& [path, snd] : impl_->soundCache)
        StopSound(snd);
}

void Audio::update() {
    if (impl_->musicLoaded)
        UpdateMusicStream(impl_->currentMusic);

    if (impl_->fadeActive && impl_->musicLoaded) {
        const float step = impl_->fadeRate * GetFrameTime();
        float v = impl_->musicVolume;
        if (v < impl_->fadeTarget) {
            v = v + step > impl_->fadeTarget ? impl_->fadeTarget : v + step;
        } else {
            v = v - step < impl_->fadeTarget ? impl_->fadeTarget : v - step;
        }
        setMusicVolume(v);
        if (v == impl_->fadeTarget) {
            impl_->fadeActive = false;
            if (impl_->fadeTarget <= 0.001f) {
                const float restore = impl_->fadeRestoreVolume;
                stopMusic();
                setMusicVolume(restore);
            }
        }
    }
}

} // namespace ArtCade::Modules
