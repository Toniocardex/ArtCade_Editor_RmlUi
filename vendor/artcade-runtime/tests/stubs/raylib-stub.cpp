// raylib-stub.cpp — implementazioni minimali per i test senza GPU.
// Fornisce i simboli dichiarati in stubs/raylib.h.
#include "raylib.h"

static int sNextId = 1;

bool FileExists(const char*) { return false; }

Image GenImageColor(int w, int h, Color) {
    Image img{};
    img.width   = w;
    img.height  = h;
    img.mipmaps = 1;
    img.format  = 7;
    img.data    = nullptr;
    return img;
}

Texture2D LoadTextureFromImage(Image img) {
    Texture2D t{};
    t.id     = static_cast<unsigned int>(sNextId++);
    t.width  = img.width;
    t.height = img.height;
    return t;
}

Texture2D LoadTexture(const char*) {
    Texture2D t{};
    t.id = 0;   // segnala fallimento — texture-manager usa il placeholder
    return t;
}

void UnloadImage(Image)    {}
void UnloadTexture(Texture2D) {}
