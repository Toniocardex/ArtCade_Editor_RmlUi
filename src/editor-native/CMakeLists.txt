# This directory is not a supported CMake entry point. The native RmlUi editor
# is configured exclusively from the repository root, which defines the single
# canonical core/UI/runtime graph.
cmake_minimum_required(VERSION 3.20)
project(ArtCadeUnsupportedNestedEntryPoint NONE)
message(FATAL_ERROR
    "[ArtCade-Editor] Configure from the repository root: cmake -S . -B build.\n"
    "src/editor-native is not an independent or legacy editor build.")
