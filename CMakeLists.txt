cmake_minimum_required(VERSION 3.24)

project(
    Meat2DEngine
    VERSION 0.4.0
    DESCRIPTION "C++20 2D game engine with deterministic simulation and multiplayer"
    LANGUAGES CXX
)

include(GNUInstallDirs)
include(CTest)

option(MEAT2D_BUILD_CLIENT "Build the SDL3 interactive sandbox" ON)
option(MEAT2D_BUILD_SERVER "Build the headless simulation server" ON)
option(MEAT2D_BUILD_REMOTE_CLIENT "Build the headless multiplayer client" ON)
option(MEAT2D_BUILD_DIRECTORY "Build the self-hostable public server directory" ON)
option(MEAT2D_BUILD_REPLAY_INSPECTOR "Build the offline replay-verification tool" ON)
option(MEAT2D_BUILD_CLI "Build the project-management command-line tool" ON)
option(MEAT2D_BUILD_LAUNCHER "Build the graphical project editor and launcher" ON)
option(MEAT2D_BUILD_EXAMPLES "Build the example games" ON)
option(MEAT2D_BUILD_TESTS "Build unit and determinism tests" ON)
option(MEAT2D_BUILD_BENCHMARKS "Build simulation benchmarks" ON)
option(MEAT2D_ENABLE_INSTALL "Enable SDK/runtime installation and CPack" ON)
option(MEAT2D_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

set(MEAT2D_SANITIZER "" CACHE STRING "Sanitizer to enable: address, undefined, or thread")
set_property(CACHE MEAT2D_SANITIZER PROPERTY STRINGS "" address undefined thread)

if(MEAT2D_SANITIZER)
    if(MSVC)
        message(FATAL_ERROR "MEAT2D_SANITIZER requires a Clang or GCC toolchain")
    endif()
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        message(FATAL_ERROR "MEAT2D_SANITIZER requires GCC, Clang, or AppleClang")
    endif()
    if(NOT MEAT2D_SANITIZER MATCHES "^(address|undefined|thread)$")
        message(FATAL_ERROR "MEAT2D_SANITIZER must be empty, address, undefined, or thread")
    endif()
endif()

set(MEAT2D_RUNTIME_TARGETS)

add_library(
    meat2d_core
    STATIC
    src/assets/Animation.cpp
    src/assets/TileMap.cpp
    src/assets/SpriteSheet.cpp
    src/assets/TextureAtlas.cpp
    src/ai/Crowd.cpp
    src/ai/LearningAgent.cpp
    src/ai/LivingSimulation.cpp
    src/ai/NeuralNetwork.cpp
    src/input/Input.cpp
    src/life/OrganismField.cpp
    src/replay/Replay.cpp
    src/scene/Scene.cpp
    src/scene/SceneHistory.cpp
    src/scene/SceneSnapshot.cpp
    src/scene/SceneStack.cpp
    src/scene/Physics.cpp
    src/sim/ChunkStore.cpp
    src/sim/Material.cpp
    src/sim/Projectile.cpp
    src/sim/Scenario.cpp
    src/sim/World.cpp
)
add_library(Meat2D::Core ALIAS meat2d_core)
set_target_properties(meat2d_core PROPERTIES EXPORT_NAME Core)

target_compile_features(meat2d_core PUBLIC cxx_std_20)
target_include_directories(
    meat2d_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

if(MSVC)
    target_compile_options(meat2d_core PRIVATE /W4 /permissive- /Zc:__cplusplus)
    if(MEAT2D_WARNINGS_AS_ERRORS)
        target_compile_options(meat2d_core PRIVATE /WX)
    endif()
else()
    target_compile_options(
        meat2d_core
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wshadow
    )
    if(MEAT2D_WARNINGS_AS_ERRORS)
        target_compile_options(meat2d_core PRIVATE -Werror)
    endif()
endif()

add_library(
    meat2d_render
    STATIC
    src/render/Camera.cpp
    src/render/DebugDraw.cpp
    src/render/Particles.cpp
    src/render/SpriteBatch.cpp
    src/render/WorldView.cpp
)
add_library(Meat2D::Render ALIAS meat2d_render)
set_target_properties(meat2d_render PROPERTIES EXPORT_NAME Render)
target_compile_features(meat2d_render PUBLIC cxx_std_20)
target_include_directories(
    meat2d_render
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(meat2d_render PUBLIC Meat2D::Core)

if(MSVC)
    target_compile_options(meat2d_render PRIVATE /W4 /permissive- /Zc:__cplusplus)
    if(MEAT2D_WARNINGS_AS_ERRORS)
        target_compile_options(meat2d_render PRIVATE /WX)
    endif()
else()
    target_compile_options(
        meat2d_render
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wshadow
    )
    if(MEAT2D_WARNINGS_AS_ERRORS)
        target_compile_options(meat2d_render PRIVATE -Werror)
    endif()
endif()

add_library(
    meat2d_tools
    STATIC
    src/tools/Process.cpp
    src/tools/ProjectBrowser.cpp
    src/tools/ProjectManager.cpp
    src/tools/SceneEditor.cpp
)
add_library(Meat2D::Tools ALIAS meat2d_tools)
set_target_properties(meat2d_tools PROPERTIES EXPORT_NAME Tools)
target_compile_features(meat2d_tools PUBLIC cxx_std_20)
target_link_libraries(meat2d_tools PUBLIC Meat2D::Render)
target_include_directories(
    meat2d_tools
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_compile_definitions(
    meat2d_tools
    PRIVATE MEAT2D_SOURCE_TEMPLATE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/templates"
)

if(MSVC)
    target_compile_options(meat2d_tools PRIVATE /W4 /permissive- /Zc:__cplusplus)
    if(MEAT2D_WARNINGS_AS_ERRORS)
        target_compile_options(meat2d_tools PRIVATE /WX)
    endif()
else()
    target_compile_options(
        meat2d_tools
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wshadow
    )
    if(MEAT2D_WARNINGS_AS_ERRORS)
        target_compile_options(meat2d_tools PRIVATE -Werror)
    endif()
endif()

add_library(
    meat2d_net
    STATIC
    src/net/ChunkCodec.cpp
    src/net/Discovery.cpp
    src/net/Fragmentation.cpp
    src/net/PacketCodec.cpp
    src/net/Reliability.cpp
    src/net/Session.cpp
    src/net/UdpSocket.cpp
)
add_library(Meat2D::Net ALIAS meat2d_net)
set_target_properties(meat2d_net PROPERTIES EXPORT_NAME Net)
target_compile_features(meat2d_net PUBLIC cxx_std_20)
target_include_directories(
    meat2d_net
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(meat2d_net PUBLIC Meat2D::Core)
if(WIN32)
    target_link_libraries(meat2d_net PUBLIC ws2_32)
endif()

if(MSVC)
    target_compile_options(meat2d_net PRIVATE /W4 /permissive- /Zc:__cplusplus)
    if(MEAT2D_WARNINGS_AS_ERRORS)
        target_compile_options(meat2d_net PRIVATE /WX)
    endif()
else()
    target_compile_options(
        meat2d_net
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wshadow
    )
    if(MEAT2D_WARNINGS_AS_ERRORS)
        target_compile_options(meat2d_net PRIVATE -Werror)
    endif()
endif()

if(MEAT2D_BUILD_CLIENT OR MEAT2D_BUILD_LAUNCHER OR MEAT2D_BUILD_EXAMPLES)
    include(FetchContent)

    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
        SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-3.4.10
        GIT_SHALLOW TRUE
        EXCLUDE_FROM_ALL
    )
    FetchContent_MakeAvailable(SDL3)
endif()

if(MEAT2D_BUILD_CLIENT)
    add_executable(meat2d_sandbox apps/sandbox/main.cpp)
    target_compile_features(meat2d_sandbox PRIVATE cxx_std_20)
    target_link_libraries(meat2d_sandbox PRIVATE Meat2D::Net Meat2D::Render SDL3::SDL3)
    list(APPEND MEAT2D_RUNTIME_TARGETS meat2d_sandbox)
endif()

if(MEAT2D_BUILD_EXAMPLES)
    add_executable(meat2d_example_deep_dig examples/deep_dig/main.cpp)
    target_compile_features(meat2d_example_deep_dig PRIVATE cxx_std_20)
    target_link_libraries(meat2d_example_deep_dig PRIVATE Meat2D::Core Meat2D::Render SDL3::SDL3)
    list(APPEND MEAT2D_RUNTIME_TARGETS meat2d_example_deep_dig)
endif()

if(MEAT2D_BUILD_LAUNCHER)
    include(FetchContent)

    FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG v1.92.8
        GIT_SHALLOW TRUE
        EXCLUDE_FROM_ALL
    )
    FetchContent_MakeAvailable(imgui)

    add_library(
        meat2d_imgui STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_demo.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp
    )
    target_include_directories(
        meat2d_imgui
        PUBLIC
            ${imgui_SOURCE_DIR}
            ${imgui_SOURCE_DIR}/backends
    )
    target_compile_features(meat2d_imgui PUBLIC cxx_std_20)
    target_link_libraries(meat2d_imgui PUBLIC SDL3::SDL3)

    set(SDLIMAGE_INSTALL OFF CACHE BOOL "" FORCE)
    set(SDLIMAGE_SAMPLES OFF CACHE BOOL "" FORCE)
    set(SDLIMAGE_TESTS OFF CACHE BOOL "" FORCE)
    set(SDLIMAGE_SHARED OFF CACHE BOOL "" FORCE)
    set(SDLIMAGE_BACKEND_STB ON CACHE BOOL "" FORCE)
    set(SDLIMAGE_PNG_LIBPNG OFF CACHE BOOL "" FORCE)
    set(SDLIMAGE_AVIF OFF CACHE BOOL "" FORCE)
    set(SDLIMAGE_JXL OFF CACHE BOOL "" FORCE)
    set(SDLIMAGE_TIF OFF CACHE BOOL "" FORCE)
    set(SDLIMAGE_WEBP OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        SDL3_image
        GIT_REPOSITORY https://github.com/libsdl-org/SDL_image.git
        GIT_TAG release-3.4.4
        GIT_SHALLOW TRUE
        GIT_SUBMODULES ""
        GIT_SUBMODULES_RECURSE FALSE
        EXCLUDE_FROM_ALL
    )
    FetchContent_MakeAvailable(SDL3_image)

    add_executable(meat2d_launcher apps/launcher/main.cpp)
    target_compile_features(meat2d_launcher PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(meat2d_launcher PRIVATE /W4 /permissive- /Zc:__cplusplus)
        if(MEAT2D_WARNINGS_AS_ERRORS)
            target_compile_options(meat2d_launcher PRIVATE /WX)
        endif()
    else()
        target_compile_options(
            meat2d_launcher
            PRIVATE
                -Wall
                -Wextra
                -Wpedantic
                -Wconversion
                -Wshadow
        )
        if(MEAT2D_WARNINGS_AS_ERRORS)
            target_compile_options(meat2d_launcher PRIVATE -Werror)
        endif()
    endif()
    target_link_libraries(
        meat2d_launcher
        PRIVATE
            Meat2D::Net
            Meat2D::Tools
            meat2d_imgui
            SDL3_image::SDL3_image
    )
    list(APPEND MEAT2D_RUNTIME_TARGETS meat2d_launcher)
endif()

if(MEAT2D_BUILD_SERVER)
    add_executable(meat2d_server apps/server/main.cpp)
    target_compile_features(meat2d_server PRIVATE cxx_std_20)
    target_link_libraries(meat2d_server PRIVATE Meat2D::Net)
    list(APPEND MEAT2D_RUNTIME_TARGETS meat2d_server)
endif()

if(MEAT2D_BUILD_REMOTE_CLIENT)
    add_executable(meat2d_remote apps/remote/main.cpp)
    target_compile_features(meat2d_remote PRIVATE cxx_std_20)
    target_link_libraries(meat2d_remote PRIVATE Meat2D::Net)
    list(APPEND MEAT2D_RUNTIME_TARGETS meat2d_remote)
endif()

if(MEAT2D_BUILD_DIRECTORY)
    add_executable(meat2d_directory apps/directory/main.cpp)
    target_compile_features(meat2d_directory PRIVATE cxx_std_20)
    target_link_libraries(meat2d_directory PRIVATE Meat2D::Net)
    list(APPEND MEAT2D_RUNTIME_TARGETS meat2d_directory)
endif()

if(MEAT2D_BUILD_REPLAY_INSPECTOR)
    add_executable(meat2d_replay apps/replay/main.cpp)
    target_compile_features(meat2d_replay PRIVATE cxx_std_20)
    target_link_libraries(meat2d_replay PRIVATE Meat2D::Core)
    list(APPEND MEAT2D_RUNTIME_TARGETS meat2d_replay)
endif()

if(MEAT2D_BUILD_CLI)
    add_executable(meat2d_cli apps/cli/main.cpp)
    set_target_properties(meat2d_cli PROPERTIES OUTPUT_NAME meat2d)
    target_compile_features(meat2d_cli PRIVATE cxx_std_20)
    target_link_libraries(meat2d_cli PRIVATE Meat2D::Tools)
    list(APPEND MEAT2D_RUNTIME_TARGETS meat2d_cli)
endif()

if(MEAT2D_BUILD_TESTS AND BUILD_TESTING)
    add_executable(meat2d_tests tests/simulation_tests.cpp)
    target_compile_features(meat2d_tests PRIVATE cxx_std_20)
    target_link_libraries(meat2d_tests PRIVATE Meat2D::Net Meat2D::Render Meat2D::Tools)
    add_test(NAME meat2d_tests COMMAND meat2d_tests)
    set_tests_properties(meat2d_tests PROPERTIES TIMEOUT 120)
endif()

if(MEAT2D_BUILD_BENCHMARKS)
    add_executable(meat2d_benchmark benchmarks/simulation_benchmark.cpp)
    target_compile_features(meat2d_benchmark PRIVATE cxx_std_20)
    target_link_libraries(meat2d_benchmark PRIVATE Meat2D::Core)
    list(APPEND MEAT2D_RUNTIME_TARGETS meat2d_benchmark)
endif()

# Keep sanitizer flags scoped to Meat2D targets so fetched third-party projects
# are not rebuilt with the engine's diagnostic configuration.
if(MEAT2D_SANITIZER)
    foreach(candidate IN ITEMS
        meat2d_core
        meat2d_render
        meat2d_tools
        meat2d_net
        meat2d_imgui
        meat2d_sandbox
        meat2d_example_deep_dig
        meat2d_launcher
        meat2d_server
        meat2d_remote
        meat2d_directory
        meat2d_replay
        meat2d_cli
        meat2d_tests
        meat2d_benchmark
    )
        if(TARGET ${candidate})
            target_compile_options(
                ${candidate}
                PRIVATE
                    -fsanitize=${MEAT2D_SANITIZER}
                    -fno-omit-frame-pointer
            )
            get_target_property(candidate_type ${candidate} TYPE)
            if(NOT candidate_type STREQUAL "STATIC_LIBRARY")
                target_link_options(
                    ${candidate}
                    PRIVATE
                        -fsanitize=${MEAT2D_SANITIZER}
                )
            endif()
        endif()
    endforeach()
endif()

if(MINGW)
    foreach(runtime_target IN LISTS MEAT2D_RUNTIME_TARGETS)
        target_link_options(
            ${runtime_target}
            PRIVATE -static -static-libgcc -static-libstdc++
        )
    endforeach()
endif()

if(MEAT2D_ENABLE_INSTALL)
    install(
        TARGETS meat2d_core meat2d_net meat2d_render meat2d_tools
        EXPORT Meat2DTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
    if(MEAT2D_RUNTIME_TARGETS)
        install(
            TARGETS ${MEAT2D_RUNTIME_TARGETS}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        )
    endif()
    install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(
        DIRECTORY templates/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/Meat2D/templates
    )
    install(
        FILES LICENSE NOTICE
        DESTINATION ${CMAKE_INSTALL_DATADIR}/Meat2D
    )
    if(MEAT2D_BUILD_CLIENT OR MEAT2D_BUILD_LAUNCHER)
        install(
            FILES ${sdl3_SOURCE_DIR}/LICENSE.txt
            DESTINATION ${CMAKE_INSTALL_DATADIR}/Meat2D/licenses
            RENAME SDL3-LICENSE.txt
        )
    endif()
    if(MEAT2D_BUILD_LAUNCHER)
        install(
            FILES ${sdl3_image_SOURCE_DIR}/LICENSE.txt
            DESTINATION ${CMAKE_INSTALL_DATADIR}/Meat2D/licenses
            RENAME SDL3_image-LICENSE.txt
        )
        install(
            FILES ${imgui_SOURCE_DIR}/LICENSE.txt
            DESTINATION ${CMAKE_INSTALL_DATADIR}/Meat2D/licenses
            RENAME Dear-ImGui-LICENSE.txt
        )
    endif()
    install(
        EXPORT Meat2DTargets
        FILE Meat2DTargets.cmake
        NAMESPACE Meat2D::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Meat2D
    )

    include(CMakePackageConfigHelpers)
    configure_package_config_file(
        cmake/Meat2DConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/Meat2DConfig.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Meat2D
    )
    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/Meat2DConfigVersion.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
    )
    install(
        FILES
            ${CMAKE_CURRENT_BINARY_DIR}/Meat2DConfig.cmake
            ${CMAKE_CURRENT_BINARY_DIR}/Meat2DConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Meat2D
    )

    set(CPACK_PACKAGE_NAME "Meat2D-Engine")
    set(CPACK_PACKAGE_VENDOR "Midwest Mystery Meat")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
    set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
    set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
    set(CPACK_GENERATOR "ZIP;TGZ")
    include(CPack)
endif()
