#pragma once
#include <QWidget>
#include <QPoint>
#include <QColor>
#include <memory>
#include <optional>
class QLineEdit;
#include <AIS_InteractiveContext.hxx>
#include <AIS_Shape.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Trsf.hxx>
#include <V3d_Viewer.hxx>
#include <V3d_View.hxx>
#include <Aspect_DisplayConnection.hxx>
#include <vector>
#include <array>

class CadViewport final : public QWidget {
    Q_OBJECT
public:
    explicit CadViewport(QWidget* parent = nullptr);
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
                      std::optional<size_t> target = std::nullopt);
    bool isPartActive(size_t index) const;
    QString partName(size_t index) const;
    void renamePart(size_t index, const QString& name);
    QColor partColor(size_t index) const;
    void setPartColor(size_t index, const QColor& color);
    void fitAll();
    void pullSelectedFace();
    void movePartAlongSelectedFace();
    void rotateSelectedPart();
    void undoPull();
    void redoPull();
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
protected:
    QPaintEngine* paintEngine() const override;
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
private:
    friend struct CadViewportPullTest;
    friend struct BooleanOperationsTest;
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
        QColor color = QColor(211,211,211);
    };
    struct BooleanChange {
        std::vector<size_t> sources;
        std::vector<CadPart> sourceStates;
        size_t resultIndex;
        CadPart resultState;
        std::vector<size_t> beforeSelection;
    };
    struct PullChange {
        size_t partIndex;
        std::array<double, 3> beforeMin, beforeMax;
        std::array<double, 3> afterMin, afterMax;
        gp_Trsf beforeOrientation, afterOrientation;
        TopoDS_Shape beforeShape, afterShape;
        double beforeRadius = 0.0, afterRadius = 0.0;
        double beforeHeight = 0.0, afterHeight = 0.0;
        double beforeBaseOffset = 0.0, afterBaseOffset = 0.0;
        ConeParameters beforeCone, afterCone;
        std::vector<PullChange> groupedChanges;
        std::shared_ptr<BooleanChange> booleanChange;
    };
    void announcePartCreated(const QString& name);
    std::vector<size_t> selectedBooleanParts() const;
    void restoreBooleanChange(const BooleanChange& change, bool after);
    void finishModelingInteraction();
    gp_Pnt partLocalCenter(const CadPart& part) const;
    gp_Pnt partLocalGizmoCenter(const CadPart& part) const;
    void applyPartState(size_t index, const std::array<double, 3>& min,
                        const std::array<double, 3>& max, const gp_Trsf& orientation);
    void applyGroupMove(double distance);
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
    void showDragDimension(const QPoint& position);
    void initializeViewer();
    void updateRotationRings();
    void updateMoveAxes();
    gp_Pnt selectionCenter() const;
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
    PullChange primitivePullBefore_;

    std::vector<CadPart> boxes_;
    std::vector<size_t> selectedPartIndices_;
    std::vector<PullChange> groupMoveBefore_;
    gp_Dir groupMoveDirection_ = gp_Dir(1, 0, 0);
    gp_Trsf dragBeforeOrientation_;
    bool groupMoveDragging_ = false;
    bool multiFaceDragging_ = false;
    bool multiFaceEditing_ = false;
    struct FaceDragPart {
        PullChange before;
        std::array<bool, 6> sides{};
    };
    std::vector<FaceDragPart> multiFaceParts_;
    bool batchingPartUpdates_ = false;
    bool groupMoveEditing_ = false;
    bool groupRotationDragging_ = false;
    bool groupRotationEditing_ = false;
    std::vector<PullChange> groupRotationBefore_;
    gp_Pnt groupRotationCenter_;
    gp_Dir groupRotationDirection_ = gp_Dir(1, 0, 0);
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
    bool rotationEditing_ = false;
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
