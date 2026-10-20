#include "DebugModel.h"
#include <archive.h>
#include <archive_entry.h>
#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QSaveFile>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BOPAlgo_ArgumentAnalyzer.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <BRepGProp.hxx>
#include <BRepBndLib.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRep_Tool.hxx>
#include <Poly_Triangulation.hxx>
#include <GProp_GProps.hxx>
#include <Bnd_Box.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <Standard_Version.hxx>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <memory>
namespace {
constexpr qint64 maxEntry=256LL*1024*1024,maxTotal=512LL*1024*1024;
void check(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
int count(const TopoDS_Shape& shape,TopAbs_ShapeEnum type) {
    TopTools_IndexedMapOfShape map;TopExp::MapShapes(shape,type,map);return map.Extent();
}
double volume(const TopoDS_Shape& shape) {GProp_GProps p;BRepGProp::VolumeProperties(shape,p);return p.Mass();}
QByteArray serialize(const TopoDS_Shape& shape) {
    std::ostringstream stream;
    BRepTools::Write(shape,stream,true,true,TopTools_FormatVersion_CURRENT);
    check(stream.good(),"Cannot serialize B-Rep.");return QByteArray::fromStdString(stream.str());
}
TopoDS_Shape deserialize(const QByteArray& data) {
    std::istringstream stream(data.toStdString());TopoDS_Shape shape;BRep_Builder builder;
    BRepTools::Read(shape,stream,builder);
    check(!shape.IsNull()&&!stream.fail(),"Cannot read exact B-Rep geometry.");return shape;
}
QString digest(const QByteArray& bytes) {return QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());}
void sameMetrics(const QJsonObject& before,const QJsonObject& after) {
    for(const auto* key:{"faces","edges","vertices","solids","shells","brepValid","closedShells"})
        check(before[key]==after[key],"Reloaded topology/validity differs from manifest.");
    const double a=before["volume"].toDouble(),b=after["volume"].toDouble();
    check(std::abs(a-b)<=1.e-9*std::max({1.,std::abs(a),std::abs(b)}),"Reloaded volume differs from manifest.");
    auto x=before["bounds"].toArray(),y=after["bounds"].toArray();
    check(x.size()==y.size(),"Reloaded bounds differ from manifest.");
    for(int i=0;i<x.size();++i) check(std::abs(x[i].toDouble()-y[i].toDouble())<=1.e-7*std::max(1.,std::abs(x[i].toDouble())),"Reloaded bounds differ from manifest.");
}
}
QJsonObject DebugModel::diagnostics(const TopoDS_Shape& shape) {
    QJsonObject result;
    result["brepValid"]=BRepCheck_Analyzer(shape).IsValid();
    result["solids"]=count(shape,TopAbs_SOLID);result["shells"]=count(shape,TopAbs_SHELL);
    result["faces"]=count(shape,TopAbs_FACE);result["edges"]=count(shape,TopAbs_EDGE);result["vertices"]=count(shape,TopAbs_VERTEX);
    bool closed=true;for(TopExp_Explorer s(shape,TopAbs_SHELL);s.More();s.Next()) closed=closed&&BRep_Tool::IsClosed(s.Current());
    result["closedShells"]=closed&&result["shells"].toInt()>0;
    BRepBuilderAPI_Copy isolated(shape,true,false);BOPAlgo_ArgumentAnalyzer interference;
    interference.SetShape1(isolated.Shape());interference.SelfInterMode()=true;interference.StopOnFirstFaulty()=true;interference.Perform();
    result["selfInterferenceCheckCompleted"]=!interference.HasErrors();
    result["selfInterferenceDetected"]=interference.HasFaulty();
    result["solidValid"]=result["brepValid"].toBool()&&result["closedShells"].toBool()&&result["solids"].toInt()>0
        &&!interference.HasErrors()&&!interference.HasFaulty();
    QJsonArray faceDetails;TopTools_IndexedMapOfShape faces;TopExp::MapShapes(shape,TopAbs_FACE,faces);
    for(int i=1;i<=faces.Extent();++i) {
        GProp_GProps area;BRepGProp::SurfaceProperties(faces(i),area);
        faceDetails.append(QJsonObject{{"faceIndex",i},{"area",area.Mass()},
            {"surfaceType",static_cast<int>(BRepAdaptor_Surface(TopoDS::Face(faces(i))).GetType())},
            {"edges",count(faces(i),TopAbs_EDGE)}});
    }
    result["faceDetails"]=faceDetails;
    int seams=0;TopTools_IndexedDataMapOfShapeListOfShape adjacency;TopExp::MapShapesAndAncestors(shape,TopAbs_EDGE,TopAbs_FACE,adjacency);
    for(int i=1;i<=adjacency.Extent();++i) {
        const auto& fs=adjacency.FindFromIndex(i);if(fs.Extent()!=2||fs.First().IsSame(fs.Last())) continue;
        const BRepAdaptor_Surface a(TopoDS::Face(fs.First())),b(TopoDS::Face(fs.Last()));
        if(a.GetType()==GeomAbs_Plane&&b.GetType()==GeomAbs_Plane&&a.Plane().Axis().Direction().IsParallel(b.Plane().Axis().Direction(),1.e-8)&&a.Plane().Distance(b.Plane().Location())<1.e-7) ++seams;
    }
    result["coplanarDividingEdges"]=seams;
    result["volume"]=volume(shape);
    Bnd_Box box;BRepBndLib::AddOptimal(shape,box,false,false);QJsonArray bounds;
    if(!box.IsVoid()&&!box.IsOpen()) {double a,b,c,d,e,f;box.Get(a,b,c,d,e,f);for(double n:{a,b,c,d,e,f}) bounds.append(n);}
    result["bounds"]=bounds;result["coordinateFrame"]="canonical B-Rep (apply part transform separately)";
    return result;
}
bool DebugModel::equivalent(const TopoDS_Shape& a,const TopoDS_Shape& b) {
    BRepBuilderAPI_Copy ac(a,true,false),bc(b,true,false);
    BRepAlgoAPI_Common common;TopTools_ListOfShape args,tools;args.Append(ac.Shape());tools.Append(bc.Shape());
    common.SetArguments(args);common.SetTools(tools);common.SetNonDestructive(true);common.Build();
    if(!common.IsDone()||common.HasErrors()) return false;
    double av=volume(a),bv=volume(b),cv=volume(common.Shape()),tol=1.e-8*std::max({1.,std::abs(av),std::abs(bv)});
    return std::isfinite(cv)&&std::abs(av-bv)<=tol&&std::abs(av-cv)<=tol&&std::abs(bv-cv)<=tol;
}
TopoDS_Shape DebugModel::remesh(const TopoDS_Shape& shape) {
    BRepBuilderAPI_Copy copy(shape,true,false);auto fresh=copy.Shape();
    BRepTools::Clean(fresh,true);
    BRepMesh_IncrementalMesh mesh(fresh,0.1,false,0.35,false);
    check(mesh.IsDone(),"OCCT remeshing failed.");
    for(TopExp_Explorer f(fresh,TopAbs_FACE);f.More();f.Next()) {
        TopLoc_Location location;const auto tri=BRep_Tool::Triangulation(TopoDS::Face(f.Current()),location);
        check(!tri.IsNull()&&tri->NbTriangles()>0,"OCCT left an unmeshed face.");
    }
    return fresh;
}
QJsonObject DebugModel::meshDiagnostics(const TopoDS_Shape& shape) {
    int meshed=0,triangles=0,outside=0,planar=0;QJsonArray faces;
    TopTools_IndexedMapOfShape indexedFaces;TopExp::MapShapes(shape,TopAbs_FACE,indexedFaces);
    for(int faceIndex=1;faceIndex<=indexedFaces.Extent();++faceIndex) {
        const auto face=TopoDS::Face(indexedFaces(faceIndex));TopLoc_Location location;
        const auto mesh=BRep_Tool::Triangulation(face,location);if(mesh.IsNull()) continue;
        ++meshed;triangles+=mesh->NbTriangles();
        if(BRepAdaptor_Surface(face).GetType()!=GeomAbs_Plane) continue;
        ++planar;int bad=0;
        for(int i=1;i<=mesh->NbTriangles();++i) {
            int a,b,c;mesh->Triangle(i).Get(a,b,c);
            // Test near each corner as well as the centroid: a narrow spike
            // need not put a triangle's centroid outside the trimmed face.
            for(double weight:{1./3.,0.9}) for(int corner=0;corner<(weight==0.9?3:1);++corner) {
                int ids[3]={a,b,c};gp_XYZ xyz(0,0,0);
                for(int j=0;j<3;++j) xyz+=mesh->Node(ids[j]).XYZ()*(j==corner?weight:(1-weight)/2);
                const auto point=gp_Pnt(xyz).Transformed(location.Transformation());
                BRepClass_FaceClassifier classifier(face,point,1.e-6);
                if(classifier.State()==TopAbs_OUT) {++bad;break;}
            }
        }
        if(bad) {outside+=bad;faces.append(QJsonObject{{"faceIndex",faceIndex},{"outsideSamples",bad}});}
    }
    return {{"meshedFaces",meshed},{"triangles",triangles},{"checkedPlanarFaces",planar},
        {"outsideTrimSamples",outside},{"suspectFaces",faces},
        {"limitation","Planar triangle samples only; zero findings do not prove correct material or rendering."}};
}
bool DebugModel::write(const QString& path,const Snapshot& snapshot,QString& error) {
    try {
        check(snapshot.objects.size()<=1022,"Too many debug snapshot objects.");
        QMap<QString,QByteArray> files;QJsonArray objects;qint64 total=0;
        for(size_t i=0;i<snapshot.objects.size();++i) {
            const auto& object=snapshot.objects[i];check(!object.shape.IsNull(),"Cannot export a null shape.");
            // Never hand document topology to serializers, diagnostics or Booleans.
            BRepBuilderAPI_Copy copy(object.shape,true,true);const auto isolated=copy.Shape();
            const auto bytes=serialize(isolated);total+=bytes.size();check(bytes.size()<=maxEntry&&total<=maxTotal,"Debug model exceeds archive size limit.");
            const auto loaded=deserialize(bytes);const auto metrics=diagnostics(isolated);
            sameMetrics(metrics,diagnostics(loaded));
            const bool solid=metrics["solidValid"].toBool();
            const bool verified=solid&&equivalent(isolated,loaded);
            const QString name=QString("shapes/%1.brep").arg(i,5,10,QChar('0'));
            auto metadata=object.metadata;metadata["file"]=name;metadata["sha256"]=digest(bytes);metadata["diagnostics"]=metrics;
            metadata["roundTripMaterialVerified"]=verified;
            if(!verified) metadata["roundTripMaterialNote"]="Boolean equivalence unavailable or inconclusive; exact payload and geometry metrics retained for diagnosis.";objects.append(metadata);files.insert(name,bytes);
        }
        auto manifest=snapshot.context;manifest["format"]="OpenDesigner diagnostic snapshot";manifest["schemaVersion"]=1;
        manifest["applicationVersion"]="0.3.6";manifest["occtVersion"]=OCC_VERSION_COMPLETE;
        manifest["createdUtc"]=QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        manifest["faceIndexConvention"]="1-based TopExp::MapShapes(shape, TopAbs_FACE); local B-Rep frame";
        manifest["objects"]=objects;files.insert("manifest.json",QJsonDocument(manifest).toJson());
        check(files["manifest.json"].size()<=maxEntry&&total+files["manifest.json"].size()<=maxTotal,"Debug manifest exceeds archive size limit.");
        QSaveFile output(path);check(output.open(QIODevice::WriteOnly),"Cannot open debug ZIP destination.");
        std::unique_ptr<archive,decltype(&archive_write_free)> zip(archive_write_new(),archive_write_free);
        check(archive_write_set_format_zip(zip.get())==ARCHIVE_OK,"Cannot initialize ZIP writer.");
        const auto callback=[](archive*,void* client,const void* data,size_t size)->la_ssize_t {
            return static_cast<QSaveFile*>(client)->write(static_cast<const char*>(data),size);
        };
        check(archive_write_open(zip.get(),&output,nullptr,callback,nullptr)==ARCHIVE_OK,"Cannot start ZIP output.");
        for(auto it=files.cbegin();it!=files.cend();++it) {
            std::unique_ptr<archive_entry,decltype(&archive_entry_free)> entry(archive_entry_new(),archive_entry_free);
            const auto name=it.key().toUtf8();archive_entry_set_pathname(entry.get(),name.constData());
            archive_entry_set_filetype(entry.get(),AE_IFREG);archive_entry_set_perm(entry.get(),0600);archive_entry_set_size(entry.get(),it.value().size());
            check(archive_write_header(zip.get(),entry.get())==ARCHIVE_OK,"Cannot write ZIP entry.");
            check(archive_write_data(zip.get(),it.value().constData(),it.value().size())==it.value().size(),"Cannot write ZIP data.");
        }
        check(archive_write_close(zip.get())==ARCHIVE_OK,"Cannot finish debug ZIP.");
        check(output.commit(),"Cannot atomically save debug ZIP.");error.clear();return true;
    } catch(const Standard_Failure& e) {error=QString::fromUtf8(e.GetMessageString());}
      catch(const std::exception& e) {error=QString::fromUtf8(e.what());}
    return false;
}
bool DebugModel::read(const QString& path,Snapshot& snapshot,QString& error) {
    snapshot={};
    try {
        QFile input(path);check(input.open(QIODevice::ReadOnly),"Cannot open debug ZIP.");
        check(input.size()<=maxTotal,"Debug ZIP exceeds size limit.");const auto bytes=input.readAll();
        std::unique_ptr<archive,decltype(&archive_read_free)> zip(archive_read_new(),archive_read_free);
        archive_read_support_format_zip(zip.get());
        check(archive_read_open_memory(zip.get(),bytes.constData(),bytes.size())==ARCHIVE_OK,"Cannot read debug ZIP.");
        QMap<QString,QByteArray> files;archive_entry* entry=nullptr;qint64 total=0;int status;
        while((status=archive_read_next_header(zip.get(),&entry))==ARCHIVE_OK) {
            const auto name=QString::fromUtf8(archive_entry_pathname(entry));
            const auto size=archive_entry_size(entry);check(size>=0&&size<=maxEntry,"Invalid debug ZIP entry size.");total+=size;
            check(size>=0&&size<=maxEntry&&total<=maxTotal&&files.size()<1024,"Debug ZIP exceeds entry limits.");
            check(archive_entry_filetype(entry)==AE_IFREG&&!archive_entry_symlink(entry)&&!archive_entry_hardlink(entry),"Non-file ZIP entry refused.");
            check(!files.contains(name)&&(name=="manifest.json"||(name.startsWith("shapes/")&&name.endsWith(".brep")&&!name.contains("..")&&name.count('/')==1)),"Unexpected or duplicate ZIP path.");
            QByteArray data(size,Qt::Uninitialized);qint64 done=0;
            while(done<size) {auto n=archive_read_data(zip.get(),data.data()+done,size-done);check(n>0,"Truncated ZIP entry.");done+=n;}
            files.insert(name,data);
        }
        check(status==ARCHIVE_EOF,"Damaged ZIP archive.");
        QJsonParseError parse;const auto document=QJsonDocument::fromJson(files.value("manifest.json"),&parse);
        check(parse.error==QJsonParseError::NoError&&document.isObject(),"Invalid debug manifest JSON.");
        auto manifest=document.object();check(manifest["schemaVersion"].toInt()==1&&manifest["format"]=="OpenDesigner diagnostic snapshot","Unsupported debug snapshot format.");
        check(manifest["objects"].isArray(),"Missing snapshot objects.");Snapshot pending;pending.context=manifest;pending.context.remove("objects");
        QMap<QString,bool> used;
        for(const auto value:manifest["objects"].toArray()) {
            check(value.isObject(),"Invalid snapshot object.");const auto metadata=value.toObject();const auto name=metadata["file"].toString();
            check(files.contains(name)&&name!="manifest.json"&&!used.contains(name),"Missing or repeated B-Rep payload.");used[name]=true;
            check(digest(files[name])==metadata["sha256"].toString(),"B-Rep checksum mismatch.");
            check(metadata["role"].isString()&&metadata["partId"].isString(),"Missing object identity.");
            const auto rows=metadata["transform3x4"].toArray();
            check(rows.size()==3,"Invalid part transformation.");
            for(const auto row:rows) {check(row.toArray().size()==4,"Invalid transformation row.");for(const auto number:row.toArray()) check(number.isDouble()&&std::isfinite(number.toDouble()),"Invalid transformation value.");}
            const auto color=metadata["colorRgba"].toArray();check(color.size()==4,"Invalid object color.");
            for(const auto number:color) check(number.isDouble()&&number.toDouble()>=0&&number.toDouble()<=1,"Invalid color component.");
            auto shape=deserialize(files[name]);sameMetrics(metadata["diagnostics"].toObject(),diagnostics(shape));
            pending.objects.push_back({metadata,shape});
        }
        check(files.size()==used.size()+1,"Unreferenced debug payload.");
        snapshot=std::move(pending);error.clear();return true;
    } catch(const Standard_Failure& e) {error=QString::fromUtf8(e.GetMessageString());}
      catch(const std::exception& e) {error=QString::fromUtf8(e.what());}
    return false;
}
