// ADR-0058 — blocking matrix: resolver, invariant math, commands, schema, runtime.
#include "editor_core_test_harness.h"

#include "editor-native/commands/box_collider_commands.h"
#include "editor-native/commands/scene_layer_commands.h"
#include "editor-native/model/box_collider_geometry.h"
#include "editor-native/model/collider_origin_math.h"
#include "editor-native/model/project_io.h"
#include "editor-native/model/scene_frame_snapshot.h"
#include "editor-native/model/transform_gizmo_math.h"
#include "core/box-collider-resolve.h"
#include "core/object-type-materialize.h"
#include "core/project-json-upgrade.h"

#include <cmath>
#include <nlohmann/json.hpp>

using namespace ArtCade;
using namespace ArtCade::EditorNative;
using namespace ArtCade::EditorNative::CoreTest;

namespace {
bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }
bool sameRect(const WorldRect& a, const WorldRect& b) {
    return near(a.x, b.x) && near(a.y, b.y)
        && near(a.width, b.width) && near(a.height, b.height);
}
ProjectDoc colliderDoc() {
    ProjectDoc doc = makeDoc();
    BoxCollider2DComponent collider;
    collider.offset = {3.f, -2.f};
    collider.size = {20.f, 10.f};
    doc.objectTypes.at("Hero").boxCollider2D = collider;
    SceneInstanceDef& instance = doc.scenes.at(kSceneA).instances.front();
    instance.transform.position = {100.f, 80.f};
    instance.transform.scale = {2.f, 3.f};
    return doc;
}
} // namespace

int main() {
    // Resolver: inherited, sparse override, and override-without-capability.
    {
        ProjectDoc doc = colliderDoc();
        EntityDef& type = doc.objectTypes.at("Hero");
        SceneInstanceDef& instance = doc.scenes.at(kSceneA).instances.front();
        auto effective = resolveEffectiveBoxCollider2D(type, instance);
        CHECK(effective.present);
        CHECK(near(effective.value.offset.x, 3.f));
        instance.boxCollider2DOverride = BoxCollider2DOverride{Vec2{-4.f, 9.f}};
        effective = resolveEffectiveBoxCollider2D(type, instance);
        CHECK(near(effective.value.offset.x, -4.f));
        CHECK(near(effective.value.size.x, 20.f));
        type.boxCollider2D.reset();
        CHECK(!resolveEffectiveBoxCollider2D(type, instance).present);
    }

    // Every preset preserves the world collider under non-uniform scale.
    {
        Transform transform;
        transform.position = {100.f, 80.f};
        transform.scale = {2.f, 3.f};
        BoxCollider2DComponent collider;
        collider.offset = {3.f, -2.f};
        collider.size = {20.f, 10.f};
        const WorldRect before = boxColliderWorldBounds(transform, collider);
        for (ColliderAnchorY y : {ColliderAnchorY::Top, ColliderAnchorY::Middle,
                                  ColliderAnchorY::Bottom}) {
            for (ColliderAnchorX x : {ColliderAnchorX::Left, ColliderAnchorX::Center,
                                      ColliderAnchorX::Right}) {
                const auto edit = originEditForColliderAnchor(transform, collider, x, y);
                CHECK(edit.has_value());
                Transform next = transform;
                next.position = edit->position;
                BoxCollider2DComponent nextCollider = collider;
                nextCollider.offset = edit->offset;
                CHECK(sameRect(before, boxColliderWorldBounds(next, nextCollider)));
            }
        }
    }

    // Atomic command, no-op, Undo/Redo, lock gate and Play gate.
    {
        EditorCoordinator coordinator{colliderDoc()};
        const auto before = collectBoxColliderBounds(
            coordinator.document(), kSceneA, kHero).front().worldBounds;
        CHECK(coordinator.execute(SetInstanceOriginFromColliderAnchorCommand{
            kSceneA, kHero, ColliderAnchorX::Left, ColliderAnchorY::Top}).ok);
        const SceneInstanceDef* changed = coordinator.document().findInstanceInScene(kSceneA, kHero);
        CHECK(changed && changed->boxCollider2DOverride.has_value());
        CHECK(sameRect(before, collectBoxColliderBounds(
            coordinator.document(), kSceneA, kHero).front().worldBounds));
        CHECK(coordinator.undo().ok);
        CHECK(!coordinator.document().findInstanceInScene(
            kSceneA, kHero)->boxCollider2DOverride.has_value());
        CHECK(coordinator.redo().ok);
        CHECK(sameRect(before, collectBoxColliderBounds(
            coordinator.document(), kSceneA, kHero).front().worldBounds));

        EditorCoordinator noOp{colliderDoc()};
        CHECK(noOp.execute(SetInstanceOriginFromColliderAnchorCommand{
            kSceneA, kHero, ColliderAnchorX::Center, ColliderAnchorY::Middle}).ok);
        const uint64_t revision = noOp.document().revision();
        const std::size_t history = noOp.undoSize();
        CHECK(noOp.execute(SetInstanceOriginFromColliderAnchorCommand{
            kSceneA, kHero, ColliderAnchorX::Center, ColliderAnchorY::Middle}).ok);
        CHECK(noOp.document().revision() == revision);
        CHECK(noOp.undoSize() == history);

        EditorCoordinator locked{colliderDoc()};
        CHECK(locked.execute(SetLayerLockedCommand{kSceneA, "layer-1", true}).ok);
        const uint64_t lockedRevision = locked.document().revision();
        CHECK(!locked.execute(SetInstanceOriginFromColliderAnchorCommand{
            kSceneA, kHero, ColliderAnchorX::Right, ColliderAnchorY::Bottom}).ok);
        CHECK(locked.document().revision() == lockedRevision);

        EditorCoordinator playing{colliderDoc()};
        CHECK(playing.playCurrentScene().ok);
        const uint64_t playRevision = playing.document().revision();
        CHECK(!playing.execute(SetInstanceOriginFromColliderAnchorCommand{
            kSceneA, kHero, ColliderAnchorX::Left, ColliderAnchorY::Bottom}).ok);
        CHECK(playing.document().revision() == playRevision);
    }

    // Removing the OT capability atomically clears overrides; Undo restores both.
    {
        ProjectDoc doc = colliderDoc();
        doc.scenes.at(kSceneA).instances.front().boxCollider2DOverride =
            BoxCollider2DOverride{Vec2{7.f, 8.f}};
        EditorCoordinator coordinator{std::move(doc)};
        CHECK(coordinator.execute(RemoveBoxColliderCommand{"Hero"}).ok);
        CHECK(!coordinator.document().data().objectTypes.at("Hero").boxCollider2D);
        CHECK(!coordinator.document().findInstanceInScene(
            kSceneA, kHero)->boxCollider2DOverride);
        CHECK(coordinator.undo().ok);
        CHECK(coordinator.document().data().objectTypes.at("Hero").boxCollider2D);
        CHECK(coordinator.document().findInstanceInScene(
            kSceneA, kHero)->boxCollider2DOverride);
    }

    // Persistence v14, v13 identity migration, strict orphan/empty rejection.
    {
        ProjectDoc doc = colliderDoc();
        doc.scenes.at(kSceneA).instances.front().boxCollider2DOverride =
            BoxCollider2DOverride{Vec2{-5.f, 6.f}};
        const SerializeResult saved = ProjectSerializer::serialize(ProjectDocument{doc});
        CHECK(saved.ok);
        CHECK(saved.value.find("\"boxCollider2DOverride\"") != std::string::npos);
        const DeserializeResult loaded = ProjectSerializer::deserialize(saved.value);
        CHECK(loaded.ok);
        CHECK(loaded.value.data().formatVersion == 14);

        nlohmann::json v13 = nlohmann::json::parse(saved.value);
        v13["formatVersion"] = 13;
        v13["schemaVersion"] = 13;
        const DeserializeResult migrated = ProjectSerializer::deserialize(v13.dump());
        CHECK(migrated.ok);
        CHECK(migrated.value.data().formatVersion == 14);

        nlohmann::json empty = nlohmann::json::parse(saved.value);
        empty["scenes"][kSceneA]["instances"][0]["boxCollider2DOverride"] =
            nlohmann::json::object();
        CHECK(!ProjectSerializer::deserialize(empty.dump()).ok);
        nlohmann::json orphan = nlohmann::json::parse(saved.value);
        for (auto& typeJson : orphan["objectTypes"]) typeJson.erase("boxCollider2D");
        CHECK(!ProjectSerializer::deserialize(orphan.dump()).ok);
    }

    // Runtime materialisation consumes the effective offset.
    {
        ProjectDoc doc = colliderDoc();
        EntityDef type = doc.objectTypes.at("Hero");
        SceneInstanceDef instance = doc.scenes.at(kSceneA).instances.front();
        instance.boxCollider2DOverride = BoxCollider2DOverride{Vec2{11.f, 12.f}};
        const EntityDef runtime = materializeInstance(type, instance, {});
        CHECK(runtime.boxCollider2D && near(runtime.boxCollider2D->offset.x, 11.f));
        CHECK(runtime.collisionBody && !runtime.collisionBody->shapes.empty());
        CHECK(near(runtime.collisionBody->shapes.front().offset.y, 12.f));
    }

    // Collider-only primary geometry/picking and universal origin marker data.
    {
        ProjectDocument document{colliderDoc()};
        const SceneFrameSnapshot frame = collectSceneFrameSnapshot(document, kSceneA, kHero);
        const auto geometry = resolveInstanceTransformGeometry(document, frame, kSceneA, kHero);
        CHECK(geometry.has_value());
        CHECK(geometry->source == InstanceTransformGeometry::Source::BoxCollider2D);
        CHECK(pickEntityAt(frame, geometry->transform.center) == kHero);
        CHECK(pickEntityAt(frame, Vec2{1000.f, 1000.f}) == INVALID_ENTITY);
        CHECK(!frame.entities.empty());
        CHECK(near(frame.entities.front().entityOrigin.x, 100.f));

        // Regression: mouse-down starts a Body preview immediately. The gizmo
        // must remain on the effective collider instead of jumping to a
        // centred placeholder around Entity Origin.
        const Transform authored =
            document.findInstanceInScene(kSceneA, kHero)->transform;
        TransformInteractionState interaction = beginTransformInteraction(
            kSceneA, kHero, TransformHandle::Body, authored, *geometry,
            geometry->transform.center);
        SceneFrameTransform2D preview =
            projectTransformInteractionGeometry(interaction);
        CHECK(near(preview.center.x, geometry->transform.center.x));
        CHECK(near(preview.center.y, geometry->transform.center.y));
        CHECK(near(preview.size.x, geometry->transform.size.x));
        CHECK(near(preview.size.y, geometry->transform.size.y));

        interaction.previewTransform.position = {112.f, 75.f};
        preview = projectTransformInteractionGeometry(interaction);
        CHECK(near(preview.center.x, geometry->transform.center.x + 12.f));
        CHECK(near(preview.center.y, geometry->transform.center.y - 5.f));
    }

    return reportAndExit("collider-origin-adr0058-test");
}
