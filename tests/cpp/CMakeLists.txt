add_executable(aura_tests
        main.cpp
)

target_include_directories(aura_tests
        PRIVATE
        ${CMAKE_SOURCE_DIR}/body/include
)

target_link_libraries(aura_tests
        PRIVATE
        aura_core
)

add_test(
        NAME aura_core_tests
        COMMAND aura_tests
)