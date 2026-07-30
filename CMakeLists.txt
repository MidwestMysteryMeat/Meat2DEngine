cmake_minimum_required(VERSION 3.24)

project(
    Meat2DEngine
    VERSION 0.1.0
    DESCRIPTION "Deterministic simulation-first 2D engine"
    LANGUAGES CXX
)

include(GNUInstallDirs)
include(CTest)

option(MEAT2D_BUILD_CLIENT "Build the SDL3 interactive sandbox" ON)
option(MEAT2D_BUILD_SERVER "Build the headless simulation server" ON)
option(MEAT2D_BUILD_TESTS "Build unit and determinism tests" ON)
option(MEAT2D_BUILD_BENCHMARKS "Build simulation benchmarks" ON)
option(MEAT2D_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

add_library(
    meat2d_core
    src/ai/LivingSimulation.cpp
    src/life/OrganismField.cpp
    src/sim/Material.cpp
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

if(MEAT2D_BUILD_CLIENT)
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

    add_executable(meat2d_sandbox apps/sandbox/main.cpp)
    target_compile_features(meat2d_sandbox PRIVATE cxx_std_20)
    target_link_libraries(meat2d_sandbox PRIVATE Meat2D::Core SDL3::SDL3)
endif()

if(MEAT2D_BUILD_SERVER)
    add_executable(meat2d_server apps/server/main.cpp)
    target_compile_features(meat2d_server PRIVATE cxx_std_20)
    target_link_libraries(meat2d_server PRIVATE Meat2D::Core)
endif()

if(MEAT2D_BUILD_TESTS AND BUILD_TESTING)
    add_executable(meat2d_tests tests/simulation_tests.cpp)
    target_compile_features(meat2d_tests PRIVATE cxx_std_20)
    target_link_libraries(meat2d_tests PRIVATE Meat2D::Core)
    add_test(NAME meat2d_tests COMMAND meat2d_tests)
endif()

if(MEAT2D_BUILD_BENCHMARKS)
    add_executable(meat2d_benchmark benchmarks/simulation_benchmark.cpp)
    target_compile_features(meat2d_benchmark PRIVATE cxx_std_20)
    target_link_libraries(meat2d_benchmark PRIVATE Meat2D::Core)
endif()

install(
    TARGETS meat2d_core
    EXPORT Meat2DTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
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
