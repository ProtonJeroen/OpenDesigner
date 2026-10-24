#pragma once
#include <QWidget>
#include <QPoint>
#include <QColor>
#include <QJsonObject>
#include <QJsonArray>
#include <memory>
#include <optional>
class QLineEdit;
class QVBoxLayout;
class CommandPanelPositioner;
class QDialogButtonBox;
class QShortcut;
class QAction;
class QMenu;
class QComboBox;
class QLabel;
class QContextMenuEvent;
#include <AIS_InteractiveContext.hxx>
#include <AIS_Shape.hxx>
#include <AIS_TextLabel.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Face.hxx>
#include <gp_Trsf.hxx>
#include <gp_Ax1.hxx>
#include <gp_Pln.hxx>
#include <V3d_Viewer.hxx>
#include <V3d_View.hxx>
#include <Aspect_DisplayConnection.hxx>
#include <vector>
#include <array>
#include <map>
#include <QStringList>
class ConstructionPanel;
#include "NativeDocument.h"
#include "SketchDrawingPanel.h"
#include "SketchSnapping.h"
#include "SketchOverdraw.h"
#include "SketchOverdrawPanel.h"
#include "SketchConstructionPanel.h"
#include "SketchProjectionPanel.h"
#include "SketchProjection.h"

class CadViewport final : public QWidget {
    Q_OBJECT
public:
    explicit CadViewport(QWidget* parent = nullptr);
    bool exportDebugModel(const QString& path, QString& error, const QJsonArray& browserSelection = {}) const;
    bool exportDocumentSnapshot(NativeDocumentSnapshot& snapshot) const;
    bool importDocumentSnapshot(const NativeDocumentSnapshot& snapshot, QString& error);
    void resetDocument();
    QString activeSketchWorkplane() const { return activeSketchWorkplaneId_; }
    bool activateSketchWorkplane(const QString& id = {}, bool alignView = true);
    void endSketchEditing();
    void lookNormalToSketch();
    void startSketchDrawing(Sketch::DrawTool tool, Sketch::Role role = Sketch::Role::Profile);
    void startSketchOverdraw();
    void startSketchConstruction(Sketch::ConstructionTool tool);
    void startSketchProjection();
    void startSketchAxes(bool localX,bool localY);
    void populateSketchAxesMenu(QMenu& menu);
    void setSketchSnapPixels(int pixels);
    int sketchSnapPixels() const { return sketchSnapPixels_; }
    void setSketchSnapping(bool enabled);
    bool sketchSnapping() const { return sketchSnapping_; }
    const std::vector<Sketch::Entity>& sketchEntities() const { return sketchEntities_; }
    const QStringList& sketchSelection() const { return selectedSketchIds_; }
    // Phase-1 transaction adapter for future drawing tools (complete candidate set).
    bool previewSketchEntities(const std::vector<Sketch::Entity>& entities, QString& error, bool showControls=true);
    void finishSketchEdit(bool confirm);
    void selectSketchEntities(const QStringList& ids);
    bool updateSketchEntity(const Sketch::Entity& entity, QString& error);
    bool deleteSketchEntities(const QStringList& ids, QString& error);
    void populateSketchMenu(QMenu& menu);
    const std::vector<Construction::Object>& constructionObjects() const { return constructions_; }
    const QStringList& constructionSelection() const { return selectedConstructionIds_; }
    void startConstruction(Construction::Method method,const QString& targetId = {});
    void selectConstructions(const QStringList& ids);
    bool updateConstruction(const Construction::Object& object);
    void deleteConstructions(const QStringList& ids);
    void fitConstructions(const QStringList& ids);
    void populateConstructionMenu(QMenu& menu);
    size_t partSlotCount() const { return boxes_.size(); }
    void addBox();
    void addCylinder();
    void addSphere();
    void addCone();
    void duplicateSelectedParts();
    enum class BooleanOperation { Unite, Subtract, Intersect };
    void uniteSelectedParts();
    void subtractSelectedParts();
    void intersectSelectedParts();
    bool applyBoolean(BooleanOperation operation, const std::vector<size_t>& parts,
                      std::optional<size_t> target = std::nullopt, bool keepTools = false);
    bool isPartActive(size_t index) const;
    QString partName(size_t index) const;
    void renamePart(size_t index, const QString& name);
    QColor partColor(size_t index) const;
    void setPartColor(size_t index, const QColor& color);
    void fitAll();
    void pullSelectedFace();
    void startPrimitivePull();
    void moveFace();
    void rotateFace();
    void alignFace();
    void deleteFace();
    enum class SelectionMode { Face, Part };
    void setSelectionMode(SelectionMode mode);
    SelectionMode selectionMode() const { return selectionMode_; }
    void movePartAlongSelectedFace();
    void rotateSelectedPart();
    void undoPull();
    void redoPull();
    QAction* undoAction() const { return undoAction_; }
    QAction* redoAction() const { return redoAction_; }
    void setSnapStep(double stepMm);
    void setMovePartMode(bool enabled);
    void setRotateMode(bool enabled);
    void setDirectMoveMode(bool enabled);
    void setAngleSnap(double degrees);
    void selectParts(const std::vector<size_t>& indices);
    void setPartVisible(size_t index, bool visible);
    bool isPartVisible(size_t index) const;
    void fitSelection(const std::vector<size_t>& indices);
signals:
    void partCreated(const QString& name);
    void partsChanged();
    void partSelectionChanged(const std::vector<size_t>& indices);
    void statusMessage(const QString& message);
    void geometryOperationFailed(const QString& operation, const QString& reason);
    void selectionModeChanged(SelectionMode mode);
    void transformModesChanged(bool faceNormalMove, bool move, bool rotate);
    void documentReplaced();
    void activeSketchWorkplaneChanged(const QString& id);
    void sketchEntitiesChanged();
    void sketchSelectionChanged(const QStringList& ids);
    void constructionsChanged();
    void constructionSelectionChanged(const QStringList& ids);
protected:
    QPaintEngine* paintEngine() const override;
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void contextMenuEvent(QContextMenuEvent*) override;
    bool eventFilter(QObject*, QEvent*) override;
private:
    friend struct ConstructionSessionTest;
    friend struct SketchSessionTest;
    friend struct SketchDrawingTest;
    friend struct SketchOverdrawSessionTest;
    friend struct SketchConstructionSessionTest;
    friend struct SketchReleaseSessionTest;
    friend struct SketchPointSessionTest;
    friend struct SketchEllipseSessionTest;
    friend struct SketchEllipseDrawingTest;
    friend struct CommandPanelSessionTest;
    friend struct SketchProjectionSessionTest;
    friend struct CadViewportPullTest;
    friend struct NativeDocumentSessionTest;
    friend struct BooleanOperationsTest;
    friend struct MoveFaceTest;
    friend struct SelectionContextTest;
    friend struct DebugModelTest;
    friend struct DeleteFaceTest;
    struct SelectedFace { size_t partIndex; TopoDS_Face face; };
    struct SelectionContext {
        enum class Kind { None, Parts, Faces, Constructions, Sketches };
        Kind kind = Kind::None;
        std::vector<size_t> parts;
        std::vector<SelectedFace> faces;
        QStringList constructions, sketches;
    };
    QAction* undoAction_ = nullptr;
    QAction* redoAction_ = nullptr;
    void updateHistoryActions();
    void clearHistorySelection();
    SelectionMode selectionMode_ = SelectionMode::Face;
    SelectionContext selectionContext() const;
    QMenu* createSelectionMenu(const QPoint& anchor);
    void populatePartMenu(QMenu& menu, const SelectionContext& selection);
    void populateFaceMenu(QMenu& menu, const SelectionContext& selection, const QPoint& anchor);
    bool supportsResize(const SelectionContext& selection) const;
    void updateSelectedFaceArrow(const QPoint& point);
    struct ConeParameters {
        double bottomRadius = 40.0;
        double topRadius = 20.0;
        double height = 80.0;
        double baseOffset = 0.0;
    };
    enum class ConePullTarget { BottomDiameter, TopDiameter, BottomHeight, TopHeight };
    struct CadPart {
        enum class GeometryKind { Box, GeneralBRep };
        GeometryKind geometryKind = GeometryKind::Box;
        // Canonical local-space B-Rep. Box bounds are temporary editing metadata.
        TopoDS_Shape shape;
        Handle(AIS_Shape) presentation;
        std::array<double, 3> min;
        std::array<double, 3> max;
        // Original local bounds for general B-Rep; movement changes instance transform.
        std::array<double, 3> baseMin{};
        std::array<double, 3> baseMax{};
        gp_Trsf orientation;
        bool cylinderPrimitive = false;
        bool spherePrimitive = false;
        bool conePrimitive = false;
        ConeParameters cone;
        double sphereRadius = 0.0;
        double cylinderRadius = 0.0;
        double cylinderHeight = 0.0;
        double cylinderBaseOffset = 0.0;
        bool visible = true;
        bool active = true;
        QString name;
        QString persistentId;
        QColor color = QColor(211,211,211);
    };
    bool recognizePullPrimitive(CadPart& part) const;
    struct BooleanChange {
        std::vector<size_t> sources;
        std::vector<CadPart> sourceStates;
        size_t resultIndex;
        CadPart resultState;
        std::vector<size_t> beforeSelection;
        QJsonObject debugOperation;
    };
    struct FaceChange { CadPart before, after; };
    struct SketchDrawingState {
        Sketch::DrawTool tool = Sketch::DrawTool::Segment;
        Sketch::Entity candidate;
        std::vector<Sketch::Point> points;
        std::optional<Sketch::Point> cursor;
        std::optional<Sketch::SnapTarget> snap;
        std::vector<Sketch::SnapTarget> intersections;
        std::optional<double> radius, startAngle, sweep;
        Sketch::Ellipse ellipse{{},10,5,0};
        std::optional<double> ellipseStart,ellipseEnd;
        bool complete=false;
    };
    struct SketchChange { std::vector<Sketch::Entity> before, after; };
    struct SketchProjectionState {
        struct Reference {
            size_t partIndex;
            QString partId;
            TopoDS_Shape root,shape;
            gp_Trsf placement;
            Handle(SelectMgr_EntityOwner) owner;
        };
        Construction::Object workplane;
        SketchProjectionPanel::Mode mode=SketchProjectionPanel::Mode::Mixed;
        std::vector<Reference> sources;
        QStringList previousSketchSelection;
        SketchProjection::Result result;
    };
    struct SketchConstructionState {
        Sketch::ConstructionTool tool=Sketch::ConstructionTool::OffsetLine;
        Sketch::Entity candidate;
        std::vector<Sketch::CommonTangent> solutions;
        std::vector<Sketch::Geometry> lines;
        QStringList referenceHits;
        std::vector<size_t> solutionHits;
        int pickRole=0,selected=-1;
        QString error,notice;
    };
    struct SketchOverdrawState {
        struct Pick {size_t piece,from,to;Sketch::Entity profile;};
        Sketch::OverdrawGraph graph;
        std::vector<Pick> picks;
        std::vector<size_t> hits;
        std::optional<size_t> currentNode,nodeHover,hover;
        QString error,feedback;
    };
    struct ConstructionChange {
        std::vector<Construction::Object> before, after;
        std::vector<Sketch::Entity> sketchesBefore, sketchesAfter;
    };
    struct ConstructionChoice { QString key, label, objectId; Construction::Reference reference; };
    struct ConstructionSessionState {
        Construction::Request request;
        Construction::Object candidate;
        QString targetId;
        QStringList selection;
        std::vector<std::vector<ConstructionChoice>> choices;
        int pickRole = -1;
    };
    struct PullChange {
        // Optional diagnostic data; history remains authoritative for Undo/Redo.
        size_t partIndex = 0;
        std::array<double, 3> beforeMin, beforeMax;
        std::array<double, 3> afterMin, afterMax;
        gp_Trsf beforeOrientation, afterOrientation;
        TopoDS_Shape beforeShape, afterShape;
        double beforeRadius = 0.0, afterRadius = 0.0;
        double beforeHeight = 0.0, afterHeight = 0.0;
        double beforeBaseOffset = 0.0, afterBaseOffset = 0.0;
        ConeParameters beforeCone, afterCone;
        std::vector<PullChange> groupedChanges;
        std::shared_ptr<ConstructionChange> constructionChange;
        std::shared_ptr<SketchChange> sketchChange;
        std::shared_ptr<BooleanChange> booleanChange;
        std::shared_ptr<FaceChange> faceChange;
        QJsonObject debugOperation;
    };
    // Transaction state only: confirmed changes use the existing modeling history.
    struct CommandSession {
        enum class State { Selection, Parameters, Preview, Confirmed, Cancelled };
        enum class Kind { Move, Rotate, MoveFace, RotateFace, AlignFace, Boolean, Pull, DeleteFace, Construction, Sketch };
        std::shared_ptr<ConstructionSessionState> construction;
        std::shared_ptr<SketchChange> sketch;
        std::shared_ptr<SketchDrawingState> drawing;
        std::shared_ptr<SketchOverdrawState> overdraw;
        std::shared_ptr<SketchConstructionState> lineConstruction;
        std::shared_ptr<SketchProjectionState> projection;
        State state = State::Selection;
        Kind kind = Kind::Move;
        std::vector<PullChange> changes;
        std::vector<size_t> selection;
        std::vector<Handle(SelectMgr_EntityOwner)> owners;
        gp_Pnt pivot;
        gp_Dir direction = gp_Dir(1,0,0);
        gp_Trsf delta;
        double value = 0;
        TopoDS_Face face;
        gp_Ax1 faceAxis = gp_Ax1(gp_Pnt(0,0,0),gp_Dir(1,0,0));
        gp_Pln referencePlane = gp_Pln(gp_Pnt(0,0,0),gp_Dir(0,0,1));
        size_t referencePart = 0;
        TopoDS_Face referenceFace;
        bool hasReference = false;
        bool coplanar = true;
        bool changed = false;
        std::shared_ptr<CadPart> faceBefore;
        TopoDS_Shape candidate;
        Handle(AIS_Shape) preview;
        std::vector<Handle(AIS_Shape)> facePreviews;
        std::vector<SelectedFace> deletedFaces;
        QString geometryError;
        bool valid = true;
        BooleanOperation operation = BooleanOperation::Unite;
        std::optional<size_t> target;
        std::vector<size_t> tools, initialParts;
        bool keepTools = false, chooseTarget = false;
        std::shared_ptr<BooleanChange> booleanChange;
    };
    QString activeSketchWorkplaneId_;
    int sketchSnapPixels_ = 10;
    bool sketchSnapping_ = true;
    SketchDrawingPanel* sketchDrawingPanel_ = nullptr;
    SketchOverdrawPanel* sketchOverdrawPanel_ = nullptr;
    SketchConstructionPanel* sketchConstructionPanel_ = nullptr;
    SketchProjectionPanel* sketchProjectionPanel_ = nullptr;
    void configureSketchProjectionPicking();
    void changeSketchProjectionMode();
    bool sketchProjectionClick(const QPoint& pixel,Qt::KeyboardModifiers modifiers);
    bool captureSketchProjectionSelection();
    bool projectionReference(const Handle(SelectMgr_EntityOwner)& owner,SketchProjectionState::Reference& result) const;
    bool validateSketchProjectionSources();
    void refreshSketchProjection();
    bool confirmSketchProjection();
    void clearSketchProjectionSources();
    void closeSketchProjectionPanel();
    std::vector<Handle(AIS_Shape)> constructionLineAlternatives_;
    void refreshSketchConstruction(bool recompute=true);
    void closeSketchConstructionPanel();
    bool acceptSketchConstructionReference(const QString& id);
    bool sketchConstructionMove(const QPoint& pixel);
    bool sketchConstructionClick(const QPoint& pixel);
    Handle(AIS_Shape) overdrawHover_;
    bool overdrawMove(const QPoint& pixel);
    bool overdrawClick(const QPoint& pixel);
    void chooseOverdrawPiece();
    void backOverdrawPiece();
    void newOverdrawStart();
    void refreshOverdraw();
    void closeOverdrawPanel();
    void showSketchMarker(Handle(AIS_Shape)& presentation,Sketch::Point point,const Quantity_Color& color);
    Handle(AIS_Shape) sketchSnapMarker_, sketchAnchorMarker_;
    Handle(AIS_TextLabel) sketchCursorLabel_;
    void refreshSketchPicking(bool restore3D=false);
    void stabilizeViewCamera();
    void validateActiveSketchContext();
    std::optional<Sketch::Point> sketchPlanePoint(const QPoint& pixel) const;
    bool sketchDrawingMove(const QPoint& pixel, Qt::KeyboardModifiers modifiers);
    bool sketchDrawingClick(const QPoint& pixel, Qt::KeyboardModifiers modifiers);
    void acceptSketchPoint(Sketch::Point point);
    void useSketchNumeric(bool polar);
    void backSketchPoint();
    void refreshSketchDrawing();
    void acceptSketchEllipsePoint(Sketch::Point point);
    void useSketchEllipseNumeric();
    void refreshSketchEllipseDrawing();
    void editSketchEllipseParameters();
    void changeSketchEllipseDirection();
    void updateSketchFeedback();
    void clearSketchFeedback();
    void closeSketchDrawingPanel();
    std::vector<Sketch::Entity> sketchEntities_;
    QStringList selectedSketchIds_;
    std::map<QString,Handle(AIS_Shape)> sketchPresentations_, sketchPreviews_;
    Handle(AIS_Shape) sketchPresentation(const Sketch::Entity& entity, const Construction::Object& owner, bool preview=false) const;
    void displaySketchPresentation(const Sketch::Entity& entity, const Construction::Object& owner, const Handle(AIS_Shape)& presentation, bool preview=false);
    void rebuildSketchPresentations();
    void restoreSketchEntities(const std::vector<Sketch::Entity>& entities);
    bool sketchMousePress(const QPoint& point, Qt::KeyboardModifiers modifiers);
    bool sketchPickable(const Sketch::Entity& entity) const;
    void clearSketchSelection();
    std::vector<Construction::Object> constructions_;
    std::map<QString,Handle(AIS_Shape)> constructionPresentations_;
    std::map<QString,Handle(AIS_TextLabel)> constructionLabels_;
    Handle(AIS_TextLabel) constructionPreviewLabel_;
    std::array<int,3> constructionNameCounters_{};
    QStringList selectedConstructionIds_;
    ConstructionPanel* constructionPanel_ = nullptr;
    std::vector<Handle(AIS_Shape)> constructionHandles_;
    int constructionDragAxis_ = -1;
    QPoint constructionDragStart_;
    gp_Vec constructionDragBefore_;
    double constructionDragValue_ = 0.0, constructionLastAngle_ = 0.0, constructionAccumulatedAngle_ = 0.0;
    double constructionHandleLength_ = 30.0;
    bool constructionRotating_ = false;
    const Construction::Object* constructionObject(const QString& id) const;
    Handle(AIS_Shape) constructionPresentation(const Construction::Object& object,bool preview=false) const;
    Handle(AIS_TextLabel) constructionLabel(const Construction::Object& object,bool preview=false) const;
    void updateConstructionLabels();
    void rebuildConstructionPresentations();
    void restoreConstructions(const std::vector<Construction::Object>& objects);
    bool commitConstructions(const std::vector<Construction::Object>& objects);
    bool previewConstruction();
    void closeConstructionSession(bool confirm);
    bool constructionMousePress(const QPoint& point,Qt::KeyboardModifiers modifiers);
    bool constructionMouseMove(const QPoint& point,Qt::KeyboardModifiers modifiers);
    void constructionMouseRelease();
    void chooseConstructionRole(size_t role);
    bool captureConstructionReference(const QPoint& point);
    bool acceptConstructionChoice(size_t role,const QString& key);
    void displayConstructionHandles();
    void clearConstructionHandles();
    double constructionCursorAngle(const QPoint& point) const;
    void restoreConstructionPicker();
    QJsonObject debugCommand(const CommandSession& session) const;
    std::optional<CommandSession> commandSession_;
    QWidget* commandPanel_ = nullptr;
    QVBoxLayout* commandLayout_ = nullptr;
    CommandPanelPositioner* commandPanelPositioner_ = nullptr;
    QDialogButtonBox* commandButtons_ = nullptr;
    QShortcut* cancelDimension_ = nullptr;
    void beginCommandSession(CommandSession::Kind kind, const std::vector<size_t>& indices,
                             const gp_Dir& direction, const gp_Pnt& pivot = gp_Pnt());
    bool previewCommand(double value);
    bool startMoveFace(size_t index, const TopoDS_Face& face);
    bool previewMoveFace(double value);
    bool presentFacePreview(const CadPart& before, bool changed, QString& error);
    Handle(AIS_Shape) createFacePreview(const CadPart& before, const TopoDS_Shape& candidate, QString& error);
    void reportGeometryFailure(const QString& operation, const QString& reason, bool popup);
    void redrawFacePreview();
    bool startRotateFace(size_t index, const TopoDS_Face& face);
    bool startAlignFace(size_t index, const TopoDS_Face& face);
    bool previewFaceTransform(double value);
    void chooseFaceRotationAxis(int choice);
    void updateFaceRotationManipulator();
    bool beginFaceRotationDrag(const QPoint& point);
    double faceRotationCursorAngle(const QPoint& point) const;
    bool pickFaceRotationAxis(const QPoint& point);
    bool pickAlignReference(const QPoint& point);
    bool beginMoveFaceDrag(const QPoint& point);
    void applyFaceState(size_t index, const CadPart& state);
    bool moveFacePicking_ = false;
    double moveFaceDragStart_ = 0;
    Handle(AIS_Shape) moveFaceArrow_;
    Handle(AIS_Shape) faceRotationManipulator_;
    QComboBox* faceAxisSelector_ = nullptr;
    QComboBox* faceAlignSelector_ = nullptr;
    QLabel* faceAlignTargetLabel_ = nullptr;
    QLabel* faceAlignReferenceLabel_ = nullptr;
    bool faceAxisPicking_ = false;
    bool alignFacePickingReference_ = false;
    bool draggingFaceRotation_ = false;
    double faceRotationPreviousAngle_ = 0.0;
    double faceRotationAccumulatedAngle_ = 0.0;
    gp_Pnt faceRotationPivotWorld_;
    gp_Dir faceRotationAxisWorld_ = gp_Dir(0,0,1);
    double faceRotationRadius_ = 40.0;
    bool updateCommandParameter(bool reportError = true);
    void confirmCommandSession();
    void cancelCommandSession();
    void closeCommandSession(bool confirm);
    void showCommandControls(const QPoint& position);
    void showCommandPanel(QWidget* panel);
    void announcePartCreated(const QString& name);
    std::vector<size_t> selectedBooleanParts() const;
    void restoreBooleanChange(const BooleanChange& change, bool after);
    void finishModelingInteraction();
    void resetModelingTools();
    void beginPullSession();
    bool previewPrimitivePull(double value);
    bool applyCylindricalFacePull(double distance);
    bool pullMode_ = false;
    bool calculateBoolean(BooleanOperation operation, const std::vector<size_t>& parts,
                          std::optional<size_t> target, bool keepTools, bool previewOnly);
    QWidget* booleanControls_ = nullptr;
    void beginBooleanSession(BooleanOperation operation);
    void selectBooleanParts(const std::vector<size_t>& indices, bool toggle);
    void updateBooleanControls();
    bool previewBooleanSession();
    void commitBooleanChange(const std::shared_ptr<BooleanChange>& command);
    gp_Pnt partLocalCenter(const CadPart& part) const;
    gp_Pnt partLocalGizmoCenter(const CadPart& part) const;
    void applyPartState(size_t index, const std::array<double, 3>& min,
                        const std::array<double, 3>& max, const gp_Trsf& orientation);
    bool applyPrimitivePull(double distance);
    void restorePullChange(const PullChange& change, bool after);
    bool prepareMultiFaceDrag();
    bool applyMultiFaceDrag(double distance);
    void updateMultiFaceHighlight();
    void clearMultiFaceHighlight();
    void refreshPartSelection();
    void applyBoxBounds(size_t index, const std::array<double, 3>& min,
                        const std::array<double, 3>& max);
    bool beginFaceDrag(const QPoint& origin);
    bool beginArrowDrag(const QPoint& origin);
    void updateFaceArrow();
    void hideFaceArrow();
    gp_Pnt coneArrowOrigin(const CadPart& part, ConePullTarget target) const;
    gp_Dir coneArrowDirection(ConePullTarget target) const;
    void displayPullArrow(Handle(AIS_Shape)& arrow, gp_Pnt origin, gp_Vec direction);
    bool pickConeArrow(const QPoint& point);
    double primitivePullStartDimension() const;
    bool primitivePullIsDiameter() const;
    QString primitivePullDimensionText() const;
    void initializeViewer();
    void updateRotationRings();
    void updateMoveAxes();
    gp_Pnt selectionCenter() const;
    gp_Pnt rotationCenter(const std::vector<size_t>& indices) const;
    gp_Pnt partWorldRotationCenter(const CadPart& part, const gp_Trsf& orientation) const;
    void setTransformSelectionMode();
    void hideMoveAxes();
    bool beginMoveAxisDrag(const QPoint& point);
    void hideRotationRings();
    bool beginRotationDrag(const QPoint& point);
    void applyRotationAngle(double angleDegrees);
    double rotationCursorAngle(const QPoint& point, int axis, const gp_Trsf& orientation) const;
    Handle(Aspect_DisplayConnection) display_;
    Handle(V3d_Viewer) viewer_;
    Handle(V3d_View) view_;
    Handle(AIS_InteractiveContext) context_;
    Handle(AIS_Shape) faceArrow_;
    std::array<Handle(AIS_Shape),4> coneArrows_;
    ConePullTarget conePullTarget_ = ConePullTarget::TopHeight;
    gp_Dir coneRadialDirection_ = gp_Dir(1,0,0);
    Handle(AIS_Shape) multiFaceHighlight_;
    size_t arrowPartIndex_ = 0;
    int arrowAxis_ = 0;
    bool arrowMax_ = true;
    bool arrowVisible_ = false;
    bool cylinderRadialArrow_ = false;
    gp_Pnt primitiveArrowOrigin_;
    gp_Dir primitiveArrowDirection_ = gp_Dir(1,0,0);
    bool primitivePullDragging_ = false;
    bool primitivePullEditing_ = false;
    bool cylindricalFacePull_ = false;
    TopoDS_Face cylindricalPullFace_;
    double cylindricalPullRadius_ = 0.0;
    PullChange primitivePullBefore_;

    std::vector<CadPart> boxes_;
    std::vector<size_t> selectedPartIndices_;
    bool multiFaceDragging_ = false;
    bool multiFaceEditing_ = false;
    struct FaceDragPart {
        PullChange before;
        std::array<bool, 6> sides{};
    };
    std::vector<FaceDragPart> multiFaceParts_;
    bool batchingPartUpdates_ = false;
    bool groupRotationDragging_ = false;
    gp_Pnt groupRotationCenter_;
    std::vector<PullChange> undoStack_;
    std::vector<PullChange> redoStack_;
    QPoint previous_;
    bool rotating_ = false;
    bool panning_ = false;
    bool draggingFace_ = false;
    bool movePartMode_ = false;
    bool dragMovesPart_ = false;
    bool rotateMode_ = false;
    bool directMoveMode_ = false;
    size_t moveIndex_ = 0;
    std::array<Handle(AIS_Shape), 3> moveAxes_;
    bool draggingRotation_ = false;
    size_t rotationIndex_ = 0;
    int rotationAxis_ = 0;
    double rotationAngle_ = 0.0;
    double rotationSnap_ = 15.0;
    double rotationStartAngle_ = 0.0;
    double rotationPreviousAngle_ = 0.0;
    double rotationAccumulatedAngle_ = 0.0;
    gp_Trsf rotationBefore_;
    std::array<Handle(AIS_Shape), 3> rotationRings_;
    QLineEdit* dragDimension_ = nullptr;
    bool dimensionEditing_ = false;
    size_t dragIndex_ = 0;
    int dragAxis_ = 0;
    bool dragMax_ = true;
    QPoint dragStart_;
    std::array<double, 3> dragMin_{}, dragMaxBounds_{};
    double dragDistance_ = 0.0;
    double dragScreenX_ = 0.0, dragScreenY_ = 0.0;
    double dragMmPerPixel_ = 0.5;
    double snapStepMm_ = 1.0;
    int partCount_ = 0;
};
