#include "../include/game-api.h"
#include "../../audio/include/audio.h"

#include <sol/sol.hpp>

namespace ArtCade::Modules {

void GameAPI::bindAudioAPI(sol::state& lua) {
    auto* audio = ctx_.audio;

    lua.set_function("audio_playSound", [audio](const std::string& path,
                                                 float vol, float pitch) {
        audio->playSound(path, vol, pitch);
    });
    lua.set_function("audio_playMusic", [audio](const std::string& path, bool loop) {
        audio->playMusic(path, loop);
    });
    lua.set_function("audio_stopMusic",  [audio]()           { audio->stopMusic();          });
    lua.set_function("audio_pauseMusic",  [audio]()           { audio->pauseMusic();         });
    lua.set_function("audio_resumeMusic", [audio]()          { audio->resumeMusic();        });
    lua.set_function("audio_stopAll",    [audio]()           { audio->stopAll();            });
    lua.set_function("audio_setVolume",  [audio](float m, float mu, float sfx) {
        audio->setMasterVolume(m);
        audio->setMusicVolume(mu);
        audio->setSFXVolume(sfx);
    });
    lua.set_function("audio_setMasterVolume", [audio](float v) { audio->setMasterVolume(v); });
    lua.set_function("audio_setMusicVolume",  [audio](float v) { audio->setMusicVolume(v);  });
    lua.set_function("audio_setSfxVolume",    [audio](float v) { audio->setSFXVolume(v);    });
    lua.set_function("audio_fadeMusic", [audio](float target, float seconds) {
        audio->fadeMusicTo(target, seconds);
    });
    lua.set_function("audio_isMusicPlaying", [audio]() -> bool {
        return audio->isMusicPlaying();
    });

    lua.script(R"(
        audio = {}
        audio.playSound  = function(path, vol, pitch) return audio_playSound(path, vol or 1, pitch or 1) end
        audio.playMusic  = function(path, loop)        return audio_playMusic(path, loop ~= false)        end
        audio.stopMusic  = function()                  return audio_stopMusic()                           end
        audio.pauseMusic  = function()                 return audio_pauseMusic()                          end
        audio.resumeMusic = function()                 return audio_resumeMusic()                         end
        audio.stopAll    = function()                  return audio_stopAll()                             end
        audio.setVolume  = function(m, mu, sfx)        return audio_setVolume(m or 1, mu or 1, sfx or 1) end
        audio.setMasterVolume = function(v)            return audio_setMasterVolume(v or 1)              end
        audio.setMusicVolume  = function(v)            return audio_setMusicVolume(v or 1)               end
        audio.setSfxVolume    = function(v)            return audio_setSfxVolume(v or 1)                 end
        audio.fadeMusic       = function(v, s)         return audio_fadeMusic(v or 0, s or 1)            end
        audio.isMusicPlaying  = function()             return audio_isMusicPlaying()                     end
    )");
}

} // namespace ArtCade::Modules
