// object-type-materialize-test.cpp — ADR-0010 sheet materialisation (no GL).

#include "object-type-materialize.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_map>

using namespace ArtCade;

static void expect(bool ok, const char* msg) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        std::exit(1);
    }
    std::printf("  [ok] %s\n", msg);
}

static SpriteAnimationAssetDef makeHeroAnim(const AssetId& sourceImage = "img-hero") {
    SpriteAnimationAssetDef anim;
    anim.id = "hero.anim";
    anim.name = "hero.anim";
    anim.sourceImageAssetId = sourceImage;
    anim.frames.push_back(SpriteFrameDef{"f0", 0, 0, 32, 32});
    SpriteAnimationClipDef idle;
    idle.id = "idle";
    idle.name = "Idle";
    idle.framesPerSecond = 8.f;
    idle.playbackMode = AnimationPlaybackMode::Loop;
    idle.frameIds = {"f0"};
    anim.clips.push_back(idle);
    return anim;
}

static EntityDef makeAnimationType(const AnimationClipId& defaultClipId = {}) {
    EntityDef type;
    type.className = "Hero";
    type.name = "Hero";
    SpritePresentationComponent presentation;
    presentation.visible = true;
    presentation.source = SpritePresentationAnimation{
        "hero.anim", defaultClipId, /*autoPlay=*/false, /*playbackSpeed=*/1.f};
    type.spritePresentation = std::move(presentation);
    return type;
}

static SceneInstanceDef makeInstance() {
    SceneInstanceDef inst;
    inst.id = 1;
    inst.objectTypeId = "Hero";
    return inst;
}

int main() {
    const std::vector<SpriteAnimationAssetDef> assets{makeHeroAnim()};

    // Animation source: sheet from animation asset even with empty defaultClipId.
    {
        const EntityDef e =
            materializeInstance(makeAnimationType(/*defaultClipId=*/{}), makeInstance(), assets);
        expect(e.spriteRenderer.has_value(), "animation: renderer present");
        expect(e.spriteRenderer->imageAssetId == "img-hero",
               "animation: imageAssetId from sourceImageAssetId");
        expect(e.sprite.spriteAssetId == "img-hero",
               "animation: sprite.spriteAssetId copied from renderer");
        expect(e.spriteAnimator.has_value(), "animation: animator present");
        expect(e.spriteAnimator->animationAssetId == "hero.anim", "animation: animator asset id");
        expect(e.spriteAnimator->defaultClipId.empty(), "animation: empty defaultClipId preserved");
        expect(!e.spritePresentation.has_value(), "animation: presentation cleared");
        expect(!e.sprite.pivotFromAsset, "animation: pivotFromAsset cleared");
        expect(e.sprite.pivot.x == 0.5f && e.sprite.pivot.y == 0.5f,
               "animation: default center pivot from presentation");
    }

    // ADR-0057: OT Bottom Center pivot survives materialize + class prototypes.
    {
        EntityDef type = makeAnimationType("idle");
        type.spritePresentation->pivot = {0.5f, 1.f};
        SceneInstanceDef inst = makeInstance();
        const EntityDef e = materializeInstance(type, inst, assets);
        expect(e.sprite.pivot.x == 0.5f && e.sprite.pivot.y == 1.f,
               "bottom-center: materialize pivot");
        expect(!e.sprite.pivotFromAsset, "bottom-center: pivotFromAsset false");

        std::unordered_map<std::string, EntityDef> prototypes;
        std::unordered_map<std::string, EntityDef> objectTypes;
        objectTypes.emplace("Hero", type);
        rebuildClassPrototypes(prototypes, objectTypes, {});
        expect(prototypes["Hero"].sprite.pivot.y == 1.f,
               "bottom-center: class prototype pivot");
    }

    // Image source unchanged.
    {
        EntityDef type;
        type.className = "Hero";
        SpritePresentationComponent presentation;
        presentation.source = SpritePresentationImage{"img-static"};
        type.spritePresentation = std::move(presentation);
        const EntityDef e = materializeInstance(type, makeInstance(), assets);
        expect(e.spriteRenderer->imageAssetId == "img-static", "image: sheet unchanged");
        expect(!e.spriteAnimator.has_value(), "image: no animator");
        expect(e.sprite.spriteAssetId == "img-static", "image: sprite id copied");
    }

    // Missing animation asset: no invented sheet.
    {
        const EntityDef e =
            materializeInstance(makeAnimationType("idle"), makeInstance(), /*assets=*/{});
        expect(e.spriteRenderer.has_value(), "missing anim: renderer present");
        expect(e.spriteRenderer->imageAssetId.empty(), "missing anim: no invented sheet");
        expect(e.sprite.spriteAssetId.empty(), "missing anim: sprite id stays empty");
        expect(e.spriteAnimator.has_value(), "missing anim: animator still created");
    }

    // Empty sourceImageAssetId on asset: no invented sheet.
    {
        std::vector<SpriteAnimationAssetDef> emptySource{makeHeroAnim(/*sourceImage=*/{})};
        const EntityDef e =
            materializeInstance(makeAnimationType("idle"), makeInstance(), emptySource);
        expect(e.spriteRenderer->imageAssetId.empty(), "empty source image: no invented sheet");
    }

    // materializeProjectEntities wires the catalog through.
    {
        ProjectDoc doc;
        doc.spriteAnimationAssets = assets;
        EntityDef type = makeAnimationType({});
        doc.objectTypes.emplace("Hero", std::move(type));
        SceneDef scene;
        scene.id = "s1";
        scene.instances.push_back(makeInstance());
        doc.scenes.emplace("s1", std::move(scene));
        materializeProjectEntities(doc);
        const EntityDef& e = doc.entities.at(1);
        expect(e.spriteRenderer->imageAssetId == "img-hero",
               "project materialize: sheet from catalog");
        expect(e.sprite.spriteAssetId == "img-hero",
               "project materialize: sprite id from catalog");
    }

    // ADR-0040: tilemap remains scene-instance authority and is copied only
    // into the transient materialized entity.
    {
        EntityDef type;
        type.className = "Ground";
        type.tilemap = TilemapComponent{}; // Must never win over the instance.
        SceneInstanceDef instance = makeInstance();
        TilemapComponent tilemap;
        tilemap.tilesetAssetId = "tiles";
        tilemap.cellSize = {16.f, 24.f};
        tilemap.chunkSize = 8;
        TilemapChunk chunk;
        chunk.cells.resize(64);
        chunk.cells[0] = TilemapCellValue{"grass", TileTransformFlags::None};
        tilemap.chunks.push_back(std::move(chunk));
        instance.tilemap = tilemap;
        const EntityDef e = materializeInstance(type, instance, assets);
        expect(e.tilemap.has_value(), "tilemap: instance component materialized");
        expect(e.tilemap->tilesetAssetId == "tiles", "tilemap: instance tileset wins");
        expect(e.tilemap->cellSize.x == 16.f && e.tilemap->cellSize.y == 24.f,
               "tilemap: cell size preserved");
        expect(e.tilemap->chunks.size() == 1 && e.tilemap->chunks[0].cells[0]
                   && e.tilemap->chunks[0].cells[0]->tileId == "grass",
               "tilemap: painted cells preserved");

        instance.tilemap.reset();
        const EntityDef absent = materializeInstance(type, instance, assets);
        expect(!absent.tilemap.has_value(), "tilemap: no type-level inheritance");
    }

    // ADR-0014: BoxCollider2D → CollisionBody.
    {
        EntityDef type;
        type.className = "Block";
        type.boxCollider2D = BoxCollider2DComponent{
            {4.f, -2.f}, {40.f, 20.f}, true, BoxColliderMode::Solid};
        const EntityDef e = materializeInstance(type, makeInstance(), assets);
        expect(e.collisionBody.has_value(), "solid: body present");
        expect(e.collisionBody->enabled, "solid: body enabled");
        expect(e.collisionBody->bodyType == BodyType::Static, "solid: Static without mover");
        expect(e.collisionBody->shapes.size() == 1, "solid: one shape");
        expect(e.collisionBody->shapes[0].type == CollisionShapeType::Rectangle,
               "solid: rectangle");
        expect(e.collisionBody->shapes[0].response == CollisionResponse::Solid, "solid: response");
        expect(!e.collisionBody->shapes[0].oneWay, "solid: not one-way");
        expect(e.collisionBody->shapes[0].offset.x == 4.f
                   && e.collisionBody->shapes[0].offset.y == -2.f,
               "solid: offset");
        expect(e.collisionBody->shapes[0].size.x == 40.f
                   && e.collisionBody->shapes[0].size.y == 20.f,
               "solid: size");
    }
    {
        EntityDef type;
        type.boxCollider2D = BoxCollider2DComponent{
            {}, {16.f, 16.f}, true, BoxColliderMode::Trigger};
        const EntityDef e = materializeInstance(type, makeInstance(), assets);
        expect(e.collisionBody.has_value(), "trigger: body present");
        expect(e.collisionBody->shapes[0].response == CollisionResponse::Sensor,
               "trigger: sensor");
    }
    {
        EntityDef type;
        type.boxCollider2D = BoxCollider2DComponent{
            {}, {64.f, 8.f}, true, BoxColliderMode::OneWayPlatform};
        const EntityDef e = materializeInstance(type, makeInstance(), assets);
        expect(e.collisionBody.has_value(), "oneWay: body present");
        expect(e.collisionBody->shapes[0].response == CollisionResponse::Solid, "oneWay: solid");
        expect(e.collisionBody->shapes[0].oneWay, "oneWay: flag");
    }
    {
        EntityDef type;
        type.boxCollider2D = BoxCollider2DComponent{
            {}, {32.f, 32.f}, false, BoxColliderMode::Solid};
        const EntityDef e = materializeInstance(type, makeInstance(), assets);
        expect(!e.collisionBody.has_value(), "disabled: no body");
    }
    {
        EntityDef type;
        type.platformerController = PlatformerControllerComponent{};
        type.boxCollider2D = BoxCollider2DComponent{
            {}, {32.f, 32.f}, true, BoxColliderMode::Solid};
        expect(resolveCollisionBodyType(type) == BodyType::Kinematic,
               "resolve: platformer → Kinematic");
        const EntityDef e = materializeInstance(type, makeInstance(), assets);
        expect(e.collisionBody->bodyType == BodyType::Kinematic,
               "platformer: Kinematic body");
    }
    {
        EntityDef type;
        type.boxCollider2D = BoxCollider2DComponent{
            {}, {32.f, 32.f}, true, BoxColliderMode::Solid};
        type.collisionBody = CollisionBodyComponent{};
        type.collisionBody->enabled = true;
        type.collisionBody->shapes.push_back(CollisionShape{});
        type.collisionBody->shapes[0].size = {999.f, 999.f};
        const EntityDef e = materializeInstance(type, makeInstance(), assets);
        expect(e.collisionBody->shapes[0].size.x == 32.f,
               "scratch collisionBody overwritten from boxCollider2D");
    }
    {
        EntityDef type;
        type.collisionBody = CollisionBodyComponent{};
        type.collisionBody->enabled = true;
        type.collisionBody->shapes.push_back(CollisionShape{});
        type.collisionBody->shapes[0].size = {999.f, 999.f};
        const EntityDef e = materializeInstance(type, makeInstance(), assets);
        expect(!e.collisionBody.has_value(),
               "no boxCollider2D clears leftover collisionBody");
    }

    std::puts("object_type_materialize_test: all passed");
    return 0;
}
