/* Characterisation of the weapon attack states that re-enter their fire path
 * from the recovery phase (M249, AS12, MP5A5). A scripted run records phases
 * and side effects as a trace that must not change when the control flow of
 * the attack state is restructured. Build with WEAPON_FN/WEAPON_HEADER/WEAPON_NAME
 * and the weapon's source file; WEAPON_EXPECTED is the pinned trace. */
#include WEAPON_HEADER
#include "weapon_traces.h"
#define WEAPON_EXPECTED WEAPON_TRACE
#include "gameplay/player_actor.h"
#include "gameplay/room_effects.h"
#include "gameplay/scene_runtime.h"
#include "gameplay/world_collision.h"
#include "gameplay/world_coords.h"
#include "main/scratch.h"
#include "main/mc_types.h"
#include "main/wipsys.h"
#include "main/session_types.h"
#include "main/task_types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

unsigned char gHostScratchpad[0x400] __attribute__((aligned(0x400)));
McSaveData gMcSaveData[2];
u8 gPlayerActorAttachmentParentParts[6][2];
PlayerStatus gPlayerStatus;

static char trace[4096];
static void note(const char* s) { strcat(trace, s); strcat(trace, ";"); }
static int g_animReady, g_attackButton, g_loads, g_armAdvancing;
static AnimationRecord g_record;

const AnimationRecord* animationGetCurrentRecord(const AnimationContext* c, const AnimationSlot* s) { return g_animReady ? &g_record : NULL; }
EffectWork* (effectSpawn)(s32 effectId, GfxCoord* parentCoord, TaskSpawnArg spawnArg, const SVECTOR* offset) { note("effect"); return NULL; }
s32 equipmentConsumeWeaponLoad(s32 item, s32 request) { char b[32]; snprintf(b, sizeof b, "consume%d", (int)request); note(b); return 0; }
void playerActorFinishWeaponAttack(Task* t) { note("finish"); }
s32 playerActorIsSlotAdvancingLinearly(Task* t, s32 a, s32 b, s32 c) { return g_armAdvancing; }
void playerActorPlayChildSlotsWithBlend(Task* t, s32 set, s32 u, s32 blend) { char b[40]; snprintf(b, sizeof b, "play%d/%d", (int)set, (int)blend); note(b); }
void playerActorResetChildSlots(Task* t, s32 set) { char b[32]; snprintf(b, sizeof b, "reset%d", (int)set); note(b); }
s32 playerActorQueryWeaponLoads(s32 m) { return g_loads; }
s8 playerActorReadAttackButton(Task* t) { return g_attackButton; }
void playerActorSetWeaponAttackFlags(Task* t, s32 a, s32 b) { char s[32]; snprintf(s, sizeof s, "flags%d%d", (int)a, (int)b); note(s); }
s32 playerActorSpawnWeaponImpact(const WorldCollisionContact* c, const GfxCoord* w, GfxCoord* o) { note("impact"); return 0; }
void playerActorTrackLockTarget(Task* t) { }
s32 worldCollisionCountContactsByKind(const WorldCollisionContact* c, s32 kind) { return 0; }
void worldCoordPlaySound(const GfxCoord* c, s32 e, s32 n) { note("sound"); }
void taskReparent(Task* child, Task* parent) { note("reparent"); }
void muzzleFlashTask(Task* t) { }

int main(void)
{
    static GameActor actor; static Task player, equip; static TmdObject model, equipModel; static GfxCoord coords[4], equipCoords[4];
    /* The attack states reserve scratch blocks; the game initializes the cursor before use (gamemain.c). */
    _scratchStackSetCursor(SCRATCH_STACK_CURSOR_SLOT);
    memset(trace, 0, sizeof trace);
    player.work = &actor; player.extra.tmd = &model; model.coords = coords;
    equip.extra.tmd = &equipModel; equipModel.coords = equipCoords;
    actor.equipmentTasks[1] = &equip;
    actor.attackButton = PLAYER_ACTOR_ATTACK_BUTTON_PRIMARY;
    actor.stateTimer = 2;
    g_loads = 5; g_armAdvancing = 1;
    for (int step = 0; step < 18; step++) {
        char b[48];
        g_animReady = step >= 2;
        g_attackButton = (step >= 9 && step <= 12) ? PLAYER_ACTOR_ATTACK_BUTTON_PRIMARY : 0;
        if (step == 9) actor.attackControl.cooldownTicks = 0;
        WEAPON_FN(&player);
        snprintf(b, sizeof b, "[%d:%d]", (int)actor.statePhase, (int)actor.stateTimer); note(b);
    }
    puts(trace);
    if (strcmp(trace, WEAPON_EXPECTED) != 0) { fprintf(stderr, "trace differs\nexpected %s\n", WEAPON_EXPECTED); return 1; }
    puts(WEAPON_NAME ": firing cycle and re-fire from recovery OK");
    return 0;
}
