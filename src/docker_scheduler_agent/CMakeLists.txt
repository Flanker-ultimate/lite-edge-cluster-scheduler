set(AGENT_DEVICE_TYPE "unknown" CACHE STRING "Device type to build docker_scheduler_agent for")

if (${AGENT_DEVICE_TYPE} STREQUAL "unknown")
    message(WARNING "If you are building docker_scheduler_agent, please specify the AGENT_DEVICE_TYPE variable by -DAGENT_DEVICE_TYPE=<device type>")
endif ()

if (${AGENT_DEVICE_TYPE} STREQUAL "ATLAS_H")
    set(ARCH "atlas200")
elseif (${AGENT_DEVICE_TYPE} STREQUAL "ATLAS_L")
    set(ARCH "atlas200")
elseif (${AGENT_DEVICE_TYPE} STREQUAL "ATLAS_I")
    set(ARCH "atlas310")
elseif (${AGENT_DEVICE_TYPE} STREQUAL "RK3588")
    set(ARCH "rk3588")
else ()
    set(ARCH "unknown")
endif ()

configure_file(device_type.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/device_type.h @ONLY)

add_executable(docker_scheduler_agent
        main.cpp
        MachineInfoCollectorBase.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/arch/${ARCH}/MachineInfoCollector.cpp
)

target_include_directories(docker_scheduler_agent
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/arch/${ARCH}
        ${CMAKE_CURRENT_BINARY_DIR}/generated
)

target_link_libraries(docker_scheduler_agent
        PRIVATE
        httplib::httplib
        nlohmann_json::nlohmann_json
        device_struct
)

if (${ARCH} STREQUAL "atlas200")
    target_link_directories(docker_scheduler_agent
            PRIVATE
            /usr/local/Ascend/driver/lib64
    )
    target_link_libraries(docker_scheduler_agent
            PRIVATE
            drvdsmi
    )
endif ()

if (${ARCH} STREQUAL "atlas310")
    target_link_directories(docker_scheduler_agent
            PRIVATE
            /usr/local/Ascend/driver/lib64
    )
    target_link_libraries(docker_scheduler_agent
            PRIVATE
            dcmi
    )
endif ()

if (${AGENT_DEVICE_TYPE} STREQUAL "ATLAS_L")
    # static link
    target_link_options(docker_scheduler_agent PRIVATE -static-libgcc -static-libstdc++)
endif ()

if (${AGENT_DEVICE_TYPE} STREQUAL "ATLAS_I")
    # static link
    target_link_options(docker_scheduler_agent PRIVATE -static-libgcc -static-libstdc++)
endif ()
