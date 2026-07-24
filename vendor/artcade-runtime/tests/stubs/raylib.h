#pragma once
// Minimal Raylib stub — solo i simboli usati da texture-manager.
// Usato esclusivamente nei test di Fase 3 (nessuna GPU richiesta).

typedef struct Color { unsigned char r, g, b, a; } Color;

static const Color MAGENTA = { 255, 0, 255, 255 };

typedef struct Image {
    void* data;
    int   width;
    int   height;
    int   mipmaps;
    int   format;
} Image;

typedef struct Texture2D {
    unsigned int id;
    int          width;
    int          height;
    int          mipmaps;
    int          format;
} Texture2D;

#ifdef __cplusplus
extern "C" {
#endif

bool      FileExists            (const char* fileName);
Image     GenImageColor         (int width, int height, Color color);
Texture2D LoadTextureFromImage  (Image image);
Texture2D LoadTexture           (const char* fileName);
void      UnloadImage           (Image image);
void      UnloadTexture         (Texture2D texture);

#ifdef __cplusplus
}
#endif
