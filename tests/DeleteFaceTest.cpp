#include "CadViewport.h"
#include "MainWindow.h"
#include "DeleteFaceGeometry.h"
#include "DebugModel.h"
#include <QApplication>
#include <QAction>
#include <QMenu>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QLineEdit>
#include <QTreeWidget>
#include <QMessageBox>
#include <QTemporaryDir>
#include <QKeyEvent>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepGProp.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <Poly_Triangulation.hxx>
#include <GProp_GProps.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <StdSelect_BRepOwner.hxx>
#include <StdSelect_ViewerSelector3d.hxx>
#include <AIS_ListOfInteractive.hxx>
#include <Image_PixMap.hxx>
#include <sstream>
#include <stdexcept>
#include <iostream>
#include <cmath>
#include <algorithm>
#include <TColStd_ListOfInteger.hxx>

namespace {
void require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
std::string stamp(const TopoDS_Shape& s) {std::ostringstream out;BRepTools::Write(s,out);return out.str();}
gp_Pnt center(const TopoDS_Face& f) {GProp_GProps p;BRepGProp::SurfaceProperties(f,p);return p.CentreOfMass();}
TopoDS_Shape stock() {return BRepPrimAPI_MakeBox(100,80,60).Shape();}
TopoDS_Shape cylinder(double x,double z,double height) {return BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(x,40,z),gp_Dir(0,0,1)),10,height).Shape();}
TopoDS_Shape hole() {return BRepAlgoAPI_Cut(stock(),cylinder(30,-10,80)).Shape();}
template<class Predicate>
std::vector<TopoDS_Face> select(const TopoDS_Shape& s,Predicate predicate) {
    std::vector<TopoDS_Face> out;
    for(TopExp_Explorer f(s,TopAbs_FACE);f.More();f.Next()) if(predicate(TopoDS::Face(f.Current()))) out.push_back(TopoDS::Face(f.Current()));
    return out;
}
auto walls(const TopoDS_Shape& s) {return select(s,[](const auto& f){return BRepAdaptor_Surface(f).GetType()==GeomAbs_Cylinder;});}
TopoDS_Shape loadStepFixture(const char* name) {
    TopoDS_Shape shape;BRep_Builder builder;
    const auto path=std::string(OPENDESIGNER_FIXTURE_DIR "/delete-unite-rotate/")+name;
    require(BRepTools::Read(shape,path.c_str(),builder),"cannot read Delete Unite/Rotate fixture");return shape;
}
std::vector<TopoDS_Face> stepTargets(const TopoDS_Shape& shape) {
    return select(shape,[](const auto& face) {
        const auto p=BRepAdaptor_Surface(face).Plane();auto n=p.Axis().Direction();
        if(face.Orientation()==TopAbs_REVERSED) n.Reverse();
        return (n.X()>0.999&&std::abs(p.Location().X()-125)<1.e-7)
            ||(n.Y()<-0.999&&std::abs(p.Location().Y())<1.e-7);
    });
}
TopoDS_Shape stepExpected(bool extendFront=true,bool trimLow=true,double lowHeight=39,double lowFront=16) {
    // Independent 2-D profiles: no production cell rules or kernel defeaturing.
    const double root=std::sqrt(3.0),crossing=80-(60-lowHeight)*root,front=extendFront?80-60*root:0;
    const auto prism=[](const std::vector<gp_Pnt>& points,double dx) {
        BRepBuilderAPI_MakePolygon p;for(const auto& v:points) p.Add(v);p.Close();
        return BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(p.Wire()).Face(),gp_Vec(dx,0,0)).Shape();
    };
    const auto low=trimLow?prism({gp_Pnt(0,lowFront,0),gp_Pnt(0,80,0),gp_Pnt(0,80,lowHeight),gp_Pnt(0,crossing,lowHeight),gp_Pnt(0,lowFront,60+(lowFront-80)/root)},125)
        :BRepPrimAPI_MakeBox(gp_Pnt(0,lowFront,0),125,80-lowFront,lowHeight).Shape();
    const auto high=prism({gp_Pnt(125,front,0),gp_Pnt(125,80,0),gp_Pnt(125,80,60),gp_Pnt(125,front,60+(front-80)/root)},100);
    return BRepAlgoAPI_Fuse(low,high).Shape();
}
auto inclinedStepTarget(const TopoDS_Shape& shape) {
    return select(shape,[](const auto& face) {
        const auto p=BRepAdaptor_Surface(face).Plane();auto n=p.Axis().Direction();
        if(face.Orientation()==TopAbs_REVERSED) n.Reverse();
        return n.X()<-0.999&&std::abs(p.Location().X()-125)<1.e-7;
    });
}
TopoDS_Shape inclinedStepExpected() {
    // Independent extrusion profiles for debug5: intersect the extended ramp
    // and footprint, retaining the real high top/front after their crossings.
    const double t=std::tan(std::acos(-1.0)/12),frontCross=36+36/t,topCross=125+15/t;
    const auto prism=[](const std::vector<gp_Pnt>& points,const gp_Vec& direction) {
        BRepBuilderAPI_MakePolygon p;for(const auto& v:points)p.Add(v);p.Close();
        return BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(p.Wire()).Face(),direction).Shape();
    };
    const auto ramp=prism({{36,0,0},{225,0,0},{225,0,60},{topCross,0,60},{36,0,45-89*t}},gp_Vec(0,80,0));
    const auto footprint=prism({{36,36,0},{36,80,0},{225,80,0},{225,0,0},{frontCross,0,0}},gp_Vec(0,0,60));
    return BRepAlgoAPI_Common(ramp,footprint).Shape();
}
void checkInclinedStep(const TopoDS_Shape& shape) {
    const auto d=DebugModel::diagnostics(shape);
    require(d["solidValid"].toBool()&&d["faces"].toInt()==8&&d["coplanarDividingEdges"].toInt()==0,"inclined step has invalid or redundant topology");
    require(std::abs(d["volume"].toDouble()-601739.4919736391)<1.e-5,"inclined step has incorrect analytical volume");
    const double t=std::tan(std::acos(-1.0)/12),frontCross=36+36/t,topCross=125+15/t;
    BRepClass3d_SolidClassifier material(shape);
    for(double x:{35.9,36.1,80.,124.99,125.01,150.,frontCross-0.01,frontCross+0.01,topCross-0.01,topCross+0.01,224.9,225.1}) {
        const double front=std::max(0.,36-(x-36)*t),top=std::min(60.,45+(x-125)*t);
        for(double y:{-0.01,0.01,front-0.001,front+0.001,79.99,80.01})
            for(double z:{-0.01,0.01,top-0.001,top+0.001,59.99,60.01}) {
                material.Perform(gp_Pnt(x,y,z),1.e-7);
                require(material.State()==((x>36&&x<225&&y>front&&y<80&&z>0&&z<top)?TopAbs_IN:TopAbs_OUT),"inclined step material differs from analytic sections");
            }
    }
}
void accepted(const TopoDS_Shape& source,const std::vector<TopoDS_Face>& faces,const TopoDS_Shape& expected) {
    require(!faces.empty(),"empty valid fixture selection");const auto before=stamp(source);
    QString error;BRepReconstruction::Result result;
    if(!DeleteFaceGeometry::remove(source,faces,result,error)) throw std::runtime_error(error.toStdString());
    double mass=0;require(BRepReconstruction::validSolid(result.solid,mass),"invalid accepted solid");
    require(BRepReconstruction::equivalent(result.solid,expected),"unexpected material in accepted solid");
    for(const auto& target:faces) {
        bool found=false;for(const auto& mapping:result.history) if(mapping.original.IsSame(target)) {
            found=true;require(mapping.modified.empty(),"deleted face has final descendants");
        }
        require(found,"deleted face has no history");
    }
    require(stamp(source)==before,"successful delete mutated source");
    // Occupancy checks are independent of builder topology and include the old
    // feature and all outer sides, where extra material is especially dangerous.
    BRepClass3d_SolidClassifier actual(result.solid),oracle(expected);
    for(double x:{-1.,5.,25.,30.,35.,75.,95.,101.}) for(double y:{-1.,5.,35.,40.,45.,75.,81.}) for(double z:{-1.,5.,35.,45.,55.,65.,75.,85.}) {
        const gp_Pnt p(x+0.13,y+0.17,z+0.19);actual.Perform(p,1.e-7);oracle.Perform(p,1.e-7);
        require(actual.State()==oracle.State(),"material probe differs from independent stock oracle");
    }
}
void refused(const TopoDS_Shape& source,const std::vector<TopoDS_Face>& faces) {
    const auto before=stamp(source);QString error;BRepReconstruction::Result result;result.solid=source;
    require(!DeleteFaceGeometry::remove(source,faces,result,error),"impossible or ambiguous deletion accepted");
    require(result.solid.IsNull()&&result.history.empty()&&!error.isEmpty(),"failure retained a result or omitted reason");
    require(stamp(source)==before,"refused delete mutated source");
}
void geometry() {
    const auto box=stock(),through=hole();const auto wall=walls(through);
    accepted(through,wall,box);
    auto duplicate=wall;duplicate.push_back(wall.front());accepted(through,duplicate,box);
    const auto blind=BRepAlgoAPI_Cut(box,cylinder(30,40,30)).Shape();
    const auto blindSelection=select(blind,[](const auto& f){return BRepAdaptor_Surface(f).GetType()==GeomAbs_Cylinder||std::abs(center(f).Z()-40)<1.e-6;});
    accepted(blind,blindSelection,box);
    refused(blind,walls(blind)); // An unselected bottom must not disappear.
    const auto boss=BRepAlgoAPI_Fuse(box,cylinder(30,60,20)).Shape();
    accepted(boss,select(boss,[](const auto& f){return center(f).Z()>60.001;}),box);
    refused(boss,walls(boss));
    const auto planarBoss=BRepAlgoAPI_Fuse(box,BRepPrimAPI_MakeBox(gp_Pnt(20,20,60),20,20,20).Shape()).Shape();
    accepted(planarBoss,select(planarBoss,[](const auto& f){return center(f).Z()>60.001;}),box);
    const auto two=BRepAlgoAPI_Cut(through,cylinder(70,-10,80)).Shape();
    auto twoWalls=walls(two);accepted(two,twoWalls,box);
    std::reverse(twoWalls.begin(),twoWalls.end());accepted(two,twoWalls,box);
    const auto conical=BRepAlgoAPI_Cut(box,BRepPrimAPI_MakeCone(gp_Ax2(gp_Pnt(30,40,-10),gp_Dir(0,0,1)),6,14,80).Shape()).Shape();
    accepted(conical,select(conical,[](const auto& f){return BRepAdaptor_Surface(f).GetType()==GeomAbs_Cone;}),box);

    BRepBuilderAPI_MakePolygon triangle;triangle.Add(gp_Pnt(90,-10,60));triangle.Add(gp_Pnt(110,-10,40));triangle.Add(gp_Pnt(110,-10,80));triangle.Close();
    const auto wedge=BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(triangle.Wire()).Face(),gp_Vec(0,100,0)).Shape();
    const auto chamfer=BRepAlgoAPI_Cut(box,wedge).Shape();
    const auto bevel=select(chamfer,[](const auto& f){const auto n=BRepAdaptor_Surface(f).Plane().Axis().Direction();return std::abs(n.X())>0.1&&std::abs(n.Z())>0.1;});
    accepted(chamfer,bevel,box);
    gp_Trsf transform;transform.SetRotation(gp_Ax1(gp_Pnt(10,-7,3),gp_Dir(1,2,3)),0.7);transform.SetTranslationPart(gp_Vec(31,-23,17));
    for(const auto& source:{through,chamfer,planarBoss}) {
        BRepBuilderAPI_Transform moved(source,transform,true);
        const auto targets=source.IsSame(through)?wall:source.IsSame(chamfer)?bevel:select(planarBoss,[](const auto& f){return center(f).Z()>60.001;});
        std::vector<TopoDS_Face> movedTargets;for(const auto& f:targets) movedTargets.push_back(TopoDS::Face(moved.ModifiedShape(f)));
        accepted(moved.Shape(),movedTargets,BRepBuilderAPI_Transform(box,transform,true).Shape());
    }
    refused(box,{});refused(box,select(box,[](const auto&){return true;}));
    const auto top=select(box,[](const auto& f){return center(f).Z()>59.99;});refused(box,top);
    auto corner=top;const auto right=select(box,[](const auto& f){return center(f).X()>99.99;});corner.push_back(right.front());refused(box,corner);
    refused(through,top); // Foreign topology, even though it is coplanar.
    auto partial=walls(two);const auto side=select(two,[](const auto& f){return center(f).X()<0.01;});partial.push_back(side.front());refused(two,partial);
    const auto sphere=BRepPrimAPI_MakeSphere(20).Shape();refused(sphere,select(sphere,[](const auto&){return true;}));
    const auto open=BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(),gp_Dir(0,0,1)),-10,10,-10,10).Face();refused(open,{open});

    const auto low=loadStepFixture("low.brep"),high=loadStepFixture("rotated-high.brep");
    const auto united=BRepAlgoAPI_Fuse(low,high).Shape();auto targets=stepTargets(united);
    require(targets.size()==2,"reported triangle/strip fixture faces missing");
    accepted(united,targets,stepExpected());
    QString stepError;BRepReconstruction::Result stepResult;
    require(DeleteFaceGeometry::remove(united,targets,stepResult,stepError),"reported step material reconstruction failed");
    double stepVolume=0;require(BRepReconstruction::validSolid(stepResult.solid,stepVolume)&&std::abs(stepVolume-596227.8260662365)<1.e-5,"reported step has wrong analytical volume");
    BRepClass3d_SolidClassifier stepMaterial(stepResult.solid);
    for(double x:{-0.13,0.13,60.,124.9,125.1,175.,224.9,225.1})
        for(double y:{-24.,-23.922,-10.,-0.001,0.001,15.999,16.001,30.,43.625,43.629,70.,80.01})
            for(double z:{-0.01,0.01,10.,23.04,23.06,38.999,39.001,50.,60.01}) {
                const double top=60+(y-80)/std::sqrt(3.);
                const bool lowMaterial=x>0&&x<125&&y>16&&y<80&&z>0&&z<std::min(39.,top);
                const bool highMaterial=x>125&&x<225&&y<80&&z>0&&z<top;
                stepMaterial.Perform(gp_Pnt(x,y,z),1.e-7);
                require(stepMaterial.State()==((lowMaterial||highMaterial)?TopAbs_IN:TopAbs_OUT),"reported step has extra or missing material");
            }
    std::reverse(targets.begin(),targets.end());accepted(united,targets,stepExpected());
    const auto transition=select(united,[](const auto& f){auto n=BRepAdaptor_Surface(f).Plane().Axis().Direction();if(f.Orientation()==TopAbs_REVERSED)n.Reverse();return n.X()>0.999&&std::abs(center(f).X()-125)<1.e-7;});
    accepted(united,transition,stepExpected(false));
    const auto strip=select(united,[](const auto& f){auto n=BRepAdaptor_Surface(f).Plane().Axis().Direction();if(f.Orientation()==TopAbs_REVERSED)n.Reverse();return n.Y()<-0.999&&std::abs(center(f).Y())<1.e-7;});
    accepted(united,strip,stepExpected(true,false));
    // A separate Box still cannot borrow another Part's tilted surface.
    refused(low,stepTargets(low));
    const auto impossible=select(united,[](const auto& f){return std::abs(center(f).Z())<1.e-7;});
    auto invalid=targets;invalid.insert(invalid.end(),impossible.begin(),impossible.end());refused(united,invalid);
    BRepBuilderAPI_Transform placed(united,transform,true);
    std::vector<TopoDS_Face> placedTargets;for(const auto& f:targets) placedTargets.push_back(TopoDS::Face(placed.ModifiedShape(f)));
    accepted(placed.Shape(),placedTargets,BRepBuilderAPI_Transform(stepExpected(),transform,true).Shape());

    const auto sequentialBefore=loadStepFixture("sequential-before.brep");
    const auto sequentialAfter=loadStepFixture("sequential-after-first.brep");
    const auto nextTargets=stepTargets(sequentialAfter);
    require(nextTargets.size()==1,"exported remaining strip target missing");
    accepted(sequentialAfter,nextTargets,stepExpected(true,true,43,22));
    for(const auto& source:{united,sequentialBefore}) {
        const bool exact=source.IsSame(sequentialBefore);
        const double height=exact?43:39,frontY=exact?22:16;
        const auto targets=stepTargets(source);require(targets.size()==2,"sequential source targets missing");
        for(bool triangleFirst:{true,false}) {
            const auto firstTargets=select(source,[&](const auto& f) {
                const auto p=BRepAdaptor_Surface(f).Plane();auto n=p.Axis().Direction();if(f.Orientation()==TopAbs_REVERSED)n.Reverse();
                return triangleFirst?(n.X()>0.999&&std::abs(center(f).X()-125)<1.e-7):(n.Y()<-0.999&&std::abs(center(f).Y())<1.e-7);
            });
            QString error;BRepReconstruction::Result first;
            require(DeleteFaceGeometry::remove(source,firstTargets,first,error),"first sequential deletion failed");
            require(BRepReconstruction::equivalent(first.solid,stepExpected(!triangleFirst,triangleFirst,height,frontY)),"first sequential deletion changed wrong material");
            if(exact&&triangleFirst) require(BRepReconstruction::equivalent(first.solid,sequentialAfter),"first deletion differs from the user's exported intermediate result");
            const auto remaining=stepTargets(first.solid);require(remaining.size()==1,"remaining face not resolved on fresh topology");
            accepted(first.solid,remaining,stepExpected(true,true,height,frontY));
        }
    }
    BRepBuilderAPI_Transform movedIntermediate(sequentialAfter,transform,true);
    const auto movedStrip=TopoDS::Face(movedIntermediate.ModifiedShape(nextTargets.front()));
    accepted(movedIntermediate.Shape(),{movedStrip},BRepBuilderAPI_Transform(stepExpected(true,true,43,22),transform,true).Shape());
    refused(sequentialAfter,select(sequentialAfter,[](const auto& f){return std::abs(center(f).Z())<1.e-7;}));

    const auto inclined=loadStepFixture("inclined-l-step.brep");const auto lStep=inclinedStepTarget(inclined);
    require(lStep.size()==1,"debug5 L-shaped transition missing");
    accepted(inclined,lStep,inclinedStepExpected());
    accepted(inclined,{TopoDS::Face(lStep.front().Reversed())},inclinedStepExpected());
    QString inclinedError;BRepReconstruction::Result inclinedResult;
    require(DeleteFaceGeometry::remove(inclined,lStep,inclinedResult,inclinedError),"debug5 reconstruction failed");
    checkInclinedStep(inclinedResult.solid);
    BRepBuilderAPI_Transform movedInclined(inclined,transform,true);
    accepted(movedInclined.Shape(),{TopoDS::Face(movedInclined.ModifiedShape(lStep.front()))},
        BRepBuilderAPI_Transform(inclinedStepExpected(),transform,true).Shape());
    auto openInclined=lStep;
    const auto inclinedBottom=select(inclined,[](const auto& face){return std::abs(center(face).Z())<1.e-7;});
    openInclined.insert(openInclined.end(),inclinedBottom.begin(),inclinedBottom.end());refused(inclined,openInclined);
    // Two possible station families do not establish a unique section axis.
    const auto notched=BRepAlgoAPI_Cut(inclined,BRepPrimAPI_MakeBox(gp_Pnt(30,0,-10),25,50,100).Shape()).Shape();
    const auto notchedTargets=inclinedStepTarget(notched);double negativeVolume=0;
    require(notchedTargets.size()==1&&BRepReconstruction::validSolid(notched,negativeVolume),"ambiguous-axis fixture is invalid or lost its target");
    refused(notched,notchedTargets);
    // A shallower low top never reaches the high top before x=225. Trimming
    // the L-step would erase that unselected high top, so it must be refused.
    BRepBuilderAPI_MakePolygon rampProfile;
    for(const auto& p:std::vector<gp_Pnt>{{36,0,0},{125,0,0},{125,0,45},{36,0,40.55}})rampProfile.Add(p);
    rampProfile.Close();
    const auto shallowRamp=BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(rampProfile.Wire()).Face(),gp_Vec(0,80,0)).Shape();
    BRepBuilderAPI_MakePolygon frontProfile;
    for(const auto& p:std::vector<gp_Pnt>{{36,36,0},{36,80,0},{125,80,0},{125,36-89*std::tan(std::acos(-1.0)/12),0}})frontProfile.Add(p);
    frontProfile.Close();
    const auto shallowLow=BRepAlgoAPI_Common(shallowRamp,BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(frontProfile.Wire()).Face(),gp_Vec(0,0,60)).Shape()).Shape();
    const auto shallow=BRepAlgoAPI_Fuse(shallowLow,BRepPrimAPI_MakeBox(gp_Pnt(125,0,0),100,80,60).Shape()).Shape();
    const auto shallowTargets=inclinedStepTarget(shallow);
    require(shallowTargets.size()==1&&BRepReconstruction::validSolid(shallow,negativeVolume),"unselected-top fixture is invalid or lost its target");
    refused(shallow,shallowTargets);
}
}

struct DeleteFaceTest {
    static void pick(CadViewport& v,size_t index,const std::vector<TopoDS_Face>& faces,bool clear=true) {
        if(clear) {v.context_->ClearSelected(false);v.selectedPartIndices_.clear();}
        for(const auto& f:faces) {
            Handle(StdSelect_BRepOwner) owner=new StdSelect_BRepOwner(f,v.boxes_[index].presentation,0,true);
            v.context_->AddOrRemoveSelected(owner,false);
        }
    }
    static void dismiss(MainWindow& w) {
        if(auto* dialog=w.findChild<QMessageBox*>("geometryErrorDialog")) dialog->accept();
        QApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    }
    static void emptySelection(CadViewport& v,QTreeWidget* tree) {
        require(v.context_->NbSelected()==0&&v.selectedPartIndices_.empty(),"history retained face/part selection");
        require(!v.context_->HasDetected()&&v.context_->MainSelector()->NbPicked()==0,"history retained detection/picks");
        require(tree->selectedItems().isEmpty()&&!tree->currentItem(),"history retained browser selection/current item");
        require(!v.commandSession_&&v.moveFaceArrow_.IsNull()&&v.faceRotationManipulator_.IsNull(),"history retained command tools");
    }
    static void reportedStep() {
        MainWindow window;window.show();QApplication::processEvents();
        auto& v=*window.findChild<CadViewport*>();auto* tree=window.findChild<QTreeWidget*>();
        v.addBox();v.addBox();
        for(size_t i=0;i<2;++i) {
            auto state=v.boxes_[i];state.shape=loadStepFixture(i==0?"low.brep":"rotated-high.brep");
            state.geometryKind=CadViewport::CadPart::GeometryKind::GeneralBRep;v.applyFaceState(i,state);
        }
        // Exact exported Parts are initially separate. The impossible first
        // Part must prevent a cross-Part Delete from partly changing the second.
        pick(v,0,stepTargets(v.boxes_[0].shape));pick(v,1,stepTargets(v.boxes_[1].shape),false);v.deleteFace();
        require(!v.commandSession_&&v.undoStack_.empty(),"separate Parts borrowed reconstruction supports");dismiss(window);
        require(v.applyBoolean(CadViewport::BooleanOperation::Unite,{0,1}),"reported Parts cannot be united");
        const auto source=v.boxes_[2].shape;const auto presentation=v.boxes_[2].presentation;
        const auto name=v.partName(2);auto* row=tree->topLevelItem(0)->child(2);
        const auto targets=stepTargets(source);require(targets.size()==2,"united report target patches missing");
        pick(v,2,targets);v.deleteFace();
        require(v.commandSession_&&v.commandSession_->valid,"reported transition Delete preview missing");
        require(v.boxes_[2].shape.IsSame(source)&&v.undoStack_.size()==1,"reported preview changed canonical material/history");
        const auto preview=v.commandSession_->facePreviews.front();
        require(BRepReconstruction::equivalent(preview->Shape(),stepExpected()),"reported preview has wrong trimmed material");
        v.cancelCommandSession();require(v.context_->DisplayStatus(preview)==PrsMgr_DisplayStatus_None&&v.boxes_[2].shape.IsSame(source),"reported Cancel failed");
        v.deleteFace();v.confirmCommandSession();const auto edited=v.boxes_[2].shape;
        require(v.undoStack_.size()==2&&BRepReconstruction::equivalent(edited,stepExpected()),"reported Delete did not commit one correct result");
        require(v.boxes_[2].presentation==presentation&&v.partName(2)==name&&tree->topLevelItem(0)->child(2)==row,"reported Delete lost united Part identity");
        v.undoPull();emptySelection(v,tree);require(v.boxes_[2].shape.IsSame(source),"reported Undo lost union");
        v.redoPull();emptySelection(v,tree);require(v.boxes_[2].shape.IsSame(edited),"reported Redo did not restore cached candidate");
    }
    static void sequentialStep() {
        for(bool triangleFirst:{true,false}) {
            MainWindow window;window.show();QApplication::processEvents();
            auto& v=*window.findChild<CadViewport*>();auto* tree=window.findChild<QTreeWidget*>();
            v.addBox();auto state=v.boxes_[0];state.shape=loadStepFixture("sequential-before.brep");
            state.geometryKind=CadViewport::CadPart::GeometryKind::GeneralBRep;v.applyFaceState(0,state);
            v.renamePart(0,"Sequential face removal");v.setPartColor(0,QColor(172,83,31));
            const auto source=v.boxes_[0].shape,presentationShape=v.boxes_[0].presentation->Shape();
            const auto presentation=v.boxes_[0].presentation;auto* row=tree->topLevelItem(0)->child(0);
            const auto targets=stepTargets(source);require(targets.size()==2,"sequential UI targets missing");
            const auto first=std::find_if(targets.begin(),targets.end(),[&](const auto& f) {
                return (std::abs(BRepAdaptor_Surface(f).Plane().Axis().Direction().X())>0.999)==triangleFirst;
            });
            require(first!=targets.end(),"sequential first target missing");
            pick(v,0,{*first});v.deleteFace();
            require(v.commandSession_&&v.commandSession_->valid&&v.boxes_[0].shape.IsSame(source),"first sequential UI preview failed or changed source");
            v.confirmCommandSession();const auto intermediate=v.boxes_[0].shape;
            require(v.undoStack_.size()==1&&BRepReconstruction::equivalent(intermediate,stepExpected(!triangleFirst,triangleFirst,43,22)),"first sequential UI commit wrong");
            const auto remaining=stepTargets(intermediate);require(remaining.size()==1,"second target missing from fresh UI topology");
            pick(v,0,remaining);v.deleteFace();
            require(v.commandSession_&&v.commandSession_->valid&&v.undoStack_.size()==1&&v.boxes_[0].shape.IsSame(intermediate),"second sequential UI preview failed or changed source/history");
            const auto preview=v.commandSession_->facePreviews.front();
            require(BRepReconstruction::equivalent(preview->Shape(),stepExpected(true,true,43,22)),"second preview has wrong material");
            v.cancelCommandSession();require(v.context_->DisplayStatus(preview)==PrsMgr_DisplayStatus_None&&v.context_->NbSelected()==1&&v.boxes_[0].shape.IsSame(intermediate),"second Cancel failed to restore the intermediate state/selection");
            v.deleteFace();v.confirmCommandSession();const auto final=v.boxes_[0].shape;
            require(v.undoStack_.size()==2&&BRepReconstruction::equivalent(final,stepExpected(true,true,43,22)),"second sequential commit wrong");
            v.undoPull();emptySelection(v,tree);require(v.boxes_[0].shape.IsSame(intermediate)&&v.undoStack_.size()==1,"first Undo skipped the intermediate result");
            v.undoPull();emptySelection(v,tree);require(v.boxes_[0].shape.IsSame(source),"second Undo lost the original solid");
            v.redoPull();emptySelection(v,tree);require(v.boxes_[0].shape.IsSame(intermediate),"first Redo did not restore cached intermediate result");
            v.redoPull();emptySelection(v,tree);require(v.boxes_[0].shape.IsSame(final),"second Redo did not restore cached final result");
            require(v.boxes_[0].presentation==presentation&&tree->topLevelItem(0)->child(0)==row&&v.partName(0)=="Sequential face removal"&&v.partColor(0)==QColor(172,83,31),"sequential history lost Part identity/properties");
            require(presentationShape.IsSame(source),"initial sequential presentation fixture was invalid");
        }
    }
    static void inclinedStep() {
        MainWindow window;window.show();QApplication::processEvents();
        auto& v=*window.findChild<CadViewport*>();auto* tree=window.findChild<QTreeWidget*>();
        v.addBox();auto state=v.boxes_[0];state.shape=loadStepFixture("inclined-l-step.brep");
        state.geometryKind=CadViewport::CadPart::GeometryKind::GeneralBRep;
        state.min=state.baseMin={36,0,0};state.max=state.baseMax={225,80,60};
        gp_Trsf placement;placement.SetRotation(gp_Ax1(gp_Pnt(10,-7,3),gp_Dir(1,2,3)),0.7);
        placement.SetTranslationPart(gp_Vec(31,-23,17));state.orientation=placement;v.applyFaceState(0,state);
        v.renamePart(0,"Inclined L step");v.setPartColor(0,QColor(73,141,188));
        const auto source=v.boxes_[0].shape;const auto presentation=v.boxes_[0].presentation;
        auto* row=tree->topLevelItem(0)->child(0);
        pick(v,0,inclinedStepTarget(source));v.deleteFace();
        require(v.commandSession_&&v.commandSession_->valid,"inclined L-step preview failed");
        const auto preview=v.commandSession_->facePreviews.front();checkInclinedStep(preview->Shape());
        require(v.boxes_[0].shape.IsSame(source)&&v.undoStack_.empty(),"L-step preview changed document/history");
        v.cancelCommandSession();
        require(v.context_->DisplayStatus(preview)==PrsMgr_DisplayStatus_None&&v.context_->NbSelected()==1&&v.boxes_[0].shape.IsSame(source),"L-step Cancel did not restore source/selection");
        v.deleteFace();v.confirmCommandSession();const auto edited=v.boxes_[0].shape;
        require(v.undoStack_.size()==1&&v.boxes_.size()==1&&BRepReconstruction::equivalent(edited,inclinedStepExpected()),"L-step commit changed unintended material/Part slots");
        for(int repeat=0;repeat<2;++repeat) {
            v.undoPull();emptySelection(v,tree);require(v.boxes_[0].shape.IsSame(source),"L-step Undo lost original topology");
            v.redoPull();emptySelection(v,tree);require(v.boxes_[0].shape.IsSame(edited),"L-step Redo lost cached topology");
        }
        require(v.boxes_[0].presentation==presentation&&tree->topLevelItem(0)->child(0)==row&&v.partName(0)=="Inclined L step"&&v.partColor(0)==QColor(73,141,188),"L-step history lost Part identity/properties");
        for(int i=1;i<=3;++i)for(int j=1;j<=4;++j)
            require(std::abs(v.boxes_[0].orientation.Value(i,j)-placement.Value(i,j))<1.e-10,"L-step history lost instance transform");
    }
    static void run() {
        MainWindow window;window.show();QApplication::processEvents();
        auto& v=*window.findChild<CadViewport*>();auto* tree=window.findChild<QTreeWidget*>();
        v.addBox();auto state=v.boxes_[0];state.shape=hole();
        v.applyFaceState(0,state);v.renamePart(0,"Delete identity");v.setPartColor(0,QColor(17,127,81));
        gp_Trsf t;t.SetRotation(gp_Ax1(gp_Pnt(),gp_Dir(1,2,3)),0.4);t.SetTranslationPart(gp_Vec(17,-9,23));
        v.boxes_[0].orientation=t;v.boxes_[0].presentation->SetLocalTransformation(t);
        const auto presentation=v.boxes_[0].presentation;const auto original=v.boxes_[0].shape;
        const auto originalStamp=stamp(original);const auto originalKind=v.boxes_[0].geometryKind;
        auto* row=tree->topLevelItem(0)->child(0);const auto targets=walls(original);
        AIS_ListOfInteractive objects;v.context_->ObjectsInside(objects);const auto baseline=objects.Extent();
        pick(v,0,targets);
        std::unique_ptr<QMenu> menu(v.createSelectionMenu(QPoint(100,100)));
        auto* action=menu->findChild<QAction*>("context.delete_face");require(action,"Delete Face context action missing");
        action->trigger();require(v.commandSession_&&v.commandSession_->valid,"Delete preview absent");
        require(v.boxes_[0].shape.IsSame(original)&&v.undoStack_.empty(),"preview modified document/history");
        require(v.commandSession_->facePreviews.size()==1&&!v.context_->IsDisplayed(presentation),"preview did not hide original");
        const auto preview=v.commandSession_->facePreviews.front();
        require(preview->DisplayMode()==AIS_Shaded&&std::abs(preview->Transparency()-0.25)<1.e-6,"wrong preview style");
        Quantity_Color blue;preview->Color(blue);require(blue.Blue()>0.9&&blue.Red()<0.3,"preview is not blue");
        TColStd_ListOfInteger modes;v.context_->ActivatedModes(preview,modes);
        require(modes.IsEmpty(),"preview can be selected");
        v.fitSelection({0});Image_PixMap shown,hidden;
        require(v.view_->ToPixMap(shown,640,480),"cannot render Delete preview");
        v.context_->Erase(preview,false);require(v.view_->ToPixMap(hidden,640,480),"cannot render without Delete preview");
        v.context_->Display(preview,false);v.context_->Deactivate(preview);
        int bluePixels=0;
        for(int y=0;y<480;++y) for(int x=0;x<640;++x) {
            const auto color=shown.PixelColor(x,y).GetRGB();
            if(color.Distance(hidden.PixelColor(x,y).GetRGB())>0.05&&color.Blue()>color.Red()+0.1&&color.Green()>color.Red()+0.05) ++bluePixels;
        }
        require(bluePixels>500,"Delete preview has no visible blue shaded surface");
        for(TopExp_Explorer f(preview->Shape(),TopAbs_FACE);f.More();f.Next()) {
            TopLoc_Location location;const auto triangles=BRep_Tool::Triangulation(TopoDS::Face(f.Current()),location);
            require(!triangles.IsNull()&&triangles->NbTriangles()>0,"preview face has no triangles");
        }
        v.cancelCommandSession();require(v.boxes_[0].shape.IsSame(original)&&v.context_->NbSelected()==int(targets.size()),"Cancel lost geometry/selection");
        require(v.context_->DisplayStatus(preview)==PrsMgr_DisplayStatus_None&&v.context_->IsDisplayed(presentation),"Cancel leaked preview");
        AIS_ListOfInteractive afterCancel;v.context_->ObjectsInside(afterCancel);require(afterCancel.Extent()==baseline,"Cancel leaked AIS objects");
        v.deleteFace();v.confirmCommandSession();
        require(!v.commandSession_&&v.undoStack_.size()==1&&v.redoStack_.empty(),"Delete was not one transaction");
        const auto edited=v.boxes_[0].shape;
        require(BRepReconstruction::equivalent(edited,stock()),"committed wrong material");
        require(v.boxes_[0].geometryKind==CadViewport::CadPart::GeometryKind::GeneralBRep,"Delete retained primitive regeneration");
        require(v.boxes_[0].presentation==presentation&&tree->topLevelItem(0)->child(0)==row,"Delete changed Part/browser identity");
        require(v.partName(0)=="Delete identity"&&v.partColor(0)==QColor(17,127,81),"Delete lost properties");
        for(int r=1;r<=3;++r) for(int c=1;c<=4;++c) require(std::abs(v.boxes_[0].orientation.Value(r,c)-t.Value(r,c))<1.e-12,"Delete changed transform");
        require(v.undoStack_.back().debugOperation["name"]=="Delete Face"&&v.undoStack_.back().debugOperation["deletedFacesBefore"].toArray().size()==int(targets.size()),"Delete debug metadata missing");
        v.undoAction()->trigger();emptySelection(v,tree);
        require(v.boxes_[0].shape.IsSame(original)&&v.boxes_[0].geometryKind==originalKind,"Undo lost snapshot");
        v.redoAction()->trigger();emptySelection(v,tree);require(v.boxes_[0].shape.IsSame(edited),"Redo recomputed or lost candidate");
        v.undoPull();pick(v,0,targets);v.deleteFace();v.cancelCommandSession();require(v.redoStack_.size()==1,"Cancel cleared Redo");
        v.deleteFace();QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);
        QApplication::sendEvent(v.commandButtons_,&escape);require(!v.commandSession_&&v.redoStack_.size()==1,"Esc did not cancel Delete");
        v.deleteFace();QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);
        QApplication::sendEvent(v.commandButtons_,&enter);require(!v.commandSession_&&v.undoStack_.size()==1,"Enter did not confirm Delete");
        const auto keyboardEdited=v.boxes_[0].shape;
        v.undoPull();pick(v,0,targets);v.deleteFace();
        QKeyEvent undoKey(QEvent::KeyPress,Qt::Key_Y,Qt::ControlModifier);
        QApplication::sendEvent(v.commandButtons_,&undoKey);emptySelection(v,tree);
        require(v.boxes_[0].shape.IsSame(keyboardEdited),"Redo from Delete panel lost cached result");v.undoPull();
        pick(v,0,targets);v.deleteFace();v.commandPanel_->close();require(!v.commandSession_&&v.redoStack_.size()==1,"closing Delete panel committed or cleared Redo");
        // A failed explicit deletion presents the shared popup and cannot commit.
        pick(v,0,select(original,[](const auto& f){return center(f).X()<0.01;}));v.deleteFace();
        auto* popup=window.findChild<QMessageBox*>("geometryErrorDialog");require(popup&&popup->isVisible()&&!popup->informativeText().isEmpty(),"missing closure failure popup");
        require(!v.commandSession_&&v.undoStack_.empty()&&v.redoStack_.size()==1&&stamp(original)==originalStamp,"failure changed document/history");dismiss(window);

        // Multi-Part preview and grouped history are atomic, including a failure
        // on the second Part after a valid candidate for the first was computed.
        v.addBox();auto second=v.boxes_[1];second.shape=hole();v.applyFaceState(1,second);
        pick(v,0,targets);pick(v,1,walls(second.shape),false);v.deleteFace();
        require(v.commandSession_&&v.commandSession_->facePreviews.size()==2,"multi-Part preview absent");
        QTemporaryDir temp;QString error;DebugModel::Snapshot capture;
        require(v.exportDebugModel(temp.filePath("preview.zip"),error),"Delete debug capture failed");
        require(DebugModel::read(temp.filePath("preview.zip"),capture,error),"Delete debug reload failed");
        require(capture.objects.size()==6&&capture.context["activeCommand"].toObject()["deletedFacesBefore"].toArray().size()==2,"multi-Part debug preview/source snapshots missing");
        v.confirmCommandSession();require(v.undoStack_.size()==1&&v.undoStack_.back().groupedChanges.size()==2,"multi-Part Delete not grouped");
        require(v.exportDebugModel(temp.filePath("committed.zip"),error)&&DebugModel::read(temp.filePath("committed.zip"),capture,error),"grouped Delete debug capture failed");
        require(capture.objects.size()==4,"grouped Delete debug omitted source snapshot");
        v.undoPull();emptySelection(v,tree);require(v.boxes_[0].shape.IsSame(original)&&v.boxes_[1].shape.IsSame(second.shape),"grouped Undo failed");
        v.redoPull();emptySelection(v,tree);v.undoPull();
        pick(v,0,targets);pick(v,1,select(second.shape,[](const auto& f){return center(f).X()<0.01;}),false);v.deleteFace();
        require(!v.commandSession_&&v.boxes_[0].shape.IsSame(original)&&v.boxes_[1].shape.IsSame(second.shape),"failed group partially applied");dismiss(window);
        // Existing Move Face and Boolean errors use the same presentation route.
        pick(v,0,select(original,[](const auto& f){return center(f).Z()>59.99;}));v.moveFace();
        v.dragDimension_->setText("-200");v.confirmCommandSession();
        require(window.findChild<QMessageBox*>("geometryErrorDialog"),"Move Face closure failure omitted popup");dismiss(window);v.cancelCommandSession();
        pick(v,0,select(original,[](const auto& f){return center(f).Z()>59.99;}));v.rotateFace();v.chooseFaceRotationAxis(2);
        v.dragDimension_->setText("180");v.confirmCommandSession();
        require(window.findChild<QMessageBox*>("geometryErrorDialog"),"Rotate Face failure omitted popup");dismiss(window);v.cancelCommandSession();
        pick(v,0,select(original,[](const auto& f){return center(f).Z()>59.99;}));v.alignFace();
        v.commandSession_->referencePlane=gp_Pln(gp_Pnt(0,0,-200),gp_Dir(0,0,1));
        v.commandSession_->hasReference=true;v.alignFacePickingReference_=false;
        v.confirmCommandSession();
        require(window.findChild<QMessageBox*>("geometryErrorDialog"),"Align Face failure omitted popup");dismiss(window);v.cancelCommandSession();
        gp_Trsf far;far.SetTranslation(gp_Vec(500,0,0));v.boxes_[1].orientation=far;
        require(!v.applyBoolean(CadViewport::BooleanOperation::Intersect,{0,1}),"disjoint intersection accepted");
        require(window.findChild<QMessageBox*>("geometryErrorDialog"),"Boolean geometry failure omitted popup");dismiss(window);
        v.addBox();const auto pullBefore=v.boxes_[2].shape;v.dragIndex_=2;v.dragAxis_=2;v.dragMax_=true;
        v.dragMin_=v.boxes_[2].min;v.dragMaxBounds_=v.boxes_[2].max;v.beginPullSession();
        v.dragDimension_->setText("-200");v.confirmCommandSession();
        require(window.findChild<QMessageBox*>("geometryErrorDialog")&&v.commandSession_&&!v.commandSession_->valid,"Pull failure omitted popup or accepted geometry");
        require(v.boxes_[2].shape.IsSame(pullBefore),"failed Pull did not restore source");dismiss(window);v.cancelCommandSession();
    }
};

int main(int argc,char** argv) {
#if defined(Q_OS_LINUX)
    if(qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")&&!qEnvironmentVariableIsEmpty("DISPLAY")) qputenv("QT_QPA_PLATFORM","xcb");
#endif
    try {
        if(argc>1&&std::string(argv[1])=="--geometry") geometry();
        else {QApplication app(argc,argv);DeleteFaceTest::run();DeleteFaceTest::reportedStep();DeleteFaceTest::sequentialStep();DeleteFaceTest::inclinedStep();}
        std::cout<<"Delete Face passed.\n";return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
