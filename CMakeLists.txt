cmake_minimum_required(VERSION 4.0)
project(IF_Physics)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Adds the Physics3D library
add_subdirectory(Physics3D)

add_executable(IF_Physics
        # 主程序源文件
        main.cpp
        Shader.cpp
        Shader.h
        Mesh.cpp
        Mesh.h
        Model.cpp
        Model.h
        DrawObjects.cpp
        DrawObjects.h
        Camera.h

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

target_include_directories(IF_Physics PRIVATE
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
target_link_libraries(IF_Physics PRIVATE
        Physics3D
        # 第三方库
        ${PROJECT_SOURCE_DIR}/dependencies/lib/libassimpd.dll.a
        ${PROJECT_SOURCE_DIR}/dependencies/lib/libglfw3.a

        # 系统库
        opengl32
)

if(MSVC)
    # Visual Studio 编译器选项
    target_compile_options(IF_Physics PRIVATE /W4)
else()
    # GCC/Clang 编译器选项
    target_compile_options(IF_Physics PRIVATE -Wall -Wextra -Wpedantic)
endif()

message(STATUS "==== IF_Physics Configuration ====")
message(STATUS "Project: ${PROJECT_NAME}")
message(STATUS "CMake Version: ${CMAKE_VERSION}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Source Dir: ${PROJECT_SOURCE_DIR}")
message(STATUS "Binary Dir: ${PROJECT_BINARY_DIR}")
message(STATUS "===================================")
