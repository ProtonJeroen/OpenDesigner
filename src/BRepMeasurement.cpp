#include "BRepTiming.h"
#include "BRepMeasurement.h"
#include "BRepMeasurementPrivate.h"
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Curve2d.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_NurbsConvert.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <Geom_BSplineCurve.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <Adaptor3d_Curve.hxx>
#include <Adaptor3d_Surface.hxx>
#include <Precision.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <Standard_Failure.hxx>
#include <cmath>

namespace BRepMeasurement {
namespace {
constexpr double integrationTolerance=1.e-12;
bool splineCurve(GeomAbs_CurveType type) {
    return type==GeomAbs_BSplineCurve||type==GeomAbs_BezierCurve;
}
bool splineSurface(const Adaptor3d_Surface& surface) {
    const auto type=surface.GetType();
    if(type==GeomAbs_BSplineSurface||type==GeomAbs_BezierSurface||type==GeomAbs_OtherSurface) return true;
    if(type==GeomAbs_SurfaceOfExtrusion||type==GeomAbs_SurfaceOfRevolution) {
        const auto basis=surface.BasisCurve();return basis.IsNull()||splineCurve(basis->GetType());
    }
    if(type==GeomAbs_OffsetSurface) {
        const auto basis=surface.BasisSurface();return basis.IsNull()||splineSurface(*basis);
    }
    return false;
}
bool needsAdaptive(const TopoDS_Shape& shape) {
    for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next()) {
        const auto face=TopoDS::Face(it.Current());
        if(splineSurface(BRepAdaptor_Surface(face))) return true;
        // A planar cap can have spline trims even though its carrier is analytic.
        for(TopExp_Explorer e(face,TopAbs_EDGE);e.More();e.Next()) {
            const auto edge=TopoDS::Edge(e.Current());double first=0,last=0;
            if(!BRep_Tool::Curve(edge,first,last).IsNull()&&splineCurve(BRepAdaptor_Curve(edge).GetType())) return true;
            if(!BRep_Tool::CurveOnSurface(edge,face,first,last).IsNull()
               &&splineCurve(BRepAdaptor_Curve2d(edge,face).GetType())) return true;
        }
    }
    return false;
}
bool hasSplineSweep(const TopoDS_Shape& shape) {
    for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next()) {
        const BRepAdaptor_Surface surface(TopoDS::Face(it.Current()));
        if(surface.GetType()!=GeomAbs_SurfaceOfExtrusion&&surface.GetType()!=GeomAbs_SurfaceOfRevolution) continue;
        const auto basis=surface.BasisCurve();if(!basis.IsNull()&&splineCurve(basis->GetType())) return true;
    }
    return false;
}
// Measurement-only choice. Multi-span exact cubic spline extrusion carriers
// hide their spans from GK's B-spline-face span option. Keep analytic, generic
// tensor-product and single-span paths unchanged; this enables no edit policy.
bool preferKnotAwareVolume(const TopoDS_Shape& shape) {
    for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next()) {
        const BRepAdaptor_Surface surface(TopoDS::Face(it.Current()));
        if(surface.GetType()!=GeomAbs_SurfaceOfExtrusion)continue;
        const auto basis=surface.BasisCurve();
        if(basis.IsNull()||basis->GetType()!=GeomAbs_BSplineCurve)continue;
        const auto curve=basis->BSpline();
        if(curve->IsRational()||curve->IsPeriodic()||curve->Degree()>3)continue;
        for(int k=2;k<curve->NbKnots();++k)
            if(curve->Knot(k)>surface.FirstUParameter()&&curve->Knot(k)<surface.LastUParameter())return true;
    }
    return false;
}
void boundedFaces(const TopoDS_Shape& shape) {
    for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next()) {
        double u0,u1,v0,v1;BRepTools::UVBounds(TopoDS::Face(it.Current()),u0,u1,v0,v1);
        for(double bound:{u0,u1,v0,v1})
            if(!std::isfinite(bound)||Precision::IsInfinite(bound))
                throw Standard_Failure("Numerical properties require finite trimmed surface domains.");
        if(u1<=u0||v1<=v0) throw Standard_Failure("Numerical properties require nondegenerate trimmed surface domains.");
    }
}
bool knotAwareVolume(const TopoDS_Shape& isolated,Result& result,QString& error,bool direct) {
    try {
        BRepTiming::Event conversionEvent("Integrate.NurbsConvert",isolated);
        BRepBuilderAPI_NurbsConvert spans(isolated,true);
        conversionEvent.finish(spans.IsDone()?spans.Shape():TopoDS_Shape{});
        if(!spans.IsDone()||!BRepCheck_Analyzer(spans.Shape()).IsValid())
            throw Standard_Failure("OCCT could not expose exact spline spans for integration.");
        boundedFaces(spans.Shape());
        result.properties=GProp_GProps();
        BRepTiming::Event integrationEvent(direct?"Integrate.Volume.GK.Direct":"Integrate.Volume.GK.Retry",spans.Shape());
        result.relativeError=BRepGProp::VolumePropertiesGK(spans.Shape(),result.properties,integrationTolerance,false,true,true);
        integrationEvent.finish({},result.properties.Mass());
        BRepTiming::Event estimate(direct?"Estimate.Volume.GK.Direct":"Estimate.Volume.GK.Retry",spans.Shape());
        estimate.finish({},result.relativeError);
        if(!std::isfinite(result.relativeError)||result.relativeError<0||result.relativeError>integrationTolerance)
            throw Standard_Failure("Knot-aware volume integration could not establish 1e-12 precision.");
        if(!std::isfinite(result.properties.Mass()))throw Standard_Failure("Nonfinite knot-aware volume.");
        if(result.properties.Mass()!=0) {
            const auto center=result.properties.CentreOfMass();
            if(!std::isfinite(center.X())||!std::isfinite(center.Y())||!std::isfinite(center.Z()))
                throw Standard_Failure("Nonfinite knot-aware volume centroid.");
        }
        error.clear();return true;
    } catch(const Standard_Failure& failure) {result.properties=GProp_GProps();error=QString::fromUtf8(failure.GetMessageString());return false;}
}
enum class InputValidity { Required, CheckedLocally };
bool measure(const TopoDS_Shape& shape,bool isVolume,Integration policy,Result& result,QString& error,
             InputValidity validity=InputValidity::Required) {
    BRepTiming::Scope timingScope("BRepMeasurement.measure");
    BRepTiming::Event measurementEvent(isVolume?"Measure.Volume":"Measure.Area",shape);
    result={};error.clear();
    try {
        if(validity==InputValidity::Required) {
            BRepTiming::Event validityEvent("Validate.BRep",shape);
            const bool valid=!shape.IsNull()&&BRepCheck_Analyzer(shape).IsValid();validityEvent.finish({},valid?1:0);
            if(!valid) throw Standard_Failure("Numerical properties require a non-null valid B-Rep.");
        } else {
            // Only the two private lexical handoffs reach this branch. The
            // identical default-options predicate already passed on this exact
            // argument; no copies, normalization or mutation occurred since.
            BRepTiming::Event reused("Validate.BRep.Reused",shape);reused.finish({},1);
        }
        TopoDS_Shape material=shape;
        if(isVolume) {
            // Adding a child to a compound changes its Free flag. Never attach
            // caller-owned topology to our temporary material container.
            BRepBuilderAPI_Copy isolated;
            if(shape.ShapeType()!=TopAbs_SOLID) {BRepTiming::Event copyMaterial("Copy",shape);isolated.Perform(shape,true,false);
                copyMaterial.finish(isolated.Shape());BRepTiming::copyOrigin(shape,isolated);material=isolated.Shape();}
            BRep_Builder builder;TopoDS_Compound solids;builder.MakeCompound(solids);
            for(TopExp_Explorer it(material,TopAbs_SOLID);it.More();it.Next()) {
                for(TopExp_Explorer shell(it.Current(),TopAbs_SHELL);shell.More();shell.Next())
                    if(!BRep_Tool::IsClosed(shell.Current())) throw Standard_Failure("Volume properties require closed solid shells.");
                if(shape.ShapeType()!=TopAbs_SOLID) builder.Add(solids,it.Current());
            }
            // Common may contain face/edge contacts alongside its material.
            // Those contacts must never contribute spurious signed volume.
            if(shape.ShapeType()!=TopAbs_SOLID) material=solids;
        }
        Result candidate;candidate.adaptive=policy==Integration::Adaptive||needsAdaptive(material);
        BRepTiming::Scope integrationScope(candidate.adaptive?"Measurement.adaptive":"Measurement.analytic");
        if(candidate.adaptive) {
            boundedFaces(material);
            BRepTiming::Event copyEvent1("Copy",material);
        BRepBuilderAPI_Copy isolated(material,true,false);
        copyEvent1.finish(isolated.Shape());BRepTiming::copyOrigin(material,isolated);
            bool integrated=false;
            if(isVolume&&preferKnotAwareVolume(material)) {
                QString directError;
                integrated=knotAwareVolume(isolated.Shape(),candidate,directError,true);
                if(!integrated) {
                    // A failed direct attempt is never an empty material set or
                    // a successful certificate. Preserve the complete old path.
                    BRepTiming::Scope fallback("Measurement.knotAwareFallback");
                    candidate.properties=GProp_GProps();candidate.relativeError=0;
                }
            }
            if(!integrated) {
                BRepTiming::Event integrationEvent(isVolume?"Integrate.Volume.GK":"Integrate.Area.Adaptive",material);
                candidate.relativeError=isVolume
                    ?BRepGProp::VolumePropertiesGK(isolated.Shape(),candidate.properties,integrationTolerance,false,true,true)
                    :BRepGProp::SurfaceProperties(isolated.Shape(),candidate.properties,integrationTolerance);
                integrationEvent.finish({},candidate.properties.Mass());
                BRepTiming::Event estimate(isVolume?"Estimate.Volume.GK":"Estimate.Area.Adaptive",material);
                estimate.finish({},candidate.relativeError);
                if(isVolume&&std::isfinite(candidate.relativeError)&&candidate.relativeError>integrationTolerance&&hasSplineSweep(material)) {
                    // Original retry and refusal policy, including non-selected
                    // representations. No converted shape escapes measurement.
                    QString retryError;
                    if(!knotAwareVolume(isolated.Shape(),candidate,retryError,false))
                        throw Standard_Failure(retryError.toUtf8().constData());
                }
            }
            if(!std::isfinite(candidate.relativeError)||candidate.relativeError<0||candidate.relativeError>integrationTolerance)
                throw Standard_Failure(QString("OCCT could not establish the requested 1e-12 property integration precision (estimated relative error %1).")
                    .arg(candidate.relativeError,0,'g',17).toUtf8().constData());
        } else if(isVolume) {
            BRepTiming::Event analyticEvent("Integrate.Volume.Analytic",material);
            BRepGProp::VolumeProperties(material,candidate.properties);analyticEvent.finish({},candidate.properties.Mass());
        } else {
            BRepTiming::Event analyticEvent("Integrate.Area.Analytic",material);
            BRepGProp::SurfaceProperties(material,candidate.properties);analyticEvent.finish({},candidate.properties.Mass());
        }
        if(!std::isfinite(candidate.properties.Mass())) throw Standard_Failure("OCCT produced a nonfinite geometric property.");
        if(candidate.properties.Mass()!=0) {
            const auto center=candidate.properties.CentreOfMass();
            if(!std::isfinite(center.X())||!std::isfinite(center.Y())||!std::isfinite(center.Z()))
                throw Standard_Failure("OCCT produced a nonfinite geometric centroid.");
        }
        measurementEvent.finish({},candidate.properties.Mass(),int(policy)*2+(candidate.adaptive?1:0));
        result=candidate;return true;
    } catch(const Standard_Failure& failure) {error=QString::fromUtf8(failure.GetMessageString());return false;}
}
}
bool detail::ValidatedInputVolume::compute(Result& result,QString& error) const {
    return measure(input,true,Integration::Automatic,result,error,InputValidity::CheckedLocally);
}
bool volume(const TopoDS_Shape& shape,Result& result,QString& error) {
    return measure(shape,true,Integration::Automatic,result,error);
}
bool area(const TopoDS_Shape& shape,Result& result,QString& error,Integration policy) {
    return measure(shape,false,policy,result,error);
}
GProp_GProps volumeProperties(const TopoDS_Shape& shape) {
    Result result;QString error;if(!volume(shape,result,error)) throw Standard_Failure(error.toUtf8().constData());return result.properties;
}
GProp_GProps areaProperties(const TopoDS_Shape& shape,Integration policy) {
    Result result;QString error;if(!area(shape,result,error,policy)) throw Standard_Failure(error.toUtf8().constData());return result.properties;
}
}
