add_executable(aura_tests
main.cpp
)

target_link_libraries(aura_tests
        PRIVATE
        aura_core
)