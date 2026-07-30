cmake_minimum_required(VERSION 3.24)

project(
    {{PROJECT_IDENTIFIER}}
    VERSION 0.1.0
    DESCRIPTION "{{PROJECT_NAME}}, built with Meat2D Engine"
    LANGUAGES CXX
)

include(FetchContent)
include(GNUInstallDirs)

set(MEAT2D_ENGINE_SOURCE "" CACHE PATH "Optional local Meat2D Engine checkout")

set(MEAT2D_BUILD_CLIENT OFF CACHE BOOL "" FORCE)
set(MEAT2D_BUILD_SERVER OFF CACHE BOOL "" FORCE)
set(MEAT2D_BUILD_REMOTE_CLIENT OFF CACHE BOOL "" FORCE)
set(MEAT2D_BUILD_DIRECTORY OFF CACHE BOOL "" FORCE)
set(MEAT2D_BUILD_LAUNCHER OFF CACHE BOOL "" FORCE)
set(MEAT2D_BUILD_CLI OFF CACHE BOOL "" FORCE)
set(MEAT2D_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(MEAT2D_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(MEAT2D_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)

if(MEAT2D_ENGINE_SOURCE)
    set(meat2d_SOURCE_DIR "${MEAT2D_ENGINE_SOURCE}")
    add_subdirectory("${MEAT2D_ENGINE_SOURCE}" meat2d-engine EXCLUDE_FROM_ALL)
else()
    FetchContent_Declare(
        meat2d
        GIT_REPOSITORY https://github.com/MidwestMysteryMeat/Meat2DEngine.git
        GIT_TAG {{ENGINE_GIT_TAG}}
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(meat2d)
endif()

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.4.10
    GIT_SHALLOW TRUE
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(SDL3)

add_executable({{PROJECT_SLUG}} src/main.cpp)
target_compile_features({{PROJECT_SLUG}} PRIVATE cxx_std_20)
target_link_libraries({{PROJECT_SLUG}} PRIVATE Meat2D::Net SDL3::SDL3)

if(MSVC)
    target_compile_options({{PROJECT_SLUG}} PRIVATE /W4 /permissive-)
else()
    target_compile_options({{PROJECT_SLUG}} PRIVATE -Wall -Wextra -Wpedantic)
    if(MINGW)
        target_link_options(
            {{PROJECT_SLUG}}
            PRIVATE -static -static-libgcc -static-libstdc++
        )
    endif()
endif()

install(TARGETS {{PROJECT_SLUG}} RUNTIME DESTINATION .)
install(FILES game.toml README.md LICENSE CREDITS.md DESTINATION .)
install(DIRECTORY assets/ DESTINATION assets)
install(
    FILES
        "${meat2d_SOURCE_DIR}/LICENSE"
    DESTINATION licenses
    RENAME Meat2DEngine-Apache-2.0.txt
)
install(
    FILES
        "${meat2d_SOURCE_DIR}/NOTICE"
    DESTINATION .
    RENAME MEAT2D_NOTICE
)
install(
    FILES
        "${sdl3_SOURCE_DIR}/LICENSE.txt"
    DESTINATION licenses
    RENAME SDL3-zlib.txt
)

set(CPACK_PACKAGE_NAME "{{PROJECT_SLUG}}")
set(CPACK_PACKAGE_VENDOR "{{PROJECT_NAME}} contributors")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_GENERATOR "ZIP;TGZ")
include(CPack)
