#include "CadViewport.h"
#include "MainWindow.h"
#include "DebugModel.h"
#include <QApplication>
#include <QAction>
#include <QMenu>
#include <QMenuBar>
#include <QLineEdit>
#include <QLocale>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>
#include <QProcess>
#include <QTimer>
#include <QFileDialog>
#include <QDialogButtonBox>
#include <QPushButton>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepTools.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <BRep_Tool.hxx>
#include <Poly_Triangulation.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <StdSelect_BRepOwner.hxx>
#include <archive.h>
#include <archive_entry.h>
#include <sstream>
#include <iostream>
#include <stdexcept>
namespace {
void require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
std::string stamp(const TopoDS_Shape& shape) {std::ostringstream s;BRepTools::Write(shape,s);return s.str();}
void readArchive(const QString& path,DebugModel::Snapshot& snapshot) {QString error;if(!DebugModel::read(path,snapshot,error)) throw std::runtime_error(error.toStdString());}
void malformedZip(const QString& path,const QByteArray& manifest,const QByteArray& brep,const char* shapeName="shapes/00000.brep") {
    auto* zip=archive_write_new();archive_write_set_format_zip(zip);require(archive_write_open_filename(zip,path.toUtf8().constData())==ARCHIVE_OK,"test archive open failed");
    for(const auto& entry:std::vector<std::pair<QByteArray,QByteArray>>{{"manifest.json",manifest},{shapeName,brep}}) {
        auto* e=archive_entry_new();archive_entry_set_pathname(e,entry.first.constData());archive_entry_set_filetype(e,AE_IFREG);archive_entry_set_perm(e,0600);archive_entry_set_size(e,entry.second.size());
        require(archive_write_header(zip,e)==ARCHIVE_OK,"test header failed");archive_write_data(zip,entry.second.constData(),entry.second.size());archive_entry_free(e);
    }
    archive_write_close(zip);archive_write_free(zip);
}
}
struct DebugModelTest {
    static void run() {
        QTemporaryDir temp;require(temp.isValid(),"temporary directory failed");
        MainWindow window;window.show();QApplication::processEvents();auto* viewport=window.findChild<CadViewport*>();require(viewport,"viewport missing");auto& v=*viewport;
        auto* menuAction=window.findChild<QAction*>("exportDebugModelAction");require(menuAction&&menuAction->text()=="Export Debug Model...","File debug export action missing");
        auto* fileMenu=qobject_cast<QMenu*>(window.menuBar()->actions().front()->menu());require(fileMenu&&fileMenu->actions().contains(menuAction),"debug export not under File");
        v.addBox();v.addBox();v.addBox();
        const auto low=BRepPrimAPI_MakeBox(100,80,50).Shape();
        const auto high=BRepPrimAPI_MakeBox(gp_Pnt(60,30,0),100,80,90).Shape();
        v.boxes_[0].shape=BRepAlgoAPI_Fuse(low,high).Shape();v.boxes_[0].geometryKind=CadViewport::CadPart::GeometryKind::GeneralBRep;
        v.boxes_[0].presentation->SetShape(v.boxes_[0].shape);v.boxes_[0].name=QString::fromUtf8("Unite — diagnostiek");v.setPartColor(0,QColor(12,99,181));
        gp_Trsf transform;transform.SetRotation(gp_Ax1(gp_Pnt(),gp_Dir(0,0,1)),0.3);transform.SetScaleFactor(1.7);transform.SetTranslationPart(gp_Vec(11,22,33));
        v.boxes_[0].orientation=transform;v.boxes_[0].presentation->SetLocalTransformation(transform);
        v.setPartVisible(1,false);v.boxes_[2].active=false;v.context_->Erase(v.boxes_[2].presentation,false);
        BRepMesh_IncrementalMesh mesh(v.boxes_[0].shape,0.1,false,0.35,false);
        TopTools_IndexedMapOfShape faces;TopExp::MapShapes(v.boxes_[0].shape,TopAbs_FACE,faces);const auto face=TopoDS::Face(faces(1));
        v.context_->ClearSelected(false);Handle(StdSelect_BRepOwner) owner=new StdSelect_BRepOwner(face,v.boxes_[0].presentation,5,true);v.context_->AddOrRemoveSelected(owner,false);
        const auto before=stamp(v.boxes_[0].shape);const auto undo=v.undoStack_.size(),redo=v.redoStack_.size();
        QString error;const auto zip=temp.filePath("model.zip");require(v.exportDebugModel(zip,error),error.toStdString().c_str());
        DebugModel::Snapshot loaded;readArchive(zip,loaded);
        require(loaded.objects.size()==3,"hidden/inactive document slots not exported");
        require(loaded.context["occtVersion"].isString(),"OCCT version missing");
        require(loaded.context["selection"].toObject()["faces"].toArray().size()==1,"selected face not exported");
        const auto selected=loaded.context["selection"].toObject()["faces"].toArray()[0].toObject();require(selected["faceIndex"]==1&&selected["partId"]=="0","selected face index/part changed");
        TopTools_IndexedMapOfShape loadedFaces;TopExp::MapShapes(loaded.objects[0].shape,TopAbs_FACE,loadedFaces);
        GProp_GProps selectedBefore,selectedAfter;BRepGProp::SurfaceProperties(face,selectedBefore);BRepGProp::SurfaceProperties(loadedFaces(1),selectedAfter);
        require(selectedBefore.CentreOfMass().Distance(selectedAfter.CentreOfMass())<1.e-9&&std::abs(selectedBefore.Mass()-selectedAfter.Mass())<1.e-8,"reloaded face selection index identifies a different geometric face");
        require(DebugModel::equivalent(loaded.objects[0].shape,v.boxes_[0].shape),"round-trip changed geometry");
        require(loaded.objects[0].metadata["roundTripMaterialVerified"].toBool(),"export did not verify round-trip material");
        require(loaded.objects[0].metadata["name"].toString()==v.boxes_[0].name,"Unicode part name lost");
        require(!loaded.objects[1].metadata["visible"].toBool()&&!loaded.objects[2].metadata["active"].toBool(),"visibility or active status lost");
        require(loaded.objects[0].metadata["colorRgba"].toArray()[2].toDouble()==v.boxes_[0].color.blueF(),"part color lost");
        const auto rows=loaded.objects[0].metadata["transform3x4"].toArray();for(int i=0;i<3;++i) for(int j=0;j<4;++j) require(std::abs(rows[i].toArray()[j].toDouble()-transform.Value(i+1,j+1))<1.e-12,"part transform lost");
        require(stamp(v.boxes_[0].shape)==before&&v.undoStack_.size()==undo&&v.redoStack_.size()==redo,"export/import changed document/history");
        require(v.selectionContext().faces.size()==1&&v.selectionContext().faces.front().face.IsSame(face),"export changed face selection");
        require(DebugModel::meshDiagnostics(loaded.objects[0].shape)["meshedFaces"].toInt()>0,"stored mesh missing");
        const auto original=stamp(loaded.objects[0].shape);const auto fresh=DebugModel::remesh(loaded.objects[0].shape);
        require(DebugModel::equivalent(fresh,loaded.objects[0].shape)&&stamp(loaded.objects[0].shape)==original,"remesh changed original material/evidence");
        // Deliberately corrupt display data while keeping exact box geometry.
        auto corrupt=BRepBuilderAPI_Copy(BRepPrimAPI_MakeBox(10,10,10).Shape(),true,false).Shape();BRepMesh_IncrementalMesh meshBox(corrupt,0.1,false,0.35,false);
        TopExp_Explorer f(corrupt,TopAbs_FACE);TopLoc_Location location;auto tri=BRep_Tool::Triangulation(TopoDS::Face(f.Current()),location);
        for(int i=1;i<=tri->NbNodes();++i) tri->SetNode(i,tri->Node(i).Translated(gp_Vec(1000,1000,1000)));
        require(DebugModel::meshDiagnostics(corrupt)["outsideTrimSamples"].toInt()>0,"bad stored display mesh was not detected");
        DebugModel::Snapshot meshEvidence;auto evidenceMetadata=loaded.objects[0].metadata;
        evidenceMetadata["transform3x4"]=QJsonArray{QJsonArray{1,0,0,0},QJsonArray{0,1,0,0},QJsonArray{0,0,1,0}};
        meshEvidence.objects.push_back({evidenceMetadata,corrupt});
        require(DebugModel::write(temp.filePath("corrupt-mesh.zip"),meshEvidence,error),"cannot export damaged mesh evidence");
        DebugModel::Snapshot meshReloaded;readArchive(temp.filePath("corrupt-mesh.zip"),meshReloaded);
        require(DebugModel::meshDiagnostics(meshReloaded.objects[0].shape)["outsideTrimSamples"].toInt()>0,"round-trip discarded corrupt triangulation evidence");
        BRepClass3d_SolidClassifier material(corrupt,gp_Pnt(1005,1005,1005),1.e-7);
        require(material.State()==TopAbs_OUT,"display-only defect was classified as actual solid material");
        const auto repaired=DebugModel::remesh(meshReloaded.objects[0].shape);
        require(DebugModel::meshDiagnostics(repaired)["outsideTrimSamples"].toInt()==0&&DebugModel::equivalent(corrupt,repaired),"fresh triangulation did not repair display-only defect");
        // Cancelling the actual File dialog must leave document/selection intact.
        QTimer::singleShot(20,[] {for(auto* widget:QApplication::topLevelWidgets()) if(auto* dialog=qobject_cast<QFileDialog*>(widget)) dialog->reject();});
        menuAction->trigger();require(stamp(v.boxes_[0].shape)==before&&v.selectionContext().faces.size()==1,"cancelled File export changed document/selection");
        // Export preview evidence without committing or cancelling the command.
        require(v.startMoveFace(0,face),"cannot start Move Face diagnostic session");require(v.previewMoveFace(2.125),"cannot create debug preview");
        const auto candidate=v.commandSession_->candidate;const auto preview=v.commandSession_->preview;
        QTimer::singleShot(20,[&] {
            for(auto* widget:QApplication::topLevelWidgets()) if(auto* dialog=qobject_cast<QFileDialog*>(widget)) {
                dialog->setDirectory(temp.path());
                if(auto* buttons=dialog->findChild<QDialogButtonBox*>()) if(auto* save=buttons->button(QDialogButtonBox::Save)) save->click();
            }
        });
        menuAction->trigger();require(QFile::exists(temp.filePath("opendesigner-debug.zip")),"File menu did not export debug ZIP");
        readArchive(temp.filePath("opendesigner-debug.zip"),loaded);require(loaded.objects.size()==5,"preview or pre-command B-Rep missing");
        require(loaded.context["activeCommand"].toObject()["value"].toDouble()==2.125,"active parameter lost");
        require(v.commandSession_&&v.commandSession_->candidate.IsSame(candidate)&&v.context_->IsDisplayed(preview),"export disturbed live preview");
        require(v.undoStack_.size()==undo&&stamp(v.boxes_[0].shape)==before,"preview export committed geometry");v.cancelCommandSession();
        require(v.context_->DisplayStatus(preview)==PrsMgr_DisplayStatus_None&&stamp(v.boxes_[0].shape)==before,"Cancel after export failed");
        require(v.startMoveFace(0,face),"cannot restart command");v.dragDimension_->setText(QLocale().toString(2.12,'f',2));v.confirmCommandSession();require(v.undoStack_.size()==undo+1,"confirmation did not add exactly one operation");
        const auto committed=stamp(v.boxes_[0].shape);require(v.exportDebugModel(temp.filePath("committed.zip"),error),error.toStdString().c_str());readArchive(temp.filePath("committed.zip"),loaded);
        require(loaded.context["lastAppliedOperation"].toObject()["name"]=="Move Face","last modeling command missing");
        require(loaded.context["lastAppliedOperation"].toObject()["value"].toDouble()==2.12,"committed parameter lost");require(loaded.objects.back().metadata["role"]=="last-operation-before","previous exact B-Rep missing");
        v.undoPull();require(stamp(v.boxes_[0].shape)==before,"Undo after export failed");v.redoPull();require(stamp(v.boxes_[0].shape)==committed,"Redo after export failed");
        // Independent developer loader, JSON reports, fresh B-Reps and world probe.
        const auto sample=gp_Pnt(130,70,20).Transformed(transform);
        const auto sampleText=QString("0,%1,%2,%3").arg(sample.X(),0,'g',17).arg(sample.Y(),0,'g',17).arg(sample.Z(),0,'g',17);
        QProcess inspector;inspector.start(QCoreApplication::applicationDirPath()+"/OpenDesignerDebugModel",{zip,"--probe",sampleText,"--render","--output",temp.filePath("inspection")});
        require(inspector.waitForFinished(30000)&&inspector.exitCode()==0,"developer inspector failed");QJsonParseError parse;
        const auto report=QJsonDocument::fromJson(inspector.readAllStandardOutput(),&parse);require(parse.error==QJsonParseError::NoError&&report["objects"].toArray().size()==3,"invalid inspector report");
        require(report["worldSpaceProbes"].toArray()[0].toObject()["classification"]=="IN","world-space probe/transform classification failed");
        require(QFile::exists(temp.filePath("inspection/report.json"))&&QFile::exists(temp.filePath("inspection/fresh-0.brep")),"inspection files missing");
        require(QFile::exists(temp.filePath("inspection/stored-0.png"))&&QFile::exists(temp.filePath("inspection/fresh-0.png"))&&QFile::exists(temp.filePath("inspection/wire-0.png")),"stored/fresh/wire diagnostic renders missing");
        // Malformed archives must fail atomically; no partially loaded snapshot.
        QFile manifest(temp.filePath("inspection/report.json"));require(manifest.open(QIODevice::ReadOnly),"cannot read report");auto json=QJsonDocument::fromJson(manifest.readAll()).object();
        auto objs=json["objects"].toArray();auto first=objs[0].toObject();first["sha256"]="wrong";objs[0]=first;json["objects"]=objs;
        malformedZip(temp.filePath("checksum.zip"),QJsonDocument(json).toJson(),"not a B-Rep");
        require(!DebugModel::read(temp.filePath("checksum.zip"),loaded,error)&&loaded.objects.empty()&&error.contains("checksum"),"corrupt archive accepted or partially loaded");
        malformedZip(temp.filePath("unsafe.zip"),QJsonDocument(json).toJson(),"x","../escape.brep");require(!DebugModel::read(temp.filePath("unsafe.zip"),loaded,error)&&loaded.objects.empty(),"unsafe ZIP path accepted");
        require(!v.exportDebugModel(temp.filePath("missing/model.zip"),error)&&stamp(v.boxes_[0].shape)==committed,"failed export damaged document");
        QFile originalZip(zip);require(originalZip.open(QIODevice::ReadOnly),"cannot read initial ZIP");const auto archiveBytes=originalZip.readAll();originalZip.close();
        DebugModel::Snapshot invalid;invalid.objects.push_back({{},TopoDS_Shape()});require(!DebugModel::write(zip,invalid,error),"null shape export succeeded");
        require(originalZip.open(QIODevice::ReadOnly)&&originalZip.readAll()==archiveBytes,"failed export replaced existing archive");originalZip.close();
        DebugModel::Snapshot openFace;openFace.objects.push_back({evidenceMetadata,face});
        require(DebugModel::write(temp.filePath("non-solid.zip"),openFace,error),"diagnostic capture refused non-solid geometry");readArchive(temp.filePath("non-solid.zip"),loaded);
        require(!loaded.objects[0].metadata["diagnostics"].toObject()["solidValid"].toBool(),"non-solid diagnostic input marked valid solid");
        v.setPartVisible(1,true);require(v.applyBoolean(CadViewport::BooleanOperation::Unite,{0,1}),"Boolean diagnostic setup failed");
        require(v.exportDebugModel(temp.filePath("boolean.zip"),error),"Boolean export failed");readArchive(temp.filePath("boolean.zip"),loaded);
        const auto booleanOperation=loaded.context["lastAppliedOperation"].toObject();require(booleanOperation["available"].toBool()&&booleanOperation["operandPartIds"].toArray()==QJsonArray{"0","1"}&&!booleanOperation["keepTools"].toBool(),"Boolean operation parameters missing");
        require(loaded.objects.size()==4&&!loaded.objects[0].metadata["active"].toBool()&&!loaded.objects[1].metadata["active"].toBool(),"Boolean-consumed parts missing from debug ZIP");
        DebugModel::Snapshot empty;require(DebugModel::write(temp.filePath("empty.zip"),empty,error),"empty document export failed");readArchive(temp.filePath("empty.zip"),loaded);require(loaded.objects.empty(),"empty snapshot changed");
    }
};
int main(int argc,char** argv) {
#if defined(Q_OS_LINUX)
    if(qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")&&!qEnvironmentVariableIsEmpty("DISPLAY")) qputenv("QT_QPA_PLATFORM","xcb");
#endif
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc,argv);try {DebugModelTest::run();std::cout<<"Debug export/import passed.\n";return 0;}catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}}
