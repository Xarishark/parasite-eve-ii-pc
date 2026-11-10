cmake_minimum_required(VERSION 3.21..3.31)
if(CMAKE_HOST_APPLE AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET
   AND (NOT DEFINED CMAKE_SYSTEM_NAME OR CMAKE_SYSTEM_NAME STREQUAL "Darwin"))
    set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING "Minimum supported macOS version")
endif()
project(parasite-eve-ii-pc VERSION 0.2.0 LANGUAGES C)

# Overlay packages and their variants define the same uninitialised globals in several objects. Apple's toolchain merges
# those tentative definitions into one; GCC and Clang elsewhere need -fcommon to do the same, or the link fails with
# multiple definitions.
if(NOT APPLE AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options($<$<COMPILE_LANGUAGE:C>:-fcommon>)
    # The decompiled sources pass PS1-style pointers where the host headers declare other types (docs/PORT_PLAN.md).
    # Clang on macOS only warns; GCC 14+ and Clang 21+ make these errors by default.
    add_compile_options($<$<COMPILE_LANGUAGE:C>:-Wno-error=incompatible-pointer-types>
                        $<$<COMPILE_LANGUAGE:C>:-Wno-error=int-conversion>
                        $<$<COMPILE_LANGUAGE:C>:-Wno-error=implicit-function-declaration>)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

# PsyZ: our fork of the PsyZ PSY-Q replacement (SDL3 video/audio/input, libgte, libcd, MDEC).
set(PSYZ_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/psyz)
add_subdirectory(${PSYZ_DIR}/psyz ${CMAKE_CURRENT_BINARY_DIR}/psyz)
target_compile_definitions(psyz PRIVATE __psyz)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    # The original task/actor workspaces are accessed through several layout views.
    add_compile_options("$<$<COMPILE_LANGUAGE:C>:-fno-strict-aliasing>"
        "$<$<COMPILE_LANGUAGE:C>:-ffunction-sections>"
        "$<$<COMPILE_LANGUAGE:C>:-fdata-sections>")
endif()

set(PE_GENERATED_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include/generated)

# The game builds from the committed layout sources (include/generated, src/port/asm_data.c, asset_parts_table.c,
# globals.c): types, sizes and registrations, no game data. The data itself is read from the user's disc images at run
# time. regenerating them needs the extraction tooling, which was removed from the tree and lives at the tag pe2-layout-tooling.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/src/port/asset_parts_table.c)
    set(_pe_game_default ON)
else()
    set(_pe_game_default OFF)
endif()
option(PE_BUILD_GAME "Build the game executable" ${_pe_game_default})
if(PE_BUILD_GAME)
    include(cmake/game.cmake)
else()
    message(STATUS "PE_BUILD_GAME is OFF: building the host tests only")
endif()


# Host test declarations. pe_host_test(name [MAIN file] [SOURCES ...]
# [INCLUDES ...] [DEFINES ...] [OPTIONS ...] [LINK ...] [STRIP]) creates the
# executable (EXCLUDE_FROM_ALL), always searches include/, and registers the
# common decompilation flags (__psyz SKIP_ASM) and psyz when asked.
function(pe_host_test name)
    cmake_parse_arguments(PE "STRIP" "MAIN" "SOURCES;INCLUDES;DEFINES;OPTIONS;LINK" ${ARGN})
    if(PE_MAIN)
        set(_main ${PE_MAIN})
    else()
        set(_main tests/${name}.c)
    endif()
    add_executable(${name} EXCLUDE_FROM_ALL ${_main} ${PE_SOURCES})
    target_include_directories(${name} PRIVATE include ${PE_INCLUDES})
    if(PE_DEFINES)
        target_compile_definitions(${name} PRIVATE ${PE_DEFINES})
    endif()
    if(PE_OPTIONS)
        target_compile_options(${name} PRIVATE ${PE_OPTIONS})
    endif()
    if(PE_LINK OR PE_STRIP)
        target_link_libraries(${name} PRIVATE ${PE_LINK})
    endif()
    if(PE_STRIP)
        if(APPLE)
            target_link_options(${name} PRIVATE -Wl,-dead_strip)
        else()
            target_link_options(${name} PRIVATE -Wl,--gc-sections)
        endif()
    endif()
endfunction()

# Host checks for scripted arrival coordinates and GTE compatibility macros.
pe_host_test(actor_walk_gap_test INCLUDES include/decomp DEFINES __psyz SKIP_ASM LINK psyz)
pe_host_test(sdk_random_test INCLUDES include/decomp DEFINES __psyz SKIP_ASM OPTIONS -UNDEBUG LINK psyz)
pe_host_test(port_random_test SOURCES tests/port_random_client.c OPTIONS -UNDEBUG LINK psyz)
pe_host_test(screen_negative_test INCLUDES include/decomp DEFINES __psyz SKIP_ASM LINK psyz)
pe_host_test(gte_compat_test INCLUDES include/decomp DEFINES __psyz SKIP_ASM LINK psyz)

# Development tool: reads one package straight from a disc image (src/port/disc_assets.c).
pe_host_test(pe2_discread MAIN tools/port/discread.c SOURCES src/port/disc_assets.c)

pe_host_test(lzss_test SOURCES src/port/lzss.c INCLUDES include/decomp src/main DEFINES __psyz SKIP_ASM LINK psyz)
pe_host_test(tmd_model_test SOURCES src/port/tmd_dispatch.c INCLUDES include/decomp src/main DEFINES __psyz SKIP_ASM LINK psyz)
pe_host_test(tmd_prim_test SOURCES src/port/tmd_prim.c src/port/tmd_dispatch.c INCLUDES include/decomp src/main DEFINES __psyz SKIP_ASM LINK psyz)
pe_host_test(tmd_ops_test SOURCES src/port/tmd_ops.c src/port/tmd_dispatch.c INCLUDES include/decomp src/main DEFINES __psyz SKIP_ASM LINK psyz)
pe_host_test(tmd_source_test SOURCES src/port/tmd_source.c INCLUDES include/decomp DEFINES __psyz SKIP_ASM LINK psyz)
pe_host_test(image_workspace_test INCLUDES include/decomp DEFINES __psyz SKIP_ASM LINK psyz)
pe_host_test(tmd_layout_test SOURCES src/port/tmd_layout.c INCLUDES include/decomp DEFINES __psyz SKIP_ASM LINK psyz)
pe_host_test(tmd_layout_assets_test SOURCES src/port/tmd_layout.c INCLUDES include/decomp ${PE_GENERATED_DIR} DEFINES __psyz SKIP_ASM LINK psyz)
pe_host_test(cap_asset_test SOURCES src/port/cap_asset.c INCLUDES include/decomp DEFINES __psyz SKIP_ASM LINK psyz)
pe_host_test(cap_resource_test SOURCES src/port/cap_asset.c src/port/cap_resource.c INCLUDES include/decomp DEFINES __psyz SKIP_ASM LINK psyz)
pe_host_test(task_message_test SOURCES src/gameplay/task_message.c INCLUDES include/decomp DEFINES __psyz SKIP_ASM LINK psyz)
pe_host_test(task_teardown_test SOURCES src/main/task.c INCLUDES include/decomp src/main DEFINES __psyz SKIP_ASM LINK psyz STRIP)
pe_host_test(cd_ring_test INCLUDES include/decomp DEFINES __psyz SKIP_ASM LINK psyz)
pe_host_test(cd_pump_test INCLUDES include/decomp DEFINES __psyz SKIP_ASM LINK psyz)
pe_host_test(sprite_packet_test INCLUDES include/decomp DEFINES __psyz SKIP_ASM LINK psyz)
pe_host_test(text_palette_test SOURCES src/main/text_palette.c INCLUDES include/decomp ${PE_GENERATED_DIR} DEFINES __psyz SKIP_ASM LINK psyz)
pe_host_test(ordering_table_test DEFINES __psyz SKIP_ASM LINK psyz)
pe_host_test(scene_broadcast_test SOURCES src/gameplay/task_message.c INCLUDES include/decomp ${PE_GENERATED_DIR} src/main
    DEFINES __psyz SKIP_ASM OPTIONS -w -Wno-incompatible-function-pointer-types LINK psyz STRIP)
pe_host_test(spu_key_status_test DEFINES __psyz LINK psyz)
pe_host_test(spu_voice_attributes_test SOURCES src/port/spu_voice_attributes.c DEFINES __psyz LINK psyz)
pe_host_test(sound_slot_start_test INCLUDES include/decomp src/main DEFINES __psyz SKIP_ASM LINK psyz STRIP)
pe_host_test(observatory_scene_lifecycle_test INCLUDES include/decomp ${PE_GENERATED_DIR}
    DEFINES __psyz SKIP_ASM OPTIONS -Wno-incompatible-function-pointer-types LINK psyz STRIP)
pe_host_test(raster_clock_test DEFINES __psyz LINK psyz)
pe_host_test(gpu_tpage_test INCLUDES include/decomp DEFINES __psyz SKIP_ASM LINK psyz)
pe_host_test(pad_trace_test SOURCES src/port/pad_trace.c)
pe_host_test(player_target_test SOURCES src/gameplay/player_target.c INCLUDES include/decomp DEFINES __psyz SKIP_ASM LINK psyz)
pe_host_test(action_trigger_test SOURCES src/gameplay/collision_grid.c src/gameplay/object_lists.c
    INCLUDES include/decomp ${PE_GENERATED_DIR} DEFINES __psyz SKIP_ASM LINK psyz STRIP)
pe_host_test(pair_contact_test SOURCES src/gameplay/collision_grid.c src/gameplay/object_lists.c
    INCLUDES include/decomp DEFINES __psyz SKIP_ASM LINK psyz STRIP)
pe_host_test(scene_sector_test INCLUDES include/decomp DEFINES __psyz SKIP_ASM LINK psyz STRIP)
if(NOT APPLE)
    # The test replaces CdGetSector with a double; GNU ld rejects the copy inside PsyZ's libcd object.
    target_link_options(scene_sector_test PRIVATE -Wl,--allow-multiple-definition)
endif()
pe_host_test(area_resource_test SOURCES src/gameplay/area_cd.c INCLUDES include/decomp DEFINES __psyz SKIP_ASM LINK psyz)
pe_host_test(item_prompt_lifecycle_test SOURCES src/gameplay/item_notice.c INCLUDES include/decomp DEFINES __psyz SKIP_ASM LINK psyz)
pe_host_test(caption_packet_test INCLUDES include/decomp DEFINES __psyz SKIP_ASM LINK psyz STRIP)
pe_host_test(game_over_image_test INCLUDES include/decomp DEFINES __psyz SKIP_ASM LINK psyz STRIP)
pe_host_test(fountain_trigger_test SOURCES src/gameplay/object_lists.c
    INCLUDES include/decomp ${PE_GENERATED_DIR} DEFINES __psyz SKIP_ASM LINK psyz STRIP)
pe_host_test(fixed_log_test SOURCES src/port/fixed_log.c src/gameplay/object_lists.c
    INCLUDES include/decomp DEFINES __psyz SKIP_ASM LINK psyz STRIP)
pe_host_test(rupert_eyes_test INCLUDES include/decomp ${PE_GENERATED_DIR} DEFINES __psyz SKIP_ASM LINK psyz STRIP)
pe_host_test(cap_room_command_test SOURCES src/gameplay/cap_commands.c src/port/cap_asset.c
    INCLUDES include/decomp DEFINES __psyz SKIP_ASM LINK psyz STRIP)
pe_host_test(memory_card_test SOURCES src/port/memory_card.c LINK psyz)

# Host tests: `cmake --build build --target host_tests && ctest --test-dir build`. They need no game data, except
# the ones in PE_DATA_TESTS, which read extracted assets given on the command line and are left to local runs.
pe_host_test(asset_disc_cache_test SOURCES src/port/asset_loader.c src/port/asset_registry.c)
pe_host_test(disc_identity_test SOURCES src/port/disc_assets.c)

pe_host_test(weapon_m950_test SOURCES src/weapons/m950/m950.c
    INCLUDES include/decomp ${PE_GENERATED_DIR} DEFINES __psyz SKIP_ASM LINK psyz)
pe_host_test(weapon_m249_test MAIN tests/weapon_fire_test.c SOURCES src/weapons/m249/m249.c
    INCLUDES include/decomp ${PE_GENERATED_DIR}
    DEFINES __psyz SKIP_ASM WEAPON_FN=m249AttackState WEAPON_HEADER="weapons/m249.h" WEAPON_NAME="m249" WEAPON_TRACE=WEAPON_TRACE_m249
    LINK psyz)
pe_host_test(weapon_as12_test MAIN tests/weapon_fire_test.c SOURCES src/weapons/as12/as12.c
    INCLUDES include/decomp ${PE_GENERATED_DIR}
    DEFINES __psyz SKIP_ASM WEAPON_FN=as12AttackState WEAPON_HEADER="weapons/as12.h" WEAPON_NAME="as12" WEAPON_TRACE=WEAPON_TRACE_as12
    LINK psyz)
pe_host_test(weapon_mp5a5_test MAIN tests/weapon_fire_test.c SOURCES tests/weapon_mp5a5_stubs.c src/weapons/mp5a5/mp5a5.c
    INCLUDES include/decomp ${PE_GENERATED_DIR}
    DEFINES __psyz SKIP_ASM WEAPON_FN=mp5a5AttackState WEAPON_HEADER="weapons/mp5a5.h" WEAPON_NAME="mp5a5" WEAPON_TRACE=WEAPON_TRACE_mp5a5 WEAPON_INDEX=30
    LINK psyz)

pe_host_test(anim_cue_latch_test)
pe_host_test(host_menu_probe SOURCES src/port/background_coverage.c INCLUDES include/decomp DEFINES __psyz SKIP_ASM LINK psyz STRIP)
pe_host_test(scene_image_timeout_test INCLUDES include/decomp DEFINES __psyz SKIP_ASM LINK psyz STRIP)
pe_host_test(background_coverage_test SOURCES src/port/background_coverage.c INCLUDES ${PSYZ_DIR}/psyz/include DEFINES __psyz)
# video.h uses PsyZ public types; this test supplies stub renderer entry points.
pe_host_test(background_pack_test SOURCES src/port/background_pack.c)
pe_host_test(background_presenter_probe SOURCES src/port/backgrounds.c src/port/background_coverage.c src/port/background_pack.c src/port/gpu_state.c src/port/background_foreground.c src/port/foreground_ot.c src/port/foreground_pack.c src/port/foreground_stage.c
    DEFINES __psyz SKIP_ASM LINK psyz)
pe_host_test(hd_native_decode MAIN tools/hd/native_decode.c SOURCES src/port/mdec_vlc.c src/port/background_pack.c
    INCLUDES include/decomp DEFINES __psyz SKIP_ASM LINK psyz)
pe_host_test(foreground_presenter_probe SOURCES src/port/background_foreground.c src/port/backgrounds.c src/port/background_coverage.c src/port/background_pack.c src/port/gpu_state.c src/port/foreground_ot.c src/port/foreground_pack.c src/port/foreground_stage.c
    DEFINES __psyz SKIP_ASM LINK psyz)
pe_host_test(foreground_packet_test SOURCES src/port/background_foreground.c DEFINES __psyz SKIP_ASM LINK psyz)
pe_host_test(gpu_state_test SOURCES src/port/gpu_state.c DEFINES __psyz SKIP_ASM LINK psyz)
pe_host_test(foreground_pack_test SOURCES src/port/foreground_pack.c)
pe_host_test(foreground_stage_test SOURCES src/port/foreground_stage.c src/port/gpu_state.c DEFINES __psyz SKIP_ASM LINK psyz)
pe_host_test(scene_timing_buffer_test SOURCES src/main/cdcmd.c INCLUDES include/decomp src/main
    DEFINES __psyz SKIP_ASM OPTIONS -ffunction-sections -fdata-sections LINK psyz STRIP)
if(MINGW)
    # COFF unwind records keep otherwise-unused cdcmd functions live in this
    # isolated allocator fixture. The game retains its normal unwind metadata.
    target_compile_options(scene_timing_buffer_test PRIVATE -fno-asynchronous-unwind-tables -fno-unwind-tables)
    # lld (MSYS2 CLANG64) only discards COMDAT sections, and -ffunction-sections
    # does not make them COMDAT on MinGW, so the unused cdcmd paths keep their
    # references to the rest of the game. GNU ld discards them; lld must be told
    # to leave those never-called references unresolved.
    include(CheckLinkerFlag)
    check_linker_flag(C "-Wl,-Xlink=-force:unresolved" PE_LLD_FORCE_UNRESOLVED)
    if(PE_LLD_FORCE_UNRESOLVED)
        target_link_options(scene_timing_buffer_test PRIVATE -Wl,-Xlink=-force:unresolved)
    endif()
endif()
pe_host_test(water_grid_packet_test INCLUDES include/decomp DEFINES __psyz SKIP_ASM LINK psyz)
pe_host_test(bezier_curve_test INCLUDES include/decomp DEFINES __psyz SKIP_ASM LINK psyz)
pe_host_test(background_capture_target_test)
target_include_directories(background_capture_target_test PRIVATE include)

enable_testing()
set(PE_DATA_TESTS cap_room_command_test rupert_eyes_test tmd_layout_assets_test text_palette_test
    observatory_scene_lifecycle_test action_trigger_test fountain_trigger_test)
# raster_clock_test asserts the video backend honours VSync, which a headless runner does not.
list(APPEND PE_DATA_TESTS raster_clock_test)
get_property(_pe_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
add_custom_target(host_tests)
foreach(_t IN LISTS _pe_targets)
    if(_t MATCHES "_test$" AND NOT _t IN_LIST PE_DATA_TESTS)
        add_dependencies(host_tests ${_t})
        # Release builds must still execute checks and fixture setup in assert().
        if(MSVC)
            target_compile_options(${_t} PRIVATE /UNDEBUG)
        else()
            target_compile_options(${_t} PRIVATE -UNDEBUG)
        endif()
        add_test(NAME ${_t} COMMAND ${_t})
    endif()
endforeach()
set_tests_properties(gpu_state_test gpu_tpage_test caption_packet_test PROPERTIES ENVIRONMENT "PSYZ_HEADLESS=1;SDL_AUDIODRIVER=dummy")

# These regressions avoid the POSIX filesystem fixtures and can run on Windows.
set(PE_PORTABLE_GRAPHICS_TESTS
    actor_walk_gap_test sdk_random_test port_random_test screen_negative_test
    gte_compat_test asset_disc_cache_test background_coverage_test background_pack_test
    foreground_packet_test gpu_state_test foreground_pack_test foreground_stage_test
    scene_timing_buffer_test water_grid_packet_test bezier_curve_test background_capture_target_test)
add_custom_target(portable_graphics_tests DEPENDS ${PE_PORTABLE_GRAPHICS_TESTS})
set_tests_properties(${PE_PORTABLE_GRAPHICS_TESTS} PROPERTIES LABELS "portable-graphics")
