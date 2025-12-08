add_executable(
        z3_schedule
        z3_schedule.cpp
)

target_link_libraries(
        z3_schedule
        PRIVATE
        time_tools
        libz3
)

target_include_directories(
        z3_schedule
        PRIVATE
        "${Z3_SOURCE_DIR}/src/api/c++"
)

add_executable(
        z3Test
        z3Test.cpp
)

target_link_libraries(
        z3Test
        PRIVATE
        time_tools
        libz3
)

target_include_directories(
        z3Test
        PRIVATE
        "${Z3_SOURCE_DIR}/src/api/c++"
)
