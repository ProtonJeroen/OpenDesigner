#include "CadViewport.h"
#include "CylindricalMoveGeometry.h"
#include "BRepReconstruction.h"
#include "BooleanGeometry.h"
#include <QFile>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QTimer>
#include <QDialogButtonBox>
#include <QMenu>
#include <QTemporaryDir>
#include <Graphic3d_ZLayerId.hxx>
#include <Image_PixMap.hxx>
#include <QPushButton>
#include <QKeyEvent>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRep_Tool.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepTools.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <sstream>
#include <StdSelect_BRepOwner.hxx>
#include <QLineEdit>
#include <QLocale>
#include <QMouseEvent>
#include <QValidator>
#include <BRepCheck_Analyzer.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepFeat_SplitShape.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
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
    static void requireRenderedArrow(const CadViewport& v, const gp_Pnt& grab) {
        Image_PixMap pixels;
        require(v.view_->ToPixMap(pixels,v.width(),v.height()),"cannot capture rendered Pull arrows");
        const QPoint point=project(v,grab);
        bool orange=false;
        for (int x=point.x()-6;x<=point.x()+6;++x)
            for (int y=point.y()-6;y<=point.y()+6;++y) {
                const auto color=pixels.PixelColor(x,y).GetRGB();
                orange=orange || (color.Red()>0.2 && color.Green()>0.05
                    && color.Red()>1.15*color.Green() && color.Green()>1.5*color.Blue());
            }
        require(orange,"Pull arrow is hidden in the rendered viewport");
    }
    static void pickSphere(CadViewport& v, size_t index) {
        // Start a fresh Face pick: a second click on a selected Face promotes
        // it to a Part in automatic selection mode.
        v.selectParts({});
        // The center click also covers the screen-right radial fallback.
        click(v,project(v,center(v,index)));
        const auto selection=v.selectionContext();
        require(selection.kind==CadViewport::SelectionContext::Kind::Faces
                && selection.faces.size()==1 && selection.faces.front().partIndex==index
                && selection.edges.empty()
                && BRepAdaptor_Surface(selection.faces.front().face).GetType()==GeomAbs_Sphere,
                "sphere surface picking did not select its owning Face");
        v.startPrimitivePull();
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
        require(v.dragDimension_->isVisible(), "pull dimension editor is not visible");
        require(v.commandPanel_->isAncestorOf(v.dragDimension_),
                "pull dimension editor is outside the command panel");
        require(v.commandPanel_->rect().contains(v.dragDimension_->mapTo(v.commandPanel_,v.dragDimension_->rect().center())),
                "pull dimension editor is outside the command panel geometry");
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
        const auto originalShape=v.boxes_[0].shape;
        pickSphere(v,0); const auto cancelled=beginPull(v); dragBy(v,cancelled,7); v.cancelCommandSession();
        require(v.boxes_[0].shape.IsSame(originalShape) && v.undoStack_.empty() && !v.arrowVisible_,"Sphere Cancel failed to restore original/close tool");
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
        require(v.commandSession_ && v.commandPanel_->isVisible(), "invalid radius closed the editor");
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
        require(v.commandSession_ && v.commandPanel_->isVisible(),"invalid height closed editor");
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

    static void runCylindricalFace() {
        CadViewport v;
        v.resize(1000,800); v.show(); QApplication::processEvents();
        v.addBox();
        const auto tool=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(50,40,0),gp_Dir(0,0,1)),20,60).Shape();
        v.boxes_[0].shape=BRepAlgoAPI_Cut(v.boxes_[0].shape,tool).Shape();
        const auto originalShape=v.boxes_[0].shape;
        v.boxes_[0].geometryKind=CadViewport::CadPart::GeometryKind::GeneralBRep;
        v.boxes_[0].baseMin=v.boxes_[0].min; v.boxes_[0].baseMax=v.boxes_[0].max;
        v.boxes_[0].presentation->SetShape(v.boxes_[0].shape);
        v.context_->Redisplay(v.boxes_[0].presentation,Standard_False);
        v.fitAll();
        TopoDS_Face cylinderFace;
        for(TopExp_Explorer it(v.boxes_[0].shape,TopAbs_FACE);it.More();it.Next()) {
            const auto face=TopoDS::Face(it.Current());
            if(BRepAdaptor_Surface(face).GetType()==GeomAbs_Cylinder) {cylinderFace=face;break;}
        }
        require(!cylinderFace.IsNull(),"cylindrical hole face missing");
        v.context_->ClearSelected(Standard_False);
        v.context_->AddOrRemoveSelected(new StdSelect_BRepOwner(cylinderFace,v.boxes_[0].presentation,5,Standard_True),Standard_False);
        std::unique_ptr<QMenu> menu(v.createSelectionMenu(QPoint(400,300)));
        auto* resize=menu->findChild<QAction*>("context.resize");
        require(resize,"general cylindrical face omitted Pull / Resize");
        resize->trigger();
        require(v.pullMode_ && v.cylindricalFacePull_ && v.arrowVisible_,"cylindrical Pull did not start");
        const QPoint start=project(v,v.primitiveArrowOrigin_.Translated(gp_Vec(v.primitiveArrowDirection_)*15.0));
        require(v.beginArrowDrag(start),"cylindrical Pull arrow could not be grabbed");
        checkDimension(v,40.0,true);
        dragBy(v,start,5.0);
        checkDimension(v,50.0,true);
        exact(v,50.0,true);
        require(v.undoStack_.size()==1,"cylindrical Pull did not create one history entry");
        bool radiusFound=false;
        for(TopExp_Explorer it(v.boxes_[0].shape,TopAbs_FACE);it.More();it.Next()) {
            const auto face=TopoDS::Face(it.Current());
            if(BRepAdaptor_Surface(face).GetType()==GeomAbs_Cylinder
                && std::abs(BRepAdaptor_Surface(face).Cylinder().Radius()-25.0)<1.e-5) radiusFound=true;
        }
        require(radiusFound,"cylindrical Pull did not change the hole diameter");
        require(BRepCheck_Analyzer(v.boxes_[0].shape).IsValid(),"cylindrical Pull produced invalid B-Rep");
        v.undoPull();
        require(v.boxes_[0].shape.IsSame(originalShape),"cylindrical Pull Undo path failed");

        // A radius at or below the geometric limit is refused without
        // replacing the source shape and leaves the editor available to cancel.
        v.context_->AddOrRemoveSelected(new StdSelect_BRepOwner(cylinderFace,v.boxes_[0].presentation,5,Standard_True),Standard_False);
        std::unique_ptr<QMenu> invalidMenu(v.createSelectionMenu(QPoint(400,300)));
        invalidMenu->findChild<QAction*>("context.resize")->trigger();
        const QPoint invalidStart=project(v,v.primitiveArrowOrigin_.Translated(gp_Vec(v.primitiveArrowDirection_)*15.0));
        require(v.beginArrowDrag(invalidStart),"invalid cylindrical Pull session did not start");
        require(!v.previewCommand(-19.995),"degenerate cylindrical Pull was accepted");
        require(!v.commandSession_->geometryError.isEmpty(),"invalid cylindrical Pull did not explain the failure");
        require(v.boxes_[0].shape.IsSame(originalShape),"invalid cylindrical Pull changed the source shape");
        v.cancelCommandSession();
    }

    static void runTrimmedBossPull(CadViewport& v,const QString& path) {
        QString error;NativeDocumentSnapshot snapshot;QByteArray bytes;
        const auto stock=BRepPrimAPI_MakeBox(163,80,60).Shape();
        const auto bore=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(54,36,-10),gp_Dir(0,0,1)),17,80).Shape();
        const auto boss=[](double x,double r){return BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(x,36,60),gp_Dir(0,0,1)),r,55).Shape();};
        const auto base=BooleanGeometry::combine<BRepAlgoAPI_Cut>(stock,bore);
        const auto expected=[&](double r) {
            const auto feature=r<=5?boss(158,r):BooleanGeometry::combine<BRepAlgoAPI_Common>(boss(158,r),BRepPrimAPI_MakeBox(gp_Pnt(0,0,60),163,80,55).Shape());
            return BooleanGeometry::combine<BRepAlgoAPI_Fuse>(base,feature);
        };
        const auto radiusFace=[](const TopoDS_Shape& shape,double r) {
            for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next()) {
                const auto f=TopoDS::Face(it.Current());const BRepAdaptor_Surface s(f);
                if(s.GetType()==GeomAbs_Cylinder&&std::abs(s.Cylinder().Radius()-r)<1.e-6) return f;
            }return TopoDS_Face();
        };
        if(!path.isEmpty()) {
            QFile file(path);require(file.open(QIODevice::ReadOnly),"Cannot read native trimmed boss file");bytes=file.readAll();file.close();
            require(NativeDocumentStorage::load(path,snapshot,error),qPrintable(error));
            require(snapshot.parts.size()==1&&BRepReconstruction::equivalent(snapshot.parts[0].shape,expected(18)),"Native document differs from actual post-Trim geometry");
        } else {
            NativePartState p;p.id="trimmed-boss-pull";p.name="Post Trim Pull";p.shape=BooleanGeometry::combine<BRepAlgoAPI_Fuse>(base,boss(121,18));snapshot.parts={p};
        }
        require(v.importDocumentSnapshot(snapshot,error),qPrintable(error));v.fitAll();QApplication::processEvents();
        if(path.isEmpty()) {
            require(v.startMoveFace(0,radiusFace(v.boxes_[0].shape,18)),"Boss Move Face activation failed");
            v.cylinderMoveDirection_->setCurrentIndex(1);v.cylinderBossBoundary_->setCurrentIndex(1);
            v.dragDimension_->setText("37");QMetaObject::invokeMethod(v.dragDimension_,"textEdited",Qt::DirectConnection,Q_ARG(QString,QString("37")));
            require(v.commandSession_->valid,"Boss Trim preview failed");v.commandButtons_->button(QDialogButtonBox::Ok)->click();
            require(!v.commandSession_&&BRepReconstruction::equivalent(v.boxes_[0].shape,expected(18)),"Boss Trim was not confirmed");
        }
        const auto before=v.boxes_[0];const auto source=before.shape;const auto history=v.undoStack_.size();const auto mantle=radiusFace(source,18);
        GProp_GProps wall;BRepGProp::SurfaceProperties(mantle,wall);
        require(wall.Mass()<2*std::acos(-1.)*18*55,"Regression must use a partial cylindrical mantle");
        double volume=0;require(BRepReconstruction::validSolid(source,volume),"Invalid post-Trim B-Rep");
        // Original edges/p-curves are valid. The coredump's bad edge is created
        // later inside OCCT offset intersections, not stored in this input.
        for(TopExp_Explorer f(source,TopAbs_FACE);f.More();f.Next()) for(TopExp_Explorer e(f.Current(),TopAbs_EDGE);e.More();e.Next()) {
            double first=0,last=0;const auto curve=BRep_Tool::CurveOnSurface(TopoDS::Edge(e.Current()),TopoDS::Face(f.Current()),first,last);
            require(!curve.IsNull()&&std::isfinite(first)&&std::isfinite(last),"Post-Trim edge has no valid p-curve");
        }
        std::ostringstream stream;BRepTools::Write(source,stream);const auto stamp=stream.str();
        const auto start=[&](double radius) {
            v.context_->ClearSelected(Standard_False);
            v.context_->AddOrRemoveSelected(new StdSelect_BRepOwner(radiusFace(v.boxes_[0].shape,radius),v.boxes_[0].presentation,5,Standard_True),Standard_False);
            std::unique_ptr<QMenu> menu(v.createSelectionMenu(QPoint(400,300)));auto* resize=menu->findChild<QAction*>("context.resize");
            require(resize,"Trimmed boss has no Pull / Resize action");resize->trigger();
            require(v.pullMode_&&v.cylindricalFacePull_&&v.arrowVisible_,"Trimmed boss Pull activation failed");
            const auto grab=project(v,v.primitiveArrowOrigin_.Translated(gp_Vec(v.primitiveArrowDirection_)*15));
            require(v.beginArrowDrag(grab),"Trimmed boss radial arrow cannot be grabbed");return grab;
        };
        const auto grab=start(18);std::cout<<"Post-Trim radial Pull drag starts (R18 -> about R23)"<<std::endl;
        dragBy(v,grab,5);require(v.commandSession_->valid,"Trimmed boss drag preview failed");
        require(BRepReconstruction::equivalent(v.boxes_[0].shape,expected(v.cylindricalPullRadius_)),"Trimmed boss drag changed unrelated material");
        v.cancelCommandSession();require(v.boxes_[0].shape.IsSame(source)&&v.undoStack_.size()==history,"Radial Cancel changed source/history");
        start(18);require(v.previewCommand(2),"Trimmed boss R20 preview failed");
        require(BRepReconstruction::equivalent(v.boxes_[0].shape,expected(20)),"Radial preview changed stock, boss height or hole");
        const auto valid=v.boxes_[0].shape;require(!v.previewCommand(22)&&!v.commandSession_->geometryError.isEmpty(),"Uncertified support crossing accepted without diagnostic");
        require(BRepReconstruction::equivalent(v.boxes_[0].shape,valid),"Invalid preview corrupted last valid result");
        require(v.previewCommand(2),"Pull could not recover after invalid radius");exact(v,40,true);
        require(!v.commandSession_,"Numeric trimmed-boss diameter did not confirm");
        const auto committed=v.boxes_[0].shape;require(v.undoStack_.size()==history+1&&BRepReconstruction::equivalent(committed,expected(20)),"Radial commit failed");
        require(v.boxes_[0].persistentId==before.persistentId&&v.boxes_[0].presentation==before.presentation,"Pull changed Part identity");
        v.undoPull();require(v.boxes_[0].shape.IsSame(source),"Radial Undo failed");v.redoPull();require(v.boxes_[0].shape.IsSame(committed),"Radial Redo failed");
        start(20);require(v.previewCommand(-1)&&BRepReconstruction::equivalent(v.boxes_[0].shape,expected(19)),"Repeated partial-mantle Pull failed");v.cancelCommandSession();
        // Unsupported neighborhoods refuse at activation, with a diagnostic,
        // without starting a drag or altering the presented Part/history.
        auto unsupported=v.boxes_[0];unsupported.shape=BooleanGeometry::combine<BRepAlgoAPI_Fuse>(base,boss(54,18));v.applyFaceState(0,unsupported);
        v.context_->ClearSelected(Standard_False);v.context_->AddOrRemoveSelected(new StdSelect_BRepOwner(radiusFace(unsupported.shape,18),unsupported.presentation,5,Standard_True),Standard_False);
        QString diagnostic;const auto connection=QObject::connect(&v,&CadViewport::statusMessage,&v,[&](const QString& text){diagnostic=text;});
        std::unique_ptr<QMenu> unsupportedMenu(v.createSelectionMenu(QPoint(400,300)));auto* resize=unsupportedMenu->findChild<QAction*>("context.resize");
        require(resize,"Unsupported solid should explain its radius refusal on activation");resize->trigger();
        require(!v.commandSession_&&!v.pullMode_&&!v.arrowVisible_&&!diagnostic.isEmpty()&&v.boxes_[0].shape.IsSame(unsupported.shape)&&v.undoStack_.size()==history+1,"Unsupported Pull activation changed document or lost diagnostic");
        QObject::disconnect(connection);
        std::ostringstream after;BRepTools::Write(source,after);require(after.str()==stamp,"Radial Pull mutated original source");
        if(!path.isEmpty()) {QFile file(path);require(file.open(QIODevice::ReadOnly)&&file.readAll()==bytes,"Native document changed");}
        std::cout<<"Post-Trim radial Pull geometry, p-curves, drag, refusal, Cancel and Undo/Redo passed\n";
    }

    static void runBoundaryGaps(CadViewport& v) {
        const auto cylinder=[](double x,double y,double z,double r,double h){return BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(x,y,z),gp_Dir(0,0,1)),r,h).Shape();};
        const auto wall=[](const TopoDS_Shape& shape,double radius){std::vector<TopoDS_Face> faces;for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next()) {const auto f=TopoDS::Face(it.Current());const BRepAdaptor_Surface s(f);if(s.GetType()==GeomAbs_Cylinder&&std::abs(s.Cylinder().Radius()-radius)<1.e-7) faces.push_back(f);}return faces;};
        const auto caps=[](const TopoDS_Shape& shape,double height){std::vector<TopoDS_Face> faces;for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next()) {const auto f=TopoDS::Face(it.Current());const BRepAdaptor_Surface s(f);if(s.GetType()!=GeomAbs_Plane) continue;auto n=s.Plane().Axis().Direction();if(f.Orientation()==TopAbs_REVERSED) n.Reverse();if(n.Z()>.99&&std::abs(s.Plane().Location().Z()-(60+height))<1.e-7) faces.push_back(f);}return faces;};
        for(int kind=0;kind<4;++kind) {
            const bool trim=kind==1,notch=kind==2,split=kind==3;const double x=trim?158:166;
            const auto oracle=[&](double radius,double height) {auto body=BooleanGeometry::combine<BRepAlgoAPI_Fuse>(BooleanGeometry::combine<BRepAlgoAPI_Cut>(BRepPrimAPI_MakeBox(163,80,60).Shape(),cylinder(notch?0:54,36,0,notch?radius:17,60)),cylinder(90,65,60,7,20));if(!notch) {auto boss=cylinder(x,36,60,radius,height);if(trim) boss=BooleanGeometry::combine<BRepAlgoAPI_Common>(boss,BRepPrimAPI_MakeBox(gp_Pnt(0,0,60),163,80,height).Shape());body=BooleanGeometry::combine<BRepAlgoAPI_Fuse>(body,boss);}return body;};
            double radius=notch?17:18,height=55;NativeDocumentSnapshot snapshot;NativePartState p;p.id="pull-gap-part";p.name="Current geometry";p.shape=oracle(radius,height);p.geometryKind=1;p.cylinderPrimitive=true;p.cylinderRadius=999;p.cylinderHeight=999;
            if(split) {
                auto patch=wall(p.shape,radius).front();const BRepAdaptor_Surface s(patch);const double u=s.FirstUParameter()+.37*(s.LastUParameter()-s.FirstUParameter());BRepFeat_SplitShape builder(p.shape);builder.Add(BRepBuilderAPI_MakeEdge(s.Value(u,s.FirstVParameter()),s.Value(u,s.LastVParameter())).Edge(),patch);builder.Build();require(builder.IsDone(),"Cannot split UI boss mantle");p.shape=builder.Shape();
                const auto cap=caps(p.shape,height).front();BRepFeat_SplitShape top(p.shape);top.Add(BRepBuilderAPI_MakeEdge(gp_Pnt(x-radius,36,60+height),gp_Pnt(x+radius,36,60+height)).Edge(),cap);top.Build();require(top.IsDone(),"Cannot split UI boss cap");p.shape=top.Shape();require(wall(p.shape,radius).size()>1&&caps(p.shape,height).size()>1,"Joint Pull fixture has no split faces");
            }
            p.transform.SetRotation(gp_Ax1(gp_Pnt(),gp_Dir(1,2,3)),.3);p.transform.SetTranslationPart(gp_Vec(30,-20,25));snapshot.parts.push_back(p);NativePartState other;other.id="pull-other";other.name="Other Part";other.shape=BRepPrimAPI_MakeBox(10,10,10).Shape();other.transform.SetTranslation(gp_Vec(250,0,0));snapshot.parts.push_back(other);QString error;require(v.importDocumentSnapshot(snapshot,error),qPrintable(error));v.fitSelection({0});QApplication::processEvents();
            const auto initial=v.boxes_[0];const auto untouched=v.boxes_[1].shape;std::vector<TopoDS_Shape> states{initial.shape};
            const auto verify=[&](double r,double h) {require(BRepReconstruction::equivalent(v.boxes_[0].shape,oracle(r,h)),"Boundary Pull preview/commit differs from independent material oracle");double mass=0;require(BRepReconstruction::validSolid(v.boxes_[0].shape,mass),"Boundary Pull result is invalid");};
            const auto start=[&](bool heightHandle) {
                v.context_->ClearSelected(Standard_False);const auto faces=heightHandle?caps(v.boxes_[0].shape,height):wall(v.boxes_[0].shape,radius);require(!faces.empty(),"Current Pull faces disappeared");
                for(const auto& f:faces) v.context_->AddOrRemoveSelected(new StdSelect_BRepOwner(f,v.boxes_[0].presentation,5,Standard_True),Standard_False);
                std::unique_ptr<QMenu> menu(v.createSelectionMenu(QPoint(400,300)));auto* action=menu->findChild<QAction*>("context.resize");require(action&&action->isEnabled(),"Current single/joint geometry has no Pull action");action->trigger();
                require(v.pullMode_&&v.cylindricalFacePull_&&v.bossHeightPull_==heightHandle&&v.arrowVisible_,"Current cylindrical Pull handle failed recognition");const auto grab=beginPull(v);checkDimension(v,heightHandle?height:2*radius,!heightHandle);return grab;
            };
            const auto grab=start(false);dragBy(v,grab,1.5);require(v.commandSession_->valid,"Boundary radius drag has no live preview");verify(v.cylindricalPullRadius_,height);v.cancelCommandSession();require(v.boxes_[0].shape.IsSame(initial.shape)&&v.undoStack_.empty(),"Radius Cancel changed source/history");
            if(!notch) {start(true);require(v.previewCommand(4),"Cap height has no live preview");verify(radius,height+4);const auto valid=v.boxes_[0].shape;require(!v.previewCommand(-height)&&!v.commandSession_->geometryError.isEmpty()&&v.boxes_[0].shape.IsSame(valid),"Invalid height lost the last valid preview");v.cancelCommandSession();require(v.boxes_[0].shape.IsSame(initial.shape)&&v.undoStack_.empty(),"Height Cancel changed source/history");}
            start(false);exact(v,2*(radius+1.75),true);radius+=1.75;require(!v.commandSession_,"Absolute diameter did not confirm");verify(radius,height);states.push_back(v.boxes_[0].shape);
            if(!notch) {start(true);exact(v,62.5);height=62.5;require(!v.commandSession_,"Absolute height did not confirm");verify(radius,height);states.push_back(v.boxes_[0].shape);}
            start(false);exact(v,2*(radius-1),true);radius-=1;verify(radius,height);states.push_back(v.boxes_[0].shape);
            if(!notch) {start(true);exact(v,40);height=40;verify(radius,height);states.push_back(v.boxes_[0].shape);}
            require(v.undoStack_.size()==states.size()-1,"Repeated Pull did not create one history step per confirmation");
            for(size_t i=states.size()-1;i>0;--i) {v.undoPull();require(v.boxes_[0].shape.IsSame(states[i-1]),"Repeated Pull Undo lost cached source");}
            for(size_t i=1;i<states.size();++i) {v.redoPull();require(v.boxes_[0].shape.IsSame(states[i]),"Repeated Pull Redo rebuilt geometry");}
            require(v.boxes_[0].persistentId==initial.persistentId&&v.boxes_[0].presentation==initial.presentation&&v.boxes_[0].name==initial.name&&v.boxes_[0].color==initial.color&&v.boxes_[0].visible==initial.visible&&v.boxes_[1].shape.IsSame(untouched),"Pull lost Part identity/appearance or changed another Part");
            for(int r=1;r<=3;++r) for(int c=1;c<=4;++c) near(v.boxes_[0].orientation.Value(r,c),initial.orientation.Value(r,c),"Pull changed Part placement");
        }
        std::cout<<"Boundary diameter/height, joint split faces, repeated confirmations, Cancel and cached history passed\n";
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
        // Independent Pull cases start with a fresh pick. Repeating the Face
        // retained by Cancel intentionally promotes it to Part selection.
        v.selectParts({});
        const gp_Pnt bottom = coneEnd(v,index,false);
        const gp_Pnt interior = bottom.Translated(gp_Vec(bottom,coneEnd(v,index,true))*0.4);
        click(v,project(v,interior));
        v.startPrimitivePull();
        v.startPrimitivePull();
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
    static void runNativePull(CadViewport& v, const QString& suppliedPath = {}) {
        NativeDocumentSnapshot snapshot;
        QString error;
        QTemporaryDir directory;
        if (suppliedPath.isEmpty()) {
            v.addCone();
            v.boxes_[0].shape=BRepPrimAPI_MakeCone(40,16,80).Shape();
            v.boxes_[0].conePrimitive=false; // Reproduce the early-v1 file.
            gp_Trsf rotation; rotation.SetRotation(gp_Ax1(gp_Pnt(),gp_Dir(0,0,1)),0.2);
            v.boxes_[0].orientation=rotation;
            v.addBox();
            const auto tool=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(175,40,0),gp_Dir(0,0,1)),19,60).Shape();
            v.boxes_[1].shape=BRepAlgoAPI_Cut(v.boxes_[1].shape,tool).Shape();
            v.boxes_[1].geometryKind=CadViewport::CadPart::GeometryKind::GeneralBRep;
            // A cut cone still has a conical support, but must not be rebuilt
            // as a complete primitive using its stale editing parameters.
            v.addCone();
            const auto notch=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(270,0,0),gp_Dir(0,0,1)),10,80).Shape();
            v.boxes_[2].shape=BRepAlgoAPI_Cut(v.boxes_[2].shape,notch).Shape();
            v.boxes_[2].conePrimitive=false;
            v.addSphere(); v.boxes_[3].spherePrimitive=false;
            v.addCylinder(); v.boxes_[4].cylinderPrimitive=false;
            require(v.exportDocumentSnapshot(snapshot),"native Pull snapshot failed");
            const QString path=directory.filePath("pull.odc");
            require(NativeDocumentStorage::save(path,snapshot,error),"native Pull save failed");
            require(NativeDocumentStorage::load(path,snapshot,error),"native Pull load failed");
        } else {
            require(NativeDocumentStorage::load(suppliedPath,snapshot,error),"supplied native document load failed");
        }
        require(v.importDocumentSnapshot(snapshot,error),"native Pull import failed");
        if (suppliedPath.isEmpty()) {
            require(!v.boxes_[2].conePrimitive,"cut cone incorrectly recovered as a complete primitive");
            v.fitAll();
        }
        size_t coneIndex=v.boxes_.size(), holeIndex=v.boxes_.size();
        TopoDS_Face coneFace, holeFace;
        for (size_t i=0;i<v.boxes_.size();++i) {
            auto analyzed=v.boxes_[i];
            const bool primitive=v.recognizePullPrimitive(analyzed);
            for (TopExp_Explorer f(v.boxes_[i].shape,TopAbs_FACE);f.More();f.Next()) {
                const auto face=TopoDS::Face(f.Current());
                const auto type=BRepAdaptor_Surface(face).GetType();
                if (primitive && analyzed.conePrimitive && type==GeomAbs_Cone) { coneIndex=i; coneFace=face; }
                if (holeIndex==v.boxes_.size() && type==GeomAbs_Cylinder && face.Orientation()==TopAbs_REVERSED) {
                    holeIndex=i; holeFace=face;
                }
            }
        }
        require(coneIndex<v.boxes_.size(),"native cone was not recognized from its shape");
        require(holeIndex<v.boxes_.size(),"native cylindrical hole missing");
        require(v.boxes_[coneIndex].shape.IsSame(snapshot.parts[coneIndex].shape),"cone recovery replaced saved geometry");
        const auto startResize=[&](size_t index,const TopoDS_Face& face) {
            v.context_->ClearSelected(Standard_False);
            v.context_->AddOrRemoveSelected(new StdSelect_BRepOwner(face,v.boxes_[index].presentation,5,Standard_True),Standard_False);
            std::unique_ptr<QMenu> menu(v.createSelectionMenu(QPoint(500,350)));
            auto* resize=menu->findChild<QAction*>("context.resize");
            require(resize,"native face omitted Pull / Resize");
            resize->trigger();
            require(v.arrowVisible_,"native face did not activate an arrow");
        };
        const auto coneBefore=v.boxes_[coneIndex];
        // Exercise every restored cone handle through production hit testing.
        for (int target=0;target<4;++target) {
            startResize(coneIndex,coneFace);
            for (const auto& arrow:v.coneArrows_)
                require(!arrow.IsNull() && v.context_->IsDisplayed(arrow),"native cone arrow is not displayed");
            const auto t=static_cast<ConeTarget>(target);
            const auto point=v.coneArrowOrigin(v.boxes_[coneIndex],t)
                .Translated(gp_Vec(v.coneArrowDirection(t))*22).Transformed(v.boxes_[coneIndex].orientation);
            requireRenderedArrow(v,point);
            require(v.beginArrowDrag(project(v,point)),"native cone arrow cannot be grabbed");
            require(v.conePullTarget_==t,"native cone picked the wrong handle");
            require(v.previewCommand(1.0),"native cone cannot be manipulated");
            v.cancelCommandSession();
            require(v.boxes_[coneIndex].shape.IsSame(coneBefore.shape),"native cone Cancel changed saved shape");
        }
        startResize(holeIndex,holeFace);
        require(!v.faceArrow_.IsNull() && v.context_->IsDisplayed(v.faceArrow_),"native hole arrow is not displayed");
        require(v.faceArrow_->ZLayer()==Graphic3d_ZLayerId_Topmost,"native hole arrow can be occluded by material");
        const auto point=v.primitiveArrowOrigin_.Translated(gp_Vec(v.primitiveArrowDirection_)*15)
            .Transformed(v.boxes_[holeIndex].orientation);
        requireRenderedArrow(v,point);
        require(v.beginArrowDrag(project(v,point)),"native hole arrow cannot be grabbed");
        require(v.previewCommand(1.0),"native hole cannot be manipulated");
        v.cancelCommandSession();
        require(v.boxes_[holeIndex].shape.IsSame(snapshot.parts[holeIndex].shape),"native hole Cancel changed saved shape");
        if (suppliedPath.isEmpty()) for (size_t index : {size_t(3),size_t(4)}) {
            const auto type=index==3 ? GeomAbs_Sphere : GeomAbs_Cylinder;
            TopoDS_Face curved;
            for (TopExp_Explorer f(v.boxes_[index].shape,TopAbs_FACE);f.More();f.Next())
                if (BRepAdaptor_Surface(TopoDS::Face(f.Current())).GetType()==type) { curved=TopoDS::Face(f.Current()); break; }
            require(!curved.IsNull(),"native untagged curved face missing");
            startResize(index,curved);
            const auto start=beginPull(v);
            require(v.previewCommand(1.0),"untagged sphere/cylinder cannot be resized");
            release(v,start);
            v.cancelCommandSession();
            require(v.boxes_[index].shape.IsSame(snapshot.parts[index].shape),"untagged primitive Cancel changed source geometry");
        }
        require(v.undoStack_.empty() && v.redoStack_.empty(),"native Pull cancellation changed history");
    }

    static void runCone(CadViewport& v) {
        v.addCone();
        checkCone(v,0,80,40,80);
        const auto originalShape=v.boxes_[0].shape;
        const auto cancelled=beginConePull(v,0,ConeTarget::BottomHeight); dragBy(v,cancelled,12); v.cancelCommandSession();
        require(v.boxes_[0].shape.IsSame(originalShape) && v.undoStack_.empty() && !v.arrowVisible_,"Cone Cancel failed to restore original/close tool");
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
        require(v.commandSession_ && v.commandPanel_->isVisible(),"invalid cone diameter closed editor");
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
        require(v.commandSession_ && v.commandPanel_->isVisible(),"invalid cone height closed editor");
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
        v.selectParts({0});
        v.setDirectMoveMode(true);
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
        require(v.selectedPartIndices_.empty()&&v.context_->NbSelected()==0,"cone Redo retained selection");
        v.selectParts({0}); // History navigation requires a fresh explicit selection.
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
        require(v.selectedPartIndices_.empty()&&v.context_->NbSelected()==0,"cone rotation Redo retained selection");
        v.selectParts({0});
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
        v.startPrimitivePull();
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
            if(!v.directMoveMode_) require(v.moveAxes_[axis].IsNull(),"idle Move gizmo remains");
            if(!v.rotateMode_) require(v.rotationRings_[axis].IsNull(),"idle Rotate gizmo remains");
            if(v.directMoveMode_) {
            require(!v.moveAxes_[axis].IsNull(),"missing move gizmo");
            bool foundShaft = false;
            for (TopExp_Explorer faces(v.moveAxes_[axis]->Shape(),TopAbs_FACE);faces.More();faces.Next()) {
                const BRepAdaptor_Surface surface(TopoDS::Face(faces.Current()));
                if (surface.GetType() != GeomAbs_Cylinder) continue;
                near(surface.Cylinder().Location().Distance(expected),0,"move shaft is not at centroid");
                foundShaft = true;
            }
            require(foundShaft,"move shaft geometry missing");
            }
            if(v.rotateMode_) {
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
    }
    static void checkGizmo(CadViewport& v, size_t index, const gp_Pnt& expected) {
        v.selectParts({index});
        v.setDirectMoveMode(true);
        near(v.partLocalGizmoCenter(v.boxes_[index]).Transformed(v.boxes_[index].orientation)
            .Distance(expected),0,"incorrect world gizmo center");
        checkDisplayedGizmos(v,expected);
        const gp_Dir x = gp_Dir(1,0,0).Transformed(v.boxes_[index].orientation);
        require(v.beginMoveAxisDrag(project(v,expected.Translated(gp_Vec(x)*25.0))),
                "move hit testing is not aligned with centroid");
        v.draggingFace_ = false;
        v.setRotateMode(true);
        checkDisplayedGizmos(v,expected);
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
        // Undo/redo restores geometry and leaves transform tools/selection inactive.
        v.selectParts({3}); v.setRotateMode(true);
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
        v.selectParts({0,3}); v.setRotateMode(true);
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
        require(v.selectedPartIndices_.empty()&&v.context_->NbSelected()==0,"multipart redo retained selection");
        near(v.rotationCenter({0,3}).Distance(average),0,"multipart redo changed pivot");
        checkDisplayedGizmos(v,average,true);
    }

    static void runRotation(CadViewport& v) {
        v.addBox(); v.addCylinder(); v.addSphere(); v.addCone();
        // A non-primitive B-Rep with a shape location as well as an instance transform.
        v.addCone();
        auto& general = v.boxes_[4];
        general.conePrimitive = false;
        gp_Trsf location;
        location.SetTranslation(gp_Vec(17,-9,23));
        general.shape = BRepPrimAPI_MakeCone(35,0,90).Shape().Moved(TopLoc_Location(location));
        general.presentation->SetShape(general.shape);
        v.addBox(); v.selectParts({5}); v.duplicateSelectedParts();
        require(v.applyBoolean(CadViewport::BooleanOperation::Subtract,{5,6},5),
                "rotation Boolean fixture failed");
        const size_t result = v.boxes_.size()-1;
        const auto compare = [&](const gp_Trsf& a, const gp_Trsf& b) {
            for (int row=1; row<=3; ++row)
                for (int col=1; col<=4; ++col)
                    near(a.Value(row,col),b.Value(row,col),"rotation transforms differ");
        };
        for (const auto indices : {std::vector<size_t>{0}, {1}, {2}, {3}, {4}, {result}, {0,3,result}}) {
            for (bool transformed : {false,true}) {
                for (const auto i : indices) {
                    gp_Trsf orientation, translation;
                    if (transformed) {
                        orientation.SetRotation(gp_Ax1(gp_Pnt(0,0,0),gp_Dir(0,0,1)),std::acos(-1.0)/2);
                        translation.SetTranslation(gp_Vec(45,-27,19));
                        orientation = translation*orientation;
                    }
                    v.applyPartState(i,v.boxes_[i].min,v.boxes_[i].max,orientation);
                }
                v.selectParts(indices); v.setRotateMode(true);
                std::vector<gp_Trsf> before, expected;
                gp_XYZ sum(0,0,0);
                for (auto i : indices) {
                    before.push_back(v.boxes_[i].orientation);
                    sum += center(v,i).XYZ();
                }
                const gp_Pnt pivot(sum / double(indices.size()));
                near(v.rotationCenter(indices).Distance(pivot),0,"incorrect world pivot");
                // Drive the gizmo angle/release/exact editor path with its drag-start state.
                v.rotationIndex_ = indices.front(); v.rotationAxis_ = 0;
                v.rotationBefore_ = before.front();
                v.groupRotationDragging_ = indices.size()>1;
                v.groupRotationCenter_ = pivot;
                gp_Trsf rotation;
                rotation.SetRotation(gp_Ax1(pivot,gp_Dir(1,0,0).Transformed(before.front())),37*std::acos(-1.0)/180);
                v.draggingRotation_ = true;
                v.beginCommandSession(CadViewport::CommandSession::Kind::Rotate,indices,
                    gp_Dir(1,0,0).Transformed(before.front()),pivot);
                v.applyRotationAngle(37);
                for (size_t n=0; n<indices.size(); ++n) {
                    expected.push_back(rotation*before[n]);
                    compare(v.boxes_[indices[n]].orientation,before[n]);
                    compare(v.boxes_[indices[n]].presentation->LocalTransformation(),expected.back());
                }
                QMouseEvent releaseEvent(QEvent::MouseButtonRelease,QPointF(10,10),QPointF(10,10),
                                         Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
                v.mouseReleaseEvent(&releaseEvent); exact(v,37);
                v.undoPull();
                for (size_t n=0;n<indices.size();++n) compare(v.boxes_[indices[n]].orientation,before[n]);
                v.redoPull();
                for (size_t n=0;n<indices.size();++n) compare(v.boxes_[indices[n]].orientation,expected[n]);
                v.undoPull(); v.selectParts(indices);
                if (indices.size()==1 && !transformed) {
                    // Keep the established face-preselection entry to the menu covered.
                    v.selectedPartIndices_.clear();
                    v.context_->ClearSelected(Standard_False);
                    TopExp_Explorer faces(v.boxes_[indices.front()].shape,TopAbs_FACE);
                    Handle(StdSelect_BRepOwner) owner = new StdSelect_BRepOwner(
                        faces.Current(),v.boxes_[indices.front()].presentation,5,Standard_True);
                    v.context_->AddOrRemoveSelected(owner,Standard_False);
                }
                // Local X is world Y after the existing quarter-turn around Z.
                QTimer::singleShot(0,[transformed]() {
                    auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
                    require(dialog,"Rotate dialog missing");
                    dialog->findChild<QComboBox*>()->setCurrentIndex(transformed ? 1 : 0);
                    dialog->findChild<QDoubleSpinBox*>()->setValue(37);
                    dialog->accept();
                });
                v.rotateSelectedPart();
                if (indices.size()==1 && !transformed)
                    require(v.context_->NbSelected()==0,"stale face selection after rotation");
                for (size_t n=0;n<indices.size();++n) compare(v.boxes_[indices[n]].orientation,expected[n]);
                v.undoPull();
                for (size_t n=0;n<indices.size();++n) compare(v.boxes_[indices[n]].orientation,before[n]);
                v.redoPull();
                for (size_t n=0;n<indices.size();++n) compare(v.boxes_[indices[n]].orientation,expected[n]);
            }
        }
    }

    static void runSessions(CadViewport& v) {
        using Kind = CadViewport::CommandSession::Kind;
        using State = CadViewport::CommandSession::State;
        v.addBox(); v.addCone();
        const auto sameTransform = [](const gp_Trsf& a, const gp_Trsf& b) {
            for (int r=1;r<=3;++r) for (int c=1;c<=4;++c)
                near(a.Value(r,c),b.Value(r,c),"session transform mismatch");
        };
        const auto owners = [&]() {
            std::vector<Handle(SelectMgr_EntityOwner)> result;
            for (v.context_->InitSelected();v.context_->MoreSelected();v.context_->NextSelected())
                result.push_back(v.context_->SelectedOwner());
            return result;
        };
        for (auto kind : {Kind::Move,Kind::Rotate}) {
            for (bool group : {false,true}) {
                for (bool transformed : {false,true}) {
                    const std::vector<size_t> indices = group ? std::vector<size_t>{0,1} : std::vector<size_t>{1};
                    v.cancelCommandSession();
                    for (auto i : indices) {
                        gp_Trsf rotation, shift;
                        if (transformed) {
                            rotation.SetRotation(gp_Ax1(gp_Pnt(7,-2,11),gp_Dir(1,2,3)),0.63);
                            shift.SetTranslation(gp_Vec(32,-17,24));
                        }
                        v.applyPartState(i,v.boxes_[i].min,v.boxes_[i].max,shift*rotation);
                    }
                    v.selectParts(indices);
                    v.setDirectMoveMode(kind==Kind::Move); v.setRotateMode(kind==Kind::Rotate);
                    const auto original = v.boxes_;
                    const auto selection = owners();
                    const auto undoSize = v.undoStack_.size(), redoSize = v.redoStack_.size();
                    const gp_Pnt pivot = v.rotationCenter(indices);
                    const gp_Dir direction = gp_Dir(1,0,0).Transformed(original[indices.front()].orientation);
                    const auto unchanged = [&]() {
                        for (auto i : indices) {
                            require(v.boxes_[i].shape.IsSame(original[i].shape),"preview replaced canonical B-Rep");
                            require(v.boxes_[i].min==original[i].min && v.boxes_[i].max==original[i].max,
                                    "preview changed bounds metadata");
                            sameTransform(v.boxes_[i].orientation,original[i].orientation);
                        }
                        require(v.selectedPartIndices_==indices && owners()==selection,"session changed selection");
                        require(v.undoStack_.size()==undoSize && v.redoStack_.size()==redoSize,
                                "preview or cancellation changed history");
                    };
                    const auto start = [&]() {
                        v.beginCommandSession(kind,indices,direction,pivot);
                        require(v.commandSession_->state==State::Parameters,"missing parameter state");
                        require(v.commandButtons_->isVisible(),"missing OK/Cancel controls");
                        v.previewCommand(12);
                        v.previewCommand(27.5);
                        require(v.commandSession_->state==State::Preview,"missing preview state");
                        unchanged();
                        gp_Trsf delta;
                        if (kind==Kind::Move) delta.SetTranslation(gp_Vec(direction)*27.5);
                        else delta.SetRotation(gp_Ax1(pivot,direction),27.5*std::acos(-1.0)/180);
                        for (auto i : indices)
                            sameTransform(v.boxes_[i].presentation->LocalTransformation(),delta*original[i].orientation);
                        // Releasing the pointer keeps a preview and creates no history entry.
                        v.draggingFace_ = kind==Kind::Move; v.draggingRotation_ = kind==Kind::Rotate;
                        QMouseEvent releaseEvent(QEvent::MouseButtonRelease,QPointF(200,200),QPointF(200,200),
                                                 Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
                        v.mouseReleaseEvent(&releaseEvent);
                        unchanged();
                        return delta;
                    };
                    start();
                    v.dragDimension_->setText(QLocale().toString(29.75,'f',2));
                    require(QMetaObject::invokeMethod(v.dragDimension_,"textEdited",Qt::DirectConnection,
                        Q_ARG(QString,v.dragDimension_->text())),"could not edit preview parameter");
                    near(v.commandSession_->value,29.75,"numeric edit did not refresh preview");
                    unchanged();
                    v.commandButtons_->button(QDialogButtonBox::Cancel)->click();
                    require(!v.commandSession_,"Cancel left session active"); unchanged();
                    for (auto i : indices) sameTransform(v.boxes_[i].presentation->LocalTransformation(),original[i].orientation);
                    start();
                    v.setMovePartMode(!v.movePartMode_);
                    require(!v.commandSession_,"tool change left preview active"); unchanged();
                    start();
                    v.dragDimension_->setText(QStringLiteral("invalid"));
                    v.commandButtons_->button(QDialogButtonBox::Ok)->click();
                    require(v.commandSession_.has_value(),"invalid input committed a command"); unchanged();
                    v.cancelCommandSession();
                    start();
                    v.commandPanel_->close();
                    require(!v.commandSession_,"closing preview controls left a pending command"); unchanged();
                    v.beginCommandSession(kind,indices,direction,pivot);
                    v.commandButtons_->button(QDialogButtonBox::Ok)->click();
                    require(!v.commandSession_,"zero command left a session active"); unchanged();
                    const auto delta = start();
                    v.commandButtons_->button(QDialogButtonBox::Ok)->click();
                    require(!v.commandSession_,"OK left session active");
                    require(v.undoStack_.size()==undoSize+1 && v.redoStack_.empty(),"confirmation is not one history entry");
                    for (auto i : indices) {
                        sameTransform(v.boxes_[i].orientation,delta*original[i].orientation);
                        require(v.boxes_[i].shape.IsSame(original[i].shape),"transform rebuilt canonical shape");
                    }
                    v.confirmCommandSession();
                    require(v.undoStack_.size()==undoSize+1,"double confirmation duplicated undo");
                    v.undoPull();
                    for (auto i : indices) sameTransform(v.boxes_[i].orientation,original[i].orientation);
                    v.redoPull();
                    for (auto i : indices) sameTransform(v.boxes_[i].orientation,delta*original[i].orientation);
                    v.undoPull(); // Leave a redo entry: cancellation must preserve it.
                }
            }
        }
        // Real pointer startup and keyboard controls, with a face-owner snapshot.
        v.selectParts({0}); v.setDirectMoveMode(true); v.fitAll();
        const auto before = v.boxes_[0];
        const size_t undoSize = v.undoStack_.size();
        const auto origin = center(v,0);
        const auto axis = gp_Dir(1,0,0).Transformed(before.orientation);
        const auto grab = project(v,origin.Translated(gp_Vec(axis)*25));
        require(v.beginMoveAxisDrag(grab),"session move gizmo startup failed");
        dragBy(v,grab,25);
        require(v.commandSession_ && v.commandSession_->value!=0,"pointer did not update preview");
        sameTransform(v.boxes_[0].orientation,before.orientation);
        v.dragDimension_->setFocus(); QApplication::processEvents();
        QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);
        QApplication::sendEvent(v.dragDimension_,&escape);
        require(!v.commandSession_,"Escape did not cancel preview");
        sameTransform(v.boxes_[0].presentation->LocalTransformation(),before.orientation);
        require(v.undoStack_.size()==undoSize,"Escape added history");
        // A pending session also cancels when another transform tool is selected.
        v.setDirectMoveMode(true);
        require(v.beginMoveAxisDrag(grab),"second move startup failed"); dragBy(v,grab,15);
        v.setRotateMode(true);
        require(!v.commandSession_,"Move-to-Rotate kept a pending preview");
        sameTransform(v.boxes_[0].presentation->LocalTransformation(),before.orientation);
        v.setDirectMoveMode(false);
        const auto y = gp_Dir(0,1,0).Transformed(before.orientation);
        const double radius = 0.65*std::max({before.max[0]-before.min[0],before.max[1]-before.min[1],before.max[2]-before.min[2]});
        const gp_Circ ring(gp_Ax2(origin,y,axis),radius);
        require(v.beginRotationDrag(project(v,ElCLib::Value(0.7,ring))),"session rotate gizmo startup failed");
        v.applyRotationAngle(31);
        sameTransform(v.boxes_[0].orientation,before.orientation);
        v.dragDimension_->setText(QLocale().toString(31.0,'f',2));
        v.dragDimension_->setFocus(); QApplication::processEvents();
        QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);
        QApplication::sendEvent(v.dragDimension_,&enter);
        require(!v.commandSession_,"Enter did not confirm preview");
        require(v.undoStack_.size()==undoSize+1,"Enter did not add exactly one command");

        // Face owners survive preview/cancel unchanged, rather than being regenerated.
        v.setRotateMode(false); v.setDirectMoveMode(false); v.selectParts({});
        TopExp_Explorer faces(v.boxes_[0].shape,TopAbs_FACE);
        Handle(StdSelect_BRepOwner) face = new StdSelect_BRepOwner(faces.Current(),v.boxes_[0].presentation,5,Standard_True);
        v.context_->AddOrRemoveSelected(face,Standard_False);
        const auto faceSelection = owners();
        v.beginCommandSession(Kind::Move,{0},gp_Dir(1,0,0)); v.previewCommand(14);
        v.cancelCommandSession();
        require(owners()==faceSelection && v.selectedPartIndices_.empty(),"Cancel lost original face owner");
        // The lower face's orange Move arrow retains its outward direction.
        v.setMovePartMode(true);
        v.arrowPartIndex_=0; v.arrowAxis_=2; v.arrowMax_=false; v.arrowVisible_=true;
        v.updateFaceArrow();
        const auto lowerBefore = v.boxes_[0];
        const gp_Pnt lower((lowerBefore.min[0]+lowerBefore.max[0])*0.5,
                           (lowerBefore.min[1]+lowerBefore.max[1])*0.5,lowerBefore.min[2]);
        const auto lowerGrab = project(v,lower.Translated(gp_Vec(0,0,-15)).Transformed(lowerBefore.orientation));
        require(v.beginArrowDrag(lowerGrab),"lower Move arrow startup failed");
        dragBy(v,lowerGrab,18);
        sameTransform(v.boxes_[0].orientation,lowerBefore.orientation);
        exact(v,18);
        gp_Trsf lowerShift;
        lowerShift.SetTranslation(gp_Vec(0,0,-18).Transformed(lowerBefore.orientation));
        sameTransform(v.boxes_[0].orientation,lowerShift*lowerBefore.orientation);
        v.undoPull(); sameTransform(v.boxes_[0].orientation,lowerBefore.orientation);
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
        if(app.arguments().contains("--boundary-gaps")) CadViewportPullTest::runBoundaryGaps(viewport);
        else if(app.arguments().contains("--trimmed-boss-pull")) {
            const int arg=app.arguments().indexOf("--trimmed-boss-pull");CadViewportPullTest::runTrimmedBossPull(viewport,app.arguments().value(arg+1));
        }
        else if (app.arguments().contains(QStringLiteral("--native-pull"))) {
            const int arg=app.arguments().indexOf(QStringLiteral("--native-pull"));
            CadViewportPullTest::runNativePull(viewport,app.arguments().value(arg+1));
        }
        else if (app.arguments().contains(QStringLiteral("--sessions")))
            CadViewportPullTest::runSessions(viewport);
        else if (app.arguments().contains(QStringLiteral("--cylindrical-face")))
            CadViewportPullTest::runCylindricalFace();
        else if (app.arguments().contains(QStringLiteral("--rotation")))
            CadViewportPullTest::runRotation(viewport);
        else if (app.arguments().contains(QStringLiteral("--gizmo")))
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
