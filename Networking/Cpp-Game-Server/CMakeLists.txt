cmake_minimum_required(VERSION 3.16)
project(cpp_game_server)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# DeterministicFloat: build as a reusable library for all demos/servers.
add_library(deterministic_float STATIC
        DeterministicFloat/deterministic_float.cpp
        DeterministicFloat/glacier_float.cpp
)

target_include_directories(deterministic_float PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/DeterministicFloat
)

add_library(cpp_game_server INTERFACE)

# cpp_game_server 的头文件路径
target_include_directories(cpp_game_server INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/include
        ${CMAKE_CURRENT_SOURCE_DIR}/NetCommon
)

target_compile_definitions(cpp_game_server INTERFACE ASIO_STANDALONE)

if (WIN32)
    target_link_libraries(cpp_game_server INTERFACE ws2_32 mswsock iphlpapi)
endif()

# 另一个可执行程序 simple_client
add_executable(simple_client
        Examples/NetClient/SimpleClients.cpp
)

target_include_directories(simple_client PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/include
        ${CMAKE_CURRENT_SOURCE_DIR}/NetCommon
)

target_compile_definitions(simple_client PRIVATE ASIO_STANDALONE)

if (WIN32)
    target_link_libraries(simple_client PRIVATE ws2_32 mswsock iphlpapi)
endif()

target_link_libraries(simple_client PRIVATE deterministic_float)

# 另一个可执行程序 simple_server
add_executable(simple_server
        Examples/NetServer/SimpleServer.cpp
)

target_include_directories(simple_server PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/include
        ${CMAKE_CURRENT_SOURCE_DIR}/NetCommon
)

target_compile_definitions(simple_server PRIVATE ASIO_STANDALONE)

if (WIN32)
    target_link_libraries(simple_server PRIVATE ws2_32 mswsock iphlpapi)
endif()

target_link_libraries(simple_server PRIVATE deterministic_float)

# 另一个可执行程序 CircleVsRect
add_executable(CircleVsRect
        Examples/Games/CircleVsRect.cpp
        Examples/Games/olcPixelGameEngine.h
        Examples/Games/olcPGEX_TransformedView.h
        Examples/Games/MMO_common.h
)

target_include_directories(CircleVsRect PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/include
        ${CMAKE_CURRENT_SOURCE_DIR}/NetCommon
)

target_link_libraries(CircleVsRect PRIVATE
        ws2_32
        mswsock
        iphlpapi

        opengl32
        gdiplus
        dwmapi
        shlwapi
)

target_compile_definitions(CircleVsRect PRIVATE ASIO_STANDALONE)

if (WIN32)
    target_link_libraries(CircleVsRect PRIVATE ws2_32 mswsock iphlpapi)
endif()

target_link_libraries(CircleVsRect PRIVATE deterministic_float)

# 另一个可执行程序 MMO_server
add_executable(MMO_server
        Examples/Games/MMO_server.cpp
        Examples/Games/olcPixelGameEngine.h
        Examples/Games/olcPGEX_TransformedView.h
        Examples/Games/MMO_common.h
)

target_include_directories(MMO_server PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/include
        ${CMAKE_CURRENT_SOURCE_DIR}/NetCommon
)

target_link_libraries(MMO_server PRIVATE
        ws2_32
        mswsock
        iphlpapi

        opengl32
        gdiplus
        dwmapi
        shlwapi
)

target_compile_definitions(MMO_server PRIVATE ASIO_STANDALONE)

if (WIN32)
    target_link_libraries(MMO_server PRIVATE ws2_32 mswsock iphlpapi)
endif()

target_link_libraries(MMO_server PRIVATE deterministic_float)

# 另一个可执行程序 Unity_server
add_executable(Unity_server
        Examples/UnityConnection/Unity_server.cpp
        Examples/UnityConnection/Unity_common.h
)

target_include_directories(Unity_server PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/include
        ${CMAKE_CURRENT_SOURCE_DIR}/NetCommon
        Examples/Games
)

target_link_libraries(Unity_server PRIVATE
        ws2_32
        mswsock
        iphlpapi

        opengl32
        gdiplus
        dwmapi
        shlwapi
)

target_compile_definitions(Unity_server PRIVATE ASIO_STANDALONE)

if (WIN32)
    target_link_libraries(Unity_server PRIVATE ws2_32 mswsock iphlpapi)
endif()

target_link_libraries(Unity_server PRIVATE deterministic_float)

if (MINGW)
    target_link_options(Unity_server PRIVATE
            -static
            -static-libgcc
            -static-libstdc++
    )
endif()
