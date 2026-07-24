// tilemap-grid-test.cpp — parity with editor/src/types/tilemap-grid.test.ts

#include "tilemap_grid.h"

#include <iostream>

static int g_passed = 0;
static int g_failed = 0;

#define CHECK(cond) \
    do { \
        if (cond) ++g_passed; \
        else { std::cerr << "FAIL: " #cond " (line " << __LINE__ << ")\n"; ++g_failed; } \
    } while (0)

static void test_standard_world_size() {
    int cols = 0;
    int rows = 0;
    ArtCade::TilemapGridLimits limits;
    tilemap_grid_dims_from_world(640.f, 320.f, 32.f, limits, cols, rows);
    CHECK(cols == 20);
    CHECK(rows == 10);
}

static void test_clamp_cols_min() {
    int cols = 0;
    int rows = 0;
    ArtCade::TilemapGridLimits limits;
    tilemap_grid_dims_from_world(50.f, 50.f, 32.f, limits, cols, rows);
    CHECK(cols == 8);
}

static void test_clamp_cols_max() {
    int cols = 0;
    int rows = 0;
    ArtCade::TilemapGridLimits limits;
    tilemap_grid_dims_from_world(9999.f, 9999.f, 32.f, limits, cols, rows);
    CHECK(cols == 64);
}

static void test_clamp_rows_min() {
    int cols = 0;
    int rows = 0;
    ArtCade::TilemapGridLimits limits;
    tilemap_grid_dims_from_world(640.f, 50.f, 32.f, limits, cols, rows);
    CHECK(rows == 6);
}

static void test_clamp_rows_max() {
    int cols = 0;
    int rows = 0;
    ArtCade::TilemapGridLimits limits;
    tilemap_grid_dims_from_world(640.f, 9999.f, 32.f, limits, cols, rows);
    CHECK(rows == 48);
}

static void test_round_not_floor() {
    int cols = 0;
    int rows = 0;
    ArtCade::TilemapGridLimits limits;
    tilemap_grid_dims_from_world(100.f, 200.f, 32.f, limits, cols, rows);
    CHECK(cols == 8);
    CHECK(rows == 6);
}

static void test_zero_tile_size_fallback() {
    int cols = 0;
    int rows = 0;
    ArtCade::TilemapGridLimits limits;
    tilemap_grid_dims_from_world(640.f, 320.f, 0.f, limits, cols, rows);
    CHECK(cols == 20);
    CHECK(rows == 10);
}

int main() {
    test_standard_world_size();
    test_clamp_cols_min();
    test_clamp_cols_max();
    test_clamp_rows_min();
    test_clamp_rows_max();
    test_round_not_floor();
    test_zero_tile_size_fallback();

    std::cout << "tilemap-grid-test: " << g_passed << " passed, " << g_failed << " failed\n";
    return g_failed > 0 ? 1 : 0;
}
