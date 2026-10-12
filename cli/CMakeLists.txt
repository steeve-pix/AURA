cmake_minimum_required(VERSION 3.20)

project(AuraCLI LANGUAGES CXX)

# The platform's terminal database supplies screen and cursor capabilities.
find_package(Curses REQUIRED)
add_executable(aura_cli main.cpp Terminal.cpp TerminalCapabilities.cpp)
target_include_directories(aura_cli PRIVATE ${CURSES_INCLUDE_DIRS})
target_link_libraries(aura_cli PRIVATE ${CURSES_LIBRARIES})
target_compile_features(aura_cli PRIVATE cxx_std_20)
target_compile_definitions(aura_cli PRIVATE AURA_PROJECT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/..")
if(APPLE)
    enable_language(OBJCXX)
    target_sources(aura_cli PRIVATE VoiceDictation.mm)
    set_source_files_properties(VoiceDictation.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    set_property(TARGET aura_cli PROPERTY OBJCXX_STANDARD 20)
    target_link_libraries(aura_cli PRIVATE "-framework Foundation")
    add_executable(aura_dictation MACOSX_BUNDLE VoiceHelper.mm VoiceCapture.mm)
    target_compile_features(aura_dictation PRIVATE cxx_std_20)
    set_source_files_properties(VoiceHelper.mm VoiceCapture.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    set_target_properties(aura_dictation PROPERTIES
        OUTPUT_NAME AuraDictation OBJCXX_STANDARD 20
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/VoiceHelper.plist"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    target_link_libraries(aura_dictation PRIVATE "-framework AppKit" "-framework AVFoundation" "-framework Speech")
    add_custom_command(TARGET aura_dictation POST_BUILD
        COMMAND /usr/bin/codesign --force --sign - --identifier dev.aura.cli.dictation "$<TARGET_BUNDLE_DIR:aura_dictation>")
    add_dependencies(aura_cli aura_dictation)
else()
    target_sources(aura_cli PRIVATE VoiceDictation.cpp)
endif()
set_target_properties(aura_cli PROPERTIES
    OUTPUT_NAME aura-cli
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    CXX_EXTENSIONS OFF
)
