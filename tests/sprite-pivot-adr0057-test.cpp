// ADR-0057 — blocking matrix: upgrade, resolver, geometry, commands, spawn.
#include "editor_core_test_harness.h"

#include "editor-native/app/editor_coordinator.h"
#include "editor-native/app/export/runtime_project_preflight.h"
#include "editor-native/commands/scene_layer_commands.h"
#include "editor-native/commands/sprite_presentation_commands.h"
#include "editor-native/model/authored_transform.h"
#include "editor-native/model/project_document.h"
#include "editor-native/model/project_io.h"
#include "editor-native/model/scene_frame_snapshot.h"
#include "editor-native/model/transform_gizmo_math.h"
#include "core/object-type-materialize.h"
#include "core/project-current-format.h"
#include "core/project-json-upgrade.h"
#include "core/sprite-presentation-resolve.h"
#include "core/sprite-visual-geometry.h"

#include <cmath>
#include <nlohmann/json.hpp>
#include <unordered_map>

using namespace ArtCade;
using namespace ArtCade::EditorNative;
using namespace ArtCade::EditorNative::CoreTest;

namespace {

bool near(float a, float b, float eps = 1e-4f) {
    return std::fabs(a - b) <= eps;
}

EntityDef makeOtWithPivot(const ObjectTypeId& id, Vec2 pivot) {
    EntityDef type;
    type.className = id;
    type.name = id;
    SpritePresentationComponent presentation;
    presentation.visible = true;
    presentation.source = SpritePresentationImage{"img"};
    presentation.pivot = pivot;
    type.spritePresentation = std::move(presentation);
    return type;
}

} // namespace

int main() {
    // --- Upgrade v12 → v13 ---
    {
        nlohmann::json root = nlohmann::json::parse(R"json({
          "formatVersion": 12,
          "projectName": "Upgrade",
          "objectTypes": [{
            "id": "Hero",
            "name": "Hero",
            "spritePresentation": {
              "visible": true,
              "source": { "kind": "image", "assetId": "img" }
            }
          }],
          "scenes": []
        })json");
        const auto upgraded = ProjectJson::upgradeProjectJsonToCurrent(root);
        CHECK(upgraded.ok);
        CHECK(upgraded.changed);
        CHECK(upgraded.root["formatVersion"] == 13);
        CHECK(upgraded.root["objectTypes"][0]["spritePresentation"]["pivot"]["x"] == 0.5);
        CHECK(upgraded.root["objectTypes"][0]["spritePresentation"]["pivot"]["y"] == 0.5);

        const auto again = ProjectJson::upgradeProjectJsonToCurrent(upgraded.root);
        CHECK(again.ok);
        CHECK(!again.changed);

        const auto bad11 = ProjectJson::upgradeProjectJsonToCurrent(
            nlohmann::json{{"formatVersion", 11}});
        CHECK(!bad11.ok);
        const auto bad14 = ProjectJson::upgradeProjectJsonToCurrent(
            nlohmann::json{{"formatVersion", 14}});
        CHECK(!bad14.ok);
    }

    // --- Resolver: OT + override + class prototypes (dynamic spawn) ---
    {
        EntityDef ot = makeOtWithPivot("Hero", {0.5f, 1.f});
        SceneInstanceDef inst;
        inst.id = 1;
        inst.objectTypeId = "Hero";
        const EffectiveSpritePresentation inherited =
            resolveEffectiveSpritePresentation(ot, inst);
        CHECK(near(inherited.pivot.x, 0.5f) && near(inherited.pivot.y, 1.f));

        SpritePresentationOverride delta;
        delta.pivot = Vec2{0.f, 0.5f};
        inst.spritePresentationOverride = delta;
        const EffectiveSpritePresentation overridden =
            resolveEffectiveSpritePresentation(ot, inst);
        CHECK(near(overridden.pivot.x, 0.f) && near(overridden.pivot.y, 0.5f));

        std::unordered_map<std::string, EntityDef> prototypes;
        std::unordered_map<std::string, EntityDef> objectTypes;
        objectTypes.emplace("Hero", ot);
        rebuildClassPrototypes(prototypes, objectTypes, {});
        CHECK(prototypes.count("Hero") == 1);
        CHECK(near(prototypes["Hero"].sprite.pivot.x, 0.5f));
        CHECK(near(prototypes["Hero"].sprite.pivot.y, 1.f));
        CHECK(!prototypes["Hero"].sprite.pivotFromAsset);

        // Authored instance materialize uses override.
        const EntityDef runtime = materializeInstance(ot, inst, {});
        CHECK(near(runtime.sprite.pivot.x, 0.f));
        CHECK(near(runtime.sprite.pivot.y, 0.5f));
        CHECK(!runtime.sprite.pivotFromAsset);
    }

    // --- Geometry pure ---
    {
        const SpriteVisualGeometry center = resolveSpriteVisualGeometry(
            {100.f, 100.f}, 0.f, {1.f, 1.f}, {100.f, 50.f}, {0.5f, 0.5f}, false, false);
        CHECK(near(center.unrotatedTopLeft.x, 50.f));
        CHECK(near(center.unrotatedTopLeft.y, 75.f));
        CHECK(near(center.visualCenter.x, 100.f));
        CHECK(near(center.visualCenter.y, 100.f));

        const SpriteVisualGeometry bottom = resolveSpriteVisualGeometry(
            {100.f, 100.f}, 0.f, {1.f, 1.f}, {100.f, 50.f}, {0.5f, 1.f}, false, false);
        CHECK(near(bottom.unrotatedTopLeft.x, 50.f));
        CHECK(near(bottom.unrotatedTopLeft.y, 50.f));
        CHECK(near(bottom.originPixels.y, 50.f));

        const SpriteVisualGeometry tl = resolveSpriteVisualGeometry(
            {0.f, 0.f}, 0.f, {1.f, 1.f}, {100.f, 50.f}, {0.f, 0.f}, false, false);
        CHECK(near(tl.unrotatedTopLeft.x, 0.f) && near(tl.unrotatedTopLeft.y, 0.f));

        const SpriteVisualGeometry flip = resolveSpriteVisualGeometry(
            {0.f, 0.f}, 0.f, {1.f, 1.f}, {100.f, 50.f}, {0.f, 1.f}, true, false);
        CHECK(near(flip.effectivePivot.x, 1.f));
        CHECK(near(flip.effectivePivot.y, 1.f));
    }

    // --- Commands: OT pivot; reset instance pivot preserves source ---
    {
        ProjectDoc doc = makeDoc();
        doc.formatVersion = 13;
        doc.objectTypes["Hero"] = makeOtWithPivot("Hero", {0.5f, 0.5f});
        ImageAssetDef image;
        image.assetId = "img";
        image.sourcePath = "a.png";
        doc.imageAssets = {image};
        ProjectDocument document{std::move(doc)};

        SetObjectTypeSpritePivotCommand setOt{"Hero", {0.5f, 1.f}};
        CHECK(setOt.apply(document).ok);
        CHECK(near(document.findObjectType("Hero")->spritePresentation->pivot.y, 1.f));
        CHECK(setOt.undo(document).ok);
        CHECK(near(document.findObjectType("Hero")->spritePresentation->pivot.y, 0.5f));
        CHECK(setOt.apply(document).ok);

        SpritePresentationOverride withSource;
        withSource.source = SpritePresentationImage{"img"};
        SetInstanceSpritePresentationOverrideCommand setOverride{
            kSceneA, kHero, withSource};
        CHECK(setOverride.apply(document).ok);

        SetInstanceSpritePivotOverrideCommand setPivot{kSceneA, kHero, Vec2{0.f, 0.f}};
        CHECK(setPivot.apply(document).ok);
        const SceneInstanceDef* inst = document.findInstanceInScene(kSceneA, kHero);
        CHECK(inst && inst->spritePresentationOverride);
        CHECK(inst->spritePresentationOverride->source.has_value());
        CHECK(inst->spritePresentationOverride->pivot.has_value());

        SetInstanceSpritePivotOverrideCommand resetPivot{kSceneA, kHero, std::nullopt};
        CHECK(resetPivot.apply(document).ok);
        inst = document.findInstanceInScene(kSceneA, kHero);
        CHECK(inst && inst->spritePresentationOverride);
        CHECK(inst->spritePresentationOverride->source.has_value());
        CHECK(!inst->spritePresentationOverride->pivot.has_value());
    }

    // --- Scene frame visualTransform + picking + gizmo capture ---
    {
        ProjectDoc doc = makeDoc();
        doc.formatVersion = 13;
        doc.objectTypes["Hero"] = makeOtWithPivot("Hero", {0.5f, 1.f});
        ImageAssetDef image;
        image.assetId = "img";
        image.sourcePath = "a.png";
        doc.imageAssets = {image};
        doc.scenes.at(kSceneA).instances.front().transform.position = {100.f, 200.f};
        ProjectDocument document{std::move(doc)};

        const SceneFrameSnapshot frame =
            collectSceneFrameSnapshot(document, kSceneA, kHero);
        CHECK(!frame.sprites.empty());
        const SceneFrameSprite& sprite = frame.sprites.front();
        CHECK(near(sprite.destination.x, 84.f));
        CHECK(near(sprite.destination.y, 168.f));
        CHECK(near(sprite.origin.x, 16.f));
        CHECK(near(sprite.origin.y, 32.f));
        CHECK(near(sprite.visualTransform.center.x, 100.f));
        CHECK(near(sprite.visualTransform.center.y, 184.f));
        CHECK(pickEntityAt(frame, Vec2{100.f, 190.f}) == kHero);
        // Visible sprite: placeholder is not a second hit target below the feet.
        CHECK(pickEntityAt(frame, Vec2{100.f, 210.f}) == INVALID_ENTITY);

        const auto geometry =
            resolveInstanceTransformGeometry(document, frame, kSceneA, kHero);
        CHECK(geometry.has_value());
        CHECK(near(geometry->effectivePivot.x, 0.5f));
        CHECK(near(geometry->effectivePivot.y, 1.f));

        const Transform authored =
            document.findInstanceInScene(kSceneA, kHero)->transform;
        TransformInteractionState interaction = beginTransformInteraction(
            kSceneA, kHero, TransformHandle::EdgeR, authored, *geometry,
            Vec2{116.f, 184.f});
        CHECK(near(interaction.effectivePivot.y, 1.f));
        // Grow width only; bottom-center pivot keeps feet (position.y) fixed.
        const Transform resized =
            resizeTransformFromHandle(interaction, Vec2{148.f, 184.f}, false, nullptr);
        CHECK(near(resized.position.y, 200.f));
        CHECK(near(resized.scale.x, 2.f));
    }

    // --- empty() includes pivot ---
    {
        SpritePresentationOverride delta;
        CHECK(spritePresentationOverrideEmpty(delta));
        delta.pivot = Vec2{0.5f, 0.5f};
        CHECK(!spritePresentationOverrideEmpty(delta));
    }

    // --- Serialize current document emits format 13 + OT pivot ---
    {
        ProjectDoc doc = makeDoc();
        doc.formatVersion = 13;
        doc.objectTypes["Hero"] = makeOtWithPivot("Hero", {0.f, 1.f});
        ProjectDocument document{std::move(doc)};
        const SerializeResult saved = ProjectSerializer::serialize(document);
        CHECK(saved.ok);
        CHECK(saved.value.find("\"formatVersion\": 13") != std::string::npos
              || saved.value.find("\"formatVersion\":13") != std::string::npos);
        CHECK(saved.value.find("\"pivot\"") != std::string::npos);
    }

    // --- End-to-end: v12 JSON → deserialize upgrade → validate → save 13 ---
    {
        const char* kV12 = R"json({
          "formatVersion": 12,
          "projectName": "UpgradeE2E",
          "activeSceneId": "s",
          "globalVariables": [],
          "objectTypes": [{
            "id": "Hero",
            "name": "Hero",
            "visible": true,
            "spritePresentation": {
              "visible": true,
              "source": { "kind": "image", "assetId": "img-hero" }
            }
          }],
          "imageAssets": [{
            "id": "img-hero",
            "name": "Hero",
            "sourcePath": "sprites/hero.ppm",
            "defaultPivot": { "x": 0.0, "y": 0.0 }
          }],
          "scenes": {
            "s": {
              "id": "s",
              "name": "S",
              "layers": [{ "id": "layer-1", "name": "Layer 1", "locked": false }],
              "defaultLayerId": "layer-1",
              "instances": [{
                "id": 42,
                "objectTypeId": "Hero",
                "layerId": "layer-1",
                "transform": { "position": { "x": 10, "y": 20 } }
              }]
            }
          }
        })json";
        const DeserializeResult loaded = ProjectSerializer::deserialize(kV12);
        CHECK(loaded.ok);
        CHECK(loaded.value.data().formatVersion == 13);
        const EntityDef* ot = loaded.value.findObjectType("Hero");
        CHECK(ot && ot->spritePresentation);
        CHECK(near(ot->spritePresentation->pivot.x, 0.5f));
        CHECK(near(ot->spritePresentation->pivot.y, 0.5f));

        const SerializeResult saved = ProjectSerializer::serialize(loaded.value);
        CHECK(saved.ok);
        const nlohmann::json savedRoot = nlohmann::json::parse(saved.value);
        CHECK(savedRoot["formatVersion"] == 13);
        std::string validationError;
        CHECK(ProjectJson::validate_current_project_json(savedRoot, validationError));
    }

    // --- defaultPivot must not affect v13 presentation / materialize ---
    {
        EntityDef ot = makeOtWithPivot("Hero", {0.5f, 1.f});
        SceneInstanceDef inst;
        inst.id = 1;
        inst.objectTypeId = "Hero";
        ImageAssetDef image;
        image.assetId = "img";
        image.sourcePath = "a.png";
        image.defaultPivot = {0.f, 0.f};
        const EntityDef runtime = materializeInstance(ot, inst, {});
        CHECK(near(runtime.sprite.pivot.x, 0.5f));
        CHECK(near(runtime.sprite.pivot.y, 1.f));
        CHECK(!runtime.sprite.pivotFromAsset);
        // Changing asset defaultPivot cannot reach the presentation resolver.
        image.defaultPivot = {1.f, 1.f};
        (void)image;
        const EntityDef again = materializeInstance(ot, inst, {});
        CHECK(near(again.sprite.pivot.x, 0.5f));
        CHECK(near(again.sprite.pivot.y, 1.f));
        const EffectiveSpritePresentation effective =
            resolveEffectiveSpritePresentation(ot, inst);
        CHECK(near(effective.pivot.x, 0.5f) && near(effective.pivot.y, 1.f));
    }

    // --- Bottom Left + Flip X + rotation: draw / OBB / picking / AABB ---
    {
        constexpr float kRot = 0.4f;
        const SpriteVisualGeometry geo = resolveSpriteVisualGeometry(
            {100.f, 100.f}, kRot, {1.f, 1.f}, {100.f, 50.f}, {0.f, 1.f},
            /*flipX=*/true, /*flipY=*/false);
        // Flip X on bottom-left → effective pivot bottom-right.
        CHECK(near(geo.effectivePivot.x, 1.f));
        CHECK(near(geo.effectivePivot.y, 1.f));

        SceneFrameSnapshot frame;
        SceneFrameEntity entity{};
        entity.entityId = kHero;
        entity.name = "Hero";
        entity.bounds = SceneFrameRect{84.f, 84.f, 32.f, 32.f};
        frame.entities.push_back(entity);

        SceneFrameSprite sprite{};
        sprite.entityId = kHero;
        sprite.assetId = "img";
        sprite.visible = true;
        sprite.destination = SceneFrameRect{
            geo.unrotatedTopLeft.x, geo.unrotatedTopLeft.y, geo.size.x, geo.size.y};
        sprite.origin = geo.originPixels;
        sprite.rotationRadians = geo.rotationRadians;
        sprite.flipX = true;
        sprite.visualTransform =
            SceneFrameTransform2D{geo.visualCenter, geo.size, geo.rotationRadians};
        frame.sprites.push_back(sprite);

        // Draw contract: destination top-left + origin = gameplay anchor.
        CHECK(near(sprite.destination.x + sprite.origin.x, 100.f));
        CHECK(near(sprite.destination.y + sprite.origin.y, 100.f));

        const TransformAabb aabb = aabbOfTransform(sprite.visualTransform);
        const auto bounds = editorBoundsForEntity(frame, kHero);
        CHECK(bounds.has_value());
        CHECK(near(bounds->x, aabb.x));
        CHECK(near(bounds->y, aabb.y));
        CHECK(near(bounds->width, aabb.width));
        CHECK(near(bounds->height, aabb.height));

        CHECK(transformContainsPoint(sprite.visualTransform, geo.visualCenter));
        CHECK(pickEntityAt(frame, geo.visualCenter) == kHero);
        // Destination-center would be wrong after pivot+flip+rotation.
        const Vec2 destinationCenter{
            sprite.destination.x + sprite.destination.width * 0.5f,
            sprite.destination.y + sprite.destination.height * 0.5f};
        CHECK(!near(destinationCenter.x, geo.visualCenter.x)
              || !near(destinationCenter.y, geo.visualCenter.y));
        CHECK(transformContainsPoint(sprite.visualTransform, destinationCenter)
              == (pickEntityAt(frame, destinationCenter) == kHero));
    }

    // --- Remove Sprite Presentation erases instance overrides; Undo exact ---
    {
        ProjectDoc doc = makeDoc();
        doc.formatVersion = 13;
        doc.objectTypes["Hero"] = makeOtWithPivot("Hero", {0.5f, 1.f});
        ImageAssetDef image;
        image.assetId = "img";
        image.sourcePath = "a.png";
        doc.imageAssets = {image};
        ProjectDocument document{std::move(doc)};

        SpritePresentationOverride delta;
        delta.source = SpritePresentationImage{"img"};
        delta.pivot = Vec2{0.f, 0.f};
        SetInstanceSpritePresentationOverrideCommand setOverride{
            kSceneA, kHero, delta};
        CHECK(setOverride.apply(document).ok);
        CHECK(document.findInstanceInScene(kSceneA, kHero)->spritePresentationOverride);

        SetObjectTypeSpritePresentationCommand remove{"Hero", std::nullopt};
        CHECK(remove.apply(document).ok);
        CHECK(!document.findObjectType("Hero")->spritePresentation);
        CHECK(!document.findInstanceInScene(kSceneA, kHero)->spritePresentationOverride);

        CHECK(remove.undo(document).ok);
        CHECK(document.findObjectType("Hero")->spritePresentation);
        CHECK(near(document.findObjectType("Hero")->spritePresentation->pivot.y, 1.f));
        const SceneInstanceDef* restored =
            document.findInstanceInScene(kSceneA, kHero);
        CHECK(restored && restored->spritePresentationOverride);
        CHECK(restored->spritePresentationOverride->source.has_value());
        CHECK(restored->spritePresentationOverride->pivot.has_value());
        CHECK(near(restored->spritePresentationOverride->pivot->x, 0.f));
    }

    // --- Play blocks pivot authoring at coordinator ---
    {
        ProjectDoc doc = makeDoc();
        doc.formatVersion = 13;
        doc.objectTypes["Hero"] = makeOtWithPivot("Hero", {0.5f, 0.5f});
        ImageAssetDef image;
        image.assetId = "img";
        image.sourcePath = "sprites/hero.ppm";
        doc.imageAssets = {image};
        EditorCoordinator coordinator{std::move(doc)};
        CHECK(coordinator.playProject().ok);
        CHECK(coordinator.isPlaying());
        const auto blockedOt = coordinator.execute(
            SetObjectTypeSpritePivotCommand{"Hero", Vec2{0.5f, 1.f}});
        CHECK(!blockedOt.ok);
        const auto blockedInst = coordinator.execute(
            SetInstanceSpritePivotOverrideCommand{kSceneA, kHero, Vec2{0.f, 0.f}});
        CHECK(!blockedInst.ok);
        CHECK(coordinator.stopPlaying().ok);
        CHECK(!coordinator.isPlaying());
    }

    // --- Layer lock checked only on first apply of instance pivot override ---
    {
        ProjectDoc doc = makeDoc();
        doc.formatVersion = 13;
        doc.objectTypes["Hero"] = makeOtWithPivot("Hero", {0.5f, 0.5f});
        ImageAssetDef image;
        image.assetId = "img";
        image.sourcePath = "sprites/hero.ppm";
        doc.imageAssets = {image};
        ProjectDocument document{std::move(doc)};

        SetLayerLockedCommand lockLayer{kSceneA, "layer-1", true};
        CHECK(lockLayer.apply(document).ok);
        SetInstanceSpritePivotOverrideCommand lockedPivot{
            kSceneA, kHero, Vec2{0.f, 1.f}};
        CHECK(!lockedPivot.apply(document).ok);

        SetLayerLockedCommand unlockLayer{kSceneA, "layer-1", false};
        CHECK(unlockLayer.apply(document).ok);
        CHECK(lockedPivot.apply(document).ok);
        CHECK(document.findInstanceInScene(kSceneA, kHero)
                  ->spritePresentationOverride->pivot.has_value());

        // Redo path: lock must not be re-checked after first successful apply.
        CHECK(lockedPivot.undo(document).ok);
        SetLayerLockedCommand relockLayer{kSceneA, "layer-1", true};
        CHECK(relockLayer.apply(document).ok);
        CHECK(lockedPivot.apply(document).ok);
    }

    // --- Export preflight: upgrade-then-validate path (v13 no-op) ---
    {
        ProjectDoc doc = makeDoc();
        doc.formatVersion = 13;
        doc.objectTypes["Hero"] = makeOtWithPivot("Hero", {0.f, 1.f});
        ImageAssetDef image;
        image.assetId = "img";
        image.sourcePath = "sprites/hero.ppm";
        doc.imageAssets = {image};
        const RuntimeProjectPreflightResult preflight =
            prepareRuntimeProjectSnapshot(ProjectDocument{std::move(doc)});
        CHECK(preflight.ok);
        const nlohmann::json root = nlohmann::json::parse(preflight.canonicalProjectJson);
        CHECK(root["formatVersion"] == ProjectJson::kCurrentProjectFormatVersion);
        const auto upgraded = ProjectJson::upgradeProjectJsonToCurrent(root);
        CHECK(upgraded.ok);
        CHECK(!upgraded.changed);
    }

    return g_failed == 0 ? 0 : 1;
}
