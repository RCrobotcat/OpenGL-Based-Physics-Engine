cmake_minimum_required(VERSION 4.0)
project(IF_Physics)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Adds the Physics3D library
add_subdirectory(Physics3D)

# Bring in networking library target: cpp_game_server
option(IF_PHYSICS_ENABLE_CPP_GAME_SERVER "Enable Cpp-Game-Server integration" ON)
set(CPP_GAME_SERVER_DIR "${PROJECT_SOURCE_DIR}/Networking/Cpp-Game-Server")
if(IF_PHYSICS_ENABLE_CPP_GAME_SERVER AND EXISTS "${CPP_GAME_SERVER_DIR}/CMakeLists.txt")
    add_subdirectory(${CPP_GAME_SERVER_DIR} EXCLUDE_FROM_ALL)
elseif(IF_PHYSICS_ENABLE_CPP_GAME_SERVER)
    message(WARNING "Cpp-Game-Server not found at: ${CPP_GAME_SERVER_DIR}")
endif()

add_executable(IF_Physics_Offline
        # 主程序源文件
        demo_offline.cpp
        Common/Shader.cpp
        Common/Shader.h
        Common/Mesh.cpp
        Common/Mesh.h
        Common/Model.cpp
        Common/Model.h
        Common/DrawObjects.cpp
        Common/DrawObjects.h
        Common/Camera.h

        # GLAD
        dependencies/src/glad.c

        # ImGui 核心文件
        dependencies/include/imgui/cpp/imgui.cpp
        dependencies/include/imgui/cpp/imgui_draw.cpp
        dependencies/include/imgui/cpp/imgui_widgets.cpp
        dependencies/include/imgui/cpp/imgui_tables.cpp
        dependencies/include/imgui/cpp/imgui_demo.cpp

        # ImGui 后端
        dependencies/include/imgui/cpp/imgui_impl_glfw.cpp
        dependencies/include/imgui/cpp/imgui_impl_opengl3.cpp
)

add_executable(IF_Physics_Online
        demo_online.cpp
        Common/Shader.cpp
        Common/Shader.h
        Common/Mesh.cpp
        Common/Mesh.h
        Common/Model.cpp
        Common/Model.h
        Common/DrawObjects.cpp
        Common/DrawObjects.h
        Common/Camera.h

        dependencies/src/glad.c

        dependencies/include/imgui/cpp/imgui.cpp
        dependencies/include/imgui/cpp/imgui_draw.cpp
        dependencies/include/imgui/cpp/imgui_widgets.cpp
        dependencies/include/imgui/cpp/imgui_tables.cpp
        dependencies/include/imgui/cpp/imgui_demo.cpp

        dependencies/include/imgui/cpp/imgui_impl_glfw.cpp
        dependencies/include/imgui/cpp/imgui_impl_opengl3.cpp
)

target_include_directories(IF_Physics_Offline PRIVATE
        ${PROJECT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/dependencies/include
        ${PROJECT_SOURCE_DIR}/dependencies/include/assimp
        ${PROJECT_SOURCE_DIR}/dependencies/include/glad
        ${PROJECT_SOURCE_DIR}/dependencies/include/GLFW
        ${PROJECT_SOURCE_DIR}/dependencies/include/glm
        ${PROJECT_SOURCE_DIR}/dependencies/include/imgui
        ${PROJECT_SOURCE_DIR}/dependencies/include/imgui/cpp
        ${PROJECT_SOURCE_DIR}/dependencies/include/KHR
        ${PROJECT_SOURCE_DIR}/dependencies/ode/include
)

target_include_directories(IF_Physics_Online PRIVATE
        ${PROJECT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/dependencies/include
        ${PROJECT_SOURCE_DIR}/dependencies/include/assimp
        ${PROJECT_SOURCE_DIR}/dependencies/include/glad
        ${PROJECT_SOURCE_DIR}/dependencies/include/GLFW
        ${PROJECT_SOURCE_DIR}/dependencies/include/glm
        ${PROJECT_SOURCE_DIR}/dependencies/include/imgui
        ${PROJECT_SOURCE_DIR}/dependencies/include/imgui/cpp
        ${PROJECT_SOURCE_DIR}/dependencies/include/KHR
        ${PROJECT_SOURCE_DIR}/dependencies/ode/include
)

# 库链接路径
link_directories(${PROJECT_SOURCE_DIR}/dependencies/lib)

# 链接库
target_link_libraries(IF_Physics_Offline PRIVATE
        Physics3D
        # 第三方库
        ${PROJECT_SOURCE_DIR}/dependencies/lib/libassimpd.dll.a
        ${PROJECT_SOURCE_DIR}/dependencies/lib/libglfw3.a

        # 系统库
        opengl32
)

target_link_libraries(IF_Physics_Online PRIVATE
        # 第三方库
        ${PROJECT_SOURCE_DIR}/dependencies/lib/libassimpd.dll.a
        ${PROJECT_SOURCE_DIR}/dependencies/lib/libglfw3.a

        # 系统库
        opengl32
)

if(TARGET cpp_game_server)
    target_link_libraries(IF_Physics_Offline PRIVATE cpp_game_server)
    target_link_libraries(IF_Physics_Online PRIVATE cpp_game_server)
elseif(IF_PHYSICS_ENABLE_CPP_GAME_SERVER)
    message(WARNING "Target cpp_game_server is unavailable, skip linking.")
endif()

if(TARGET deterministic_float)
    target_link_libraries(IF_Physics_Online PRIVATE deterministic_float)
endif()

option(IF_PHYSICS_ENABLE_PHYSICS_SERVER "Build PhysicsServer executable" ON)
if(IF_PHYSICS_ENABLE_PHYSICS_SERVER)
    if(TARGET cpp_game_server AND TARGET deterministic_float)
        add_executable(PhysicsServer
                PhysicsServer/PhysicsServer.cpp
                PhysicsServer/PhysicsCommon.h
        )

        target_include_directories(PhysicsServer PRIVATE
                ${PROJECT_SOURCE_DIR}
        )

        target_link_libraries(PhysicsServer PRIVATE
                Physics3D
                cpp_game_server
                deterministic_float
        )
    else()
        message(WARNING "PhysicsServer skipped: cpp_game_server or deterministic_float target is missing.")
    endif()
endif()

if(MSVC)
    # Visual Studio 编译器选项
    target_compile_options(IF_Physics PRIVATE /W4)
    target_compile_options(IF_Physics_Online PRIVATE /W4)
    if(TARGET PhysicsServer)
        target_compile_options(PhysicsServer PRIVATE /W4)
    endif()
else()
    # GCC/Clang 编译器选项
    target_compile_options(IF_Physics_Offline PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(IF_Physics_Online PRIVATE -Wall -Wextra -Wpedantic)
    if(TARGET PhysicsServer)
        target_compile_options(PhysicsServer PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

message(STATUS "==== IF_Physics Configuration ====")
message(STATUS "Project: ${PROJECT_NAME}")
message(STATUS "CMake Version: ${CMAKE_VERSION}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Source Dir: ${PROJECT_SOURCE_DIR}")
message(STATUS "Binary Dir: ${PROJECT_BINARY_DIR}")
message(STATUS "===================================")
