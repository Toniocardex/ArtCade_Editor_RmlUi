# Write runtime-build-info.json next to game.exe (ADR-0019).
# Usage: cmake -DINFO_OUT=... -DASSET_KEY_ID=... -P write-runtime-build-info.cmake
if(NOT INFO_OUT)
    message(FATAL_ERROR "INFO_OUT required")
endif()
if(NOT ASSET_KEY_ID)
    set(ASSET_KEY_ID "artcade-dev-key-v1")
endif()
set(_json "{
  \"engineVersion\": \"2.0.0\",
  \"runtimeBuildId\": \"local\",
  \"projectFormatMin\": 11,
  \"projectFormatMax\": 11,
  \"assetKeyId\": \"${ASSET_KEY_ID}\"
}
")
file(WRITE "${INFO_OUT}" "${_json}")
