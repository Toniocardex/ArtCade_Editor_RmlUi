// ADR-0058 — collider-only geometry, effective offset, and Edit Origin.
#include "editor_core_test_harness.h"

#include "editor-native/commands/box_collider_commands.h"
#include "editor-native/model/box_collider_geometry.h"
#include "editor-native/model/project_document.h"
#include "editor-native/model/scene_frame_snapshot.h"
#include "editor-native/model/transform_gizmo_math.h"
#include "core/box-collider2d-resolve.h"
#include "core/object-type-materialize.h"
#include "core/project-json-upgrade.h"

#include <cmath>
#include <nlohmann/json.hpp>

using namespace ArtCade;
using namespace ArtCade::EditorNative;
using namespace ArtCade::EditorNative::CoreTest;

namespace {
bool near(float a, float b, float eps = 1e-4f) {
    return std::fabs(a - b) <= eps;
}

ProjectDocument makeColliderDoc() {
    ProjectDoc doc = makeDoc();
    doc.formatVersion = 14;
    EntityDef solid;
    solid.className = "solid";
    solid.name = "solid";
    solid.boxCollider2D = BoxCollider2DComponent{
        Vec2{0.f, 0.f}, Vec2{96.f, 48.f}, true, BoxColliderMode::Solid};
    doc.objectTypes["solid"] = solid;
    SceneInstanceDef& inst = doc.scenes.at(kSceneA).instances.front();
    inst.objectTypeId = "solid";
    inst.visible = false;
    inst.transform.position = {200.f, 100.f};
    inst.transform.scale = {1.f, 1.f};
    return ProjectDocument{std::move(doc)};
}
} // namespace

int main() {
    {
        EntityDef type;
        type.boxCollider2D = BoxCollider2DComponent{
            Vec2{2.f, 3.f}, Vec2{32.f, 16.f}, true, BoxColliderMode::Solid};
        SceneInstanceDef inst;
        EffectiveBoxCollider2D inherited = resolveEffectiveBoxCollider2D(type, inst);
        CHECK(inherited.present);
        CHECK(near(inherited.value.offset.x, 2.f));
        BoxCollider2DOverride delta;
        delta.offset = Vec2{9.f, -4.f};
        inst.boxCollider2DOverride = delta;
        EffectiveBoxCollider2D overridden = resolveEffectiveBoxCollider2D(type, inst);
        CHECK(near(overridden.value.offset.x, 9.f));
        CHECK(near(overridden.value.offset.y, -4.f));
        type.boxCollider2D.reset();
        CHECK(!resolveEffectiveBoxCollider2D(type, inst).present);
    }

    {
        ProjectDocument document = makeColliderDoc();
        const SceneFrameSnapshot frame = collectSceneFrameSnapshot(document, kSceneA, kHero);
        CHECK(pickEntityAt(frame, Vec2{200.f, 100.f}) == kHero);
        CHECK(pickEntityAt(frame, Vec2{250.f, 100.f}) == INVALID_ENTITY);
        const auto geometry = resolveInstanceTransformGeometry(document, frame, kSceneA, kHero);
        CHECK(geometry.has_value());
        CHECK(geometry->source == TransformGeometrySource::Collider);
        CHECK(near(geometry->transform.size.x, 96.f));
        CHECK(near(geometry->transform.size.y, 48.f));
        CHECK(near(geometry->entityOrigin.x, 200.f));
    }

    {
        ProjectDocument document = makeColliderDoc();
        const SceneInstanceDef* before = document.findInstanceInScene(kSceneA, kHero);
        const EntityDef* type = document.findObjectType("solid");
        const WorldRect worldBefore = boxColliderWorldBounds(before->transform, *type->boxCollider2D);
        const uint64_t revision = document.revision();

        SetInstanceOriginFromColliderAnchorCommand command{
            kSceneA, kHero, NormalizedAnchor::BotCenter};
        CHECK(command.apply(document).ok);
        CHECK(document.revision() == revision + 1);
        const SceneInstanceDef* after = document.findInstanceInScene(kSceneA, kHero);
        CHECK(after->boxCollider2DOverride && after->boxCollider2DOverride->offset);
        const EffectiveBoxCollider2D effective = resolveEffectiveBoxCollider2D(*type, *after);
        const WorldRect worldAfter = boxColliderWorldBounds(after->transform, effective.value);
        CHECK(near(worldBefore.x, worldAfter.x));
        CHECK(near(worldBefore.y, worldAfter.y));
        CHECK(near(worldBefore.width, worldAfter.width));
        CHECK(near(worldBefore.height, worldAfter.height));
        CHECK(near(after->transform.position.y, worldBefore.y + worldBefore.height));

        CHECK(command.undo(document).ok);
        const SceneInstanceDef* undone = document.findInstanceInScene(kSceneA, kHero);
        CHECK(near(undone->transform.position.x, 200.f));
        CHECK(near(undone->transform.position.y, 100.f));
        CHECK(!undone->boxCollider2DOverride);
    }

    {
        ProjectDocument document = makeColliderDoc();
        ProjectDoc cloned = document.data();
        SceneInstanceDef second = cloned.scenes.at(kSceneA).instances.front();
        second.id = 99;
        second.transform.position.x += 200.f;
        cloned.scenes.at(kSceneA).instances.push_back(second);
        document = ProjectDocument{std::move(cloned)};

        SetInstanceOriginFromColliderAnchorCommand command{
            kSceneA, kHero, NormalizedAnchor::TopLeft};
        CHECK(command.apply(document).ok);
        CHECK(document.findInstanceInScene(kSceneA, kHero)->boxCollider2DOverride);
        CHECK(!document.findInstanceInScene(kSceneA, 99)->boxCollider2DOverride);
    }

    {
        ProjectDocument document = makeColliderDoc();
        SetInstanceBoxColliderOffsetOverrideCommand setOverride{
            kSceneA, kHero, Vec2{7.f, 8.f}};
        CHECK(setOverride.apply(document).ok);
        RemoveBoxColliderCommand remove{"solid"};
        CHECK(remove.apply(document).ok);
        CHECK(!document.findObjectType("solid")->boxCollider2D);
        CHECK(!document.findInstanceInScene(kSceneA, kHero)->boxCollider2DOverride);
        CHECK(remove.undo(document).ok);
        CHECK(document.findObjectType("solid")->boxCollider2D);
        const auto& restored = document.findInstanceInScene(kSceneA, kHero)->boxCollider2DOverride;
        CHECK(restored && restored->offset);
        CHECK(near(restored->offset->x, 7.f));
        CHECK(near(restored->offset->y, 8.f));
    }

    {
        nlohmann::json v13{{"formatVersion", 13}};
        const auto upgraded = ProjectJson::upgradeProjectJsonToCurrent(v13);
        CHECK(upgraded.ok);
        CHECK(upgraded.changed);
        CHECK(upgraded.root["formatVersion"] == 14);
        const auto again = ProjectJson::upgradeProjectJsonToCurrent(upgraded.root);
        CHECK(again.ok);
        CHECK(!again.changed);
    }

    {
        EntityDef type;
        type.className = "solid";
        type.name = "solid";
        type.boxCollider2D = BoxCollider2DComponent{
            Vec2{0.f, 0.f}, Vec2{32.f, 32.f}, true, BoxColliderMode::Solid};
        SceneInstanceDef inst;
        inst.id = 10;
        inst.objectTypeId = "solid";
        BoxCollider2DOverride delta;
        delta.offset = Vec2{5.f, -6.f};
        inst.boxCollider2DOverride = delta;
        const EntityDef runtime = materializeInstance(type, inst, {});
        CHECK(runtime.boxCollider2D);
        CHECK(near(runtime.boxCollider2D->offset.x, 5.f));
        CHECK(near(runtime.boxCollider2D->offset.y, -6.f));
        CHECK(runtime.collisionBody && !runtime.collisionBody->shapes.empty());
        CHECK(near(runtime.collisionBody->shapes.front().offset.x, 5.f));
    }

    return finish();
}
