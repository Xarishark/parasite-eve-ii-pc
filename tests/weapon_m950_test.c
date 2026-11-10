/* Characterisation of m950AttackState: the full firing cycle and the re-fire
 * from the recovery phase, recorded as a trace of phases and side effects. */
#include "weapons/m950.h"
#include "gameplay/player_actor.h"
#include "gameplay/room_effects.h"
#include "gameplay/scene_runtime.h"
#include "gameplay/world_coords.h"
#include "main/scratch.h"
#include "main/mc_types.h"
#include "main/session_types.h"
#include "main/task_types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

unsigned char gHostScratchpad[0x400] __attribute__((aligned(0x400)));
McSaveData gMcSaveData[2];
u8 gPlayerActorAttachmentParentParts[6][2];

static char trace[4096];
static void note(const char* s) { strcat(trace, s); strcat(trace, ";"); }
static int g_animReady, g_attackButton, g_loads, g_armAdvancing;
static AnimationRecord g_record;

const AnimationRecord* animationGetCurrentRecord(const AnimationContext* c, const AnimationSlot* s) { return g_animReady ? &g_record : NULL; }
EffectWork* (effectSpawn)(s32 effectId, GfxCoord* parentCoord, TaskSpawnArg spawnArg, const SVECTOR* offset) { note("effect"); return NULL; }
s32 equipmentConsumeWeaponLoad(s32 item, s32 request) { note("consume"); return 0; }
void playerActorFinishWeaponAttack(Task* t) { note("finish"); }
s32 playerActorIsSlotAdvancingLinearly(Task* t, s32 a, s32 b, s32 c) { return g_armAdvancing; }
void playerActorPlayChildSlotsWithBlend(Task* t, s32 set, s32 u, s32 blend) { char b[40]; snprintf(b, sizeof b, "play%d/%d", (int)set, (int)blend); note(b); }
s32 playerActorQueryWeaponLoads(s32 m) { return g_loads; }
s8 playerActorReadAttackButton(Task* t) { return g_attackButton; }
void playerActorSetWeaponAttackFlags(Task* t, s32 a, s32 b) { note("flags"); }
s32 playerActorSpawnWeaponImpact(const WorldCollisionContact* c, const GfxCoord* w, GfxCoord* o) { note("impact"); return 0; }
void playerActorTrackLockTarget(Task* t) { }
void worldCoordPlaySound(const GfxCoord* c, s32 e, s32 n) { note("sound"); }

int main(void)
{
    static GameActor actor; static Task player, equip; static TmdObject model, equipModel; static GfxCoord coords[4], equipCoords[4];
    /* The attack states reserve scratch blocks; the game initializes the cursor before use (gamemain.c). */
    _scratchStackSetCursor(SCRATCH_STACK_CURSOR_SLOT);
    memset(trace, 0, sizeof trace);
    player.work = &actor; player.extra.tmd = &model; model.coords = coords;
    equip.extra.tmd = &equipModel; equipModel.coords = equipCoords;
    actor.equipmentTasks[1] = &equip;
    g_loads = 5; g_armAdvancing = 1;
    for (int step = 0; step < 14; step++) {
        char b[48];
        g_animReady = step >= 2;
        g_attackButton = step >= 8 && step <= 10;
        m950AttackState(&player);
        snprintf(b, sizeof b, "[%d:%d]", (int)actor.statePhase, (int)actor.stateTimer); note(b);
    }
    puts(trace);
    const char* expected =
        "flags;play9/1;[1:0];[1:0];[2:0];[3:3];[3:2];[3:1];consume;sound;effect;play10/2;[4:0];impact;[5:0];[3:3];[3:2];[3:1];consume;sound;effect;play10/2;[4:0];impact;[5:0];[5:0];";
    if (strcmp(trace, expected) != 0) { fprintf(stderr, "trace differs\nexpected %s\n", expected); return 1; }
    puts("m950: firing cycle and re-fire from recovery OK");
    return 0;
}
