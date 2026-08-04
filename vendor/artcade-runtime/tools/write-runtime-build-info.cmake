# Write runtime-build-info.json next to game.exe (ADR-0019 / ADR-0053).
# Usage:
#   cmake -DINFO_OUT=... -DASSET_KEY_ID=...
#        [-DRUNTIME_BUILD_ID=...] [-DPLATFORMER_GROUND_SUPPORT=ADR-0052]
#        -P write-runtime-build-info.cmake
if(NOT INFO_OUT)
    message(FATAL_ERROR "INFO_OUT required")
endif()
if(NOT ASSET_KEY_ID)
    set(ASSET_KEY_ID "artcade-dev-key-v1")
endif()
if(NOT RUNTIME_BUILD_ID)
    set(RUNTIME_BUILD_ID "local")
endif()
if(NOT PLATFORMER_GROUND_SUPPORT)
    set(PLATFORMER_GROUND_SUPPORT "ADR-0052")
endif()
set(_json "{
  \"engineVersion\": \"2.0.0\",
  \"runtimeBuildId\": \"${RUNTIME_BUILD_ID}\",
  \"platformerGroundSupport\": \"${PLATFORMER_GROUND_SUPPORT}\",
  \"projectFormatMin\": 12,
  \"projectFormatMax\": 14,
  \"assetKeyId\": \"${ASSET_KEY_ID}\"
}
")
file(WRITE "${INFO_OUT}" "${_json}")
