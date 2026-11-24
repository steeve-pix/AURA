cmake_minimum_required(VERSION 3.20)

project(AuraCLI LANGUAGES CXX)

# A source build ID makes local updates visible even without a release-number bump.
find_program(AURA_STAMP_PYTHON NAMES python3 REQUIRED)
file(GLOB_RECURSE cli_stamp_inputs CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp" "${CMAKE_CURRENT_SOURCE_DIR}/*.mm"
        "${CMAKE_CURRENT_SOURCE_DIR}/*.py" "${CMAKE_CURRENT_SOURCE_DIR}/*.sh"
        "${CMAKE_CURRENT_SOURCE_DIR}/../python/*.py")
list(FILTER cli_stamp_inputs EXCLUDE REGEX "/(build[^/]*|cmake-build-[^/]*|__pycache__|\\.[^/]+)/")
list(APPEND cli_stamp_inputs "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt"
        "${CMAKE_CURRENT_SOURCE_DIR}/../include/aura/sim/ActionSpec.hpp")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${cli_stamp_inputs})
execute_process(COMMAND "${AURA_STAMP_PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/build_stamp.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/.." "${CMAKE_CURRENT_BINARY_DIR}/generated/CliBuildStamp.hpp"
        RESULT_VARIABLE stamp_result)
if (NOT stamp_result EQUAL 0)
    message(FATAL_ERROR "Could not generate the CLI source build ID")
endif ()
include_directories("${CMAKE_CURRENT_BINARY_DIR}/generated")

# The platform's terminal database supplies screen and cursor capabilities.
find_package(Curses REQUIRED)
add_executable(aura_cli app/main.cpp terminal/Terminal.cpp terminal/TerminalCapabilities.cpp screens/NNTraining.cpp screens/ControlScreen.cpp screens/ModelsScreen.cpp screens/TrainingSetup.cpp screens/EvaluationScreen.cpp screens/CriticScreen.cpp)
target_include_directories(aura_cli PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CURSES_INCLUDE_DIRS})
target_link_libraries(aura_cli PRIVATE ${CURSES_LIBRARIES})
target_compile_features(aura_cli PRIVATE cxx_std_20)
target_compile_definitions(aura_cli PRIVATE AURA_PROJECT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/..")
if (APPLE)
    enable_language(OBJCXX)
    target_sources(aura_cli PRIVATE platform/VoiceDictation.mm)
    set_source_files_properties(platform/VoiceDictation.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    set_property(TARGET aura_cli PROPERTY OBJCXX_STANDARD 20)
    target_link_libraries(aura_cli PRIVATE "-framework Foundation")
    add_executable(aura_dictation MACOSX_BUNDLE platform/VoiceHelper.mm platform/VoiceCapture.mm)
    target_include_directories(aura_dictation PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_features(aura_dictation PRIVATE cxx_std_20)
    set_source_files_properties(platform/VoiceHelper.mm platform/VoiceCapture.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    set_target_properties(aura_dictation PROPERTIES
            OUTPUT_NAME AuraDictation OBJCXX_STANDARD 20
            MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/platform/VoiceHelper.plist"
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    target_link_libraries(aura_dictation PRIVATE "-framework AppKit" "-framework AVFoundation" "-framework Speech")
    add_custom_command(TARGET aura_dictation POST_BUILD
            COMMAND /usr/bin/codesign --force --sign - --identifier dev.aura.cli.dictation "$<TARGET_BUNDLE_DIR:aura_dictation>")
    add_dependencies(aura_cli aura_dictation)
else ()
    target_sources(aura_cli PRIVATE platform/VoiceDictation.cpp)
endif ()
set_target_properties(aura_cli PROPERTIES
        OUTPUT_NAME aura-cli
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        CXX_EXTENSIONS OFF
)

include(CTest)
if (BUILD_TESTING)
    add_executable(cli_update_tests ../tests/cli/cli_update_tests.cpp)
    target_include_directories(cli_update_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_features(cli_update_tests PRIVATE cxx_std_20)
    target_compile_options(cli_update_tests PRIVATE -UNDEBUG)
    add_test(NAME cli_update COMMAND cli_update_tests)
    add_executable(critic_screen_tests ../tests/cli/critic_screen_tests.cpp screens/CriticScreen.cpp terminal/TerminalCapabilities.cpp)
    target_include_directories(critic_screen_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CURSES_INCLUDE_DIRS})
    target_link_libraries(critic_screen_tests PRIVATE ${CURSES_LIBRARIES})
    target_compile_features(critic_screen_tests PRIVATE cxx_std_20)
    target_compile_options(critic_screen_tests PRIVATE -UNDEBUG)
    add_test(NAME critic_screen COMMAND critic_screen_tests)
    add_executable(evaluation_screen_tests ../tests/cli/evaluation_screen_tests.cpp screens/EvaluationScreen.cpp terminal/TerminalCapabilities.cpp)
    target_include_directories(evaluation_screen_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CURSES_INCLUDE_DIRS})
    target_link_libraries(evaluation_screen_tests PRIVATE ${CURSES_LIBRARIES})
    target_compile_features(evaluation_screen_tests PRIVATE cxx_std_20)
    target_compile_options(evaluation_screen_tests PRIVATE -UNDEBUG)
    add_test(NAME evaluation_screen COMMAND evaluation_screen_tests)
    add_executable(models_screen_tests ../tests/cli/models_screen_tests.cpp screens/ModelsScreen.cpp screens/TrainingSetup.cpp terminal/TerminalCapabilities.cpp)
    target_include_directories(models_screen_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CURSES_INCLUDE_DIRS})
    target_link_libraries(models_screen_tests PRIVATE ${CURSES_LIBRARIES})
    target_compile_features(models_screen_tests PRIVATE cxx_std_20)
    target_compile_options(models_screen_tests PRIVATE -UNDEBUG)
    add_test(NAME models_screen COMMAND models_screen_tests)
    add_executable(research_screen_tests ../tests/cli/research_screen_tests.cpp terminal/TerminalCapabilities.cpp)
    target_include_directories(research_screen_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CURSES_INCLUDE_DIRS})
    target_link_libraries(research_screen_tests PRIVATE ${CURSES_LIBRARIES})
    target_compile_features(research_screen_tests PRIVATE cxx_std_20)
    target_compile_options(research_screen_tests PRIVATE -UNDEBUG)
    add_test(NAME research_screen COMMAND research_screen_tests)
    add_executable(nn_training_screen_tests ../tests/cli/nn_training_screen_tests.cpp
            screens/NNTraining.cpp terminal/TerminalCapabilities.cpp)
    target_include_directories(nn_training_screen_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CURSES_INCLUDE_DIRS})
    target_link_libraries(nn_training_screen_tests PRIVATE ${CURSES_LIBRARIES})
    target_compile_features(nn_training_screen_tests PRIVATE cxx_std_20)
    # Assertions also run when the CLI is built in Release mode.
    target_compile_options(nn_training_screen_tests PRIVATE -UNDEBUG)
    add_test(NAME nn_training_screen COMMAND nn_training_screen_tests)
    add_executable(control_screen_tests ../tests/cli/control_screen_tests.cpp
            screens/ControlScreen.cpp terminal/TerminalCapabilities.cpp)
    target_include_directories(control_screen_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CURSES_INCLUDE_DIRS})
    target_link_libraries(control_screen_tests PRIVATE ${CURSES_LIBRARIES})
    target_compile_features(control_screen_tests PRIVATE cxx_std_20)
    target_compile_options(control_screen_tests PRIVATE -UNDEBUG)
    add_test(NAME control_screen COMMAND control_screen_tests)
endif ()

if (BUILD_TESTING)
    # Exercise the real terminal handlers with memory-only fixtures; never run physics/training.
    get_target_property(shortcut_sources aura_cli SOURCES)
    list(REMOVE_ITEM shortcut_sources app/main.cpp)
    add_executable(cli_shortcut_driver ../tests/cli/cli_shortcut_driver.cpp ${shortcut_sources})
    target_include_directories(cli_shortcut_driver PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CURSES_INCLUDE_DIRS})
    target_link_libraries(cli_shortcut_driver PRIVATE ${CURSES_LIBRARIES})
    target_compile_features(cli_shortcut_driver PRIVATE cxx_std_20)
    target_compile_definitions(cli_shortcut_driver PRIVATE AURA_PROJECT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/..")
    if (APPLE)
        set_property(TARGET cli_shortcut_driver PROPERTY OBJCXX_STANDARD 20)
        target_link_libraries(cli_shortcut_driver PRIVATE "-framework Foundation")
    endif ()
    find_program(AURA_TEST_PYTHON python3)
    if (AURA_TEST_PYTHON)
        add_test(NAME terminal_shortcuts COMMAND ${AURA_TEST_PYTHON} ${CMAKE_CURRENT_SOURCE_DIR}/../tests/cli/cli_shortcuts_smoke.py)
        set_tests_properties(terminal_shortcuts PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/.. TIMEOUT 90)
    endif ()
endif ()

# Organization labels do not exclude historical tests from the full suite.
foreach(aura_test IN ITEMS
    cli_update critic_screen evaluation_screen models_screen research_screen
    nn_training_screen control_screen terminal_shortcuts
)
    if(TEST ${aura_test})
        set_property(TEST ${aura_test} APPEND PROPERTY LABELS "current;cli")
    endif()
endforeach()
