cmake_minimum_required(VERSION 3.21)
project(OpenDesigner VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets)
find_package(OpenCASCADE REQUIRED)
add_executable(OpenDesigner
    src/main.cpp
    src/MainWindow.h src/MainWindow.cpp
    src/CadViewport.h src/CadViewport.cpp
)
target_include_directories(OpenDesigner PRIVATE ${OpenCASCADE_INCLUDE_DIR})
target_link_libraries(OpenDesigner PRIVATE Qt6::Widgets ${OpenCASCADE_LIBRARIES})
if(WIN32)
    set_target_properties(OpenDesigner PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
install(TARGETS OpenDesigner RUNTIME DESTINATION bin)
