# The game executable and its overlay packages; included from CMakeLists.txt when PE_BUILD_GAME is ON.
# The game proper (the former decompilation sources), compiled for the host.
file(GLOB GAME_MAIN_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/main/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gameplay/*.c)
# main.c only switches to the PS1 boot stack; the port entry (src/port/main.c) replaces it.
list(FILTER GAME_MAIN_SOURCES EXCLUDE REGEX "/src/main/main\\.c$")
add_library(game_main OBJECT ${GAME_MAIN_SOURCES})
target_include_directories(game_main PUBLIC include include/decomp
    ${PE_GENERATED_DIR})
target_compile_definitions(game_main PUBLIC __psyz SKIP_ASM)
target_compile_options(game_main PRIVATE -w -Wno-incompatible-function-pointer-types -g -fno-omit-frame-pointer -include port/gpu_len.h)
target_link_libraries(game_main PUBLIC psyz)
target_compile_options(game_main PRIVATE "SHELL:-include port/random.h")

# Overlay packages linked into the executable. The PS1 game swaps overlay code in and out of one address range;
# on the host a package is compiled in and its names resolve at link time. Packages share global names
# (docs/PORT_PLAN.md), so only the packages listed here are linked; add one when the game first needs it.
set(PE_PACKAGES
    src/title/title.c;src/mapui/map_dryfield/map_dryfield.c;src/mapui/map_dryfield_full/map_dryfield_full.c;src/mapui/map_neo_ark/map_neo_ark.c;src/mapui/map_shelter/map_shelter.c;src/options/options/options.c
    CACHE STRING "Overlay sources linked into the executable")
option(PE_ALL_VARIANTS "With PE_ALL_OVERLAYS, link every per-package variant too" ON)
option(PE_ALL_OVERLAYS "Compile and link every overlay package, so every stage loads" ON)
if(PE_ALL_OVERLAYS)
    include(cmake/overlay_sources.cmake)
    set(PE_OVERLAY_LIST ${PE_OVERLAY_SOURCES})
else()
    set(PE_OVERLAY_LIST)
    foreach(src ${PE_PACKAGES})
        list(APPEND PE_OVERLAY_LIST ${CMAKE_CURRENT_SOURCE_DIR}/${src})
    endforeach()
endif()
if(NOT PE_ALL_OVERLAYS)
    list(APPEND PE_OVERLAY_LIST
        ${CMAKE_CURRENT_SOURCE_DIR}/src/aya/game_over/game_over.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/weapons/m93r/m93r.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/weapons/tonfa_baton/tonfa_baton.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/stages/akropolis/stage.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/stages/akropolis/rooms/mist_shooting_gallery/mist_shooting_gallery.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/stages/akropolis/rooms/mist_shooting_gallery/mist_shooting_gallery_courses.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/stages/akropolis/actors/shooting_gallery_pierce/shooting_gallery_pierce.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/stages/akropolis/actors/shooting_gallery_pierce/shooting_gallery_pierce_gallery.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/stages/akropolis/actors/shooting_gallery_targets/shooting_gallery_targets.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/stages/akropolis/rooms/plaza/plaza.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/stages/akropolis/rooms/west_elevator_hall/west_elevator_hall.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/stages/akropolis/rooms/east_elevator_hall/east_elevator_hall.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/stages/akropolis/actors/east_elevator_hall_swat_member/east_elevator_hall_swat_member.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/stages/akropolis/rooms/square/square.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/stages/akropolis/rooms/patio/patio.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/stages/akropolis/rooms/hallway/hallway.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/stages/akropolis/rooms/security_room/security_room.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/stages/akropolis/actors/security_monitors/security_monitors.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/stages/akropolis/rooms/fountain/fountain.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/stages/akropolis/rooms/fountain/fountain_water.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/stages/akropolis/rooms/forked_road/forked_road.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/stages/akropolis/rooms/forked_road/forked_road_effects.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/stages/akropolis/rooms/observatory/observatory.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/stages/akropolis/rooms/observatory/observatory_effects.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/stages/akropolis/rooms/observatory/observatory_rides.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/stages/akropolis/rooms/promenade/promenade.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/stages/akropolis/rooms/sanctuary/sanctuary.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/stages/akropolis/rooms/roof_garden/roof_garden.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/stages/akropolis/rooms/bridge/bridge.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/stages/akropolis/rooms/fire_escape/fire_escape.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/stages/akropolis/rooms/helicopter_landing_pad/helicopter_landing_pad.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/stages/akropolis/rooms/helicopter_landing_pad/helicopter_landing_pad_departure.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/stages/akropolis/rooms/helicopter_landing_pad/helicopter_landing_pad_effects.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/stages/akropolis/actors/helicopter_departure/helicopter_departure.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/stages/akropolis/actors/helicopter_landing_pad_no_9/helicopter_landing_pad_no_9.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/stages/akropolis/actors/helicopter_landing_pad_no_9/helicopter_landing_pad_no_9_combat.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/enemies/stranger/lesser/lesser_stranger.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/pe/pyrokinesis/pyrokinesis.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/pe/healing/healing.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/stages/akropolis/actors/roof_garden_swat_member/roof_garden_swat_member.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/stages/akropolis/actors/sanctuary_no_9/sanctuary_no_9.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/actors/actor_210700/actor_210700.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/actors/actor_402200/actor_402200.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/actors/actor_04000/actor_04000.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/actors/actor_401300/actor_401300.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/stages/akropolis/rooms/cafeteria/cafeteria.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/stages/akropolis/rooms/cafeteria/cafeteria_effects.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/stages/akropolis/actors/cafeteria_boss_stranger/cafeteria_boss_stranger.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/actors/actor_202900/actor_202900.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/actors/actor_210600/actor_210600.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/actors/actor_210600/actor_210600_contacts.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/actors/actor_310600/actor_310600.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/actors/actor_401800/actor_401800.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/actors/actor_05700/actor_05700.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/stages/akropolis/actors/patio_swat_member/patio_swat_member.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/stages/akropolis/actors/patio_leaping_strangers/patio_leaping_strangers.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/actors/actor_00700/actor_00700.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/actors/actor_01500/actor_01500.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/actors/actor_02600/actor_02600.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/actors/actor_207200/actor_207200.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/actors/actor_207200/actor_207200_2.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/actors/actor_300700/actor_300700.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/actors/actor_300700/actor_300700_2.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/actors/actor_401000/actor_401000.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/stages/akropolis/actors/square_grinning_stranger/square_grinning_stranger.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/stages/akropolis/actors/police_officers/police_officers.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/stages/akropolis/rooms/mist_parking/mist_parking.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/stages/akropolis/rooms/mist_r18/mist_r18.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/stages/akropolis/rooms/mist_parking/mist_parking_scenes.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/stages/akropolis/rooms/mist_parking/mist_parking_conversation.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/stages/akropolis/rooms/mist_parking/mist_parking_glows.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/stages/akropolis/rooms/mist_parking/mist_parking_shop.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/stages/akropolis/actors/parking_pierce/parking_pierce.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/actors/actor_213100/actor_213100.c)
    list(REMOVE_DUPLICATES PE_OVERLAY_LIST)
endif()
# Each PS1 overlay owns animation symbols that share names in the console
# address space. Preserve each copy when those overlays coexist on the host.
set_property(SOURCE
    src/stages/akropolis/rooms/mist_parking/mist_parking.c
    src/stages/akropolis/rooms/mist_parking/mist_parking_conversation.c
    APPEND PROPERTY COMPILE_DEFINITIONS
    gAyaStandClip=gMistParkingAyaStandClip
    gAyaAnim07Clip=gMistParkingAyaAnim07Clip
    gAyaTalkClip=gMistParkingAyaTalkClip
    gAyaTalkStartClip=gMistParkingAyaTalkStartClip
    gAyaTalkEndClip=gMistParkingAyaTalkEndClip)
set_property(SOURCE src/stages/akropolis/rooms/square/square.c
    APPEND PROPERTY COMPILE_DEFINITIONS gRoomCutsceneSoundTask=gAkropolisSquareRoomCutsceneSoundTask)
set_property(SOURCE src/stages/akropolis/rooms/plaza/plaza.c
    APPEND PROPERTY COMPILE_DEFINITIONS gAyaIdleClip=gAkropolisPlazaAyaIdleClip)
set_property(SOURCE src/actors/actor_213100/actor_213100.c
    APPEND PROPERTY COMPILE_DEFINITIONS
    actorMsgPlaceEuler=jodieActorMsgPlaceEuler
    gAyaStandClip=gJodieAyaStandClip
    gAyaAnim07Clip=gJodieAyaAnim07Clip
    gAyaTalkClip=gJodieAyaTalkClip
    gAyaTalkStartClip=gJodieAyaTalkStartClip
    gAyaTalkEndClip=gJodieAyaTalkEndClip)
# Each retail package owns these exports; resident host overlays need distinct names.
set_property(SOURCE src/stages/akropolis/rooms/patio/patio.c
    APPEND PROPERTY COMPILE_DEFINITIONS
    gAyaStandClip=gAkropolisPatioAyaStandClip
    gShelterR47PlayerLookAtMapClip=gAkropolisPatioPlayerLookAtMapClip)
set_property(SOURCE src/stages/akropolis/actors/patio_leaping_strangers/patio_leaping_strangers.c
    APPEND PROPERTY COMPILE_DEFINITIONS actorMsgPlaceEuler=patioLeapingStrangersMsgPlaceEuler)
set_property(SOURCE src/actors/actor_401000/actor_401000.c
    APPEND PROPERTY COMPILE_DEFINITIONS
    actorMsgIsPresent=actor401000MsgIsPresent
    actorMsgPlaceRecordYaw=actor401000MsgPlaceRecordYaw
    actorMsgSetVisibility=actor401000MsgSetVisibility)
set_property(SOURCE src/actors/actor_300700/actor_300700.c src/actors/actor_300700/actor_300700_2.c
    APPEND PROPERTY COMPILE_DEFINITIONS
    gRatAndMothRatBody=gActor300700RatBody ratSpawn=actor300700RatSpawn)
set_property(SOURCE src/actors/actor_310600/actor_310600.c
    APPEND PROPERTY COMPILE_DEFINITIONS actorMsgPlaceEuler=actor310600MsgPlaceEuler)
set_property(SOURCE src/actors/actor_202900/actor_202900.c
    APPEND PROPERTY COMPILE_DEFINITIONS actorMsgSetPairVisibility=actor202900MsgSetPairVisibility)
set_property(SOURCE src/actors/actor_210600/actor_210600.c src/actors/actor_210600/actor_210600_contacts.c
    APPEND PROPERTY COMPILE_DEFINITIONS actorMsgPlace=actor210600MsgPlace)
set_property(SOURCE src/actors/actor_401800/actor_401800.c
    APPEND PROPERTY COMPILE_DEFINITIONS
    actorMsgIsPresent=actor401800MsgIsPresent
    actorMsgReleaseHold=actor401800MsgReleaseHold
    actorMsgPlaceRecordYaw=actor401800MsgPlaceRecordYaw
    actorMsgSetVisibility=actor401800MsgSetVisibility)
set_property(SOURCE src/stages/akropolis/rooms/cafeteria/cafeteria_effects.c
    APPEND PROPERTY COMPILE_DEFINITIONS ActorContact_ScratchPosition=gCafeteriaEffectsScratchPosition)
set_property(SOURCE src/stages/akropolis/actors/cafeteria_boss_stranger/cafeteria_boss_stranger.c
    APPEND PROPERTY COMPILE_DEFINITIONS ActorContact_ScratchPosition=gCafeteriaBossScratchPosition)
set_property(SOURCE src/stages/akropolis/rooms/hallway/hallway.c
    APPEND PROPERTY COMPILE_DEFINITIONS ActorContact_ScratchPosition=gAkropolisHallwayScratchPosition)
set_property(SOURCE src/stages/akropolis/rooms/security_room/security_room.c
    APPEND PROPERTY COMPILE_DEFINITIONS ActorContact_ScratchPosition=gAkropolisSecurityRoomScratchPosition)
set_property(SOURCE src/stages/akropolis/rooms/sanctuary/sanctuary.c
    APPEND PROPERTY COMPILE_DEFINITIONS ActorContact_ScratchPosition=gAkropolisSanctuaryScratchPosition)
set_property(SOURCE src/stages/akropolis/rooms/roof_garden/roof_garden.c
    APPEND PROPERTY COMPILE_DEFINITIONS
    ActorContact_ScratchPosition=gAkropolisRoofGardenScratchPosition
    gAyaStandClip=gAkropolisRoofGardenAyaStandClip)
set_property(SOURCE src/stages/akropolis/actors/roof_garden_swat_member/roof_garden_swat_member.c
    APPEND PROPERTY COMPILE_DEFINITIONS actorMsgSetPairVisibility=roofGardenSwatMsgSetPairVisibility)
set_property(SOURCE src/stages/akropolis/rooms/forked_road/forked_road_effects.c
    APPEND PROPERTY COMPILE_DEFINITIONS gAyaStandClip=gAkropolisForkedRoadAyaStandClip)
set_property(SOURCE src/actors/actor_401300/actor_401300.c
    APPEND PROPERTY COMPILE_DEFINITIONS
    actorMsgIsPresent=actor401300MsgIsPresent
    actorMsgReleaseHold=actor401300MsgReleaseHold
    actorMsgSetVisibility=actor401300MsgSetVisibility
    ActorContact_ScratchPosition=gActor401300ScratchPosition)
set_property(SOURCE src/actors/actor_04000/actor_04000.c
    APPEND PROPERTY COMPILE_DEFINITIONS actorMsgPlace=actor04000MsgPlace)
set_property(SOURCE src/actors/actor_402200/actor_402200.c
    APPEND PROPERTY COMPILE_DEFINITIONS
    gGolemStaggerClip=gActor402200GolemStaggerClip
    gGolemIdleClip=gActor402200GolemIdleClip)
set_property(SOURCE src/weapons/tonfa_baton/tonfa_baton.c
    APPEND PROPERTY COMPILE_DEFINITIONS gAyaIdleClip=gTonfaBatonAyaIdleClip)
set_property(SOURCE src/actors/actor_00700/actor_00700.c
    APPEND PROPERTY COMPILE_OPTIONS -include ${CMAKE_CURRENT_SOURCE_DIR}/cmake/aliases/actor_200700__actor_00700.h)
add_library(game_overlays OBJECT ${PE_OVERLAY_LIST})
target_sources(game_overlays PRIVATE src/port/area_updates.c)
target_sources(game_overlays PRIVATE src/port/encounter_spots.c)
target_include_directories(game_overlays PUBLIC include include/decomp
    ${PE_GENERATED_DIR})
target_compile_definitions(game_overlays PUBLIC __psyz SKIP_ASM)
target_compile_options(game_overlays PRIVATE -w -Wno-incompatible-function-pointer-types -g -fno-omit-frame-pointer -include port/gpu_len.h)
target_link_libraries(game_overlays PUBLIC psyz)
target_compile_options(game_overlays PRIVATE "SHELL:-include port/random.h")

set_property(SOURCE src/stages/akropolis/rooms/helicopter_landing_pad/helicopter_landing_pad.c
    APPEND PROPERTY COMPILE_DEFINITIONS actorMsgPlaceEuler=gHelipadLiftPlaceEuler)
set_property(SOURCE src/stages/akropolis/rooms/helicopter_landing_pad/helicopter_landing_pad_departure.c
    APPEND PROPERTY COMPILE_DEFINITIONS ActorContact_ScratchPosition=gHelipadDepartureScratchPosition)
set_property(SOURCE src/stages/akropolis/actors/helicopter_departure/helicopter_departure.c
    APPEND PROPERTY COMPILE_DEFINITIONS actorMsgPlaceEuler=gHelicopterDeparturePlaceEuler actorMsgPlaceRotMatrix=gHelicopterDeparturePlaceRotMatrix gGolemNo9BodyAkropolis=gHelicopterDepartureGolemNo9Body)

set_property(SOURCE src/stages/akropolis/actors/helicopter_landing_pad_no_9/helicopter_landing_pad_no_9.c
    APPEND PROPERTY COMPILE_DEFINITIONS
    gGolemPawnBeamSwordChargeRunClip=gHelipadNo9GolemPawnBeamSwordChargeRunClip
    gGolemPawnBeamSwordChargeSlashClip=gHelipadNo9GolemPawnBeamSwordChargeSlashClip
    gGolemPawnBeamSwordChargeWindupClip=gHelipadNo9GolemPawnBeamSwordChargeWindupClip
    gGolemAdvanceClip=gHelipadNo9GolemAdvanceClip
    gGolemBuildupClip=gHelipadNo9GolemBuildupClip
    gGolemGrenade=gHelipadNo9GolemGrenade
    gGolemRecoilBehindClip=gHelipadNo9GolemRecoilBehindClip
    gGolemRecoilFrontClip=gHelipadNo9GolemRecoilFrontClip
    gGolemScreamRecoverClip=gHelipadNo9GolemScreamRecoverClip
    gGolemNo9AkropolisChestPiece=gHelipadNo9GolemNo9AkropolisChestPiece
    gGolemNo9AkropolisProp=gHelipadNo9GolemNo9AkropolisProp
    gGolemNo9AkropolisSword=gHelipadNo9GolemNo9AkropolisSword
    gGolemNo9BodyAkropolis=gHelipadNo9GolemNo9BodyAkropolis
    actorMsgPlaceRotMatrix=gHelipadNo9ActorMsgPlaceRotMatrix)
set_property(SOURCE src/stages/akropolis/actors/helicopter_landing_pad_no_9/helicopter_landing_pad_no_9_combat.c
    APPEND PROPERTY COMPILE_DEFINITIONS
    gGolemPawnBeamSwordChargeRunClip=gHelipadNo9GolemPawnBeamSwordChargeRunClip
    gGolemPawnBeamSwordChargeSlashClip=gHelipadNo9GolemPawnBeamSwordChargeSlashClip
    gGolemPawnBeamSwordChargeWindupClip=gHelipadNo9GolemPawnBeamSwordChargeWindupClip
    gGolemAdvanceClip=gHelipadNo9GolemAdvanceClip
    gGolemBuildupClip=gHelipadNo9GolemBuildupClip
    gGolemGrenade=gHelipadNo9GolemGrenade
    gGolemRecoilBehindClip=gHelipadNo9GolemRecoilBehindClip
    gGolemRecoilFrontClip=gHelipadNo9GolemRecoilFrontClip
    gGolemScreamRecoverClip=gHelipadNo9GolemScreamRecoverClip
    gGolemNo9AkropolisChestPiece=gHelipadNo9GolemNo9AkropolisChestPiece
    gGolemNo9AkropolisProp=gHelipadNo9GolemNo9AkropolisProp
    gGolemNo9AkropolisSword=gHelipadNo9GolemNo9AkropolisSword
    gGolemNo9BodyAkropolis=gHelipadNo9GolemNo9BodyAkropolis
    actorMsgPlaceRotMatrix=gHelipadNo9ActorMsgPlaceRotMatrix)

# src/port/asm_data.c holds game tables converted from the user's disc by the tooling at the tag pe2-layout-tooling; it is
# committed; the fallback below only covers a checkout without it.
if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/src/port/asm_data.c)
    file(WRITE ${CMAKE_CURRENT_SOURCE_DIR}/src/port/asm_data.c "")
endif()
add_executable(parasite-eve-ii-pc src/port/main.c src/port/psyq_host.c src/port/memory_card.c src/port/fixed_log.c src/port/pad_trace.c src/port/lzss.c src/port/tmd_dispatch.c src/port/tmd_prim.c src/port/tmd_ops.c src/port/mdec_vlc.c src/port/tmd_source.c src/port/tmd_layout.c src/port/cap_asset.c src/port/cap_resource.c src/port/disc_assets.c src/port/asset_loader.c src/port/asset_registry.c src/port/asset_parts_table.c src/port/asm_data.c src/port/globals.c src/port/unresolved_stubs.c $<TARGET_OBJECTS:game_main>
    $<TARGET_OBJECTS:game_overlays>)
target_include_directories(parasite-eve-ii-pc PRIVATE include include/decomp src/main)
target_compile_definitions(parasite-eve-ii-pc PRIVATE __psyz SKIP_ASM)
target_link_libraries(parasite-eve-ii-pc PRIVATE psyz)
target_sources(parasite-eve-ii-pc PRIVATE src/port/spu_voice_attributes.c)
option(PE2_HD_BACKGROUNDS "Show the experimental HD background pack options" OFF)
if(PE2_HD_BACKGROUNDS)
    target_compile_definitions(parasite-eve-ii-pc PRIVATE PE2_HD_BACKGROUNDS_ENABLED=1)
endif()
target_sources(parasite-eve-ii-pc PRIVATE src/port/host_ui.c src/port/backgrounds.c src/port/background_coverage.c src/port/background_pack.c src/port/gpu_state.c src/port/background_foreground.c src/port/foreground_ot.c src/port/foreground_pack.c src/port/foreground_stage.c)

# Variant units: a package compiled from another package's shared source with its own -D parameters.
# Each is a separate object library so the parameters stay per package (see docs/PORT_PLAN.md).
function(pe_overlay_variant pkg unit src defs hdr)
    string(REPLACE "/" "__" tgt "pe_var_${unit}")
    if(NOT TARGET ${tgt})
        set(_vsrc ${CMAKE_CURRENT_SOURCE_DIR}/${src})
        if(PE_ALL_VARIANTS)
            # A file of its own, so per-object name changes (cmake/overlay_renames.cmake) do not reach the other
            # objects built from the same source.
            file(WRITE ${CMAKE_BINARY_DIR}/variants/${tgt}.c "#include \"${_vsrc}\"\n")
            set(_vsrc ${CMAKE_BINARY_DIR}/variants/${tgt}.c)
        endif()
        add_library(${tgt} OBJECT EXCLUDE_FROM_ALL ${_vsrc})
        target_include_directories(${tgt} PRIVATE include include/decomp ${PE_GENERATED_DIR})
        target_compile_definitions(${tgt} PRIVATE __psyz SKIP_ASM ${defs})
        target_compile_options(${tgt} PRIVATE -w -Wno-incompatible-function-pointer-types -Wno-return-mismatch)
        target_link_libraries(${tgt} PRIVATE psyz)
        target_compile_options(${tgt} PRIVATE "SHELL:-include port/random.h")
        if(hdr)
            target_compile_options(${tgt} PRIVATE -include ${CMAKE_CURRENT_SOURCE_DIR}/${hdr})
        endif()
        set_property(GLOBAL APPEND PROPERTY PE_VARIANT_TARGETS ${tgt})
    endif()
endfunction()
include(cmake/overlay_variants.cmake)
# The P08 bank is also the unarmed Aya bank selected after returning the
# shooting-gallery loadout. Reuse its manifest-specific build parameters.
if(NOT PE_ALL_OVERLAYS OR PE_ALL_VARIANTS)
    target_compile_options(pe_var_p08__p08 PRIVATE -g -fno-omit-frame-pointer -include port/gpu_len.h)
    # Both console packages export their own copy of the authored idle clip.
    # Keep P08's bank bound to its copy when both overlays are resident.
    target_compile_definitions(pe_var_p08__p08 PRIVATE gAyaIdleClip=gP08AyaIdleClip)
    target_sources(parasite-eve-ii-pc PRIVATE $<TARGET_OBJECTS:pe_var_p08__p08>)
endif()
if(NOT PE_ALL_OVERLAYS OR PE_ALL_VARIANTS)
    target_compile_options(pe_var_actor_101900__actor_01900 PRIVATE -g -fno-omit-frame-pointer "SHELL:-include port/gpu_len.h")
    target_compile_definitions(pe_var_actor_101900__actor_01900 PRIVATE PE_HOST_ALIAS_ACTOR301900)
    target_sources(parasite-eve-ii-pc PRIVATE $<TARGET_OBJECTS:pe_var_actor_101900__actor_01900>)
endif()
if(NOT PE_ALL_OVERLAYS OR PE_ALL_VARIANTS)
    target_compile_options(pe_var_actor_101200__actor_01200 PRIVATE -g -fno-omit-frame-pointer "SHELL:-include port/gpu_len.h")
    target_sources(parasite-eve-ii-pc PRIVATE $<TARGET_OBJECTS:pe_var_actor_101200__actor_01200>)
endif()
if(NOT PE_ALL_OVERLAYS OR PE_ALL_VARIANTS)
    # This manifest variant exports both gActor01100Tasks and its gActor301100Tasks alias.
    target_compile_options(pe_var_actor_301100__actor_01100 PRIVATE -g -fno-omit-frame-pointer "SHELL:-include port/gpu_len.h")
    target_compile_definitions(pe_var_actor_301100__actor_01100 PRIVATE PE_HOST_ALIAS_ACTOR101100)
    target_sources(parasite-eve-ii-pc PRIVATE $<TARGET_OBJECTS:pe_var_actor_301100__actor_01100>)
endif()
if(NOT PE_ALL_OVERLAYS OR PE_ALL_VARIANTS)
    foreach(_fork_actor IN ITEMS
        actor_101600__actor_01600 actor_102000__actor_102000_text
        actor_102400__actor_02400 actor_102500__actor_02500
        actor_103700__actor_03700 actor_103800__actor_03800
        actor_104600__actor_04600 actor_105500__actor_05500
        actor_107000__actor_07000)
        target_compile_options(pe_var_${_fork_actor} PRIVATE -g -fno-omit-frame-pointer "SHELL:-include port/gpu_len.h")
        target_sources(parasite-eve-ii-pc PRIVATE $<TARGET_OBJECTS:pe_var_${_fork_actor}>)
    endforeach()
    target_compile_definitions(pe_var_actor_105500__actor_05500 PRIVATE PE_HOST_ALIAS_ACTOR205500)
    target_compile_definitions(pe_var_actor_102000__actor_102000_text PRIVATE
        gGolemScreamRecoverClip=gPawnGolemScreamRecoverClip
        gGolemPawnRookImpactSound=gPawnGolemImpactSound
        gGolemBuildupClip=gPawnGolemBuildupClip
        gGolemRecoilBehindClip=gPawnGolemRecoilBehindClip
        gGolemStaggerClip=gPawnGolemStaggerClip
        gGolemIdleClip=gPawnGolemIdleClip
        gGolemWalkClip=gPawnGolemWalkClip
        gGolemRecoilFrontClip=gPawnGolemRecoilFrontClip)
endif()
get_property(_pe_vars GLOBAL PROPERTY PE_VARIANT_TARGETS)
add_custom_target(overlay_variants DEPENDS ${_pe_vars})

# Experiment: with PE_ALL_OVERLAYS also link every per-package variant, to find the name clashes the registry has to resolve.
if(PE_ALL_OVERLAYS AND PE_ALL_VARIANTS)
    get_target_property(_have parasite-eve-ii-pc SOURCES)
    foreach(_v ${_pe_vars})
        if(NOT "${_have}" MATCHES "TARGET_OBJECTS:${_v}>")
            target_sources(parasite-eve-ii-pc PRIVATE $<TARGET_OBJECTS:${_v}>)
        endif()
    endforeach()
endif()

# Private names for globals that several overlays define (tools/port/gen_overlay_renames.py).
function(pe_rename_object tgt src defs)
    if(NOT TARGET ${tgt})
        return()
    endif()
    if(tgt MATCHES "^pe_var_")
        target_compile_definitions(${tgt} PRIVATE ${defs})
    else()
        set_property(SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/${src} APPEND PROPERTY COMPILE_DEFINITIONS "${defs}")
    endif()
endfunction()
if(PE_ALL_OVERLAYS AND PE_ALL_VARIANTS AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/cmake/overlay_renames.cmake)
    include(cmake/overlay_renames.cmake)
    # The generated renames cover definitions, but these rooms split their
    # transition descriptor/message tables from the callbacks and latched data.
    # Apply the same names to sibling references; otherwise a room can dispatch
    # the factory's public roomEventTask and reload an empty destination.
    foreach(_tower IN ITEMS dryfield_water_tower dryfield_night_water_tower dryfield_breezeway
            dryfield_night_dilapidated_house dryfield_night_motel_balcony dryfield_night_parking_lot
            shelter_b2_north_maintenance_walkway)
        set(_tower_prefix "PE_rooms_${_tower}_${_tower}")
        pe_rename_object(game_overlays src/rooms/${_tower}/${_tower}.c
            "gRoomEventTaskDesc=${_tower_prefix}_2_gRoomEventTaskDesc")
        pe_rename_object(game_overlays src/rooms/${_tower}/${_tower}_2.c
            "roomEventTask=${_tower_prefix}_roomEventTask")
    endforeach()
    pe_rename_object(game_overlays src/rooms/dryfield_breezeway/dryfield_breezeway_2.c
        "gRoomEventTaskDesc=PE_rooms_dryfield_breezeway_dryfield_breezeway_2_gRoomEventTaskDesc")
    pe_rename_object(game_overlays src/rooms/dryfield_water_tower/dryfield_water_tower.c
        "gRoomEventReq=PE_rooms_dryfield_water_tower_dryfield_water_tower_gRoomEventReq")
    pe_rename_object(game_overlays src/rooms/dryfield_water_tower/dryfield_water_tower_2.c
        "roomVariantWaterTowerMsg=PE_rooms_dryfield_water_tower_dryfield_water_tower_roomVariantWaterTowerMsg;waterTowerSoundMsg=PE_rooms_dryfield_water_tower_dryfield_water_tower_waterTowerSoundMsg")
endif()
