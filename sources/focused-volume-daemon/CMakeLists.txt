cmake_minimum_required(VERSION 3.20)
project(focused-volume-daemon LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Core DBus)
find_package(KF6Service REQUIRED)
find_path(PULSE_INCLUDE_DIR pulse/pulseaudio.h REQUIRED)
find_library(PULSE_LIBRARY pulse REQUIRED)

add_executable(focused-volume-daemon src/main.cpp)
target_compile_features(focused-volume-daemon PRIVATE cxx_std_20)
target_include_directories(focused-volume-daemon PRIVATE ${PULSE_INCLUDE_DIR})
target_link_libraries(focused-volume-daemon PRIVATE Qt6::Core Qt6::DBus KF6::Service ${PULSE_LIBRARY})
install(TARGETS focused-volume-daemon RUNTIME DESTINATION bin)
