cmake_minimum_required(VERSION 3.18)
project(rc_monitor C)

set(CMAKE_C_STANDARD 11)

# Source files
set(SOURCES
    src/rc_monitor.c
)

# Include path
include_directories(include)

# --- Android JNI shared library ---
if(ANDROID)
    add_library(rc_monitor SHARED
        ${SOURCES}
        src/rc_monitor_jni.c
    )
    target_link_libraries(rc_monitor
        log    # Android logging
    )
    # Strip debug symbols for release
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        set_target_properties(rc_monitor PROPERTIES
            LINK_FLAGS "-s"
        )
    endif()

# --- Desktop static library (for testing) ---
else()
    # Compiler warnings
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)

    # Optional: Address Sanitizer + Undefined Behavior Sanitizer
    option(ENABLE_SANITIZERS "Enable ASan + UBSan" OFF)
    if(ENABLE_SANITIZERS)
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address,undefined)
    endif()

    add_library(rc_monitor_static STATIC ${SOURCES})
    target_include_directories(rc_monitor_static PUBLIC include)

    # Test binary
    if(EXISTS ${CMAKE_SOURCE_DIR}/test/test_rc_monitor.c)
        add_executable(test_rc_monitor test/test_rc_monitor.c)
        target_link_libraries(test_rc_monitor rc_monitor_static)
    endif()

    # RC Emulator (interactive testing tool)
    if(EXISTS ${CMAKE_SOURCE_DIR}/emulator/rc_emulator.c)
        add_executable(rc_emulator emulator/rc_emulator.c)
        target_link_libraries(rc_emulator rc_monitor_static)
        # Relax -Wpedantic for ncurses system headers (macOS SDK has extra
        # semicolons that trip -Wpedantic, and _string.h uses 'defined' in
        # macro expansion which trips -Wexpansion-to-defined).
        target_compile_options(rc_emulator PRIVATE
            -Wno-pedantic -Wno-expansion-to-defined)
        find_package(Curses)
        if(CURSES_FOUND)
            target_include_directories(rc_emulator PRIVATE ${CURSES_INCLUDE_DIRS})
            target_link_libraries(rc_emulator ${CURSES_LIBRARIES})
        else()
            target_link_libraries(rc_emulator ncurses)
        endif()
    endif()

    # Recording verification tool
    if(EXISTS ${CMAKE_SOURCE_DIR}/test/verify_recording.c)
        add_executable(verify_recording test/verify_recording.c)
        target_link_libraries(verify_recording rc_monitor_static)
    endif()

    # Fuzz targets (requires clang with libFuzzer support)
    option(ENABLE_FUZZING "Enable libFuzzer targets" OFF)
    if(ENABLE_FUZZING)
        # Separate instrumented library for fuzz targets (coverage + sanitizers)
        # so the regular rc_monitor_static stays clean for test_rc_monitor et al.
        add_library(rc_monitor_fuzz STATIC ${SOURCES})
        target_include_directories(rc_monitor_fuzz PUBLIC include)
        target_compile_options(rc_monitor_fuzz PRIVATE
            -fsanitize=fuzzer-no-link,address,undefined -fno-omit-frame-pointer)

        # fuzz_feed — DUML frame parser fuzzer
        add_executable(fuzz_feed test/fuzz_feed.c)
        target_link_libraries(fuzz_feed rc_monitor_fuzz)
        target_compile_options(fuzz_feed PRIVATE -fsanitize=fuzzer,address,undefined)
        target_link_options(fuzz_feed PRIVATE -fsanitize=fuzzer,address,undefined)

        # fuzz_payload — payload decoder fuzzer
        add_executable(fuzz_payload test/fuzz_payload.c)
        target_link_libraries(fuzz_payload rc_monitor_fuzz)
        target_compile_options(fuzz_payload PRIVATE -fsanitize=fuzzer,address,undefined)
        target_link_options(fuzz_payload PRIVATE -fsanitize=fuzzer,address,undefined)

        # fuzz_build_packet — round-trip builder+parser fuzzer
        add_executable(fuzz_build_packet test/fuzz_build_packet.c)
        target_link_libraries(fuzz_build_packet rc_monitor_fuzz)
        target_compile_options(fuzz_build_packet PRIVATE -fsanitize=fuzzer,address,undefined)
        target_link_options(fuzz_build_packet PRIVATE -fsanitize=fuzzer,address,undefined)

        # gen_corpus — seed corpus generator (not a fuzzer, just a tool)
        add_executable(gen_corpus test/gen_corpus.c)
        target_link_libraries(gen_corpus rc_monitor_fuzz)
        target_compile_options(gen_corpus PRIVATE -fsanitize=address,undefined)
        target_link_options(gen_corpus PRIVATE -fsanitize=address,undefined)

        # Convenience target: generate seed corpus
        add_custom_target(gen-corpus
            COMMAND gen_corpus
                ${CMAKE_BINARY_DIR}/corpus_feed
                ${CMAKE_BINARY_DIR}/corpus_payload
            DEPENDS gen_corpus
            COMMENT "Generating seed corpus for fuzzers"
        )

        # Convenience targets: run fuzzers with tuned flags
        add_custom_target(fuzz-feed
            COMMAND fuzz_feed
                ${CMAKE_BINARY_DIR}/corpus_feed
                -dict=${CMAKE_SOURCE_DIR}/test/fuzz.dict
                -max_len=8192
                -max_total_time=300
            DEPENDS fuzz_feed gen-corpus
            COMMENT "Running fuzz_feed"
        )

        add_custom_target(fuzz-payload
            COMMAND fuzz_payload
                ${CMAKE_BINARY_DIR}/corpus_payload
                -dict=${CMAKE_SOURCE_DIR}/test/fuzz.dict
                -max_len=32
                -max_total_time=300
            DEPENDS fuzz_payload gen-corpus
            COMMENT "Running fuzz_payload"
        )

        add_custom_target(fuzz-build-packet
            COMMAND fuzz_build_packet
                ${CMAKE_BINARY_DIR}/corpus_feed
                -dict=${CMAKE_SOURCE_DIR}/test/fuzz.dict
                -max_len=1450
                -max_total_time=300
            DEPENDS fuzz_build_packet gen-corpus
            COMMENT "Running fuzz_build_packet"
        )
    endif()
endif()
