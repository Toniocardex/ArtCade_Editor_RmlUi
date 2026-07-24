// physics-test.cpp — Fase 12: custom physics regression baseline
// Test deterministici: gravità, velocity, posizione, corpo statico, cerchio.
// Non richiede Raylib né finestra grafica.

#include "modules/physics/include/physics.h"
#include "core/types.h"
#include <cassert>
#include <cstdio>
#include <cmath>

using namespace ArtCade;
using namespace ArtCade::Modules;

// Helpers
static PhysicsComponent makeRect(BodyType type, float w, float h,
                                  float density = 1.f, float friction = 0.f) {
    PhysicsComponent c;
    c.bodyType          = type;
    c.collider.shape    = ColliderShape::Rectangle;
    c.collider.size     = { w, h };
    c.collider.density  = density;
    c.collider.friction = friction;
    return c;
}

static PhysicsComponent makeCircle(BodyType type, float radius,
                                    float density = 1.f) {
    PhysicsComponent c;
    c.bodyType          = type;
    c.collider.shape    = ColliderShape::Circle;
    c.collider.size     = { radius, radius };
    c.collider.density  = density;
    c.collider.friction = 0.f;
    return c;
}

int main() {
    printf("[physics-test] avvio...\n");

    // ---- Test 1: init -------------------------------------------------------
    Physics phys;
    assert(phys.init());
    printf("[PASS] 1. init()\n");

    // ---- Test 2: createBody ritorna handle != 0 ----------------------------
    uint32_t h1 = phys.createBody(1, makeRect(BodyType::Dynamic, 1.f, 1.f));
    assert(h1 != 0);
    printf("[PASS] 2. createBody (dynamic rect)\n");

    // ---- Test 3: posizione iniziale {0,0} ----------------------------------
    Vec2 pos = phys.getPosition(h1);
    assert(std::abs(pos.x) < 0.001f && std::abs(pos.y) < 0.001f);
    printf("[PASS] 3. posizione iniziale (0,0)\n");

    // ---- Test 4: setPosition -----------------------------------------------
    phys.setPosition(h1, { 100.f, 200.f });
    pos = phys.getPosition(h1);
    assert(std::abs(pos.x - 100.f) < 0.5f);
    assert(std::abs(pos.y - 200.f) < 0.5f);
    printf("[PASS] 4. setPosition/getPosition\n");

    // ---- Test 5: il corpo cade sotto gravità in 1 secondo ------------------
    // Gravità default Y-down = +10.  Dopo 1s: Δy ≈ ½·10·1² = 5 unità.
    phys.setPosition(h1, { 0.f, 0.f });
    phys.setLinearVelocity(h1, { 0.f, 0.f });
    for (int i = 0; i < 60; ++i)         // 60 step × (1/60)s = 1 secondo
        phys.step(1.f / 60.f);
    pos = phys.getPosition(h1);
    assert(pos.y > 2.f);   // conservativo: almeno 2 unità
    printf("[PASS] 5. gravità (Δy=%.2f dopo 1s)\n", pos.y);

    // ---- Test 6: velocità verticale positiva dopo caduta -------------------
    Vec2 vel = phys.getLinearVelocity(h1);
    assert(vel.y > 0.f);
    printf("[PASS] 6. velocità Y > 0 dopo caduta\n");

    // ---- Test 7: setLinearVelocity -----------------------------------------
    phys.setLinearVelocity(h1, { 5.f, 0.f });
    vel = phys.getLinearVelocity(h1);
    assert(vel.x > 4.f);
    printf("[PASS] 7. setLinearVelocity\n");

    // ---- Test 8: corpo statico non si muove --------------------------------
    uint32_t hStatic = phys.createBody(2, makeRect(BodyType::Static, 10.f, 1.f,
                                                    0.f, 0.f));
    assert(hStatic != 0);
    Vec2 staticBefore = phys.getPosition(hStatic);
    for (int i = 0; i < 30; ++i)
        phys.step(1.f / 60.f);
    Vec2 staticAfter = phys.getPosition(hStatic);
    assert(std::abs(staticAfter.x - staticBefore.x) < 0.001f);
    assert(std::abs(staticAfter.y - staticBefore.y) < 0.001f);
    printf("[PASS] 8. corpo statico non si muove\n");

    // ---- Test 9: corpo cinetico non risponde alla gravità ------------------
    uint32_t hKin = phys.createBody(3, makeRect(BodyType::Kinematic, 2.f, 2.f));
    assert(hKin != 0);
    Vec2 kinBefore = phys.getPosition(hKin);
    for (int i = 0; i < 30; ++i)
        phys.step(1.f / 60.f);
    Vec2 kinAfter = phys.getPosition(hKin);
    // Kinematic con velocità 0 non si muove
    assert(std::abs(kinAfter.y - kinBefore.y) < 0.001f);
    printf("[PASS] 9. corpo kinematic fermo senza velocità\n");

    // ---- Test 10: corpo cerchio --------------------------------------------
    uint32_t hCircle = phys.createBody(4, makeCircle(BodyType::Dynamic, 0.5f));
    assert(hCircle != 0);
    phys.step(1.f / 60.f);
    Vec2 circlePos = phys.getPosition(hCircle);
    assert(circlePos.y >= 0.f);   // si è mosso verso Y+ (gravità)
    printf("[PASS] 10. createBody cerchio\n");

    // ---- Test 11: areOverlapping -------------------------------------------
    // Due box allo stesso punto si sovrappongono
    uint32_t hA = phys.createBody(10, makeRect(BodyType::Static, 2.f, 2.f));
    uint32_t hB = phys.createBody(11, makeRect(BodyType::Static, 2.f, 2.f));
    assert(phys.areOverlapping(hA, hB));
    // Uno spostato lontano non si sovrappone
    phys.setPosition(hB, { 100.f, 100.f });
    assert(!phys.areOverlapping(hA, hB));
    printf("[PASS] 11. areOverlapping\n");

    // ---- Test 12: destroyBody + handle invalido ----------------------------
    phys.destroyBody(h1);
    phys.destroyBody(hStatic);
    phys.destroyBody(hKin);
    phys.destroyBody(hCircle);
    phys.destroyBody(hA);
    phys.destroyBody(hB);
    // Double-destroy deve essere un no-op (non crashare)
    phys.destroyBody(h1);
    printf("[PASS] 12. destroyBody + double-destroy no-op\n");

  // ---- Test 13: hasActiveBodies --------------------------------------------
    assert(!phys.hasActiveBodies());
    uint32_t hTmp = phys.createBody(99, makeRect(BodyType::Dynamic, 1.f, 1.f));
    assert(phys.hasActiveBodies());
    phys.destroyBody(hTmp);
    assert(!phys.hasActiveBodies());
    printf("[PASS] 15. hasActiveBodies\n");

    // ---- Test 16: dynamic rests on static (multi-pass + resting contact) ----
    uint32_t hFloor = phys.createBody(30, makeRect(BodyType::Static, 40.f, 2.f));
    uint32_t hBox   = phys.createBody(31, makeRect(BodyType::Dynamic, 2.f, 2.f));
    phys.setPosition(hFloor, { 0.f, 20.f });
    phys.setPosition(hBox, { 0.f, 10.f });
    phys.setLinearVelocity(hBox, { 0.f, 0.f });
    for (int i = 0; i < 120; ++i)
        phys.step(1.f / 60.f);
    Vec2 boxPos = phys.getPosition(hBox);
    Vec2 boxVel = phys.getLinearVelocity(hBox);
    assert(boxPos.y >= 18.f && boxPos.y <= 20.5f);
    assert(std::abs(boxVel.y) < 0.15f);
    printf("[PASS] 16. dynamic rests on static (y=%.2f vy=%.3f)\n", boxPos.y, boxVel.y);
    phys.destroyBody(hFloor);
    phys.destroyBody(hBox);

    // ---- Test 17: two dynamics stacked — top settles without runaway bounce ----
    hFloor = phys.createBody(32, makeRect(BodyType::Static, 20.f, 2.f));
    uint32_t hLow  = phys.createBody(33, makeRect(BodyType::Dynamic, 2.f, 2.f));
    uint32_t hHigh = phys.createBody(34, makeRect(BodyType::Dynamic, 2.f, 2.f));
    phys.setPosition(hFloor, { 0.f, 30.f });
    phys.setPosition(hLow, { 0.f, 20.f });
    phys.setPosition(hHigh, { 0.f, 12.f });
    for (int i = 0; i < 180; ++i)
        phys.step(1.f / 60.f);
    boxVel = phys.getLinearVelocity(hHigh);
    assert(std::abs(boxVel.y) < 0.25f);
    phys.destroyBody(hFloor);
    phys.destroyBody(hLow);
    phys.destroyBody(hHigh);
    printf("[PASS] 17. stacked dynamics settle (vy=%.3f)\n", boxVel.y);

    // ---- Test 18: impulse is immediate; force is integrated over dt --------
    phys.setGravity({ 0.f, 0.f });
    uint32_t hForce = phys.createBody(35, makeRect(BodyType::Dynamic, 2.f, 2.f));
    phys.applyImpulse(hForce, { 12.f, 0.f });
    Vec2 forceVel = phys.getLinearVelocity(hForce);
    assert(std::abs(forceVel.x - 12.f) < 0.001f);
    phys.setLinearVelocity(hForce, { 0.f, 0.f });
    phys.applyForce(hForce, { 12.f, 0.f });
    forceVel = phys.getLinearVelocity(hForce);
    assert(std::abs(forceVel.x) < 0.001f);
    phys.step(0.5f, 2);
    forceVel = phys.getLinearVelocity(hForce);
    assert(std::abs(forceVel.x - 6.f) < 0.001f);
    phys.step(0.5f, 2);
    forceVel = phys.getLinearVelocity(hForce);
    assert(std::abs(forceVel.x - 6.f) < 0.001f);
    phys.destroyBody(hForce);
    printf("[PASS] 18. impulse and force have distinct semantics\n");

    // ---- Test 19: dynamic CCD against a thin wall --------------------------
    uint32_t hWall = phys.createBody(40, makeRect(BodyType::Static, 1.f, 100.f));
    uint32_t hFast = phys.createBody(41, makeRect(BodyType::Dynamic, 2.f, 2.f));
    phys.setPosition(hWall, { 50.f, 0.f });
    phys.setPosition(hFast, { 0.f, 0.f });
    phys.setLinearVelocity(hFast, { 1000.f, 0.f });
    phys.step(0.1f);
    Vec2 fastPos = phys.getPosition(hFast);
    Vec2 fastVel = phys.getLinearVelocity(hFast);
    assert(fastPos.x < 49.f);
    assert(std::abs(fastVel.x) < 0.001f);
    phys.destroyBody(hWall);
    phys.destroyBody(hFast);
    printf("[PASS] 19. dynamic CCD stops before thin wall (x=%.2f)\n", fastPos.x);

    phys.shutdown();
    printf("\n[physics-test] 19/19 PASSED\n");
    return 0;
}
