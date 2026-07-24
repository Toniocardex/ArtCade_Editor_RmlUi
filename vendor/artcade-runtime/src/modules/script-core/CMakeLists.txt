add_library(artcade-script-core STATIC
    src/script-api-catalog.cpp
)
target_include_directories(artcade-script-core PUBLIC include)
target_link_libraries(artcade-script-core PUBLIC artcade-core)
