cmake_minimum_required(VERSION 3.21)
project(OpenDesigner VERSION 0.4.2 LANGUAGES CXX)
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

add_library(OpenDesignerCommandPanels STATIC src/CommandPanelPositioner.cpp src/CommandPanelPositioner.h)
target_include_directories(OpenDesignerCommandPanels PUBLIC src)
target_link_libraries(OpenDesignerCommandPanels PUBLIC Qt6::Widgets)

add_executable(OpenDesigner
    src/main.cpp
    src/MainWindow.h src/MainWindow.cpp
    src/CadViewport.h src/CadViewport.cpp src/BooleanOperations.cpp
        src/BRepReconstruction.cpp src/MoveFaceGeometry.cpp src/MoveFaceGeometry.h src/MoveFace.cpp
        src/FaceOperations.cpp src/FaceTransformGeometry.cpp src/FaceTransformGeometry.h
        src/SelectionContext.cpp src/NativeDocument.h
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
    TKLCAF
    TKXCAF
    TKBinXCAF
    TKG3d
    TKG2d
    TKMath
    TKernel
)
add_library(OpenDesignerConstruction STATIC src/ConstructionGeometry.cpp src/ConstructionGeometry.h src/SketchGeometry.cpp src/SketchGeometry.h src/SketchSnapping.cpp src/SketchSnapping.h src/SketchOverdraw.cpp src/SketchOverdraw.h src/SketchConstruction.cpp src/SketchConstruction.h src/SketchProjection.cpp src/SketchProjection.h)
target_include_directories(OpenDesignerConstruction PUBLIC src ${OpenCASCADE_INCLUDE_DIR})
target_link_libraries(OpenDesignerConstruction PUBLIC ${OPENDESIGNER_LIBRARIES})
add_library(OpenDesignerDeleteFaceGeometry STATIC src/DeleteFaceGeometry.cpp src/DeleteFaceGeometry.h
    src/PlanarFaceDeletion.cpp src/PlanarFaceDeletion.h)
target_include_directories(OpenDesignerDeleteFaceGeometry PUBLIC src ${OpenCASCADE_INCLUDE_DIR})
target_link_libraries(OpenDesignerDeleteFaceGeometry PUBLIC ${OPENDESIGNER_LIBRARIES})
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
    add_executable(OpenDesignerCommandPanelTests tests/CommandPanelPositionerTest.cpp)
    target_link_libraries(OpenDesignerCommandPanelTests PRIVATE OpenDesignerCommandPanels)
    add_test(NAME command-panel-positioning COMMAND OpenDesignerCommandPanelTests)
    add_test(NAME command-panel-positioning-hidpi COMMAND OpenDesignerCommandPanelTests)
    set_tests_properties(command-panel-positioning-hidpi PROPERTIES ENVIRONMENT "QT_SCALE_FACTOR=2")
    add_executable(OpenDesignerSketchProjectionTests tests/SketchProjectionTest.cpp src/NativeDocument.cpp)
    target_link_libraries(OpenDesignerSketchProjectionTests PRIVATE OpenDesignerConstruction)
    add_test(NAME sketch-projection-geometry COMMAND OpenDesignerSketchProjectionTests)
    add_executable(OpenDesignerSketchPointTests tests/SketchPointTest.cpp src/NativeDocument.cpp)
    target_link_libraries(OpenDesignerSketchPointTests PRIVATE OpenDesignerConstruction)
    add_test(NAME sketch-construction-points COMMAND OpenDesignerSketchPointTests)
    add_executable(OpenDesignerSketchConstructionTests tests/SketchConstructionTest.cpp src/NativeDocument.cpp)
    target_link_libraries(OpenDesignerSketchConstructionTests PRIVATE OpenDesignerConstruction)
    add_test(NAME sketch-construction-geometry COMMAND OpenDesignerSketchConstructionTests)
    add_executable(OpenDesignerOverdrawTests tests/SketchOverdrawTest.cpp src/NativeDocument.cpp)
    target_link_libraries(OpenDesignerOverdrawTests PRIVATE OpenDesignerConstruction)
    add_test(NAME sketch-overdraw-geometry COMMAND OpenDesignerOverdrawTests)
    add_executable(OpenDesignerSketchSnappingTests tests/SketchSnappingTest.cpp)
    target_link_libraries(OpenDesignerSketchSnappingTests PRIVATE OpenDesignerConstruction)
    add_test(NAME sketch-snapping COMMAND OpenDesignerSketchSnappingTests)
    add_executable(OpenDesignerSketchTests tests/SketchGeometryTest.cpp src/NativeDocument.cpp)
    target_link_libraries(OpenDesignerSketchTests PRIVATE OpenDesignerConstruction)
    add_test(NAME sketch-geometry-storage COMMAND OpenDesignerSketchTests)
    set_tests_properties(sketch-geometry-storage PROPERTIES TIMEOUT 60)
    add_executable(OpenDesignerConstructionTests tests/ConstructionGeometryTest.cpp)
    target_link_libraries(OpenDesignerConstructionTests PRIVATE OpenDesignerConstruction)
    add_test(NAME construction-geometry COMMAND OpenDesignerConstructionTests)
    add_executable(OpenDesignerNativeDocumentTests tests/NativeDocumentTest.cpp src/NativeDocument.cpp)
    target_include_directories(OpenDesignerNativeDocumentTests PRIVATE src ${OpenCASCADE_INCLUDE_DIR})
    target_link_libraries(OpenDesignerNativeDocumentTests PRIVATE OpenDesignerConstruction)
    add_test(NAME native-document-roundtrip COMMAND OpenDesignerNativeDocumentTests)
    set_tests_properties(native-document-roundtrip PROPERTIES TIMEOUT 60)

    add_executable(OpenDesignerPullTests
        tests/CadViewportPullTest.cpp
        src/CadViewport.h src/CadViewport.cpp src/BooleanOperations.cpp
    src/BRepReconstruction.cpp src/MoveFaceGeometry.cpp src/MoveFaceGeometry.h src/MoveFace.cpp src/FaceOperations.cpp src/FaceTransformGeometry.cpp src/FaceTransformGeometry.h src/SelectionContext.cpp
    )
    target_include_directories(OpenDesignerPullTests PRIVATE src ${OpenCASCADE_INCLUDE_DIR})
    target_link_libraries(OpenDesignerPullTests PRIVATE ${OPENDESIGNER_LIBRARIES})
    add_test(NAME primitive-pull COMMAND OpenDesignerPullTests)
    add_test(NAME cylindrical-face-pull COMMAND OpenDesignerPullTests --cylindrical-face)
    add_test(NAME native-document-pull COMMAND OpenDesignerPullTests --native-pull)
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
    target_compile_definitions(OpenDesignerBooleanTests PRIVATE OPENDESIGNER_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
    target_link_libraries(OpenDesignerBooleanTests PRIVATE ${OPENDESIGNER_LIBRARIES})
    add_test(NAME boolean-operations COMMAND OpenDesignerBooleanTests)
    set_tests_properties(primitive-pull cylindrical-face-pull native-document-pull cone-pull gizmo-center rotation-pivot command-sessions PROPERTIES TIMEOUT 60)
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
        tests/SelectionContextTest.cpp tests/NativeDocumentSessionTest.cpp tests/ConstructionSessionTest.cpp tests/SketchSessionTest.cpp tests/SketchDrawingTest.cpp tests/SketchOverdrawSessionTest.cpp tests/SketchConstructionSessionTest.cpp tests/SketchReleaseSessionTest.cpp tests/SketchPointSessionTest.cpp tests/CommandPanelSessionTest.cpp src/MainWindow.h src/MainWindow.cpp
        src/CadViewport.h src/CadViewport.cpp src/BooleanOperations.cpp
        src/MoveFace.cpp src/FaceOperations.cpp src/FaceTransformGeometry.cpp
        src/FaceTransformGeometry.h src/SelectionContext.cpp src/MoveFaceGeometry.h src/BRepReconstruction.cpp src/MoveFaceGeometry.cpp)
    target_include_directories(OpenDesignerSelectionContextTests PRIVATE src ${OpenCASCADE_INCLUDE_DIR})
    target_link_libraries(OpenDesignerSelectionContextTests PRIVATE ${OPENDESIGNER_LIBRARIES})
    add_test(NAME selection-context COMMAND OpenDesignerSelectionContextTests)
    add_test(NAME native-document-sessions COMMAND OpenDesignerSelectionContextTests --documents)
    add_test(NAME native-partless-documents COMMAND OpenDesignerSelectionContextTests --partless)
    set_tests_properties(native-partless-documents PROPERTIES TIMEOUT 120)
    add_test(NAME sketch-sessions COMMAND OpenDesignerSelectionContextTests --sketch)
    add_test(NAME sketch-point-sessions COMMAND OpenDesignerSelectionContextTests --sketch-points)
    add_test(NAME command-panel-sessions COMMAND OpenDesignerSelectionContextTests --command-panels)
    set_tests_properties(command-panel-sessions PROPERTIES TIMEOUT 120)
    set_tests_properties(sketch-point-sessions PROPERTIES TIMEOUT 120)
    add_test(NAME sketch-drawing COMMAND OpenDesignerSelectionContextTests --drawing)
    add_test(NAME sketch-arc-direction COMMAND OpenDesignerSelectionContextTests --arc-direction)
    add_test(NAME sketch-overdraw-traversal COMMAND OpenDesignerSelectionContextTests --overdraw-traversal)
    set_tests_properties(sketch-arc-direction sketch-overdraw-traversal PROPERTIES TIMEOUT 120)
    add_test(NAME sketch-overdraw-sessions COMMAND OpenDesignerSelectionContextTests --overdraw)
    add_test(NAME sketch-construction-sessions COMMAND OpenDesignerSelectionContextTests --sketch-construction)
    add_test(NAME sketch-axes-workplane-presets COMMAND OpenDesignerSelectionContextTests --sketch-release)
    set_tests_properties(sketch-axes-workplane-presets PROPERTIES TIMEOUT 120)
    set_tests_properties(sketch-construction-sessions PROPERTIES TIMEOUT 120)
    set_tests_properties(sketch-overdraw-sessions PROPERTIES TIMEOUT 120)
    set_tests_properties(sketch-drawing PROPERTIES TIMEOUT 120)
    set_tests_properties(sketch-sessions PROPERTIES TIMEOUT 120)
    add_test(NAME construction-sessions COMMAND OpenDesignerSelectionContextTests --construction)
    set_tests_properties(construction-sessions PROPERTIES TIMEOUT 120)
    set_tests_properties(native-document-sessions PROPERTIES TIMEOUT 120)
    add_executable(OpenDesignerFaceTransformTests tests/FaceTransformTest.cpp src/FaceTransformGeometry.cpp
        src/BRepReconstruction.cpp src/MoveFaceGeometry.cpp)
    target_include_directories(OpenDesignerFaceTransformTests PRIVATE src ${OpenCASCADE_INCLUDE_DIR})
    target_compile_definitions(OpenDesignerFaceTransformTests PRIVATE OPENDESIGNER_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
    target_link_libraries(OpenDesignerFaceTransformTests PRIVATE ${OPENDESIGNER_LIBRARIES})
    add_test(NAME face-transform-geometry COMMAND OpenDesignerFaceTransformTests)
    set_tests_properties(boolean-operations move-face selection-context face-transform-geometry PROPERTIES TIMEOUT 120)
endif()

if(OPENDESIGNER_BUILD_TESTS)
    add_executable(OpenDesignerDeleteFaceTests
        tests/DeleteFaceTest.cpp src/MainWindow.cpp src/CadViewport.cpp src/BooleanOperations.cpp
        src/BRepReconstruction.cpp src/MoveFaceGeometry.cpp src/MoveFace.cpp
        src/FaceOperations.cpp src/FaceTransformGeometry.cpp src/SelectionContext.cpp)
    target_include_directories(OpenDesignerDeleteFaceTests PRIVATE src ${OpenCASCADE_INCLUDE_DIR})
    target_compile_definitions(OpenDesignerDeleteFaceTests PRIVATE OPENDESIGNER_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
    target_link_libraries(OpenDesignerDeleteFaceTests PRIVATE ${OPENDESIGNER_LIBRARIES})
    add_test(NAME delete-face-geometry COMMAND OpenDesignerDeleteFaceTests --geometry)
    add_test(NAME delete-face-session COMMAND OpenDesignerDeleteFaceTests)
    set_tests_properties(delete-face-geometry delete-face-session PROPERTIES TIMEOUT 120)
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
foreach(viewport_target OpenDesigner OpenDesignerPullTests OpenDesignerBooleanTests OpenDesignerMoveFaceTests OpenDesignerSelectionContextTests OpenDesignerDebugModelTests OpenDesignerDeleteFaceTests)
    if(TARGET ${viewport_target})
        target_sources(${viewport_target} PRIVATE src/DebugExport.cpp src/DeleteFace.cpp src/ModelingErrors.cpp src/NativeDocument.cpp src/ConstructionTools.cpp src/ConstructionPanel.cpp src/SketchTools.cpp src/SketchDrawing.cpp src/SketchDrawingPanel.cpp src/SketchOverdrawTools.cpp src/SketchOverdrawPanel.cpp src/SketchConstructionTools.cpp src/SketchConstructionPanel.cpp)
        target_link_libraries(${viewport_target} PRIVATE OpenDesignerDeleteFaceGeometry OpenDesignerDebug OpenDesignerConstruction OpenDesignerCommandPanels)
    endif()
endforeach()
install(TARGETS OpenDesignerDebugModel RUNTIME DESTINATION bin)
