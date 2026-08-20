cmake_minimum_required(VERSION 3.20)
project(focused-volume-app-resolver LANGUAGES CXX)

find_package(Qt6 REQUIRED COMPONENTS Core Gui)
find_package(KF6Service REQUIRED)
find_package(X11 REQUIRED)

add_executable(focused-volume-app-resolver src/main.cpp)
target_compile_features(focused-volume-app-resolver PRIVATE cxx_std_20)
target_link_libraries(focused-volume-app-resolver PRIVATE Qt6::Core Qt6::Gui KF6::Service X11::X11)

install(TARGETS focused-volume-app-resolver RUNTIME DESTINATION bin)
