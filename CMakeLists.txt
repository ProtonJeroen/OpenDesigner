cmake_minimum_required(VERSION 3.21)
project(OpenDesigner VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets)

# Some Fedora Open CASCADE packages export TKIVtk and TKIVtkDraw targets
# whose interfaces reference VTK::CommonCore and VTK::IOImage.
# Define those imported VTK targets before loading the OCCT config.
# Other OCCT distributions (e.g. Ubuntu) do not require VTK.
find_package(VTK QUIET COMPONENTS CommonCore IOImage)
find_package(OpenCASCADE REQUIRED)

add_executable(OpenDesigner
    src/main.cpp
    src/MainWindow.h src/MainWindow.cpp
    src/CadViewport.h src/CadViewport.cpp
)
target_include_directories(OpenDesigner PRIVATE ${OpenCASCADE_INCLUDE_DIR})

# Do not link every OCCT toolkit (Draw, STEP, VTK, etc.) for a basic viewer.
# Use only the libraries required by our current modeling and display code.
target_link_libraries(OpenDesigner PRIVATE
    Qt6::Widgets
    TKOpenGl
    TKV3d
    TKService
    TKBRep
    TKPrim
    TKTopAlgo
    TKGeomBase
    TKGeomAlgo
    TKG3d
    TKG2d
    TKMath
    TKernel
)
if(WIN32)
    set_target_properties(OpenDesigner PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
install(TARGETS OpenDesigner RUNTIME DESTINATION bin)
