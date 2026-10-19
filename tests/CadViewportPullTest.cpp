#include "CadViewport.h"
#include <QApplication>
#include <QLineEdit>
#include <QLocale>
#include <QMouseEvent>
#include <QValidator>
#include <BRepCheck_Analyzer.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <gp_Ax1.hxx>
#include <gp_Circ.hxx>
#include <ElCLib.hxx>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void near(double actual, double expected, const char* message) {
    require(std::abs(actual - expected) < 1.0e-7, message);
}
}

// Exercise the production picking, drag, dimension editor and undo paths.
struct CadViewportPullTest {
    static gp_Pnt center(const CadViewport& v, size_t index) {
        GProp_GProps properties;
        BRepGProp::VolumeProperties(v.boxes_[index].shape, properties);
        return properties.CentreOfMass().Transformed(v.boxes_[index].orientation);
    }
    static void checkSphere(const CadViewport& v, size_t index, double radius,
                            const gp_Pnt& expectedCenter) {
        const auto& part = v.boxes_[index];
        require(part.spherePrimitive, "sphere metadata lost");
        require(BRepCheck_Analyzer(part.shape).IsValid(), "invalid sphere B-Rep");
        near(part.sphereRadius, radius, "incorrect radius metadata");
        near(center(v,index).Distance(expectedCenter), 0.0, "sphere center moved");
        GProp_GProps properties;
        BRepGProp::VolumeProperties(part.shape, properties);
        const double volume = 4.0/3.0 * std::acos(-1.0) * radius*radius*radius;
        require(std::abs(properties.Mass()/volume-1.0) < 1.0e-7, "incorrect sphere volume");
        require(part.presentation->Shape().IsSame(part.shape), "presentation out of sync");
        const auto transform = part.presentation->LocalTransformation();
        near(v.partLocalCenter(part).Transformed(transform).Distance(expectedCenter),
             0.0, "presentation center moved");
    }
    static QPoint project(const CadViewport& v, const gp_Pnt& point) {
        Standard_Integer x, y;
        v.view_->Convert(point.X(),point.Y(),point.Z(),x,y);
        return QPoint(x,y);
    }
    static void click(CadViewport& v, const QPoint& point) {
        QMouseEvent press(QEvent::MouseButtonPress,QPointF(point),QPointF(point),
                          Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
        v.mousePressEvent(&press);
        QMouseEvent release(QEvent::MouseButtonRelease,QPointF(point),QPointF(point),
                            Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
        v.mouseReleaseEvent(&release);
    }
    static void pickSphere(CadViewport& v, size_t index) {
        // The center click also covers the screen-right radial fallback.
        click(v,project(v,center(v,index)));
        require(v.arrowVisible_ && v.arrowPartIndex_ == index,
                "sphere picking did not create a pull arrow");
        require(!v.faceArrow_.IsNull(), "missing orange arrow presentation");
    }
    static QPoint beginPull(CadViewport& v) {
        const auto& part = v.boxes_[v.arrowPartIndex_];
        const auto point = v.primitiveArrowOrigin_.Translated(
            gp_Vec(v.primitiveArrowDirection_)*15.0).Transformed(part.orientation);
        const QPoint screen = project(v,point);
        require(v.beginArrowDrag(screen), "pull arrow could not be grabbed");
        return screen;
    }
    static void release(CadViewport& v, const QPoint& screen) {
        const QString expected = v.primitivePullDimensionText();
        QMouseEvent event(QEvent::MouseButtonRelease,QPointF(screen),QPointF(screen),
                          Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
        v.mouseReleaseEvent(&event);
        require(v.dragDimension_->text() == expected, "dimension changed on mouse release");
        require(v.dragDimension_->hasAcceptableInput(), "displayed dimension cannot be submitted");
    }
    static void checkDimension(const CadViewport& v, double value, bool diameter) {
        const QString expected = (diameter ? QStringLiteral("Ø %1 mm") : QStringLiteral("%1 mm"))
            .arg(QLocale().toString(value,'f',2));
        require(v.dragDimension_->text() == expected, "incorrect absolute dimension display");
    }
    static void dragBy(CadViewport& v, const QPoint& start, double distance) {
        const QPoint end = start + QPoint(qRound(v.dragScreenX_*distance/v.dragMmPerPixel_),
                                          qRound(v.dragScreenY_*distance/v.dragMmPerPixel_));
        QMouseEvent move(QEvent::MouseMove,QPointF(end),QPointF(end),
                         Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
        v.mouseMoveEvent(&move);
    }
    static void exact(CadViewport& v, double dimension, bool decorated = false) {
        const QString number = QLocale().toString(dimension,'f',2);
        v.dragDimension_->setText(decorated ? QStringLiteral("Ø %1 mm").arg(number) : number);
        require(v.dragDimension_->hasAcceptableInput(), "exact dimension rejected by validator");
        require(QMetaObject::invokeMethod(v.dragDimension_,"returnPressed",Qt::DirectConnection),
                "could not submit exact dimension");
    }
    static void run(CadViewport& v) {
        v.addSphere();
        const gp_Pnt originalCenter = center(v,0);
        pickSphere(v,0);
        const QPoint start = beginPull(v);
        checkDimension(v,80.0,true);
        require(v.dragDimension_->text() == QStringLiteral("Ø 80,00 mm"), "Dutch diameter format incorrect");
        const QPoint end = start + QPoint(qRound(v.dragScreenX_*12.0/v.dragMmPerPixel_),
                                         qRound(v.dragScreenY_*12.0/v.dragMmPerPixel_));
        QMouseEvent move(QEvent::MouseMove,QPointF(end),QPointF(end),
                         Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
        v.mouseMoveEvent(&move);
        require(v.boxes_[0].sphereRadius > 40.0, "outward drag did not enlarge sphere");
        checkDimension(v,2.0*v.boxes_[0].sphereRadius,true);
        const QString lastDiameter = v.dragDimension_->text();
        release(v,end);
        require(v.dragDimension_->text() == lastDiameter, "last dragged diameter was not retained");
        exact(v,100.50,true);
        checkSphere(v,0,50.25,originalCenter);
        require(v.undoStack_.size() == 1, "drag and exact input must be one undo step");
        v.undoPull();
        checkSphere(v,0,40.0,originalCenter);
        v.redoPull();
        checkSphere(v,0,50.25,originalCenter);

        // Numeric-only pull, shrinking and invalid radius rejection.
        pickSphere(v,0);
        const QPoint shrink = beginPull(v);
        const QPoint inward = shrink - QPoint(qRound(v.dragScreenX_*4.0/v.dragMmPerPixel_),
                                              qRound(v.dragScreenY_*4.0/v.dragMmPerPixel_));
        QMouseEvent inwardMove(QEvent::MouseMove,QPointF(inward),QPointF(inward),
                               Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
        v.mouseMoveEvent(&inwardMove);
        require(v.boxes_[0].sphereRadius < 50.25, "inward drag did not shrink sphere");
        const double draggedRadius = v.boxes_[0].sphereRadius;
        checkDimension(v,2.0*draggedRadius,true);
        release(v,inward);
        exact(v,-100.0);
        checkSphere(v,0,draggedRadius,originalCenter);
        require(v.primitivePullEditing_, "invalid radius closed the editor");
        require(!v.applyPrimitivePull(std::numeric_limits<double>::quiet_NaN()),
                "NaN radius accepted");
        exact(v,89.50);
        checkSphere(v,0,44.75,originalCenter);
        v.undoPull();
        checkSphere(v,0,50.25,originalCenter);
        v.redoPull();
        checkSphere(v,0,44.75,originalCenter);

        // Apply a rotation and translation, then resize through the same UI paths.
        gp_Trsf rotation, translation;
        rotation.SetRotation(gp_Ax1(gp_Pnt(0,0,0),gp_Dir(1,2,3)),0.7);
        translation.SetTranslation(gp_Vec(135,-42,27));
        const gp_Trsf transform = translation*rotation;
        v.applyPartState(0,v.boxes_[0].min,v.boxes_[0].max,transform);
        v.fitAll();
        const gp_Pnt transformedCenter = center(v,0);
        pickSphere(v,0);
        const QPoint transformedPull = beginPull(v);
        release(v,transformedPull);
        exact(v,96.0);
        checkSphere(v,0,48.0,transformedCenter);
        v.undoPull();
        checkSphere(v,0,44.75,transformedCenter);
        v.redoPull();
        checkSphere(v,0,48.0,transformedCenter);

        v.selectParts({0});
        v.duplicateSelectedParts();
        require(v.boxes_.size() == 2, "sphere duplication failed");
        const gp_Pnt duplicateCenter = center(v,1);
        near(duplicateCenter.Distance(transformedCenter.Translated(gp_Vec(25,0,0))),
             0.0,"duplicate offset incorrect");
        checkSphere(v,1,48.0,duplicateCenter);
        v.setPartVisible(0,false);
        v.fitAll();
        pickSphere(v,1);
        const QPoint duplicatePull = beginPull(v);
        release(v,duplicatePull);
        exact(v,80.0);
        checkSphere(v,1,40.0,duplicateCenter);
        checkSphere(v,0,48.0,transformedCenter);
        v.undoPull();
        checkSphere(v,1,48.0,duplicateCenter);
        v.redoPull();
        checkSphere(v,1,40.0,duplicateCenter);

        // Cylinder regression: pulling the bottom down keeps the top fixed.
        v.addCylinder();
        v.arrowPartIndex_ = 2;
        v.arrowVisible_ = true;
        v.cylinderRadialArrow_ = false;
        v.primitiveArrowDirection_ = gp_Dir(0,0,-1);
        const auto& cylinder = v.boxes_[2];
        const double x = (cylinder.baseMin[0]+cylinder.baseMax[0])*0.5;
        v.primitiveArrowOrigin_ = gp_Pnt(x,0,0);
        v.updateFaceArrow();
        const QPoint capPull = beginPull(v);
        checkDimension(v,80.0,false);
        require(v.applyPrimitivePull(12.5), "bottom cap pull failed");
        v.dragDistance_ = 12.5;
        release(v,capPull);
        exact(v,95.0);
        near(v.boxes_[2].min[2],-15.0,"bottom cap did not move down");
        near(v.boxes_[2].max[2],80.0,"top cap moved during bottom pull");
        near(v.boxes_[2].cylinderHeight,95.0,"cylinder height incorrect");
        v.undoPull();
        near(v.boxes_[2].cylinderHeight,80.0,"cylinder undo failed");
        near(v.boxes_[2].cylinderBaseOffset,0.0,"cylinder base undo failed");
        v.redoPull();
        near(v.boxes_[2].cylinderBaseOffset,-15.0,"cylinder base redo failed");
        require(BRepCheck_Analyzer(v.boxes_[2].shape).IsValid(),"invalid cylinder after redo");

        // The shared path must retain independent top-cap and radial edits too.
        v.arrowPartIndex_ = 2;
        v.arrowVisible_ = true;
        v.primitiveArrowDirection_ = gp_Dir(0,0,1);
        v.primitiveArrowOrigin_ = gp_Pnt(x,0,80);
        v.updateFaceArrow();
        const QPoint topPull = beginPull(v);
        require(v.applyPrimitivePull(5.0), "top cap pull failed");
        v.dragDistance_ = 5.0;
        release(v,topPull);
        exact(v,100.0);
        near(v.boxes_[2].min[2],-15.0,"bottom moved during top pull");
        near(v.boxes_[2].max[2],85.0,"top cap did not move");
        v.undoPull();
        near(v.boxes_[2].max[2],80.0,"top cap undo failed");
        v.redoPull();
        v.arrowPartIndex_ = 2;
        v.arrowVisible_ = true;
        v.cylinderRadialArrow_ = true;
        v.primitiveArrowDirection_ = gp_Dir(1,0,0);
        v.primitiveArrowOrigin_ = gp_Pnt(x+40,0,35);
        v.updateFaceArrow();
        const QPoint radialPull = beginPull(v);
        checkDimension(v,80.0,true);
        require(v.applyPrimitivePull(7.0), "cylinder radial pull failed");
        v.dragDistance_ = 7.0;
        release(v,radialPull);
        exact(v,94.50);
        near(v.boxes_[2].cylinderRadius,47.25,"cylinder radial edit failed");
        near(v.boxes_[2].cylinderHeight,100.0,"radial pull changed cylinder height");
        v.undoPull();
        near(v.boxes_[2].cylinderRadius,40.0,"cylinder radial undo failed");
        v.redoPull();
        near(v.boxes_[2].cylinderRadius,47.25,"cylinder radial redo failed");

        // A primitive created away from the origin must retain its own center.
        v.addSphere();
        v.setPartVisible(1,false);
        v.setPartVisible(2,false);
        gp_Trsf offOriginRotation;
        offOriginRotation.SetRotation(gp_Ax1(gp_Pnt(0,0,0),gp_Dir(0,1,0)),0.4);
        v.applyPartState(3,v.boxes_[3].min,v.boxes_[3].max,translation*offOriginRotation);
        v.fitAll();
        const gp_Pnt offOriginCenter = center(v,3);
        pickSphere(v,3);
        const QPoint offOriginPull = beginPull(v);
        release(v,offOriginPull);
        exact(v,85.0);
        checkSphere(v,3,42.5,offOriginCenter);
        v.undoPull();
        checkSphere(v,3,40.0,offOriginCenter);
        v.redoPull();
        checkSphere(v,3,42.5,offOriginCenter);

        // Accepting an unchanged decorated diameter must not reinterpret it as a delta.
        pickSphere(v,3);
        const QPoint unchangedPull = beginPull(v);
        release(v,unchangedPull);
        require(QMetaObject::invokeMethod(v.dragDimension_,"returnPressed",Qt::DirectConnection),
                "could not accept unchanged dimension");
        checkSphere(v,3,42.5,offOriginCenter);

        // Reuse a resized, transformed cylinder as the source of a duplicate.
        v.setPartVisible(2,true);
        v.applyPartState(2,v.boxes_[2].min,v.boxes_[2].max,transform);
        v.selectParts({2});
        v.duplicateSelectedParts();
        require(v.boxes_.size() == 5, "cylinder duplication failed");
        v.setPartVisible(2,false);
        v.setPartVisible(3,false);
        v.fitAll();
        const gp_Pnt fixedBottom = capCenter(v,4,false);
        const gp_Pnt fixedTop = capCenter(v,4,true);
        prepareCylinderArrow(v,4,true,false);
        const QPoint copyRadial = beginPull(v);
        dragBy(v,copyRadial,6.0);
        checkDimension(v,2.0*v.boxes_[4].cylinderRadius,true);
        const QString draggedCylinderDiameter = v.dragDimension_->text();
        release(v,copyRadial);
        require(v.dragDimension_->text() == draggedCylinderDiameter,"cylinder diameter changed on release");
        exact(v,120.25);
        near(v.boxes_[4].cylinderRadius,60.125,"absolute duplicate diameter incorrect");
        near(v.boxes_[2].cylinderRadius,47.25,"duplicate diameter changed source");
        near(capCenter(v,4,false).Distance(fixedBottom),0.0,"radial edit moved bottom");
        near(capCenter(v,4,true).Distance(fixedTop),0.0,"radial edit moved top");
        v.undoPull();
        near(v.boxes_[4].cylinderRadius,47.25,"duplicate diameter undo failed");
        v.redoPull();
        near(v.boxes_[4].cylinderRadius,60.125,"duplicate diameter redo failed");

        prepareCylinderArrow(v,4,false,false);
        const QPoint copyBottom = beginPull(v);
        dragBy(v,copyBottom,8.0);
        checkDimension(v,v.boxes_[4].cylinderHeight,false);
        const double draggedHeight = v.boxes_[4].cylinderHeight;
        const QString draggedHeightText = v.dragDimension_->text();
        release(v,copyBottom);
        require(v.dragDimension_->text() == draggedHeightText,"height changed on release");
        exact(v,0.0);
        near(v.boxes_[4].cylinderHeight,draggedHeight,"zero height changed geometry");
        require(v.primitivePullEditing_,"invalid height closed editor");
        exact(v,120.50);
        near(v.boxes_[4].cylinderHeight,120.50,"absolute bottom height incorrect");
        near(capCenter(v,4,true).Distance(fixedTop),0.0,"transformed top moved with bottom pull");
        v.undoPull();
        near(capCenter(v,4,false).Distance(fixedBottom),0.0,"bottom undo did not restore location");
        v.redoPull();
        near(capCenter(v,4,true).Distance(fixedTop),0.0,"bottom redo moved top");

        const gp_Pnt newBottom = capCenter(v,4,false);
        prepareCylinderArrow(v,4,false,true);
        const QPoint copyTop = beginPull(v);
        dragBy(v,copyTop,-7.0);
        checkDimension(v,v.boxes_[4].cylinderHeight,false);
        release(v,copyTop);
        exact(v,90.25);
        near(v.boxes_[4].cylinderHeight,90.25,"absolute top height incorrect");
        near(capCenter(v,4,false).Distance(newBottom),0.0,"transformed bottom moved with top pull");
        v.undoPull();
        near(v.boxes_[4].cylinderHeight,120.50,"absolute top height undo failed");
        v.redoPull();
        near(v.boxes_[4].cylinderHeight,90.25,"absolute top height redo failed");
        require(BRepCheck_Analyzer(v.boxes_[4].shape).IsValid(),"invalid transformed cylinder");

        // A second locale must use its own decimal separator, including while editing.
        QLocale::setDefault(QLocale(QLocale::English,QLocale::UnitedStates));
        v.dragDimension_->findChild<QValidator*>()->setLocale(QLocale());
        prepareCylinderArrow(v,4,true,false);
        const QPoint englishPull = beginPull(v);
        release(v,englishPull);
        require(v.dragDimension_->text() == QStringLiteral("Ø 120.25 mm"),"English diameter format incorrect");
        exact(v,100.0,true);
        near(v.boxes_[4].cylinderRadius,50.0,"English absolute input incorrect");
        checkDimension(v,100.0,true);
    }

    using ConeTarget = CadViewport::ConePullTarget;
    static gp_Pnt coneEnd(const CadViewport& v, size_t index, bool top) {
        const auto& part = v.boxes_[index];
        return gp_Pnt((part.baseMin[0]+part.baseMax[0])*0.5,
            (part.baseMin[1]+part.baseMax[1])*0.5,
            part.baseMin[2]+part.cone.baseOffset+(top ? part.cone.height : 0.0))
            .Transformed(part.orientation);
    }
    static void checkCone(const CadViewport& v, size_t index, double bottomDiameter,
                          double topDiameter, double height) {
        const auto& part = v.boxes_[index];
        require(part.conePrimitive, "cone metadata lost");
        require(part.geometryKind == CadViewport::CadPart::GeometryKind::GeneralBRep,
                "cone is not a GeneralBRep");
        near(part.cone.bottomRadius*2.0,bottomDiameter,"incorrect cone bottom diameter");
        near(part.cone.topRadius*2.0,topDiameter,"incorrect cone top diameter");
        near(part.cone.height,height,"incorrect cone height");
        require(BRepCheck_Analyzer(part.shape).IsValid(),"invalid cone B-Rep");
        GProp_GProps properties;
        BRepGProp::VolumeProperties(part.shape,properties);
        const double expected = std::acos(-1.0)*height/12.0 *
            (bottomDiameter*bottomDiameter+bottomDiameter*topDiameter+topDiameter*topDiameter);
        require(std::abs(properties.Mass()/expected-1.0) < 1.0e-7,"incorrect cone volume");
        require(part.presentation->Shape().IsSame(part.shape),"cone presentation out of sync");
        const gp_Pnt localCenter = v.partLocalCenter(part);
        near(localCenter.Transformed(part.orientation).Distance(
            localCenter.Transformed(part.presentation->LocalTransformation())),
            0.0,"cone presentation transform out of sync");
    }
    static void pickCone(CadViewport& v, size_t index) {
        const gp_Pnt bottom = coneEnd(v,index,false);
        const gp_Pnt interior = bottom.Translated(gp_Vec(bottom,coneEnd(v,index,true))*0.4);
        click(v,project(v,interior));
        require(v.arrowVisible_ && v.arrowPartIndex_ == index,"cone surface picking failed");
        for (const auto& arrow : v.coneArrows_)
            require(!arrow.IsNull(),"missing cone pull manipulator");
    }
    static QPoint beginConePull(CadViewport& v, size_t index, ConeTarget target) {
        pickCone(v,index);
        const auto& part = v.boxes_[index];
        const gp_Pnt grab = v.coneArrowOrigin(part,target)
            .Translated(gp_Vec(v.coneArrowDirection(target))*22.0).Transformed(part.orientation);
        const QPoint screen = project(v,grab);
        // Go through mousePressEvent so hit testing, face selection and editor
        // transitions are exercised just as in the actual application.
        QMouseEvent press(QEvent::MouseButtonPress,QPointF(screen),QPointF(screen),
                          Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
        v.mousePressEvent(&press);
        require(v.primitivePullDragging_,"cone arrow drag did not start");
        require(v.conePullTarget_ == target,"wrong cone arrow picked");
        return screen;
    }
    static void runCone(CadViewport& v) {
        v.addCone();
        checkCone(v,0,80,40,80);
        const gp_Pnt bottom = coneEnd(v,0,false), top = coneEnd(v,0,true);
        QPoint start = beginConePull(v,0,ConeTarget::BottomDiameter);
        checkDimension(v,80,true);
        dragBy(v,start,5.0);
        checkDimension(v,90,true);
        const QString lastDimension = v.dragDimension_->text();
        release(v,start);
        require(v.dragDimension_->text() == lastDimension,"cone diameter changed on release");
        exact(v,100.50,true);
        checkCone(v,0,100.50,40,80);
        near(coneEnd(v,0,false).Distance(bottom),0,"diameter moved cone base");
        near(coneEnd(v,0,true).Distance(top),0,"diameter moved cone top");
        require(v.undoStack_.size() == 1,"cone drag and exact input must be one undo step");
        v.undoPull(); checkCone(v,0,80,40,80);
        v.redoPull(); checkCone(v,0,100.50,40,80);

        start = beginConePull(v,0,ConeTarget::TopDiameter);
        checkDimension(v,40,true);
        dragBy(v,start,-5.0);
        checkDimension(v,30,true);
        release(v,start);
        exact(v,0.0);
        checkCone(v,0,100.50,0,80);
        v.undoPull(); checkCone(v,0,100.50,40,80);
        v.redoPull(); checkCone(v,0,100.50,0,80);
        // A zero-radius apex has no planar top face; its diameter arrow stays available.
        start = beginConePull(v,0,ConeTarget::TopDiameter);
        checkDimension(v,0,true);
        release(v,start);
        exact(v,-1.0);
        checkCone(v,0,100.50,0,80);
        require(v.primitivePullEditing_,"invalid cone diameter closed editor");
        exact(v,60.25);
        checkCone(v,0,100.50,60.25,80);

        start = beginConePull(v,0,ConeTarget::BottomHeight);
        checkDimension(v,80,false);
        dragBy(v,start,6.0);
        checkDimension(v,86,false);
        release(v,start);
        exact(v,95.50);
        checkCone(v,0,100.50,60.25,95.50);
        near(coneEnd(v,0,true).Distance(top),0,"top moved during cone bottom-height pull");
        near(coneEnd(v,0,false).Distance(bottom.Translated(gp_Vec(0,0,-15.50))),
             0,"cone bottom did not move");
        v.undoPull(); checkCone(v,0,100.50,60.25,80);
        v.redoPull(); checkCone(v,0,100.50,60.25,95.50);
        const gp_Pnt newBottom = coneEnd(v,0,false);
        start = beginConePull(v,0,ConeTarget::TopHeight);
        dragBy(v,start,-4.0);
        checkDimension(v,91.50,false);
        release(v,start);
        exact(v,0.0);
        require(v.primitivePullEditing_,"invalid cone height closed editor");
        exact(v,70.25);
        checkCone(v,0,100.50,60.25,70.25);
        near(coneEnd(v,0,false).Distance(newBottom),0,"bottom moved during top-height pull");
        v.undoPull(); checkCone(v,0,100.50,60.25,95.50);
        v.redoPull(); checkCone(v,0,100.50,60.25,70.25);

        // Equal radii and an upper radius larger than the lower radius remain editable.
        start = beginConePull(v,0,ConeTarget::TopDiameter);
        release(v,start); exact(v,100.50);
        checkCone(v,0,100.50,100.50,70.25);
        start = beginConePull(v,0,ConeTarget::TopDiameter);
        release(v,start); exact(v,120.0);
        checkCone(v,0,100.50,120,70.25);
        v.undoPull(); checkCone(v,0,100.50,100.50,70.25);
        v.redoPull(); checkCone(v,0,100.50,120,70.25);

        // Exercise the real move/rotation gizmo command lifecycles and their undo.
        v.setDirectMoveMode(true);
        v.selectParts({0});
        const gp_Pnt beforeMove = coneEnd(v,0,false);
        const gp_Pnt gizmoCenter = v.partLocalGizmoCenter(v.boxes_[0]);
        const QPoint moveGrab = project(v,gizmoCenter.Translated(gp_Vec(25,0,0)));
        require(v.beginMoveAxisDrag(moveGrab),"cone move gizmo could not be grabbed");
        dragBy(v,moveGrab,130.0);
        QMouseEvent transformRelease(QEvent::MouseButtonRelease,QPointF(moveGrab),QPointF(moveGrab),
                                     Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
        v.mouseReleaseEvent(&transformRelease);
        exact(v,130.0);
        near(coneEnd(v,0,false).Distance(beforeMove.Translated(gp_Vec(130,0,0))),
             0,"cone move distance incorrect");
        v.undoPull();
        near(coneEnd(v,0,false).Distance(beforeMove),0,"cone move undo failed");
        v.redoPull();
        near(coneEnd(v,0,false).Distance(beforeMove.Translated(gp_Vec(130,0,0))),
             0,"cone move redo failed");
        v.setDirectMoveMode(false);
        v.setRotateMode(true);
        const gp_Pnt beforeRotation = coneEnd(v,0,false);
        const auto& part = v.boxes_[0];
        const gp_Pnt pivot = v.partLocalGizmoCenter(part).Transformed(part.orientation);
        const double ringRadius = 0.65*std::max({part.max[0]-part.min[0],
            part.max[1]-part.min[1],part.max[2]-part.min[2]});
        const gp_Circ ring(gp_Ax2(pivot,gp_Dir(0,1,0),gp_Dir(1,0,0)),ringRadius);
        const QPoint rotationGrab = project(v,ElCLib::Value(0.7,ring));
        require(v.beginRotationDrag(rotationGrab),"cone rotation gizmo could not be grabbed");
        v.applyRotationAngle(35.0);
        v.mouseReleaseEvent(&transformRelease);
        exact(v,35.0);
        const gp_Pnt rotatedBottom = coneEnd(v,0,false);
        require(rotatedBottom.Distance(beforeRotation)>1.0,"cone did not rotate");
        v.undoPull();
        near(coneEnd(v,0,false).Distance(beforeRotation),0,"cone rotation undo failed");
        v.redoPull();
        near(coneEnd(v,0,false).Distance(rotatedBottom),0,"cone rotation redo failed");
        v.setRotateMode(false);
        v.duplicateSelectedParts();
        require(v.boxes_.size() == 2,"cone duplication failed");
        checkCone(v,1,100.50,120,70.25);
        v.setPartVisible(0,false);
        v.fitAll();
        const gp_Pnt copyTop = coneEnd(v,1,true), copyBottom = coneEnd(v,1,false);
        start = beginConePull(v,1,ConeTarget::BottomDiameter);
        release(v,start); exact(v,90.25);
        checkCone(v,1,90.25,120,70.25);
        checkCone(v,0,100.50,120,70.25);
        near(coneEnd(v,1,false).Distance(copyBottom),0,"transformed diameter moved bottom");
        v.undoPull(); checkCone(v,1,100.50,120,70.25);
        v.redoPull(); checkCone(v,1,90.25,120,70.25);
        start = beginConePull(v,1,ConeTarget::BottomHeight);
        release(v,start); exact(v,100.0);
        near(coneEnd(v,1,true).Distance(copyTop),0,"transformed cone top moved");
        v.undoPull(); checkCone(v,1,90.25,120,70.25);
        v.redoPull(); checkCone(v,1,90.25,120,100);
        const gp_Pnt movedBottom = coneEnd(v,1,false);
        start = beginConePull(v,1,ConeTarget::TopHeight);
        release(v,start); exact(v,85.25);
        near(coneEnd(v,1,false).Distance(movedBottom),0,"transformed cone bottom moved");
        v.undoPull(); checkCone(v,1,90.25,120,100);
        v.redoPull(); checkCone(v,1,90.25,120,85.25);
        start = beginConePull(v,1,ConeTarget::TopDiameter);
        release(v,start); exact(v,0.0);
        checkCone(v,1,90.25,0,85.25);
        v.undoPull(); checkCone(v,1,90.25,120,85.25);
        v.redoPull(); checkCone(v,1,90.25,0,85.25);
        start = beginConePull(v,1,ConeTarget::TopDiameter);
        release(v,start); exact(v,50.25);
        checkCone(v,1,90.25,50.25,85.25);

        v.setDirectMoveMode(true);
        for (const auto& arrow : v.coneArrows_) require(arrow.IsNull(),"cone arrows survive transform mode");
        v.setDirectMoveMode(false);
        v.setPartVisible(1,false);
        v.addBox();
        const auto boxBefore = v.boxes_[2];
        const gp_Pnt boxTop((boxBefore.min[0]+boxBefore.max[0])*0.5,
            (boxBefore.min[1]+boxBefore.max[1])*0.5,boxBefore.max[2]);
        click(v,project(v,boxTop));
        require(v.arrowVisible_ && v.arrowPartIndex_ == 2 && v.arrowAxis_ == 2 && v.arrowMax_,
                "box top-face arrow regression");
        const QPoint boxGrab = project(v,boxTop.Translated(gp_Vec(0,0,15)));
        require(v.beginArrowDrag(boxGrab),"box arrow drag regression");
        dragBy(v,boxGrab,5.0);
        QMouseEvent boxRelease(QEvent::MouseButtonRelease,QPointF(boxGrab),QPointF(boxGrab),
                              Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
        v.mouseReleaseEvent(&boxRelease);
        exact(v,10.25);
        near(v.boxes_[2].max[2],boxBefore.max[2]+10.25,"box relative input changed");
        v.undoPull(); near(v.boxes_[2].max[2],boxBefore.max[2],"box undo regression");
        v.redoPull(); near(v.boxes_[2].max[2],boxBefore.max[2]+10.25,"box redo regression");
    }

    static gp_Pnt expectedConeCenter(const CadViewport& v, size_t index) {
        const auto& part = v.boxes_[index];
        const auto& c = part.cone;
        // Independent analytical centroid, measured from the lower end plane.
        const double r = c.bottomRadius, t = c.topRadius;
        const double z = c.height*(r*r+2*r*t+3*t*t)/(4*(r*r+r*t+t*t));
        return gp_Pnt((part.baseMin[0]+part.baseMax[0])*0.5,
            (part.baseMin[1]+part.baseMax[1])*0.5,part.baseMin[2]+c.baseOffset+z)
            .Transformed(part.orientation);
    }
    static void checkDisplayedGizmos(const CadViewport& v, const gp_Pnt& expected, bool group = false) {
        const auto& part = v.boxes_[v.rotationIndex_];
        const double ringRadius = group ? 60.0 : 0.65*std::max({part.max[0]-part.min[0],
            part.max[1]-part.min[1],part.max[2]-part.min[2]});
        for (int axis = 0; axis < 3; ++axis) {
            require(!v.moveAxes_[axis].IsNull(),"missing move gizmo");
            bool foundShaft = false;
            for (TopExp_Explorer faces(v.moveAxes_[axis]->Shape(),TopAbs_FACE);faces.More();faces.Next()) {
                const BRepAdaptor_Surface surface(TopoDS::Face(faces.Current()));
                if (surface.GetType() != GeomAbs_Cylinder) continue;
                near(surface.Cylinder().Location().Distance(expected),0,"move shaft is not at centroid");
                foundShaft = true;
            }
            require(foundShaft,"move shaft geometry missing");
            require(!v.rotationRings_[axis].IsNull(),"missing rotation gizmo");
            bool foundSection = false;
            for (TopExp_Explorer edges(v.rotationRings_[axis]->Shape(),TopAbs_EDGE);edges.More();edges.Next()) {
                const BRepAdaptor_Curve curve(TopoDS::Edge(edges.Current()));
                if (curve.GetType() != GeomAbs_Circle || std::abs(curve.Circle().Radius()-1.7)>1.0e-7)
                    continue;
                // The circular shaft profile lies on the rotation arc's centerline.
                near(curve.Circle().Location().Distance(expected),ringRadius,
                     "rotation arc is not centered on centroid");
                foundSection = true;
            }
            require(foundSection,"rotation shaft profile missing");
        }
    }
    static void checkGizmo(CadViewport& v, size_t index, const gp_Pnt& expected) {
        v.setDirectMoveMode(true);
        v.setRotateMode(true);
        v.selectParts({index});
        near(v.partLocalGizmoCenter(v.boxes_[index]).Transformed(v.boxes_[index].orientation)
            .Distance(expected),0,"incorrect world gizmo center");
        checkDisplayedGizmos(v,expected);
        const gp_Dir x = gp_Dir(1,0,0).Transformed(v.boxes_[index].orientation);
        require(v.beginMoveAxisDrag(project(v,expected.Translated(gp_Vec(x)*25.0))),
                "move hit testing is not aligned with centroid");
        v.draggingFace_ = false;
        const gp_Dir y = gp_Dir(0,1,0).Transformed(v.boxes_[index].orientation);
        const auto& part = v.boxes_[index];
        const double radius = 0.65*std::max({part.max[0]-part.min[0],part.max[1]-part.min[1],
            part.max[2]-part.min[2]});
        const gp_Circ ring(gp_Ax2(expected,y,x),radius);
        const QPoint onRing = project(v,ElCLib::Value(0.7,ring));
        require(v.beginRotationDrag(onRing),"rotation hit testing is not aligned with centroid");
        v.applyRotationAngle(25.0);
        near(v.partLocalGizmoCenter(v.boxes_[index]).Transformed(v.boxes_[index].orientation)
            .Distance(expected),0,"actual rotation pivot differs from gizmo centroid");
        checkDisplayedGizmos(v,expected);
        // Restore the test rotation so later independent expectations use the same transform.
        v.applyRotationAngle(0.0);
        v.draggingRotation_ = false;
        v.setRotateMode(false);
        v.setDirectMoveMode(false);
    }
    static void runGizmo(CadViewport& v) {
        v.addBox(); v.addCylinder(); v.addSphere(); v.addCone();
        checkGizmo(v,0,gp_Pnt(50,40,30));
        checkGizmo(v,1,gp_Pnt(125,0,40));
        checkGizmo(v,2,gp_Pnt(250,0,0));
        checkGizmo(v,3,gp_Pnt(375,0,220.0/7.0));
        for (size_t i = 0; i < 3; ++i) v.setPartVisible(i,false);
        QPoint start = beginConePull(v,3,ConeTarget::TopDiameter);
        release(v,start); exact(v,0);
        checkGizmo(v,3,gp_Pnt(375,0,20)); // A pointed cone has centroid at h/4.
        start = beginConePull(v,3,ConeTarget::TopDiameter);
        release(v,start); exact(v,60);
        checkGizmo(v,3,expectedConeCenter(v,3));
        start = beginConePull(v,3,ConeTarget::BottomDiameter);
        release(v,start); exact(v,100);
        checkGizmo(v,3,expectedConeCenter(v,3));
        start = beginConePull(v,3,ConeTarget::BottomHeight);
        release(v,start); exact(v,100);
        checkGizmo(v,3,expectedConeCenter(v,3));
        start = beginConePull(v,3,ConeTarget::TopHeight);
        release(v,start); exact(v,70);
        checkGizmo(v,3,expectedConeCenter(v,3));
        // Undo/redo while the transform tools are enabled must refresh after B-Rep restore.
        v.setDirectMoveMode(true); v.setRotateMode(true); v.selectParts({3});
        v.undoPull();
        near(v.boxes_[3].cone.height,100,"cone height undo failed");
        checkDisplayedGizmos(v,expectedConeCenter(v,3));
        v.redoPull();
        near(v.boxes_[3].cone.height,70,"cone height redo failed");
        checkDisplayedGizmos(v,expectedConeCenter(v,3));
        v.setRotateMode(false); v.setDirectMoveMode(false);
        gp_Trsf rotation,translation;
        rotation.SetRotation(gp_Ax1(gp_Pnt(0,0,0),gp_Dir(1,2,3)),0.6);
        translation.SetTranslation(gp_Vec(130,-35,22));
        v.applyPartState(3,v.boxes_[3].min,v.boxes_[3].max,translation*rotation);
        checkGizmo(v,3,expectedConeCenter(v,3));
        v.selectParts({3}); v.duplicateSelectedParts();
        checkGizmo(v,4,expectedConeCenter(v,4));
        near(expectedConeCenter(v,4).Distance(expectedConeCenter(v,3).Translated(gp_Vec(25,0,0))),
             0,"duplicate centroid did not follow world translation");
        v.setPartVisible(3,false); v.fitAll();
        start = beginConePull(v,4,ConeTarget::TopDiameter);
        release(v,start); exact(v,0);
        checkGizmo(v,4,expectedConeCenter(v,4));
        v.undoPull(); checkGizmo(v,4,expectedConeCenter(v,4));
        v.redoPull(); checkGizmo(v,4,expectedConeCenter(v,4));

        // Cylinder cap Pull also changes its actual center; the sphere stays concentric.
        v.setPartVisible(4,false); v.setPartVisible(1,true); v.fitAll();
        prepareCylinderArrow(v,1,false,false);
        start = beginPull(v); release(v,start); exact(v,100);
        checkGizmo(v,1,gp_Pnt(125,0,30));
        prepareCylinderArrow(v,1,false,true);
        start = beginPull(v); release(v,start); exact(v,90);
        checkGizmo(v,1,gp_Pnt(125,0,25));
        v.setPartVisible(1,false); v.setPartVisible(2,true); v.fitAll();
        pickSphere(v,2); start = beginPull(v); release(v,start); exact(v,120);
        checkGizmo(v,2,gp_Pnt(250,0,0));

        // Keep the established equal-weight multipart pivot instead of a mass-weighted one.
        v.setPartVisible(0,true); v.setPartVisible(3,true);
        const gp_Pnt a(50,40,30), b = expectedConeCenter(v,3);
        const gp_Pnt average((a.X()+b.X())*0.5,(a.Y()+b.Y())*0.5,(a.Z()+b.Z())*0.5);
        v.setDirectMoveMode(true); v.setRotateMode(true); v.selectParts({0,3});
        near(v.selectionCenter().Distance(average),0,"multipart averaging changed");
        checkDisplayedGizmos(v,average,true);
        const gp_Circ groupRing(gp_Ax2(average,gp_Dir(0,1,0),gp_Dir(1,0,0)),60);
        const QPoint grab = project(v,ElCLib::Value(0.7,groupRing));
        require(v.beginRotationDrag(grab),"multipart rotation picking failed");
        v.applyRotationAngle(30);
        near(v.selectionCenter().Distance(average),0,"group pivot drifted during rotation");
        checkDisplayedGizmos(v,average,true);
        QMouseEvent releaseGroup(QEvent::MouseButtonRelease,QPointF(grab),QPointF(grab),
                                Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
        v.mouseReleaseEvent(&releaseGroup); exact(v,30);
        v.undoPull();
        near(v.partLocalGizmoCenter(v.boxes_[0]).Transformed(v.boxes_[0].orientation).Distance(a),
             0,"multipart undo changed box center");
        near(expectedConeCenter(v,3).Distance(b),0,"multipart undo changed cone center");
        checkDisplayedGizmos(v,average,true);
        v.redoPull();
        near(v.selectionCenter().Distance(average),0,"multipart redo changed pivot");
        checkDisplayedGizmos(v,average,true);
    }

    static gp_Pnt capCenter(const CadViewport& v, size_t index, bool top) {
        const auto& part = v.boxes_[index];
        return gp_Pnt((part.baseMin[0]+part.baseMax[0])*0.5,
                      (part.baseMin[1]+part.baseMax[1])*0.5,
                      part.baseMin[2]+part.cylinderBaseOffset+(top?part.cylinderHeight:0.0))
            .Transformed(part.orientation);
    }
    static void prepareCylinderArrow(CadViewport& v, size_t index, bool radial, bool top) {
        const auto& part = v.boxes_[index];
        v.arrowPartIndex_ = index;
        v.arrowVisible_ = true;
        v.cylinderRadialArrow_ = radial;
        v.primitiveArrowDirection_ = radial ? gp_Dir(1,0,0) : gp_Dir(0,0,top?1:-1);
        v.primitiveArrowOrigin_ = capCenter(v,index,false).Transformed(part.orientation.Inverted());
        if (radial) {
            v.primitiveArrowOrigin_.SetX(v.primitiveArrowOrigin_.X()+part.cylinderRadius);
            v.primitiveArrowOrigin_.SetZ(v.primitiveArrowOrigin_.Z()+part.cylinderHeight*0.5);
        } else if (top) {
            v.primitiveArrowOrigin_.SetZ(v.primitiveArrowOrigin_.Z()+part.cylinderHeight);
        }
        v.updateFaceArrow();
    }
};

int main(int argc, char** argv) {
#if defined(Q_OS_LINUX)
    // Match main.cpp: OCCT embeds an X11 drawable, including on Wayland desktops.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM") &&
        !qEnvironmentVariableIsEmpty("DISPLAY"))
        qputenv("QT_QPA_PLATFORM", QByteArray("xcb"));
#endif
    QLocale::setDefault(QLocale(QLocale::Dutch,QLocale::Netherlands));
    QApplication app(argc,argv);
    CadViewport viewport;
    viewport.resize(1000,800);
    viewport.show();
    app.processEvents();
    try {
        if (app.arguments().contains(QStringLiteral("--gizmo")))
            CadViewportPullTest::runGizmo(viewport);
        else if (app.arguments().contains(QStringLiteral("--cone")))
            CadViewportPullTest::runCone(viewport);
        else
            CadViewportPullTest::run(viewport);
        std::cout << "Absolute diameters/heights, localized display/input, drag/release, transforms, duplication and undo/redo passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
