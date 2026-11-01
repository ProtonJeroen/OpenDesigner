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
add_library(OpenDesignerConstruction STATIC src/SketchDisplayBounds.cpp src/SketchDisplayBounds.h src/ConstructionGeometry.cpp src/ConstructionGeometry.h src/SketchGeometry.cpp src/SketchGeometry.h src/SketchSnapping.cpp src/SketchSnapping.h src/SketchOverdraw.cpp src/SketchOverdraw.h src/SketchConstruction.cpp src/SketchConstruction.h src/SketchProjection.cpp src/SketchProjection.h)
target_sources(OpenDesignerConstruction PRIVATE src/SketchProfileValidation.cpp src/SketchProfileValidation.h
    src/SketchProfileBuilder.cpp src/SketchProfileBuilder.h
    src/SketchProfileExtrusion.cpp src/SketchProfileExtrusion.h src/BooleanGeometry.h
    src/SketchProfileSweep.h src/SketchProfileRevolution.cpp src/SketchProfileRevolution.h)
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
    add_executable(OpenDesignerProfileRevolutionTests tests/SketchProfileRevolutionTest.cpp)
    target_link_libraries(OpenDesignerProfileRevolutionTests PRIVATE OpenDesignerConstruction)
    add_test(NAME sketch-profile-revolution COMMAND OpenDesignerProfileRevolutionTests)
    add_executable(OpenDesignerProfileExtrusionTests tests/SketchProfileExtrusionTest.cpp src/BRepReconstruction.cpp)
    target_link_libraries(OpenDesignerProfileExtrusionTests PRIVATE OpenDesignerConstruction)
    add_test(NAME sketch-profile-extrusion COMMAND OpenDesignerProfileExtrusionTests)
    add_executable(OpenDesignerProfileBuilderTests tests/SketchProfileBuilderTest.cpp)
    target_link_libraries(OpenDesignerProfileBuilderTests PRIVATE OpenDesignerConstruction)
    add_test(NAME sketch-profile-wire-face-construction COMMAND OpenDesignerProfileBuilderTests)
    add_executable(OpenDesignerProfileValidationTests tests/SketchProfileValidationTest.cpp)
    target_link_libraries(OpenDesignerProfileValidationTests PRIVATE OpenDesignerConstruction)
    add_test(NAME sketch-closed-profile-validation COMMAND OpenDesignerProfileValidationTests)
    add_executable(OpenDesignerSketchBoundsTests tests/SketchDisplayBoundsTest.cpp)
    target_link_libraries(OpenDesignerSketchBoundsTests PRIVATE OpenDesignerConstruction)
    add_test(NAME sketch-display-bounds COMMAND OpenDesignerSketchBoundsTests)
    add_executable(OpenDesignerSketchEllipseTests tests/SketchEllipseTest.cpp src/NativeDocument.cpp)
    target_link_libraries(OpenDesignerSketchEllipseTests PRIVATE OpenDesignerConstruction)
    add_test(NAME sketch-ellipse-foundation COMMAND OpenDesignerSketchEllipseTests)
    add_executable(OpenDesignerEllipseSnappingTests tests/SketchEllipseSnappingTest.cpp)
    target_link_libraries(OpenDesignerEllipseSnappingTests PRIVATE OpenDesignerConstruction)
    add_test(NAME sketch-ellipse-snapping COMMAND OpenDesignerEllipseSnappingTests)
    add_executable(OpenDesignerCommandPanelTests tests/CommandPanelPositionerTest.cpp)
    target_link_libraries(OpenDesignerCommandPanelTests PRIVATE OpenDesignerCommandPanels)
    add_test(NAME command-panel-positioning COMMAND OpenDesignerCommandPanelTests)
    add_test(NAME command-panel-positioning-hidpi COMMAND OpenDesignerCommandPanelTests)
    set_tests_properties(command-panel-positioning-hidpi PROPERTIES ENVIRONMENT "QT_SCALE_FACTOR=2")
    # These tests assert native keyboard focus. Other GUI processes on the same
    # desktop can steal it and delay WM placement; isolate that shared resource.
    set_tests_properties(command-panel-positioning command-panel-positioning-hidpi PROPERTIES RUN_SERIAL TRUE)
    add_executable(OpenDesignerSketchProjectionTests tests/SketchProjectionTest.cpp src/NativeDocument.cpp)
    target_link_libraries(OpenDesignerSketchProjectionTests PRIVATE OpenDesignerConstruction)
    add_test(NAME sketch-projection-geometry COMMAND OpenDesignerSketchProjectionTests)
    add_executable(OpenDesignerCircularProjectionTests tests/SketchCircularProjectionTest.cpp src/NativeDocument.cpp)
    target_link_libraries(OpenDesignerCircularProjectionTests PRIVATE OpenDesignerConstruction)
    add_test(NAME sketch-circular-projection COMMAND OpenDesignerCircularProjectionTests)
    add_executable(OpenDesignerEllipticalProjectionTests tests/SketchEllipticalProjectionTest.cpp src/NativeDocument.cpp)
    target_link_libraries(OpenDesignerEllipticalProjectionTests PRIVATE OpenDesignerConstruction)
    add_test(NAME sketch-elliptical-projection COMMAND OpenDesignerEllipticalProjectionTests)
    add_executable(OpenDesignerFaceProjectionTests tests/SketchFaceProjectionTest.cpp src/NativeDocument.cpp)
    target_link_libraries(OpenDesignerFaceProjectionTests PRIVATE OpenDesignerConstruction)
    add_test(NAME sketch-face-projection COMMAND OpenDesignerFaceProjectionTests)
    add_executable(OpenDesignerCylinderSilhouetteTests tests/SketchCylinderSilhouetteTest.cpp src/NativeDocument.cpp)
    target_link_libraries(OpenDesignerCylinderSilhouetteTests PRIVATE OpenDesignerConstruction)
    add_test(NAME sketch-cylinder-silhouettes COMMAND OpenDesignerCylinderSilhouetteTests)
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
        tests/SelectionContextTest.cpp tests/DeletePartSessionTest.cpp tests/ActivePartSessionTest.cpp tests/ProfileExtrusionSessionTest.cpp tests/ProfileRevolutionSessionTest.cpp tests/SketchDisplayBoundsSessionTest.cpp tests/WorkplaneRenderingTest.cpp tests/NativeDocumentSessionTest.cpp tests/ConstructionSessionTest.cpp tests/SketchSessionTest.cpp tests/SketchDrawingTest.cpp tests/SketchOrthogonalLineTest.cpp tests/SketchOverdrawSessionTest.cpp tests/SketchConstructionSessionTest.cpp tests/SketchReleaseSessionTest.cpp tests/SketchPointSessionTest.cpp tests/SketchEllipseSessionTest.cpp tests/SketchEllipseDrawingTest.cpp tests/CommandPanelSessionTest.cpp tests/SketchProjectionSessionTest.cpp src/MainWindow.h src/MainWindow.cpp
        src/CadViewport.h src/CadViewport.cpp src/BooleanOperations.cpp
        src/MoveFace.cpp src/FaceOperations.cpp src/FaceTransformGeometry.cpp
        src/FaceTransformGeometry.h src/SelectionContext.cpp src/MoveFaceGeometry.h src/BRepReconstruction.cpp src/MoveFaceGeometry.cpp)
    target_include_directories(OpenDesignerSelectionContextTests PRIVATE src ${OpenCASCADE_INCLUDE_DIR})
    target_link_libraries(OpenDesignerSelectionContextTests PRIVATE ${OPENDESIGNER_LIBRARIES})
    add_test(NAME delete-part-sessions COMMAND OpenDesignerSelectionContextTests --delete-parts)
    set_tests_properties(delete-part-sessions PROPERTIES TIMEOUT 120)
    add_test(NAME active-part-sessions COMMAND OpenDesignerSelectionContextTests --active-part)
    set_tests_properties(active-part-sessions PROPERTIES TIMEOUT 120)
    add_test(NAME profile-revolution-sessions COMMAND OpenDesignerSelectionContextTests --revolution)
    add_test(NAME profile-revolution-sessions-hidpi COMMAND OpenDesignerSelectionContextTests --revolution)
    set_tests_properties(profile-revolution-sessions profile-revolution-sessions-hidpi PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
    set_tests_properties(profile-revolution-sessions-hidpi PROPERTIES ENVIRONMENT "QT_SCALE_FACTOR=2")
    add_test(NAME profile-extrusion-sessions COMMAND OpenDesignerSelectionContextTests --extrusion)
    add_test(NAME profile-extrusion-sessions-hidpi COMMAND OpenDesignerSelectionContextTests --extrusion)
    set_tests_properties(profile-extrusion-sessions-hidpi PROPERTIES ENVIRONMENT "QT_SCALE_FACTOR=2")
    add_test(NAME selection-context COMMAND OpenDesignerSelectionContextTests)
    add_test(NAME native-document-sessions COMMAND OpenDesignerSelectionContextTests --documents)
    add_test(NAME native-partless-documents COMMAND OpenDesignerSelectionContextTests --partless)
    set_tests_properties(native-partless-documents PROPERTIES TIMEOUT 120)
    add_test(NAME workplane-rendering COMMAND OpenDesignerSelectionContextTests --workplane-rendering)
    add_test(NAME workplane-rendering-hidpi COMMAND OpenDesignerSelectionContextTests --workplane-rendering)
    set_tests_properties(workplane-rendering workplane-rendering-hidpi PROPERTIES TIMEOUT 180)
    set_tests_properties(workplane-rendering-hidpi PROPERTIES ENVIRONMENT "QT_SCALE_FACTOR=2")
    add_test(NAME sketch-display-bounds-sessions COMMAND OpenDesignerSelectionContextTests --display-bounds)
    add_test(NAME sketch-display-bounds-sessions-hidpi COMMAND OpenDesignerSelectionContextTests --display-bounds)
    set_tests_properties(sketch-display-bounds-sessions sketch-display-bounds-sessions-hidpi PROPERTIES TIMEOUT 120)
    set_tests_properties(sketch-display-bounds-sessions-hidpi PROPERTIES ENVIRONMENT "QT_SCALE_FACTOR=2")
    add_test(NAME sketch-sessions COMMAND OpenDesignerSelectionContextTests --sketch)
    add_test(NAME sketch-point-sessions COMMAND OpenDesignerSelectionContextTests --sketch-points)
    add_test(NAME sketch-ellipse-sessions COMMAND OpenDesignerSelectionContextTests --ellipses)
    add_test(NAME sketch-ellipse-sessions-hidpi COMMAND OpenDesignerSelectionContextTests --ellipses)
    add_test(NAME sketch-ellipse-drawing COMMAND OpenDesignerSelectionContextTests --ellipse-drawing)
    add_test(NAME sketch-ellipse-drawing-hidpi COMMAND OpenDesignerSelectionContextTests --ellipse-drawing)
    set_tests_properties(sketch-ellipse-drawing sketch-ellipse-drawing-hidpi PROPERTIES TIMEOUT 180)
    set_tests_properties(sketch-ellipse-drawing-hidpi PROPERTIES ENVIRONMENT "QT_SCALE_FACTOR=2")
    set_tests_properties(sketch-ellipse-sessions sketch-ellipse-sessions-hidpi PROPERTIES TIMEOUT 120)
    set_tests_properties(sketch-ellipse-sessions-hidpi PROPERTIES ENVIRONMENT "QT_SCALE_FACTOR=2")
    add_test(NAME sketch-projection-sessions COMMAND OpenDesignerSelectionContextTests --projection)
    add_test(NAME sketch-projection-sessions-hidpi COMMAND OpenDesignerSelectionContextTests --projection)
    set_tests_properties(sketch-projection-sessions sketch-projection-sessions-hidpi PROPERTIES TIMEOUT 180)
    set_tests_properties(sketch-projection-sessions-hidpi PROPERTIES ENVIRONMENT "QT_SCALE_FACTOR=2")
    add_test(NAME command-panel-sessions COMMAND OpenDesignerSelectionContextTests --command-panels)
    set_tests_properties(command-panel-sessions PROPERTIES TIMEOUT 120)
    set_tests_properties(sketch-point-sessions PROPERTIES TIMEOUT 120)
    add_test(NAME sketch-orthogonal-lines COMMAND OpenDesignerSelectionContextTests --orthogonal-lines)
    add_test(NAME sketch-orthogonal-lines-hidpi COMMAND OpenDesignerSelectionContextTests --orthogonal-lines)
    set_tests_properties(sketch-orthogonal-lines sketch-orthogonal-lines-hidpi PROPERTIES TIMEOUT 120)
    set_tests_properties(sketch-orthogonal-lines-hidpi PROPERTIES ENVIRONMENT "QT_SCALE_FACTOR=2")
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
        target_sources(${viewport_target} PRIVATE src/DebugExport.cpp src/DeleteFace.cpp src/ModelingErrors.cpp src/NativeDocument.cpp src/ConstructionTools.cpp src/ConstructionPanel.cpp src/SketchTools.cpp src/SketchDrawing.cpp src/SketchEllipseDrawing.cpp src/SketchDrawingPanel.cpp src/SketchOverdrawTools.cpp src/SketchOverdrawPanel.cpp src/SketchConstructionTools.cpp src/SketchConstructionPanel.cpp src/SketchProjectionTools.cpp src/SketchProjectionPanel.cpp src/SketchProfileExtrusionTools.cpp src/SketchProfileRevolutionTools.cpp)
        target_link_libraries(${viewport_target} PRIVATE OpenDesignerDeleteFaceGeometry OpenDesignerDebug OpenDesignerConstruction OpenDesignerCommandPanels)
    endif()
endforeach()
install(TARGETS OpenDesignerDebugModel RUNTIME DESTINATION bin)
