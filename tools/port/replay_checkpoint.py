#!/usr/bin/env python3
"""Run a native input replay and inspect its EOF state through LLDB (read only).

Example: replay_checkpoint.py movie.pad --expect bp=140 --expect carried.172=10
The complete debugger output is retained in --log. No target function is called.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import shlex
import subprocess
import tempfile

PREFIX = "PE2_CHECKPOINT "


def checkpoint_value(snapshot, path):
    """Resolve dict keys and list indices in a dotted expectation path."""
    value = snapshot
    for part in path.split("."):
        if isinstance(value, dict):
            value = value.get(part)
        elif isinstance(value, list) and part.isdecimal() and int(part) < len(value):
            value = value[int(part)]
        else:
            return None
    return value


def collect(target):
    import lldb

    if target.GetProcess().GetState() != lldb.eStateStopped:
        raise RuntimeError("checkpoint requires a stopped process")

    def child(value, name):
        result = value.GetChildMemberWithName(name)
        if not result.IsValid():
            raise RuntimeError(f"missing debug field {name}: {result.GetError()}")
        return result

    def number(value):
        error = value.GetError()
        if not value.IsValid() or error.Fail():
            raise RuntimeError(str(error))
        return value.GetValueAsSigned()

    session = target.FindFirstGlobalVariable("gGameSession").Dereference()
    location = child(session, "location")
    status = target.FindFirstGlobalVariable("gPlayerStatus")
    save = child(target.FindFirstGlobalVariable("gMcSaveData").GetChildAtIndex(0), "state")
    carried = child(save, "carriedItems")
    if number(child(carried, "tableId")) != 0:
        raise RuntimeError("checkpoint requires the save-backed carried range")
    first, count = (number(child(carried, name)) for name in ("firstRow", "rowCount"))
    rows = child(save, "itemRows")
    if first < 0 or count < 0 or first + count > rows.GetNumChildren():
        raise RuntimeError("carried range exceeds its inventory table")
    inventory = {}
    for index in range(first, first + count):
        row = rows.GetChildAtIndex(index)
        item, qty = (number(child(row, name)) for name in ("itemId", "qty"))
        if item:
            key = str(item)
            inventory[key] = inventory.get(key, 0) + qty
    player_task = child(session, "ptrSlots").GetChildAtIndex(3).Dereference()
    actor_address = child(player_task, "work").GetValueAsUnsigned()
    actor_type = target.FindFirstType("GameActor")
    if not actor_address or not actor_type.IsValid():
        raise RuntimeError(f"player actor is not available (HP={number(child(status, 'hp'))}, area={number(child(location, 'area'))})")
    actor = target.CreateValueFromAddress("player", lldb.SBAddress(actor_address, target), actor_type)
    coord = child(child(actor, "collisionBodies").GetChildAtIndex(0), "coord").Dereference()
    position = child(child(coord, "coord"), "t")
    snapshot = {
        "actor": {"state": number(child(actor, "state")),
                  "mode": number(child(actor, "mode")),
                  "movementMode": number(child(actor, "movementMode")),
                  "statePhase": number(child(actor, "statePhase")),
                  "stateTimer": number(child(actor, "stateTimer")),
                  "surfaceClass": number(child(actor, "surfaceClass")),
                  "scriptedMotionPending": number(child(actor, "scriptedMotionPending")),
                  "jumpSteps": (number(child(child(actor, "scriptMotion"), "jumpSteps"))
                                if number(child(actor, "mode")) == 2 and number(child(actor, "state")) == 3 else None),
                  "turnRateIndex": number(child(actor, "turnRateIndex")),
                  "aimControl": number(child(actor, "aimControl")),
                  "aimYaw": number(child(actor, "aimYaw")),
                  "aimTrackingState": number(child(actor, "aimTrackingState")),
                  "weaponForwardView": [number(child(child(child(actor, "weaponCollisionCoord"), "workm"), "m").GetChildAtIndex(i).GetChildAtIndex(2)) for i in range(3)],
                  "movementInputDisabled": number(child(actor, "movementInputDisabled")),
                  "padHeld": number(child(actor, "padHeld")),
                  "movementSign": number(child(actor, "movementSign")),
                  "turnSign": number(child(actor, "turnSign")),
                  "velocity": [number(child(child(actor, "velocity"), axis)) for axis in ("vx", "vy", "vz")],
                  "rotation": [number(child(child(actor, "rotation"), axis)) for axis in ("vx", "vy", "vz")],
                  "rootForward": [number(child(child(coord, "coord"), "m").GetChildAtIndex(i).GetChildAtIndex(2)) for i in (0, 2)],
                  "position": [number(position.GetChildAtIndex(i)) for i in range(3)]},
        "location": {name: number(child(location, name)) for name in
                     ("stage", "area", "roomState", "view", "warp", "variant")},
        "bp": number(child(status, "bp")),
        "playerStatus": {name: number(child(status, name)) for name in
                         ("hp", "hpMax", "mp", "mpMax", "exp", "weapon", "weaponSlotItem", "armor", "statusFlags")},
        "weaponLoads": {
            str(index + 0x80): {field: number(child(child(save, "weaponItems").GetChildAtIndex(index), field))
                                for field in ("primaryItemId", "primaryQty", "secondaryItemId", "secondaryQty")}
            for index in range(32)
            if number(child(child(save, "weaponItems").GetChildAtIndex(index), "primaryItemId"))
        },
        "carried": inventory,
        "inventoryRows": [
            {"index": index - first, **{field: number(child(rows.GetChildAtIndex(index), field))
                                       for field in ("itemId", "qty", "attachSlot")}}
            for index in range(first, first + count)
            if number(child(rows.GetChildAtIndex(index), "itemId"))
        ],
        "menuSelection": {name: number(child(target.FindFirstGlobalVariable(symbol), "selectedItemIndex"))
                          for name, symbol in (("main", "gItemMenuMainCommandList"),
                                               ("inventory", "gItemMenuInventoryList"),
                                               ("command", "gItemMenuCommandList"),
                                               ("equippedWeapon", "gItemMenuEquippedWeaponList"),
                                               ("weaponChoice", "gItemMenuWeaponChoiceList"),
                                               ("armorAttachment", "gItemMenuArmorAttachmentList"))},
        "uiOpen": number(child(session, "uiOpen")),
        "eventState": number(child(session, "eventState")),
    }
    display = target.FindFirstGlobalVariable("gDisplayState")
    view = child(target.FindFirstGlobalVariable("gGfxViewCoord"), "workm")
    snapshot["camera"] = {
        "rotation": [[number(child(view, "m").GetChildAtIndex(row).GetChildAtIndex(column))
                      for column in range(3)] for row in range(3)],
        "translation": [number(child(view, "t").GetChildAtIndex(i)) for i in range(3)],
        "screenDistance": number(child(display, "screenDistance")),
        "playerViewPosition": [number(child(child(coord, "workm"), "t").GetChildAtIndex(i)) for i in range(3)],
    }
    snapshot["flow"] = {
        "pendingMode": number(child(display, "pendingMode")),
        "gameMode": number(child(display, "gameMode")),
        "menuHoldState": number(child(display, "holdState")),
        "displayOwner": number(child(display, "displayOwner")),
        "frameBuffer": number(child(display, "frameBuffer")),
        "drawBuffer": number(child(display, "drawBuffer")),
        "menuHoldCount": number(child(display, "holdCount")),
        "hudInputDelay": number(target.FindFirstGlobalVariable("gHudInputDelayUpdates")),
        "cutsceneHold": number(child(session, "cutsceneHold")),
        "dirActionBusy": number(child(session, "dirActionBusy")),
        "battleResetPending": number(child(session, "battleResetPending")),
        "flowFlags": number(child(session, "flowFlags")),
        "transitionTasks": [],
    }
    in_gallery = snapshot["location"]["stage"] == 1 and snapshot["location"]["area"] == 20
    if in_gallery:
        snapshot["gallery"] = {
            "trainingActive": number(target.FindFirstGlobalVariable("gShootingGalleryPierceGalleryTrainingActive")),
            "selectedCourse": number(target.FindFirstGlobalVariable("gShootingGalleryPierceGallerySelectedCourseLevel")),
            "tasks": [],
        }
    task = child(target.FindFirstGlobalVariable("gTaskDefaultList"), "next")
    visited = set()
    while task.GetValueAsUnsigned():
        address = task.GetValueAsUnsigned()
        if address in visited:
            raise RuntimeError("cycle in default task list")
        visited.add(address)
        value = task.Dereference()
        callback = child(value, "callback").GetValueAsUnsigned()
        name = target.ResolveLoadAddress(callback).GetSymbol().GetName() or ""
        if in_gallery and name in ("_mistShootingGalleryCourseTask", "shootingGalleryPierceGalleryTrainingMenuTask"):
            entry = {"callback": name, "state": number(child(value, "state"))}
            if name == "_mistShootingGalleryCourseTask" and child(value, "work").GetValueAsUnsigned():
                work_type = target.FindFirstType("MistShootingGalleryWork")
                if not work_type.IsValid():
                    raise RuntimeError("missing gallery course debug type")
                work = target.CreateValueFromAddress("galleryCourse", lldb.SBAddress(child(value, "work").GetValueAsUnsigned(), target), work_type)
                entry["course"] = {field: number(child(work, field)) for field in
                                   ("course", "phase", "resumePhase", "scriptFrame", "timeLeft", "spawnIndex", "timer",
                                    "liveTargets", "targetLockMask", "interrupted", "actionTriggered", "captionStep", "lethalHit")}
                entry["course"]["kills"] = [number(child(work, "kills").GetChildAtIndex(i)) for i in range(13)]
            snapshot["gallery"]["tasks"].append(entry)
        if name in ("_sceneBattleResultTask", "gameFlowReloadSessionTask",
                    "gameFlowPrepareSessionReloadTask", "loadingFinishSessionLoadTask"):
            snapshot["flow"]["transitionTasks"].append({
                "callback": name, "state": number(child(value, "state")),
                "status": number(child(value, "status")),
            })
        task = child(child(value, "node"), "next")
    combat = target.FindFirstGlobalVariable("gSceneCombatState")
    attachment = target.FindFirstGlobalVariable("gAttachmentState")
    snapshot["parasiteEnergy"] = {
        "levels": [number(child(save, "attachLevels").GetChildAtIndex(i)) for i in range(12)],
        **{name: number(child(attachment, name)) for name in ("mode", "wheelIndex", "activeIndex", "flags", "queuedIndex", "menuOpen", "soundStep", "duration", "metabolismTicks", "antibodyTicks", "energyShotTicks", "bodyWard", "mindWard")},
        "elementSelection": [number(child(target.FindFirstGlobalVariable("gItemMenuPeElementLists").GetChildAtIndex(i), "selectedItemIndex")) for i in range(4)],
    }
    display = target.FindFirstGlobalVariable("gDisplayState")
    snapshot["timing"] = {
        name: number(child(display, name))
        for name in ("gameTick", "loopTicks", "frameCount", "vsyncCount")
    }
    snapshot["timing"]["randomLcgState"] = number(target.FindFirstGlobalVariable("gRandomLcgState")) & 0xFFFFFFFF
    signals = child(child(combat, "signals"), "bytes")
    snapshot["combat"] = {
        "battleRefs": number(child(combat, "battleRefs")),
        "actorControl": number(child(combat, "actorControl")),
        "battlePhase": number(child(signals, "battlePhase")),
        "endDelayFrames": number(child(signals, "endDelayFrames")),
        "crawlerEntranceReady": number(child(combat, "maggotCaterpillarEntranceReady")),
        "batWave": number(child(combat, "actor03700Wave")),
    }
    order = target.FindFirstGlobalVariable("_gInventoryCollectedItemOrder")
    bits = child(save, "collectedBits")
    snapshot["collected"] = []
    for index in range(order.GetNumChildren()):
        item_id = number(order.GetChildAtIndex(index))
        if item_id == 0xFFFF:
            break
        bit = item_id & 0x7F
        if number(bits.GetChildAtIndex(bit // 32)) & (1 << (bit % 32)):
            snapshot["collected"].append(item_id)

    if snapshot["location"]["stage"] == 1:
        bank = target.FindFirstGlobalVariable("gGameFlagNibbleBanks").GetChildAtIndex(0)
        packed = child(child(bank, "payload"), "packedFlags")
        snapshot["akropolis"] = {
            "cafeteriaRouteProgress": number(packed.GetChildAtIndex(0)) >> 4,
            "sanctuaryRouteProgress": number(packed.GetChildAtIndex(0x001 // 2)) & 15,
            "securityPanelKeys": number(packed.GetChildAtIndex(0x009 // 2)) & 15,
            "bridgeProgress": number(packed.GetChildAtIndex(0x002 // 2)) >> 4,
            "bridgeFireEscapeDoorSceneSeen": number(packed.GetChildAtIndex(0x010 // 2)) >> 4,
            "cafeteriaBossReport": number(packed.GetChildAtIndex(0x00E // 2)) >> 4,
            "objective": number(packed.GetChildAtIndex(0x0A2 // 2)),
            "storyDialogue": number(packed.GetChildAtIndex(0x155 // 2)) & 15,
            "cafeteriaDoorState": number(packed.GetChildAtIndex(0x008 // 2)) >> 4,
            "patioCutsceneProgress": number(packed.GetChildAtIndex(0x021 // 2)) & 15,
            "patioAmbushSeen": number(packed.GetChildAtIndex(0x026 // 2)) >> 4,
            "parthenonKey": bool(number(child(save, "collectedBits").GetChildAtIndex(0)) & 2),
            "blueKey": bool(number(child(save, "collectedBits").GetChildAtIndex(0)) & 16),
            "playerHp": number(child(status, "hp")),
        }

    if snapshot["location"]["stage"] == 1 and snapshot["location"]["area"] == 19:
        snapshot["parking"] = {
            "pierceInParking": number(packed.GetChildAtIndex(0x0ED // 2)) & 15,
            "dryfieldDepartureState": number(packed.GetChildAtIndex(0x0F1 // 2)) & 15,
        }
    if snapshot["location"]["stage"] == 1 and snapshot["location"]["area"] == 14:
        prompt = target.FindFirstGlobalVariable("gActionPromptSlots").GetChildAtIndex(0)
        screen = child(child(prompt, "screen"), "xy")
        snapshot["bridge"] = {
            "cursor": {"x": number(child(screen, "x")), "y": number(child(screen, "y")),
                       "speed": number(child(prompt, "cursorSpeed")), "mode": number(child(prompt, "mode"))},
            "tasks": [],
        }
        task = child(target.FindFirstGlobalVariable("gTaskDefaultList"), "next")
        visited = set()
        while task.GetValueAsUnsigned():
            address = task.GetValueAsUnsigned()
            if address in visited:
                raise RuntimeError("cycle in Bridge task list")
            visited.add(address)
            value = task.Dereference()
            name = target.ResolveLoadAddress(child(value, "callback").GetValueAsUnsigned()).GetSymbol().GetName() or ""
            if "Keypad" in name and "Bridge" in name:
                snapshot["bridge"]["tasks"].append({"callback": name, "state": number(child(value, "state"))})
            if name == "akropolisBridgeKeypadTask" and child(value, "work").GetValueAsUnsigned():
                types = target.FindTypes("_KeypadWork")
                candidates = [types.GetTypeAtIndex(i) for i in range(types.GetSize())
                              if any(types.GetTypeAtIndex(i).GetFieldAtIndex(j).GetName() == "keypadExamined"
                                     for j in range(types.GetTypeAtIndex(i).GetNumberOfFields()))]
                if len(candidates) != 1:
                    raise RuntimeError("cannot identify Bridge keypad work type")
                work = target.CreateValueFromAddress("keypad", lldb.SBAddress(child(value, "work").GetValueAsUnsigned(), target), candidates[0])
                snapshot["bridge"]["keypad"] = {field: number(child(work, field)) for field in
                                               ("code", "digitCount", "rejectedCount", "blinkCount", "timer", "selectedKey", "keypadExamined")}
            task = child(child(value, "node"), "next")

    if snapshot["location"]["stage"] == 1 and snapshot["location"]["area"] == 6:
        snapshot["securityRoom"] = {
            "sanctuaryRouteProgress": number(packed.GetChildAtIndex(0x001 // 2)) & 15,
            "panelKeys": number(packed.GetChildAtIndex(0x009 // 2)) & 15,
            "monitorScenesSeen": number(packed.GetChildAtIndex(0x00A // 2)) >> 4,
            "rupertSceneFinished": number(packed.GetChildAtIndex(0x102 // 2)) >> 4,
            "monitorBrightness": number(packed.GetChildAtIndex(0x02A // 2)) >> 4,
            "savedView": number(child(child(save, "location"), "view")),
            "tasks": [],
        }
        prompt = target.FindFirstGlobalVariable("gActionPromptSlots").GetChildAtIndex(0)
        screen = child(child(prompt, "screen"), "xy")
        snapshot["securityRoom"]["cursor"] = {
            "x": number(child(screen, "x")), "y": number(child(screen, "y")),
            "speed": number(child(prompt, "cursorSpeed")),
            "mode": number(child(prompt, "mode")),
        }
        snapshot["securityRoom"]["menuSelection"] = {
            name: number(child(target.FindFirstGlobalVariable(symbol), "selectedItemIndex"))
            for name, symbol in (("hotspot", "gItemMenuHotspotCommandList"),
                                 ("collected", "gItemMenuCollectedList"))
        }
        task = child(target.FindFirstGlobalVariable("gTaskDefaultList"), "next")
        visited = set()
        while task.GetValueAsUnsigned():
            address = task.GetValueAsUnsigned()
            if address in visited:
                raise RuntimeError("cycle in security-room task list")
            visited.add(address)
            value = task.Dereference()
            callback = child(value, "callback").GetValueAsUnsigned()
            name = target.ResolveLoadAddress(callback).GetSymbol().GetName()
            if name in ("_securityMonitorsRupertTask", "_securityMonitorsSwatTask",
                        "akropolisSecurityRoomMonitorTask", "akropolisSecurityRoomPowerSupplyTask"):
                snapshot["securityRoom"]["tasks"].append({
                    "callback": name, "state": number(child(value, "state")),
                })
                if name == "akropolisSecurityRoomMonitorTask":
                    types = target.FindTypes("_MonitorWork")
                    candidates = [types.GetTypeAtIndex(i) for i in range(types.GetSize())
                                  if types.GetTypeAtIndex(i).GetFieldAtIndex(0).GetName() == "screenLevel"]
                    if len(candidates) != 1:
                        raise RuntimeError("cannot identify security monitor work type")
                    work_address = child(value, "work").GetValueAsUnsigned()
                    work = target.CreateValueFromAddress("monitor", lldb.SBAddress(work_address, target), candidates[0])
                    snapshot["securityRoom"]["monitor"] = {
                        field: number(child(work, field)) for field in
                        ("screenLevel", "hotspotId", "enemySceneArmed", "enemyCaptionStarted")
                    }
                if name == "akropolisSecurityRoomPowerSupplyTask":
                    types = target.FindTypes("_PowerSupplyWork")
                    candidates = [types.GetTypeAtIndex(i) for i in range(types.GetSize())
                                  if types.GetTypeAtIndex(i).GetFieldAtIndex(0).GetName() == "usedKey"]
                    if len(candidates) != 1:
                        raise RuntimeError("cannot identify security power-supply work type")
                    work_address = child(value, "work").GetValueAsUnsigned()
                    work = target.CreateValueFromAddress("power", lldb.SBAddress(work_address, target), candidates[0])
                    snapshot["securityRoom"]["powerSupply"] = {
                        field: number(child(work, field)) for field in ("usedKey", "timer", "hotspotId")
                    }
            task = child(child(value, "node"), "next")

    if snapshot["location"]["stage"] == 1 and snapshot["location"]["area"] == 5:
        task = child(target.FindFirstGlobalVariable("gTaskDefaultList"), "next")
        visited = set()
        while task.GetValueAsUnsigned():
            address = task.GetValueAsUnsigned()
            if address in visited:
                raise RuntimeError("cycle in task list")
            visited.add(address)
            value = task.Dereference()
            callback = child(value, "callback").GetValueAsUnsigned()
            name = target.ResolveLoadAddress(callback).GetSymbol().GetName()
            if name == "_akropolisPlazaSequenceTask":
                address = child(value, "work").GetValueAsUnsigned()
                work = target.CreateValueFromAddress("plaza", lldb.SBAddress(address, target), target.FindFirstType("_SequenceWork"))
                snapshot["plaza"] = {"eventStep": number(child(work, "step"))}
                break
            task = child(child(value, "node"), "next")
        trigger = target.FindFirstGlobalVariable("gWorldCollisionActionTriggerListHead")
        kinds, visited = [], set()
        while trigger.GetValueAsUnsigned():
            address = trigger.GetValueAsUnsigned()
            if address in visited:
                raise RuntimeError("cycle in action trigger list")
            visited.add(address)
            value = trigger.Dereference()
            kinds.append(number(child(value, "parameter0")))
            trigger = child(value, "next")
        if "plaza" in snapshot:
            snapshot["plaza"]["actionKinds"] = kinds
    if snapshot["location"]["stage"] == 1 and snapshot["location"]["area"] == 1:
        bank = target.FindFirstGlobalVariable("gGameFlagNibbleBanks").GetChildAtIndex(0)
        packed = child(child(bank, "payload"), "packedFlags")
        byte = number(packed.GetChildAtIndex(0x01F // 2))
        snapshot["square"] = {
            "telephoneSceneSeen": byte & 15,
            "telephoneDialogueCounter": number(packed.GetChildAtIndex(0x003 // 2)) & 15,
            "telephoneCallPending": number(target.FindFirstGlobalVariable("_gAkropolisSquareTelephoneCallPending")),
            "savedWarp": number(child(child(save, "location"), "warp")),
            "strangerAlive": False,
        }
        task = child(target.FindFirstGlobalVariable("gTaskDefaultList"), "next")
        visited = set()
        while task.GetValueAsUnsigned():
            address = task.GetValueAsUnsigned()
            if address in visited:
                raise RuntimeError("cycle in task list")
            visited.add(address)
            value = task.Dereference()
            callback = child(value, "callback").GetValueAsUnsigned()
            name = target.ResolveLoadAddress(callback).GetSymbol().GetName()
            if name == "_squareGrinningStrangerTask":
                snapshot["square"]["strangerAlive"] = True
            task = child(child(value, "node"), "next")
    if snapshot["location"]["stage"] == 1 and snapshot["location"]["area"] == 3:
        bank = target.FindFirstGlobalVariable("gGameFlagNibbleBanks").GetChildAtIndex(0)
        packed = child(child(bank, "payload"), "packedFlags")
        snapshot["patio"] = {
            "cutsceneProgress": number(packed.GetChildAtIndex(0x021 // 2)) & 15,
            "cafeteriaRouteProgress": number(packed.GetChildAtIndex(0)) >> 4,
            "playerHp": number(child(status, "hp")),
            "battleRefs": number(child(target.FindFirstGlobalVariable("gSceneCombatState"), "battleRefs")),
            "actorControl": number(child(target.FindFirstGlobalVariable("gSceneCombatState"), "actorControl")),
            "strangers": [],
        }
        task = child(target.FindFirstGlobalVariable("gTaskDefaultList"), "next")
        visited = set()
        while task.GetValueAsUnsigned():
            address = task.GetValueAsUnsigned()
            if address in visited:
                raise RuntimeError("cycle in task list")
            visited.add(address)
            value = task.Dereference()
            callback = child(value, "callback").GetValueAsUnsigned()
            name = target.ResolveLoadAddress(callback).GetSymbol().GetName()
            if name == "_actor01900Task":
                work_address = child(value, "work").GetValueAsUnsigned()
                if not work_address:
                    raise RuntimeError("patio Stranger is not initialized")
                work = target.CreateValueFromAddress("stranger", lldb.SBAddress(work_address, target), target.FindFirstType("_Actor01900Work"))
                enemy_address = child(child(value, "spawnArg2"), "pointer").GetValueAsUnsigned()
                enemy = target.CreateValueFromAddress("enemy", lldb.SBAddress(enemy_address, target), target.FindFirstType("Enemy"))
                snapshot["patio"]["strangers"].append({
                    "state": number(child(work, "state")),
                    "hp": number(child(enemy, "hp")),
                })
            task = child(child(value, "node"), "next")
    if snapshot["location"]["stage"] == 1 and snapshot["location"]["area"] == 2:
        bank = target.FindFirstGlobalVariable("gGameFlagNibbleBanks").GetChildAtIndex(0)
        packed = child(child(bank, "payload"), "packedFlags")
        snapshot["eastHall"] = {
            "cafeteriaRouteProgress": number(packed.GetChildAtIndex(0)) >> 4,
            "cafeteriaDoorState": number(packed.GetChildAtIndex(0x008 // 2)) >> 4,
            "patioCutsceneProgress": number(packed.GetChildAtIndex(0x021 // 2)) & 15,
            "parthenonKey": bool(number(child(save, "collectedBits").GetChildAtIndex(0)) & 2),
            "openingStarted": number(target.FindFirstGlobalVariable("_gAkropolisEastElevatorHallOpeningEventStarted")),
            "playerHp": number(child(status, "hp")),
            "debugMode": number(child(target.FindFirstGlobalVariable("gDisplayState"), "debugMode")),
        }
    if snapshot["location"]["stage"] == 1 and snapshot["location"]["area"] == 4:
        combat = target.FindFirstGlobalVariable("gSceneCombatState")
        snapshot["cafeteria"] = {
            "sceneProgress": number(target.FindFirstGlobalVariable("_gAkropolisCafeteriaSceneProgress")),
            "battleRefs": number(child(combat, "battleRefs")),
            "actorControl": number(child(combat, "actorControl")),
            "sceneTasks": [],
            "enemies": [],
        }
        task = child(target.FindFirstGlobalVariable("gTaskDefaultList"), "next")
        visited = set()
        while task.GetValueAsUnsigned():
            address = task.GetValueAsUnsigned()
            if address in visited:
                raise RuntimeError("cycle in task list")
            visited.add(address)
            value = task.Dereference()
            callback = child(value, "callback").GetValueAsUnsigned()
            name = target.ResolveLoadAddress(callback).GetSymbol().GetName() or ""
            if name.startswith("_akropolisCafeteria"):
                snapshot["cafeteria"]["sceneTasks"].append({"callback": name, "state": number(child(value, "state"))})
            if name == "_cafeteriaBossStrangerTask":
                enemy_address = child(child(value, "spawnArg2"), "pointer").GetValueAsUnsigned()
                if enemy_address:
                    enemy = target.CreateValueFromAddress("enemy", lldb.SBAddress(enemy_address, target), target.FindFirstType("Enemy"))
                    snapshot["cafeteria"]["enemies"].append({"callback": name, "hp": number(child(enemy, "hp"))})
            task = child(child(value, "node"), "next")
    if snapshot["location"]["stage"] == 1 and snapshot["location"]["area"] == 9:
        snapshot["forkedRoad"] = {
            "fountainDoorOpened": number(packed.GetChildAtIndex(0x013 // 2)) & 15,
            "returnRideSeen": number(packed.GetChildAtIndex(0x025 // 2)) & 15,
            "ambushSeen": number(packed.GetChildAtIndex(0x0CC // 2)) >> 4,
            "returnRideSpawned": number(child(target.FindFirstGlobalVariable("_gAkropolisForkedRoadReturnRideLatch"), "spawned")),
        }
    if snapshot["location"]["stage"] == 1 and snapshot["location"]["area"] == 10:
        snapshot["observatory"] = {
            "arrivalSceneStarted": number(target.FindFirstGlobalVariable("_gAkropolisObservatoryArrivalSceneStarted")),
            "returnRideSeen": number(packed.GetChildAtIndex(0x025 // 2)) & 15,
            "batAmbushSeen": number(packed.GetChildAtIndex(0x0CA // 2)) >> 4,
        }
    if snapshot["location"]["stage"] == 1 and snapshot["location"]["area"] == 11:
        snapshot["promenade"] = {
            "arrivalMovieStarted": number(target.FindFirstGlobalVariable("_gAkropolisPromenadeArrivalMovieStarted")),
            "battleFollowupState": number(target.FindFirstGlobalVariable("_gAkropolisPromenadeBattleFollowupState")),
        }
    if snapshot["location"]["stage"] == 1 and snapshot["location"]["area"] == 12:
        snapshot["sanctuary"] = {
            "rupertLeft": number(packed.GetChildAtIndex(0x006 // 2)) >> 4,
            "bombSceneState": number(packed.GetChildAtIndex(0x007 // 2)) & 15,
            "redKey": bool(number(child(save, "collectedBits").GetChildAtIndex(0)) & 8),
            "sceneEvent": number(child(save, "sceneEvent")),
            "tasks": [],
            "interactionTasks": [],
            "eyeRects": {},
        }
        for name, symbol in (("open", "_gActor210700OpenEyesUploads"),
                             ("half", "_gActor210700HalfOpenEyesUploads"),
                             ("closed", "_gActor210700ClosedEyesUploads")):
            rect = child(target.FindFirstGlobalVariable(symbol).GetChildAtIndex(0), "destination")
            snapshot["sanctuary"]["eyeRects"][name] = [number(child(rect, field)) for field in ("x", "y", "w", "h")]
        task = child(target.FindFirstGlobalVariable("gTaskDefaultList"), "next")
        visited = set()
        while task.GetValueAsUnsigned():
            address = task.GetValueAsUnsigned()
            if address in visited:
                raise RuntimeError("cycle in Sanctuary task list")
            visited.add(address)
            value = task.Dereference()
            callback = child(value, "callback").GetValueAsUnsigned()
            name = target.ResolveLoadAddress(callback).GetSymbol().GetName() or ""
            if name in ("_capWeaponEventTask", "itemPickupActionPromptTask", "_uiDispatchPanelLifecycle"):
                record = {"callback": name, "state": number(child(value, "state"))}
                snapshot["sanctuary"]["interactionTasks"].append(record)
            if name in ("_actor210700Task", "_sanctuaryNo9Task", "_akropolisSanctuaryCutsceneTask"):
                record = {"callback": name, "state": number(child(value, "state"))}
                if name == "_actor210700Task":
                    work_address = child(value, "work").GetValueAsUnsigned()
                    work = target.CreateValueFromAddress("rupert", lldb.SBAddress(work_address, target), target.FindFirstType("_Actor210700Work"))
                    record["blinkStep"] = number(child(work, "blinkStep"))
                snapshot["sanctuary"]["tasks"].append(record)
            task = child(child(value, "node"), "next")
        pad = target.FindFirstGlobalVariable("gPadStates").GetChildAtIndex(0)
        snapshot["sanctuary"]["pad"] = {field: number(child(pad, field)) for field in
                                        ("buttons", "pressedButtons", "inputBlockPolls")}
        snapshot["sanctuary"]["confirmMask"] = number(target.FindFirstGlobalVariable("gPadConfirmButtonMask"))
    if snapshot["location"]["stage"] == 1 and snapshot["location"]["area"] == 13:
        snapshot["roofGarden"] = {
            "arrivalSceneStarted": number(target.FindFirstGlobalVariable("_gAkropolisRoofGardenArrivalSceneStarted")),
            "enemyDropState": number(packed.GetChildAtIndex(0x0CB // 2)) & 15,
            "rupertLeft": number(packed.GetChildAtIndex(0x006 // 2)) >> 4,
            "bombSceneState": number(packed.GetChildAtIndex(0x007 // 2)) & 15,
            "sceneEvent": number(child(save, "sceneEvent")),
        }
        snapshot["roofGarden"]["redKey"] = bool(number(child(save, "collectedBits").GetChildAtIndex(0)) & 8)
    if snapshot["location"]["stage"] == 1 and snapshot["location"]["area"] == 15:
        snapshot["fireEscape"] = {
            "telephoneInspected": number(packed.GetChildAtIndex(0x16A // 2)) >> 4,
            "telephoneDialogueCounter": number(packed.GetChildAtIndex(0x003 // 2)) & 15,
            "sceneEvent": number(child(save, "sceneEvent")),
        }
    if snapshot["location"]["stage"] == 1 and snapshot["location"]["area"] in (9, 10, 11, 15):
        queue = target.FindFirstGlobalVariable("gCdCmdQueue")
        snapshot["roomMovie"] = {
            "frame": number(child(queue, "movieFrame")),
            "ready": number(child(queue, "movieReady")),
            "streamStartSector": number(child(target.FindFirstGlobalVariable("gStreamInterFile"), "startSector")),
            "actorControl": number(child(target.FindFirstGlobalVariable("gSceneCombatState"), "actorControl")),
            "padScriptFlags": number(child(session, "padScriptFlags")),
            "tasks": [],
        }
        task = child(target.FindFirstGlobalVariable("gTaskDefaultList"), "next")
        visited = set()
        while task.GetValueAsUnsigned():
            address = task.GetValueAsUnsigned()
            if address in visited:
                raise RuntimeError("cycle in movie task list")
            visited.add(address)
            value = task.Dereference()
            callback = child(value, "callback").GetValueAsUnsigned()
            name = target.ResolveLoadAddress(callback).GetSymbol().GetName() or ""
            if (name.startswith(("_akropolisForkedRoad", "_akropolisObservatory", "_akropolisPromenade")) and ("MoviePathTask" in name or "SkipFade" in name or name == "_akropolisObservatoryScriptedBattleTask")) or (snapshot["location"]["area"] == 15 and name in ("_roomCutsceneTask", "_roomCutsceneSoundTask")):
                snapshot["roomMovie"]["tasks"].append({"callback": name, "state": number(child(value, "state"))})
            task = child(child(value, "node"), "next")
    if (snapshot["location"]["stage"] == 1 and snapshot["location"]["area"] in (3, 4, 7, 8, 9, 10, 11, 13, 14, 15, 16, 19)) or snapshot["location"]["stage"] in (2, 3, 4, 5):
        snapshot["roomEnemies"] = []
        task = child(target.FindFirstGlobalVariable("gTaskDefaultList"), "next")
        visited = set()
        while task.GetValueAsUnsigned():
            address = task.GetValueAsUnsigned()
            if address in visited:
                raise RuntimeError("cycle in task list")
            visited.add(address)
            value = task.Dereference()
            callback = child(value, "callback").GetValueAsUnsigned()
            name = target.ResolveLoadAddress(callback).GetSymbol().GetName() or ""
            if name in ("_madChaserTask", "_madChaserHiddenTask", "_actor403200HostTask", "_actor444000GluttonTask", "_actor00300EnemyTask", "_actor00400BodyTask", "_actor02100Task", "_ratTask", "_mothTask", "_actor401000StrangerTask", "_actor401800Task", "_actor01100Task", "_actor03700Task", "_akropolisBridgeEnemyTask", "_maggotCaterpillarTask", "_lesserStrangerTask", "_helicopterLandingPadNo9BodyTask", "_helicopterLandingPadNo9TransformerTask", "_helicopterLandingPadNo9BlastSourceTask", "_parkingPierceTask", "_desertChaserTask", "_actor02500Task", "_actor02400BodyTask", "_actor400500Task", "_actor01500Task", "_actor04000Task", "_actor01600Task", "_actor400600Task", "_actor07000SlouchTask", "_actor07000SlouchHiddenTask", "_actor207200CreepingStrangerTask", "_skullStalkerTask", "_generatorTask", "_generatorLifeSupportTask", "_actor401300Task", "_actor356100Task", "_actor03800Task", "_actor205200PartTask", "_actor205200BodyTask", "_actor105100Task", "_actor521100BodyTask", "_actor403100Task", "_actor403000Task", "mineCavernTargetTask") or (snapshot["location"]["area"] in (3, 4, 7, 9, 10) and name == "_actor01900Task"):
                enemy_address = child(child(value, "spawnArg2"), "pointer").GetValueAsUnsigned()
                if enemy_address:
                    enemy = target.CreateValueFromAddress("enemy", lldb.SBAddress(enemy_address, target), target.FindFirstType("Enemy"))
                    record = {"callback": name, "hp": number(child(enemy, "hp")), "taskState": number(child(value, "state"))}
                    record["dormant"] = record["hp"] == -999
                    record["targetFlags"] = number(child(child(child(child(enemy, "node"), "state"), "parts"), "flags"))
                    record["targeted"] = number(child(child(child(child(enemy, "node"), "state"), "parts"), "targeted"))
                    if name in ("_madChaserTask", "_madChaserHiddenTask", "_actor00300EnemyTask", "_actor00400BodyTask", "_actor02100Task", "_ratTask", "_actor401800Task", "_actor401000StrangerTask", "_actor01900Task", "_desertChaserTask", "_actor03700Task", "_actor02500Task", "_actor02400BodyTask", "_actor400500Task", "_actor01500Task", "_actor04000Task", "_actor01600Task", "_actor400600Task", "_actor07000SlouchTask", "_actor07000SlouchHiddenTask", "_actor207200CreepingStrangerTask", "_skullStalkerTask", "_generatorTask", "_generatorLifeSupportTask", "_actor401300Task", "_actor356100Task", "_actor03800Task", "_actor205200PartTask", "_actor205200BodyTask", "_actor105100Task", "_actor521100BodyTask", "_actor403100Task", "_actor403000Task", "mineCavernTargetTask"):
                        model = child(child(value, "extra"), "tmd").Dereference()
                        coord = child(child(model, "coords").Dereference(), "coord")
                        record["rootPosition"] = [number(child(coord, "t").GetChildAtIndex(i)) for i in range(3)]
                    if name in ("_actor403200HostTask", "_actor444000GluttonTask"):
                        record["reactionFlags"] = number(child(enemy, "reactionFlags"))
                        model = child(child(value, "extra"), "tmd").Dereference()
                        coords = child(model, "coords")
                        record["rootPosition"] = [number(child(child(coords.Dereference(), "coord"), "t").GetChildAtIndex(i)) for i in range(3)]
                        # Mirror the game's local-to-world walk in Python, without target calls.
                        coord_type = target.FindFirstType("GfxCoord")
                        coord_address = coords.GetValueAsUnsigned() + 4 * coord_type.GetByteSize()
                        point = [0, 0, 0]
                        view_address = target.FindFirstGlobalVariable("gGfxViewCoord").GetLoadAddress()
                        chain_seen = set()
                        while coord_address and coord_address != view_address:
                            if coord_address in chain_seen:
                                raise RuntimeError("cycle in Glutton mouth coordinate chain")
                            chain_seen.add(coord_address)
                            node = target.CreateValueFromAddress("mouthCoord", lldb.SBAddress(coord_address, target), coord_type)
                            matrix = child(node, "coord")
                            rotation = child(matrix, "m")
                            translation = child(matrix, "t")
                            point = [((sum(number(rotation.GetChildAtIndex(i).GetChildAtIndex(j)) * point[j] for j in range(3)) >> 12) + number(translation.GetChildAtIndex(i)) + 32768) % 65536 - 32768 for i in range(3)]
                            coord_address = child(node, "parent").GetValueAsUnsigned()
                        if coord_address == view_address:
                            record["mouthPosition"] = point
                            player_position = snapshot["actor"]["position"]
                            record["mouthDistanceXZ"] = int(((player_position[0] - point[0]) ** 2 + (player_position[2] - point[2]) ** 2) ** 0.5)
                        work_address = child(value, "work").GetValueAsUnsigned()
                        if work_address:
                            work = target.CreateValueFromAddress("glutton", lldb.SBAddress(work_address, target), target.FindFirstType("GluttonWork"))
                            for field in ("state", "stateTicks", "phase", "attackDelay", "animId", "wallDistance", "pendingHeals", "hostExposed"):
                                record[field] = number(child(work, field))
                            slot = child(child(work, "hostRig"), "slots").GetChildAtIndex(1)
                            record["mouthCue"] = number(child(child(child(slot, "currentPose"), "indices"), "recordIndex")) & 0x3FF
                    if name == "_actor403000Task":
                        work_address = child(value, "work").GetValueAsUnsigned()
                        work = target.CreateValueFromAddress("blizzardChaser", lldb.SBAddress(work_address, target), target.FindFirstType("Actor403000Work"))
                        for field in ("state", "prevState", "stateFrame", "dropDelay", "playerCaught", "seenTargetsDestroyed"):
                            record[field] = number(child(work, field))
                        record["attackTarget"] = [number(child(child(work, "attackTarget"), axis)) for axis in ("vx", "vy", "vz")]
                    if name == "_desertChaserTask":
                        work_address = child(value, "work").GetValueAsUnsigned()
                        # All Desert Chaser variants put their state first; later work layouts differ.
                        work = target.CreateValueFromAddress("desertChaser", lldb.SBAddress(work_address, target), target.FindFirstType("DesertChaserWork"))
                        record["state"] = number(child(work, "state"))
                    if name == "_parkingPierceTask":
                        model = child(child(value, "extra"), "tmd").Dereference()
                        coord = child(child(model, "coords").Dereference(), "coord")
                        record["rootPosition"] = [number(child(coord, "t").GetChildAtIndex(i)) for i in range(3)]
                        work_types = target.FindTypes("_Work")
                        work_type = next(work_types.GetTypeAtIndex(i) for i in range(work_types.GetSize())
                                         if any(work_types.GetTypeAtIndex(i).GetFieldAtIndex(j).GetName() == "billboardTask"
                                                for j in range(work_types.GetTypeAtIndex(i).GetNumberOfFields())))
                        work_address = child(value, "work").GetValueAsUnsigned()
                        work = target.CreateValueFromAddress("parkingPierce", lldb.SBAddress(work_address, target), work_type)
                        walk = child(work, "walk")
                        record.update({field: number(child(walk, field)) for field in ("motion", "motionStep")})
                        record["walkTarget"] = [number(child(child(walk, "target"), axis)) for axis in ("vx", "vy", "vz")]
                        record["walkVelocity"] = [number(child(child(walk, "velocity"), axis)) for axis in ("vx", "vy", "vz")]
                    if name == "_helicopterLandingPadNo9TransformerTask":
                        work_address = child(value, "work").GetValueAsUnsigned()
                        if work_address:
                            work = target.CreateValueFromAddress("transformer", lldb.SBAddress(work_address, target), target.FindFirstType("_TransformerWork"))
                            record.update({field: number(child(work, field)) for field in ("state", "transformerIndex", "status", "sparkFrames")})
                            record["blastFlags"] = number(child(child(work, "blast"), "flags"))
                            record["targeted"] = number(child(child(child(child(enemy, "node"), "state"), "parts"), "targeted"))
                        coord = child(child(enemy, "coord").Dereference(), "workm")
                        record["contactViewPosition"] = [number(child(coord, "t").GetChildAtIndex(i)) for i in range(3)]
                        view_matrix = child(target.FindFirstGlobalVariable("gGfxViewCoord"), "workm")
                        view_translation = [number(child(view_matrix, "t").GetChildAtIndex(i)) for i in range(3)]
                        view_rotation = child(view_matrix, "m")
                        delta = [record["contactViewPosition"][i] - view_translation[i] for i in range(3)]
                        record["contactWorldPosition"] = [round(sum(number(view_rotation.GetChildAtIndex(j).GetChildAtIndex(i)) * delta[j] for j in range(3)) / 4096) for i in range(3)]
                        root = child(child(child(value, "extra"), "tmd").Dereference(), "coords").Dereference()
                        record["rootPosition"] = [number(child(child(root, "coord"), "t").GetChildAtIndex(i)) for i in range(3)]
                    if name == "_helicopterLandingPadNo9BlastSourceTask":
                        work_address = child(value, "work").GetValueAsUnsigned()
                        work = target.CreateValueFromAddress("blastSource", lldb.SBAddress(work_address, target), target.FindFirstType("_BlastSourceWork"))
                        record.update({field: number(child(work, field)) for field in ("state", "flareFrames", "flareTaskState")})
                        record["targeted"] = number(child(child(child(child(enemy, "node"), "state"), "parts"), "targeted"))
                        coord = child(child(child(value, "extra"), "coordBody").Dereference(), "coord").Dereference()
                        record["rootPosition"] = [number(child(child(coord, "coord"), "t").GetChildAtIndex(i)) for i in range(3)]
                    if name == "_helicopterLandingPadNo9BodyTask":
                        model = child(child(value, "extra"), "tmd").Dereference()
                        coord = child(child(model, "coords").Dereference(), "coord")
                        record["rootPosition"] = [number(child(coord, "t").GetChildAtIndex(i)) for i in range(3)]
                        work_address = child(value, "work").GetValueAsUnsigned()
                        if work_address:
                            work = target.CreateValueFromAddress("helipadNo9", lldb.SBAddress(work_address, target), target.FindFirstType("HelicopterLandingPadNo9Work"))
                            record.update({field: number(child(work, field)) for field in
                                           ("state", "subState", "animationFrame", "stateCounter",
                                            "lapDistance", "lapSide", "lapSpeed", "playerDistance",
                                            "slashedTransformer", "lethalAttackPhase")})
                    if name == "_actor01100Task":
                        model = child(child(value, "extra"), "tmd").Dereference()
                        coord = child(child(model, "coords").Dereference(), "coord")
                        record["rootPosition"] = [number(child(coord, "t").GetChildAtIndex(i)) for i in range(3)]
                        work_address = child(value, "work").GetValueAsUnsigned()
                        work = target.CreateValueFromAddress("mossback", lldb.SBAddress(work_address, target), target.FindFirstType("_Actor01100Work"))
                        record.update({field: number(child(work, field)) for field in ("state", "mode", "reaction", "recentDamage", "hidden")})
                    if name == "_actor03700Task":
                        model = child(child(value, "extra"), "tmd").Dereference()
                        coord = child(child(model, "coords").Dereference(), "coord")
                        record["rootPosition"] = [number(child(coord, "t").GetChildAtIndex(i)) for i in range(3)]
                        work_address = child(value, "work").GetValueAsUnsigned()
                        work = target.CreateValueFromAddress("bat", lldb.SBAddress(work_address, target), target.FindFirstType("_Actor03700Work"))
                        record.update({field: number(child(work, field)) for field in ("action", "actionStep", "holdingPlayer", "waveDirector")})
                    if name == "_actor400500Task":
                        work_address = child(value, "work").GetValueAsUnsigned()
                        work = target.CreateValueFromAddress("grayStalker", lldb.SBAddress(work_address, target), target.FindFirstType("_Actor400500GrayStalkerWork"))
                        record.update({field: number(child(work, field)) for field in
                                       ("state", "subState", "cloakLevel", "playerCaught", "posture", "commandActive")})
                    if name == "_actor105100Task":
                        work_address = child(value, "work").GetValueAsUnsigned()
                        work = target.CreateValueFromAddress("stinger", lldb.SBAddress(work_address, target), target.FindFirstType("_Actor105100Work"))
                        record.update({field: number(child(work, field)) for field in
                                       ("action", "actionStep", "timer", "summonCount", "charging", "staggerDamage", "staggerTimer")})
                        shield = child(child(work, "shield"), "fields")
                        record["shieldActive"] = number(child(shield, "active"))
                        record["shieldCooldown"] = number(child(shield, "cooldown"))
                    if name == "_maggotCaterpillarTask":
                        model = child(child(value, "extra"), "tmd").Dereference()
                        coord = child(child(model, "coords").Dereference(), "coord")
                        record["rootPosition"] = [number(child(coord, "t").GetChildAtIndex(i)) for i in range(3)]
                        work_address = child(value, "work").GetValueAsUnsigned()
                        work = target.CreateValueFromAddress("crawler", lldb.SBAddress(work_address, target), target.FindFirstType("MaggotCaterpillarWork"))
                        record.update({field: number(child(work, field)) for field in ("behaviour", "step")})
                    snapshot["roomEnemies"].append(record)
            task = child(child(value, "node"), "next")

    if snapshot["location"]["stage"] == 1 and snapshot["location"]["area"] == 8:
        bank = target.FindFirstGlobalVariable("gGameFlagNibbleBanks").GetChildAtIndex(0)
        packed = child(child(bank, "payload"), "packedFlags")
        climb = target.FindFirstGlobalVariable("_gAkropolisFountainClimbTrigger")
        snapshot["fountain"] = {
            "hiddenPathFound": number(packed.GetChildAtIndex(0x012 // 2)) >> 4,
            "climbEnabled": bool(number(child(climb, "flags")) & 0x40),
            "climbLinked": False,
            "actionTriggerCount": 0,
        }
        trigger = target.FindFirstGlobalVariable("gWorldCollisionActionTriggerListHead")
        visited = set()
        while trigger.GetValueAsUnsigned():
            address = trigger.GetValueAsUnsigned()
            if address in visited:
                raise RuntimeError("cycle in action trigger list")
            visited.add(address)
            if address == climb.GetLoadAddress():
                snapshot["fountain"]["climbLinked"] = True
            trigger = child(trigger.Dereference(), "next")
        snapshot["fountain"]["actionTriggerCount"] = len(visited)

    # Identify the loaded resources independently of numeric location IDs.
    grid = target.FindFirstGlobalVariable("gWorldCollisionActiveGrid")
    room_task = child(session, "ptrSlots").GetChildAtIndex(7)
    snapshot["roomRuntime"] = {
        "grid": target.ResolveLoadAddress(grid.GetValueAsUnsigned()).GetSymbol().GetName() if grid.IsValid() and grid.GetValueAsUnsigned() else None,
        "controller": target.ResolveLoadAddress(child(room_task.Dereference(), "callback").GetValueAsUnsigned()).GetSymbol().GetName() if room_task.GetValueAsUnsigned() else None,
    }
    snapshot["direction"] = {field: number(target.FindFirstGlobalVariable(symbol)) for field, symbol in (
        ("active", "gDirectionActionActive"), ("control", "gDirectionActionControl"),
        ("phase", "gDirectionActionPhase"), ("parameter0", "gDirectionActionParameter0"),
        ("parameter1", "gDirectionActionParameter1"), ("warpResult", "_gDirectionWarpQueryResult"),
        ("soundId", "_gDirectionWarpSoundId"),
    )}

    # The software renderer's retained texture window affects subsequent rooms.
    # Include it when available to distinguish command-state faults from assets.
    window = [target.FindFirstGlobalVariable(name) for name in
              ("twin_and_x", "twin_and_y", "twin_or_x", "twin_or_y")]
    if all(value.IsValid() for value in window):
        snapshot["gpu"] = {"textureWindow": [number(value) for value in window]}
    return snapshot


def checkpoint_command(debugger, command, result, internal_dict):
    try:
        import lldb
        target = debugger.GetSelectedTarget()
        process = target.GetProcess()
        if process.GetState() != lldb.eStateStopped:
            raise RuntimeError("checkpoint requires a stopped process")
        for thread in process:
            if thread.GetStopReason() in (lldb.eStopReasonException, lldb.eStopReasonSignal):
                description = thread.GetStopDescription(512) or "signal/exception"
                # SIGINT is used to inspect a controlled replay waiting on its FIFO.
                if thread.GetStopReason() == lldb.eStopReasonSignal and thread.GetStopReasonDataAtIndex(0) == 2:
                    continue
                raise RuntimeError(f"checkpoint rejected after target fault: {description}; use pe2-transition and bt")
        result.AppendMessage(PREFIX + json.dumps(collect(target), sort_keys=True))
    except Exception as error:
        result.SetError(str(error))


def transition_command(debugger, command, result, internal_dict):
    """Read the retained mode descriptor and session state after a fault."""
    target = debugger.GetSelectedTarget()
    unavailable = []

    def read(value, unsigned=False):
        # LLDB returns zero for invalid SBValues too. Keep missing debug info
        # distinct from a real zero (especially HP and the player task slot).
        if not value.IsValid() or value.GetError().Fail():
            unavailable.append(value.GetName() or "<unknown>")
            return None
        error = lldb.SBError()
        number = value.GetValueAsUnsigned(error) if unsigned else value.GetValueAsSigned(error)
        if error.Fail():
            unavailable.append(value.GetName() or "<unknown>")
            return None
        return number

    import lldb
    # The pointer to this singleton can be folded away in optimized builds.
    state = target.FindFirstGlobalVariable("_gStageTransitionStorage")
    if not state.IsValid() or state.GetError().Fail():
        result.SetError("stage transition storage is unavailable")
        return
    descriptor = state.GetChildMemberWithName("taskDesc").Dereference()
    callback = descriptor.GetChildMemberWithName("callback").GetValueAsUnsigned()
    status = target.FindFirstGlobalVariable("gPlayerStatus")
    snapshot = {name: read(state.GetChildMemberWithName(name))
                for name in ("entryMode", "spawnArg1", "transitionKind")}
    snapshot["hp"] = read(status.GetChildMemberWithName("hp"))
    snapshot["callback"] = target.ResolveLoadAddress(callback).GetSymbol().GetName() or ""
    session = target.FindFirstGlobalVariable("gGameSession").Dereference()
    snapshot["location"] = {name: read(session.GetChildMemberWithName("location").GetChildMemberWithName(name))
                            for name in ("stage", "area", "roomState", "variant")}
    display = target.FindFirstGlobalVariable("gDisplayState")
    snapshot["display"] = {name: read(display.GetChildMemberWithName(name))
                           for name in ("demoScene", "gameMode", "pendingMode")}
    snapshot["deathVariant"] = read(session.GetChildMemberWithName("deathVariant"))
    # Keep crash diagnostics usable even when collect() cannot find a player.
    snapshot["playerTask"] = read(session.GetChildMemberWithName("ptrSlots").GetChildAtIndex(3), unsigned=True)
    snapshot["display"].update({name: read(display.GetChildMemberWithName(name))
                                for name in ("displayOwner", "holdState", "holdCount")})
    combat = target.FindFirstGlobalVariable("gSceneCombatState")
    snapshot["combat"] = {name: read(combat.GetChildMemberWithName(name))
                          for name in ("actorControl", "battleRefs")}
    for symbol_name in ("Psyz_PadsOverride.padFrame", "_Psyz_PadsOverride.padFrame"):
        symbols = target.FindSymbols(symbol_name)
        if symbols.GetSize():
            address = symbols.GetContextAtIndex(0).GetSymbol().GetStartAddress().GetLoadAddress(target)
            error = lldb.SBError()
            index = target.GetProcess().ReadUnsignedFromMemory(address, 8, error)
            if error.Success():
                snapshot["inputSamples"] = index
                break
    snapshot["taskLists"] = {}
    for label, global_name in (("game", "gTaskDefaultList"), ("display", "gDisplayTaskList")):
        node = target.FindFirstGlobalVariable(global_name).GetChildMemberWithName("next")
        rows, visited = [], set()
        while node.GetValueAsUnsigned() and len(rows) < 256:
            address = node.GetValueAsUnsigned()
            if address in visited:
                rows.append({"cycle": address})
                break
            visited.add(address)
            task = node.Dereference()
            if not task.IsValid() or task.GetError().Fail():
                rows.append({"unreadable": address})
                break
            callback_address = task.GetChildMemberWithName("callback").GetValueAsUnsigned()
            rows.append({"callback": target.ResolveLoadAddress(callback_address).GetSymbol().GetName() or "",
                         "state": task.GetChildMemberWithName("state").GetValueAsSigned(),
                         "address": address})
            node = task.GetChildMemberWithName("node").GetChildMemberWithName("next")
        snapshot["taskLists"][label] = rows
    if unavailable:
        snapshot["unavailableDebugValues"] = sorted(set(unavailable))
    result.AppendMessage("PE2_TRANSITION " + json.dumps(snapshot, sort_keys=True))


def vram_command(debugger, command, result, internal_dict):
    import lldb
    try:
        paths = shlex.split(command)
        if len(paths) != 1:
            raise ValueError("pe2-vram requires one output path")
        target = debugger.GetSelectedTarget()
        vram = target.FindFirstGlobalVariable("vram")
        size = 1024 * 512 * 2
        if not vram.IsValid() or vram.GetType().GetByteSize() != size:
            raise RuntimeError("software VRAM is not available")
        error = lldb.SBError()
        data = target.GetProcess().ReadMemory(vram.GetLoadAddress(), size, error)
        if error.Fail() or data is None or len(data) != size:
            raise RuntimeError(f"cannot read VRAM: {error}")
        path = Path(paths[0])
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        result.AppendMessage(f"VRAM saved: {path} ({size} bytes)")
    except Exception as error:
        result.SetError(str(error))


def __lldb_init_module(debugger, internal_dict):
    debugger.HandleCommand(f"command script add --overwrite -f {__name__}.transition_command pe2-transition")
    debugger.HandleCommand(f"command script add --overwrite -f {__name__}.checkpoint_command pe2-checkpoint")
    debugger.HandleCommand(f"command script add --overwrite -f {__name__}.vram_command pe2-vram")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("replay", type=Path)
    parser.add_argument("--binary", type=Path, default=Path("build-port/parasite-eve-ii-pc"))
    parser.add_argument("--disc", type=Path, default=Path("rom/image/SLUS-01042.cue"))
    parser.add_argument("--log", type=Path, default=Path("build-port/replay-checkpoint.log"))
    parser.add_argument("--vram", type=Path, help="save the complete software VRAM at EOF (RGB555, 1024x512)")
    parser.add_argument("--memory-card-dir", type=Path,
                        help="persistent cards for save/load fixtures (default: isolated temporary empty cards)")
    parser.add_argument("--backend", choices=("software", "gpu"), default="software",
                        help="software or a hidden GPU window for the read-only checkpoint")
    parser.add_argument("--clock", choices=("host", "video"),
                        help="select PsyZ wall/device time or offline video/audio time (default inherits environment)")
    parser.add_argument("--raster-clock", choices=("host", "virtual"),
                        help="scanline clock: virtual removes host frame-budget jitter (requires a matching PsyZ build)")
    parser.add_argument("--expect", action="append", default=[], metavar="KEY=JSON")
    args = parser.parse_args()
    if args.vram and args.backend != "software":
        parser.error("--vram reads software VRAM; use --backend software")
    if not shutil.which("lldb"):
        parser.error("lldb is required")
    for path in (args.replay, args.binary, args.disc):
        if not path.is_file():
            parser.error(f"missing file: {path}")
    source = Path(__file__).resolve().parents[2] / "src/port/psyq_host.c"
    lines = source.read_text().splitlines()
    eof_lines = [i + 1 for i, line in enumerate(lines) if '[pad] replay %s at frame' in line]
    if len(eof_lines) != 1:
        parser.error("cannot identify the replay EOF diagnostic in psyq_host.c")
    commands = [
        f"breakpoint set -f psyq_host.c -l {eof_lines[0]}",
        "run " + json.dumps(str(args.disc.resolve())),
        "command script import " + json.dumps(str(Path(__file__).resolve())),
    ]
    if args.vram:
        commands.append("pe2-vram " + json.dumps(str(args.vram.resolve())))
    commands.append("pe2-checkpoint")
    # Resume through the EOF diagnostic and normal exit under LLDB. Detaching
    # can race debugger shutdown and lose the completion marker even when the
    # snapshot was read successfully.
    commands.append("continue")
    invocation = ["lldb", "-b", str(args.binary.resolve())]
    for command in commands:
        invocation += ["-o", command]
    invocation += ["-k", "bt", "-k", "command script import " + json.dumps(str(Path(__file__).resolve())),
                   "-k", "pe2-transition"]
    env = dict(os.environ, PSYZ_HEADLESS="gpu" if args.backend == "gpu" else "1", PE2_SKIPINTRO="1", PE2_PAD_REPLAY=str(args.replay.resolve()))
    if args.clock:
        env["PSYZ_AUDIO_CLOCK"] = args.clock
    if args.raster_clock:
        env["PSYZ_RASTER_CLOCK"] = args.raster_clock
    for key in ("PE2_FAST", "PE2_PAD_RECORD", "PE2_PAD_CONTROL", "PE2_PAD_CONTROL_FIFO"):
        env.pop(key, None)
    args.log.parent.mkdir(parents=True, exist_ok=True)
    with args.log.open("w") as log:
        log.write("PE2_REPLAY_CLOCK " + env.get("PSYZ_AUDIO_CLOCK", "host") + "\n")
        log.write("PE2_REPLAY_RASTER_CLOCK " + env.get("PSYZ_RASTER_CLOCK", "host") + "\n")
        log.flush()
        with tempfile.TemporaryDirectory(prefix="pe2-replay-card-") as temporary_card:
            card_root = args.memory_card_dir.resolve() if args.memory_card_dir else Path(temporary_card)
            card_root.mkdir(parents=True, exist_ok=True)
            env["PE2_MEMORY_CARD_DIR"] = str(card_root)
            log.write("PE2_REPLAY_MEMORY_CARD_DIR " + str(card_root) + "\n")
            log.flush()
            completed = subprocess.run(invocation, env=env, stdout=log, stderr=subprocess.STDOUT)
    output = args.log.read_text()
    snapshots = [json.loads(line.split(PREFIX, 1)[1]) for line in output.splitlines() if line.startswith(PREFIX)]
    if completed.returncode or len(snapshots) != 1 or "[pad] replay complete at frame" not in output:
        raise SystemExit(f"replay failed or checkpoint missing; see {args.log}")
    snapshot = snapshots[0]
    print(json.dumps(snapshot, indent=2, sort_keys=True))
    for expectation in args.expect:
        key, expected = expectation.split("=", 1)
        actual = checkpoint_value(snapshot, key)
        if actual != json.loads(expected):
            raise SystemExit(f"FAIL {key}: expected {expected}, got {actual!r}; see {args.log}")
    print(f"Replay checkpoint OK ({len(args.expect)} expectations); log: {args.log}")


if __name__ == "__main__":
    main()
