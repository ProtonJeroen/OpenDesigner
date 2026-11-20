#pragma once
// Isolated F2.9B test state. No production UI mode or acceptance entry point.
#include "BoxShadowValidator.h"
#include "BRepSweepConstruction.h"
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepTools.hxx>
#include <AIS_Shape.hxx>
#include <QString>
#include <cmath>
namespace BoxProvisionalExperiment {
using Clock=std::chrono::steady_clock;
inline double elapsed(Clock::time_point t) {return std::chrono::duration<double,std::milli>(Clock::now()-t).count();}
enum class Operation { MoveFace };
enum class Mode { FaceNormal };
enum class Certification { Uncertified };
struct State {
    QString partId;
    TopoDS_Shape source,candidate;
    TopoDS_Face selected;
    quint64 revision=0;
    double displacement=0;
    Operation operation=Operation::MoveFace;
    Mode mode=Mode::FaceNormal;
    const Certification certification=Certification::Uncertified;
    bool displayable=false;
    Handle(AIS_Shape) overlay;
    QString error;
    double preparationMs=0,rawMs=0,checksMs=0,renderMs=0,totalMs=0,fullOnOkMs=0;
};
// This gate is intentionally numerical and bounded to planar/line output. It
// does not measure volume, run SI or compare material; split walls are allowed.
inline bool displayGate(const TopoDS_Shape& shape,double displacement,QString& error) {
    namespace E=ExperimentalBoxShadow;namespace C=BRepCertification;
    const auto fail=[&](const char* text){error=QString::fromLatin1(text);return false;};
    if(shape.IsNull())return fail("null provisional construction");
    if(E::map(shape,TopAbs_SOLID).Extent()!=1||E::map(shape,TopAbs_SHELL).Extent()!=1
       ||E::map(shape,TopAbs_FACE).Extent()>64||E::map(shape,TopAbs_EDGE).Extent()>256
       ||E::map(shape,TopAbs_VERTEX).Extent()>256)return fail("provisional topology outside bounded scope");
    for(TopExp_Explorer v(shape,TopAbs_VERTEX);v.More();v.Next()) {
        auto p=BRep_Tool::Pnt(TopoDS::Vertex(v.Current()));for(int i=1;i<=3;++i)if(!std::isfinite(p.Coord(i)))return fail("nonfinite vertex");
    }
    for(TopExp_Explorer f(shape,TopAbs_FACE);f.More();f.Next()) {
        auto face=TopoDS::Face(f.Current());BRepAdaptor_Surface s(face);
        if(!face.Location().IsIdentity()||s.GetType()!=GeomAbs_Plane||E::map(face,TopAbs_WIRE).Extent()!=1)return fail("unsupported provisional Face");
        double u0,u1,v0,v1;BRepTools::UVBounds(face,u0,u1,v0,v1);
        for(double value:{u0,u1,v0,v1})if(!std::isfinite(value)||Precision::IsInfinite(value))return fail("unbounded trim");
        if(u0>=u1||v0>=v1)return fail("degenerate trim");
    }
    for(TopExp_Explorer e(shape,TopAbs_EDGE);e.More();e.Next()) {
        auto edge=TopoDS::Edge(e.Current());BRepAdaptor_Curve c(edge);
        if(!edge.Location().IsIdentity()||c.GetType()!=GeomAbs_Line||!std::isfinite(c.FirstParameter())
           ||!std::isfinite(c.LastParameter())||Precision::IsInfinite(c.FirstParameter())
           ||Precision::IsInfinite(c.LastParameter())||c.FirstParameter()>=c.LastParameter())return fail("unsupported Edge/trim");
    }
    if(!C::withinTolerance(shape,1.e-6)||!C::validBRep(shape)||!C::closedShells(shape)||!C::manifold(shape))return fail("unsafe provisional B-Rep");
    BRepClass3d_SolidClassifier classifier(shape);classifier.PerformInfinitePoint(Precision::Confusion());
    if(classifier.State()!=TopAbs_OUT)return fail("provisional orientation");
    Bnd_Box bounds;BRepBndLib::AddOptimal(shape,bounds,false,false);
    if(bounds.IsVoid()||bounds.IsOpen())return fail("unbounded candidate");
    double x,y,z,X,Y,Z;bounds.Get(x,y,z,X,Y,Z);
    if(x!=0||y!=0||z!=0||X!=40||Y!=30||Z!=20+displacement)return fail("provisional bounds/intent mismatch");
    return true;
}
inline bool construct(State& p,bool injectFailure=false) {
    const auto started=Clock::now();
    try {
        if(p.mode!=Mode::FaceNormal||!std::isfinite(p.displacement)||(p.displacement!=15&&p.displacement!=-5)) {
            p.error="Only +15/-5 axial top-cap requests are supported by this experiment.";return false;
        }
        auto phase=Clock::now();BRepBuilderAPI_Copy isolated(p.source,true,false);
        auto target=TopoDS::Face(isolated.ModifiedShape(p.selected));target.Orientation(p.selected.Orientation());
        ExperimentalBoxShadow::Box source;auto valid=ExperimentalBoxShadow::inspect(isolated.Shape(),source);
        if(valid.verdict!=ExperimentalBoxShadow::Verdict::Certified||!target.IsEqual(source.top)
           ||source.lo!=ExperimentalBoxShadow::Point{0,0,0}||source.hi!=ExperimentalBoxShadow::Point{40,30,20}) {
            p.error="Source/selection outside exact fixture contract";return false;
        }
        p.preparationMs=elapsed(phase);
        phase=Clock::now();const auto rawBegin=BRepTiming::details.size();
        auto region=BRepReconstruction::detail::sweepPatch(target,gp_Vec(0,0,p.displacement));
        if(injectFailure)throw Standard_Failure("injected failure after sweep construction");
        p.candidate=BRepReconstruction::detail::applySweep(isolated.Shape(),region,p.displacement);p.rawMs=elapsed(phase);
        unsigned booleans=0;
        for(size_t i=rawBegin;i<BRepTiming::details.size();++i) {
            const auto& kind=BRepTiming::details[i].kind;
            if(kind.rfind("Boolean.",0)==0)++booleans;
            if(kind=="Boolean.MaterialCommon"||kind=="Measure.Volume"||kind=="Measure.Area")throw std::runtime_error("certification leaked into raw construction");
        }
        if(booleans!=1)throw std::runtime_error("raw construction did not use exactly one production Fuse/Cut");
        phase=Clock::now();p.displayable=displayGate(p.candidate,p.displacement,p.error);p.checksMs=elapsed(phase);
        if(!p.displayable)p.candidate.Nullify();return p.displayable;
    } catch(const Standard_Failure& e){p.error=QString::fromUtf8(e.GetMessageString());}
      catch(const std::exception& e){p.error=QString::fromUtf8(e.what());}
    p.candidate.Nullify();p.displayable=false;p.totalMs=elapsed(started);return false;
}
}
