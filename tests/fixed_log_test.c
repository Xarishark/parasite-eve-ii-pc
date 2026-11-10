#include "gameplay/world_collision.h"
#include "main/scratch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern long cln(long);
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
_Alignas(void*) u8 gHostScratchpad[0x400];
s32 gWorldCollisionSurfacePushbackSuppression[8];
/* ASan retains the production list-head tables, even though this fixture
 * exercises only tagged floor contacts. Keep their external heads real. */
WorldCollisionOccluder* gWorldCollisionOccluderListHead;
WorldCollisionTrigger* gWorldCollisionViewBoundaryListHead;
void Psyz_PadsOverride(int p, char* f, int n) { (void)p; (void)f; (void)n; }
int main(void)
{
    /* SDK cor_04 at 0x8004AB60 in the retail executable takes Q12 input.
     * These include exact powers and its integer approximation between them. */
    const struct { long input, expected; } cases[] = {
        {1, -34068}, {2048, -2839}, {4096, 0}, {8192, 2839},
        {12288, 4500}, {65536, 11356}, {524288, 19873}
    };
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i)
        CHECK(cln(cases[i].input) == cases[i].expected);
    for (unsigned bits = 1; bits <= 255; ++bits) {
        const u8 mask = bits;
        unsigned highest = 0;
        for (unsigned remaining = bits >> 1; remaining; remaining >>= 1) ++highest;
        CHECK(worldCollisionSurfaceClassFromMask(&mask) == (s32)highest);
    }
    *SCRATCH_STACK_CURSOR_SLOT = (void*)SCRATCH_STACK_CURSOR_SLOT;
    for (unsigned surface = 0; surface < 8; ++surface) {
        const u8 mask = 1U << surface;
        CHECK(worldCollisionSurfaceClassFromMask(&mask) == (s32)surface);
        CHECK(worldCollisionSurfaceClassFromKey(WORLD_COLLISION_CONTACT_GRID_FLOOR | surface) == (s32)surface);
        WorldCollisionContact contact = {
            .flags = WORLD_COLLISION_CONTACT_OCCUPIED | WORLD_COLLISION_CONTACT_LAST,
            .distance = 0,
            .key.value = WORLD_COLLISION_CONTACT_GRID_FLOOR | surface,
            .response.direction = {0, -4096, 0, 0},
        };
        WorldCollisionDelta delta;
        s32 actualMask = 0;
        CHECK(worldCollisionResolveResponsePushback(&contact, &delta, 1, &actualMask) == WORLD_COLLISION_PUSHBACK_GRID_HIT);
        CHECK(actualMask == mask);
        CHECK(worldCollisionSurfaceClassFromMask((const u8*)&actualMask) == (s32)surface);
        actualMask = 0;
        worldCollisionResolvePushback(&contact, &delta, 1, &actualMask);
        CHECK(actualMask == mask);
        CHECK(*SCRATCH_STACK_CURSOR_SLOT == (void*)SCRATCH_STACK_CURSOR_SLOT);
    }
    WorldCollisionContact contact = {
        .flags = WORLD_COLLISION_CONTACT_OCCUPIED | WORLD_COLLISION_CONTACT_LAST,
        .distance = 126,
        .key.value = WORLD_COLLISION_CONTACT_GRID_FLOOR,
        .response.direction = {0, -4096, 0, 0},
    };
    WorldCollisionDelta delta;
    worldCollisionResolvePushback(&contact, &delta, 1, NULL);
    CHECK(delta.fixed.vx.word == 0 && delta.fixed.vy.word == -8257536 && delta.fixed.vz.word == 0);
    contact.response.direction.vx = -4096;
    contact.response.direction.vy = 0;
    contact.response.direction.vz = 4096;
    worldCollisionResolvePushback(&contact, &delta, 1, NULL);
    CHECK(delta.fixed.vx.word == -8257536 && delta.fixed.vy.word == 0 && delta.fixed.vz.word == 8257536);
    puts("SDK Q12 logarithm and all eight tagged floor/contact surface classes OK");
    return 0;
}
