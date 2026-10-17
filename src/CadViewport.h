#pragma once
#include <QWidget>
#include <QPoint>
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
    void duplicateSelectedParts();
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
        double cylinderRadius = 0.0;
        double cylinderHeight = 0.0;
        double cylinderBaseOffset = 0.0;
        bool visible = true;
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
        std::vector<PullChange> groupedChanges;
    };
    gp_Pnt partLocalCenter(const CadPart& part) const;
    void applyPartState(size_t index, const std::array<double, 3>& min,
                        const std::array<double, 3>& max, const gp_Trsf& orientation);
    void applyGroupMove(double distance);
    bool applyCylinderPull(double distance);
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
    Handle(AIS_Shape) multiFaceHighlight_;
    size_t arrowPartIndex_ = 0;
    int arrowAxis_ = 0;
    bool arrowMax_ = true;
    bool arrowVisible_ = false;
    bool cylinderRadialArrow_ = false;
    gp_Pnt cylinderArrowOrigin_;
    gp_Dir cylinderArrowDirection_ = gp_Dir(1,0,0);
    bool cylinderPullDragging_ = false;
    bool cylinderPullEditing_ = false;
    PullChange cylinderPullBefore_;

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
