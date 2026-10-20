cmake_minimum_required(VERSION 3.21)
project(OpenDesigner VERSION 0.3.6 LANGUAGES CXX)
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
find_package(LibArchive REQUIRED)

add_executable(OpenDesigner
    src/main.cpp
    src/MainWindow.h src/MainWindow.cpp
    src/CadViewport.h src/CadViewport.cpp src/BooleanOperations.cpp
        src/BRepReconstruction.cpp src/MoveFaceGeometry.cpp src/MoveFaceGeometry.h src/MoveFace.cpp
        src/FaceOperations.cpp src/FaceTransformGeometry.cpp src/FaceTransformGeometry.h
        src/SelectionContext.cpp
)
target_include_directories(OpenDesigner PRIVATE ${OpenCASCADE_INCLUDE_DIR})

# Do not link every OCCT toolkit (Draw, STEP, VTK, etc.) for a basic viewer.
# Use only the libraries required by our current modeling and display code.
set(OPENDESIGNER_LIBRARIES
    Qt6::Widgets
    TKOpenGl
    TKV3d
    TKService
    TKBRep
    TKPrim
    TKTopAlgo
    TKBO
    TKBool
    TKGeomBase
    TKGeomAlgo
    TKOffset
    TKFeat
    TKShHealing
    TKMesh
    TKG3d
    TKG2d
    TKMath
    TKernel
)
add_library(OpenDesignerDebug STATIC src/DebugModel.cpp src/DebugModel.h)
target_include_directories(OpenDesignerDebug PUBLIC src ${OpenCASCADE_INCLUDE_DIR})
target_link_libraries(OpenDesignerDebug PUBLIC ${OPENDESIGNER_LIBRARIES} LibArchive::LibArchive)
add_executable(OpenDesignerDebugModel tools/DebugModelInspector.cpp)
target_link_libraries(OpenDesignerDebugModel PRIVATE OpenDesignerDebug)
target_link_libraries(OpenDesigner PRIVATE ${OPENDESIGNER_LIBRARIES})
if(WIN32)
    set_target_properties(OpenDesigner PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
install(TARGETS OpenDesigner RUNTIME DESTINATION bin)

# These integration tests need an X11/OpenGL display (or xvfb-run on Linux).
option(OPENDESIGNER_BUILD_TESTS "Build viewport pull regression tests" OFF)
if(OPENDESIGNER_BUILD_TESTS)
    enable_testing()
    add_executable(OpenDesignerPullTests
        tests/CadViewportPullTest.cpp
        src/CadViewport.h src/CadViewport.cpp src/BooleanOperations.cpp
    src/BRepReconstruction.cpp src/MoveFaceGeometry.cpp src/MoveFaceGeometry.h src/MoveFace.cpp src/FaceOperations.cpp src/FaceTransformGeometry.cpp src/FaceTransformGeometry.h src/SelectionContext.cpp
    )
    target_include_directories(OpenDesignerPullTests PRIVATE src ${OpenCASCADE_INCLUDE_DIR})
    target_link_libraries(OpenDesignerPullTests PRIVATE ${OPENDESIGNER_LIBRARIES})
    add_test(NAME primitive-pull COMMAND OpenDesignerPullTests)
    add_test(NAME cone-pull COMMAND OpenDesignerPullTests --cone)
    add_test(NAME command-sessions COMMAND OpenDesignerPullTests --sessions)
    add_test(NAME rotation-pivot COMMAND OpenDesignerPullTests --rotation)
    add_test(NAME gizmo-center COMMAND OpenDesignerPullTests --gizmo)
    add_executable(OpenDesignerBooleanTests
        tests/BooleanOperationsTest.cpp
        src/MainWindow.h src/MainWindow.cpp
        src/CadViewport.h src/CadViewport.cpp src/BooleanOperations.cpp
        src/BRepReconstruction.cpp src/MoveFaceGeometry.cpp src/MoveFaceGeometry.h src/MoveFace.cpp
        src/FaceOperations.cpp src/FaceTransformGeometry.cpp src/FaceTransformGeometry.h
        src/SelectionContext.cpp
    )
    target_include_directories(OpenDesignerBooleanTests PRIVATE src ${OpenCASCADE_INCLUDE_DIR})
    target_link_libraries(OpenDesignerBooleanTests PRIVATE ${OPENDESIGNER_LIBRARIES})
    add_test(NAME boolean-operations COMMAND OpenDesignerBooleanTests)
    set_tests_properties(primitive-pull cone-pull gizmo-center rotation-pivot command-sessions PROPERTIES TIMEOUT 60)
    add_executable(OpenDesignerMoveFaceTests
        tests/MoveFaceTest.cpp src/MainWindow.h src/MainWindow.cpp
        src/CadViewport.h src/CadViewport.cpp src/BooleanOperations.cpp
        src/MoveFace.cpp src/FaceOperations.cpp src/FaceTransformGeometry.cpp
        src/FaceTransformGeometry.h src/SelectionContext.cpp src/MoveFaceGeometry.h src/BRepReconstruction.cpp src/MoveFaceGeometry.cpp)
    target_include_directories(OpenDesignerMoveFaceTests PRIVATE src ${OpenCASCADE_INCLUDE_DIR})
    target_compile_definitions(OpenDesignerMoveFaceTests PRIVATE OPENDESIGNER_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
    target_link_libraries(OpenDesignerMoveFaceTests PRIVATE ${OPENDESIGNER_LIBRARIES})
    add_test(NAME move-face COMMAND OpenDesignerMoveFaceTests)
    add_executable(OpenDesignerSelectionContextTests
        tests/SelectionContextTest.cpp src/MainWindow.h src/MainWindow.cpp
        src/CadViewport.h src/CadViewport.cpp src/BooleanOperations.cpp
        src/MoveFace.cpp src/FaceOperations.cpp src/FaceTransformGeometry.cpp
        src/FaceTransformGeometry.h src/SelectionContext.cpp src/MoveFaceGeometry.h src/BRepReconstruction.cpp src/MoveFaceGeometry.cpp)
    target_include_directories(OpenDesignerSelectionContextTests PRIVATE src ${OpenCASCADE_INCLUDE_DIR})
    target_link_libraries(OpenDesignerSelectionContextTests PRIVATE ${OPENDESIGNER_LIBRARIES})
    add_test(NAME selection-context COMMAND OpenDesignerSelectionContextTests)
    add_executable(OpenDesignerFaceTransformTests tests/FaceTransformTest.cpp src/FaceTransformGeometry.cpp
        src/BRepReconstruction.cpp src/MoveFaceGeometry.cpp)
    target_include_directories(OpenDesignerFaceTransformTests PRIVATE src ${OpenCASCADE_INCLUDE_DIR})
    target_compile_definitions(OpenDesignerFaceTransformTests PRIVATE OPENDESIGNER_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
    target_link_libraries(OpenDesignerFaceTransformTests PRIVATE ${OPENDESIGNER_LIBRARIES})
    add_test(NAME face-transform-geometry COMMAND OpenDesignerFaceTransformTests)
    set_tests_properties(boolean-operations move-face selection-context face-transform-geometry PROPERTIES TIMEOUT 120)
endif()

if(OPENDESIGNER_BUILD_TESTS)
    add_executable(OpenDesignerDebugModelTests tests/DebugModelTest.cpp
        src/MainWindow.cpp src/CadViewport.cpp src/BooleanOperations.cpp
        src/BRepReconstruction.cpp src/MoveFaceGeometry.cpp src/MoveFace.cpp
        src/FaceOperations.cpp src/FaceTransformGeometry.cpp src/SelectionContext.cpp)
    target_include_directories(OpenDesignerDebugModelTests PRIVATE src ${OpenCASCADE_INCLUDE_DIR})
    target_link_libraries(OpenDesignerDebugModelTests PRIVATE OpenDesignerDebug)
    add_dependencies(OpenDesignerDebugModelTests OpenDesignerDebugModel)
    add_test(NAME debug-model-export COMMAND OpenDesignerDebugModelTests)
    set_tests_properties(debug-model-export PROPERTIES TIMEOUT 120)
endif()

# Keep document snapshot capture linked wherever the viewport is exercised.
foreach(viewport_target OpenDesigner OpenDesignerPullTests OpenDesignerBooleanTests OpenDesignerMoveFaceTests OpenDesignerSelectionContextTests OpenDesignerDebugModelTests)
    if(TARGET ${viewport_target})
        target_sources(${viewport_target} PRIVATE src/DebugExport.cpp)
        target_link_libraries(${viewport_target} PRIVATE OpenDesignerDebug)
    endif()
endforeach()
install(TARGETS OpenDesignerDebugModel RUNTIME DESTINATION bin)
