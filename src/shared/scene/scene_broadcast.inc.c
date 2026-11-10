/* Shared production message forwarder; also compiled by its focused test. */
#include "gameplay/scene_runtime.h"
#include "main/task.h"
#include "gameplay/message.h"

/// High byte of `Enemy::workType` for an actor the scene manager placed.
enum {
    SCENE_ACTOR_BANK_SHIFT  = 8,
    SCENE_PLACED_ACTOR_BANK = ENEMY_WORK_ACTOR >> SCENE_ACTOR_BANK_SHIFT,
};

/// Forwards a message and its first payload to every placed actor in the scene.
///
/// The incoming message ID is ignored. `childMessage` selects each child's
/// handler; `payload` carries its first integer or address word, with zero as
/// its second word. Borrowed payloads must stay live through synchronous dispatch.
/// Children publish live `Enemy` work. Save the next sibling before dispatch can
/// unlink the child; callbacks must leave it and the traversal boundary usable.
/// Returns 0 and discards child results.
static s32 _sceneBroadcastToPlacedActors(Task* scene, s32 messageId, TaskMessageWord payload, s32 childMessage)
{
    Task*  head;
    Task*  child;
    Task*  next;
    Enemy* enemy;
    u32    bank;

    head = scene->firstChild;
    if (head == NULL) {
        return 0;
    }
    child = head;
    do {
        enemy = child->spawnArg2.pointer;
        bank  = enemy->workType >> SCENE_ACTOR_BANK_SHIFT;
        next  = child->nextSibling;
        if (bank == SCENE_PLACED_ACTOR_BANK) {
            taskMessageDispatch(child, childMessage, payload, 0);
        }
        child = next;
    } while (child != head);
    return 0;
}

