set(AGENT_DEVICE_TYPE "unknown" CACHE STRING "Device type to build docker_scheduler_agent for")

if (${AGENT_DEVICE_TYPE} STREQUAL "unknown")
    message(WARNING "If you are building docker_scheduler_agent, please specify the AGENT_DEVICE_TYPE variable by -DAGENT_DEVICE_TYPE=<device type>")
endif ()

if (WIN32)
    # Windows 开发/调试场景下不编译依赖厂商 SDK 的采集实现，统一走 unknown stub。
    set(ARCH "unknown")
else ()
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
        spdlog::spdlog
)

if (${ARCH} STREQUAL "atlas200")
    if (NOT WIN32)
        target_link_directories(docker_scheduler_agent
                PRIVATE
                /usr/local/Ascend/driver/lib64
        )
        target_link_libraries(docker_scheduler_agent
                PRIVATE
                drvdsmi
        )
    endif ()
endif ()

if (${ARCH} STREQUAL "atlas310")
    if (NOT WIN32)
        target_link_directories(docker_scheduler_agent
                PRIVATE
                /usr/local/Ascend/driver/lib64
        )
        target_link_libraries(docker_scheduler_agent
                PRIVATE
                dcmi
        )
    endif ()
endif ()

if (${AGENT_DEVICE_TYPE} STREQUAL "ATLAS_L")
    # static link
    if (NOT MSVC)
        target_link_options(docker_scheduler_agent PRIVATE -static-libgcc -static-libstdc++)
    endif ()
endif ()

if (${AGENT_DEVICE_TYPE} STREQUAL "ATLAS_I")
    # static link
    if (NOT MSVC)
        target_link_options(docker_scheduler_agent PRIVATE -static-libgcc -static-libstdc++)
    endif ()
endif ()
