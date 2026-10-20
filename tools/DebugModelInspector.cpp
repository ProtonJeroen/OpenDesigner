#include "DebugModel.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QComboBox>
#include <QLabel>
#include <QVBoxLayout>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QDir>
#include <QFileInfo>
#include <sstream>
#include <cmath>
#include <QSaveFile>
#include <QTextStream>
#include <Image_AlienPixMap.hxx>
#include <AIS_InteractiveContext.hxx>
#include <AIS_Shape.hxx>
#include <Aspect_DisplayConnection.hxx>
#include <Aspect_NeutralWindow.hxx>
#include <OpenGl_GraphicDriver.hxx>
#include <V3d_Viewer.hxx>
#include <V3d_View.hxx>
#include <Prs3d_Drawer.hxx>
#include <BRepTools.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <Standard_Failure.hxx>
#include <TopoDS_Shape.hxx>
#include <memory>
#include <stdexcept>
namespace {
gp_Trsf placement(const QJsonObject& object) {
    const auto rows=object["transform3x4"].toArray();gp_Trsf trsf;
    if(rows.size()==3) {const auto a=rows[0].toArray(),b=rows[1].toArray(),c=rows[2].toArray();
        trsf.SetValues(a[0].toDouble(),a[1].toDouble(),a[2].toDouble(),a[3].toDouble(),
            b[0].toDouble(),b[1].toDouble(),b[2].toDouble(),b[3].toDouble(),c[0].toDouble(),c[1].toDouble(),c[2].toDouble(),c[3].toDouble());}
    return trsf;
}
class Canvas final:public QWidget {
public:
    Canvas():QWidget(nullptr) {setAttribute(Qt::WA_NativeWindow);setAttribute(Qt::WA_PaintOnScreen);setAttribute(Qt::WA_NoSystemBackground);setMinimumSize(640,480);}
    void showObject(const DebugModel::Object& object,int mode,bool fit) {
        initialize();context_->RemoveAll(false);
        auto shape=mode==1?DebugModel::remesh(object.shape):BRepBuilderAPI_Copy(object.shape,true,mode==0).Shape();
        if(mode==2) BRepTools::Clean(shape,true);
        Handle(AIS_Shape) presentation=new AIS_Shape(shape);
        presentation->SetLocalTransformation(placement(object.metadata));
        const auto rgba=object.metadata["colorRgba"].toArray();
        if(rgba.size()==4) presentation->SetColor(Quantity_Color(rgba[0].toDouble(),rgba[1].toDouble(),rgba[2].toDouble(),Quantity_TOC_RGB));
        // The stored-mesh view must not silently regenerate the evidence.
        presentation->Attributes()->SetAutoTriangulation(mode==1);
        context_->Display(presentation,mode==2?AIS_WireFrame:AIS_Shaded,-1,false);
        if(fit) {view_->FitAll(0.05,false);view_->ZFitAll();}view_->Invalidate();view_->Redraw();
    }
    void saveImage(const QString& path) {
        Image_AlienPixMap image;
        if(!view_->ToPixMap(image,640,480)||!image.Save(path.toUtf8().constData())) throw std::runtime_error("Cannot render diagnostic image.");
    }
    QPaintEngine* paintEngine() const override {return nullptr;}
protected:
    void paintEvent(QPaintEvent*) override {initialize();view_->Redraw();}
    void resizeEvent(QResizeEvent*) override {
        if(view_.IsNull()) return;
        auto window=Handle(Aspect_NeutralWindow)::DownCast(view_->Window());window->SetSize(width(),height());view_->MustBeResized();
    }
    void mousePressEvent(QMouseEvent* e) override {previous_=e->pos();if(e->button()==Qt::LeftButton) view_->StartRotation(previous_.x(),previous_.y());}
    void mouseMoveEvent(QMouseEvent* e) override {
        if(e->buttons()&Qt::LeftButton) view_->Rotation(e->pos().x(),e->pos().y());
        if(e->buttons()&Qt::MiddleButton) view_->Pan(e->pos().x()-previous_.x(),previous_.y()-e->pos().y());previous_=e->pos();
    }
    void wheelEvent(QWheelEvent* e) override {view_->SetScale(view_->Scale()*(e->angleDelta().y()>0?1.15:1/1.15));view_->Redraw();}
private:
    void initialize() {
        if(!view_.IsNull()) return;
        Handle(Aspect_DisplayConnection) display=new Aspect_DisplayConnection;
        Handle(OpenGl_GraphicDriver) driver=new OpenGl_GraphicDriver(display);
        Handle(V3d_Viewer) viewer=new V3d_Viewer(driver);viewer->SetDefaultLights();viewer->SetLightOn();
        context_=new AIS_InteractiveContext(viewer);view_=viewer->CreateView();
        Handle(Aspect_NeutralWindow) window=new Aspect_NeutralWindow;
        window->SetNativeHandle(static_cast<Aspect_Drawable>(winId()));window->SetSize(width(),height());view_->SetWindow(window);
        if(!window->IsMapped()) window->Map();view_->SetBackgroundColor(Quantity_NOC_GRAY20);view_->SetProj(V3d_XposYnegZpos);view_->MustBeResized();
    }
    Handle(AIS_InteractiveContext) context_;Handle(V3d_View) view_;QPoint previous_;
};
}
int main(int argc,char** argv) {
    bool view=false,render=false;for(int i=1;i<argc;++i) {if(QByteArray(argv[i])=="--view") view=true;if(QByteArray(argv[i])=="--render") render=true;}
#if defined(Q_OS_LINUX)
    if((view||render)&&qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")&&!qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY")&&!qEnvironmentVariableIsEmpty("DISPLAY")) qputenv("QT_QPA_PLATFORM","xcb");
#endif
    std::unique_ptr<QCoreApplication> app;
    if(view||render) app=std::make_unique<QApplication>(argc,argv);else app=std::make_unique<QCoreApplication>(argc,argv);
    QCoreApplication::setApplicationName("OpenDesignerDebugModel");
    QCommandLineParser parser;parser.setApplicationDescription("Read-only B-Rep debug inspector; no document is opened or modified.");parser.addHelpOption();
    parser.addPositionalArgument("archive","Debug ZIP exported by OpenDesigner.");
    parser.addOption({"view","Show an isolated read-only OCCT viewer (stored mesh / fresh mesh / exact wireframe)."});
    parser.addOption({"render","Render stored/fresh/wireframe PNGs (requires --output and an OpenGL display)."});
    parser.addOption({"output","Write report.json and fresh B-Reps to a NEW directory.","directory"});
    parser.addOption({"probe","Classify a world-space material sample: partId,x,y,z. May be repeated.","sample"});parser.process(*app);
    if(parser.positionalArguments().size()!=1) parser.showHelp(2);
    try {
        if(render&&!parser.isSet("output")) throw std::runtime_error("--render requires --output.");
        DebugModel::Snapshot snapshot;QString error;
        if(!DebugModel::read(parser.positionalArguments().front(),snapshot,error)) throw std::runtime_error(error.toStdString());
        QDir output;
        if(parser.isSet("output")) {
            const auto path=parser.value("output");if(QFileInfo::exists(path)||!QDir().mkpath(path)) throw std::runtime_error("Output must be a new directory.");output=QDir(path);
        }
        QJsonArray objects,probes;
        for(size_t i=0;i<snapshot.objects.size();++i) {
            const auto& object=snapshot.objects[i];auto info=object.metadata;
            info["storedMesh"]=DebugModel::meshDiagnostics(object.shape);
            try {
                const auto fresh=DebugModel::remesh(object.shape);
                info["freshMesh"]=DebugModel::meshDiagnostics(fresh);
                if(info["diagnostics"].toObject()["solidValid"].toBool()) {
                    const bool equal=DebugModel::equivalent(object.shape,fresh);info["remeshPreservesMaterial"]=equal;
                    if(!equal) info["remeshError"]="Remeshing failed material equivalence check; do not use this fresh result.";
                } else info["materialComparisonUnavailable"]="Invalid or non-solid diagnostic input.";
                if(parser.isSet("output")) {
                    const auto path=output.filePath(QString("fresh-%1.brep").arg(i));
                    std::ostringstream stream;BRepTools::Write(fresh,stream);QSaveFile file(path);
                    if(!file.open(QIODevice::WriteOnly)||file.write(QByteArray::fromStdString(stream.str()))<0||!file.commit()) throw std::runtime_error("Cannot write fresh B-Rep.");
                }
            } catch(const Standard_Failure& e) {info["remeshError"]=e.GetMessageString();}
              catch(const std::exception& e) {info["remeshError"]=e.what();}
            objects.append(info);
        }
        for(const auto& sample:parser.values("probe")) {
            const auto values=sample.split(',');if(values.size()!=4) throw std::runtime_error("Probe needs partId,x,y,z.");
            bool x=false,y=false,z=false;const gp_Pnt world(values[1].toDouble(&x),values[2].toDouble(&y),values[3].toDouble(&z));
            if(!x||!y||!z||!std::isfinite(world.X())||!std::isfinite(world.Y())||!std::isfinite(world.Z())) throw std::runtime_error("Invalid probe coordinates.");
            bool found=false;
            for(const auto& object:snapshot.objects) if(object.metadata["role"]=="document-part"&&object.metadata["partId"]==values[0]) {
                found=true;BRepClass3d_SolidClassifier classifier(object.shape,world.Transformed(placement(object.metadata).Inverted()),1.e-7);
                const auto state=classifier.State();probes.append(QJsonObject{{"sample",sample},{"classification",state==TopAbs_IN?"IN":state==TopAbs_OUT?"OUT":state==TopAbs_ON?"ON":"UNKNOWN"}});
            }
            if(!found) throw std::runtime_error("Probe part ID not found.");
        }
        auto report=snapshot.context;report["objects"]=objects;report["worldSpaceProbes"]=probes;
        report["interpretation"]="IN at a known unwanted point means real solid material. Compare stored/fresh meshes and exact wireframe. A valid B-Rep or clean mesh does not establish intended modeling semantics.";
        const auto bytes=QJsonDocument(report).toJson();QTextStream(stdout)<<bytes;
        if(parser.isSet("output")) {QSaveFile file(output.filePath("report.json"));if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()||!file.commit()) throw std::runtime_error("Cannot save report.");}
        if(view||render) {
            QWidget window;window.setWindowTitle("OpenDesigner — Debug Model Inspector (read-only)");QVBoxLayout layout(&window);
            QComboBox parts,modes;QLabel help("Left drag: rotate · Middle drag: pan · Wheel: zoom. Stored mesh never auto-remeshes. Missing meshes can appear blank.");
            int initial=-1;
            for(size_t i=0;i<snapshot.objects.size();++i) {
                const auto& object=snapshot.objects[i];
                parts.addItem(object.metadata["partId"].toString()+" · "+object.metadata["name"].toString()+" · "+object.metadata["role"].toString()
                    +(object.metadata["visible"].toBool()?QString():" [hidden]")+(object.metadata["active"].toBool()?QString():" [inactive]"));
                if(initial<0&&object.metadata["role"]=="document-part"&&object.metadata["active"].toBool()&&object.metadata["visible"].toBool()) initial=i;
            }
            if(initial>=0) parts.setCurrentIndex(initial);
            modes.addItems({"Stored AIS mesh","Fresh triangulation","Exact B-Rep wireframe"});Canvas canvas;
            layout.addWidget(&parts);layout.addWidget(&modes);layout.addWidget(&help);layout.addWidget(&canvas);
            int previous=-1;
            const auto refresh=[&] {if(parts.currentIndex()<0) return;try {canvas.showObject(snapshot.objects[parts.currentIndex()],modes.currentIndex(),previous!=parts.currentIndex());previous=parts.currentIndex();}
                catch(const Standard_Failure& e) {help.setText(QString::fromUtf8(e.GetMessageString()));}catch(const std::exception& e) {help.setText(QString::fromUtf8(e.what()));}};
            QObject::connect(&parts,&QComboBox::currentIndexChanged,&window,refresh);QObject::connect(&modes,&QComboBox::currentIndexChanged,&window,refresh);
            window.resize(1000,800);window.show();app->processEvents();refresh();
            if(render) {
                const char* names[]={"stored","fresh","wire"};
                for(size_t i=0;i<snapshot.objects.size();++i) for(int mode=0;mode<3;++mode) {
                    // Fit to the exact wireframe first; each representation then
                    // uses the same camera, independent of corrupt mesh bounds.
                    if(mode==0) canvas.showObject(snapshot.objects[i],2,true);
                    canvas.showObject(snapshot.objects[i],mode,false);
                    canvas.saveImage(output.filePath(QString("%1-%2.png").arg(names[mode]).arg(i)));
                }
            }
            if(view) return app->exec();
        }
        return 0;
    } catch(const Standard_Failure& e) {QTextStream(stderr)<<e.GetMessageString()<<'\n';}
      catch(const std::exception& e) {QTextStream(stderr)<<e.what()<<'\n';}
    return 1;
}
