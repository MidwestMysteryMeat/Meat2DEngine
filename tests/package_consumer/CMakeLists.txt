cmake_minimum_required(VERSION 3.24)

project(Meat2DPackageConsumer LANGUAGES CXX)

find_package(Meat2D CONFIG REQUIRED)

add_executable(meat2d_package_consumer main.cpp)
target_compile_features(meat2d_package_consumer PRIVATE cxx_std_20)
target_link_libraries(meat2d_package_consumer PRIVATE Meat2D::Core)
