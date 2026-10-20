#include "BRepReconstruction.h"
#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRep_Tool.hxx>
#include <BOPAlgo_ArgumentAnalyzer.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <BRepTools_History.hxx>
#include <GProp_GProps.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <gp_Cylinder.hxx>
#include <gp_Cone.hxx>
#include <Standard_Failure.hxx>
#include <algorithm>
#include <cmath>

namespace {
int count(const TopoDS_Shape& shape,TopAbs_ShapeEnum type) {
    TopTools_IndexedMapOfShape map; TopExp::MapShapes(shape,type,map); return map.Extent();
}
bool sameSupport(const TopoDS_Face& original, const TopoDS_Face& result,
                 const gp_Pln* expected = nullptr) {
    const BRepAdaptor_Surface a(original), b(result);
    if(a.GetType()!=b.GetType()) return false;
    if(a.GetType()==GeomAbs_Plane) {
        const auto p=expected?*expected:a.Plane(), q=b.Plane();
        auto pn=p.Axis().Direction(), qn=q.Axis().Direction();
        if(original.Orientation()==TopAbs_REVERSED) pn.Reverse();
        if(result.Orientation()==TopAbs_REVERSED) qn.Reverse();
        return pn.IsEqual(qn,1.e-7) && p.Distance(q.Location())<1.e-5;
    }
    if(a.GetType()==GeomAbs_Cylinder) {
        const auto p=a.Cylinder(),q=b.Cylinder();
        return p.Axis().IsCoaxial(q.Axis(),1.e-7,1.e-5)
            && std::abs(p.Radius()-q.Radius())<1.e-5;
    }
    if(a.GetType()==GeomAbs_Cone) {
        const auto p=a.Cone(),q=b.Cone();
        return p.Axis().Direction().IsParallel(q.Axis().Direction(),1.e-7)
            && p.Apex().Distance(q.Apex())<1.e-5
            && std::abs(std::abs(p.SemiAngle())-std::abs(q.SemiAngle()))<1.e-7;
    }
    return false;
}
}
bool BRepReconstruction::validSolid(const TopoDS_Shape& shape,double& volume) {
    if(shape.IsNull() || count(shape,TopAbs_SOLID)!=1 || count(shape,TopAbs_SHELL)!=1
       || !BRepCheck_Analyzer(shape).IsValid()) return false;
    TopExp_Explorer solid(shape,TopAbs_SOLID),shell(shape,TopAbs_SHELL);
    if(!BRep_Tool::IsClosed(shell.Current())) return false;
    for(const auto type : {TopAbs_FACE,TopAbs_EDGE,TopAbs_VERTEX})
        if(count(shape,type)!=count(solid.Current(),type)) return false;
    GProp_GProps properties; BRepGProp::VolumeProperties(shape,properties);
    volume=properties.Mass();
    if(!std::isfinite(volume)||volume<=1.e-12) return false;
    BRepBuilderAPI_Copy isolated(shape,true,false);
    BOPAlgo_ArgumentAnalyzer check;
    check.SetShape1(isolated.Shape()); check.SelfInterMode()=true;
    check.StopOnFirstFaulty()=true; check.Perform();
    return !check.HasErrors()&&!check.HasFaulty();
}
bool BRepReconstruction::prepare(const TopoDS_Shape& source,const TopoDS_Face& target,
                                 Prepared& result,QString& error) {
    result={};
    error.clear();
    try {
        BRepBuilderAPI_Copy copy(source,true,false);
        const auto copiedTarget=TopoDS::Face(copy.ModifiedShape(target));
        ShapeUpgrade_UnifySameDomain unify(copy.Shape(),true,true,false);
        unify.SetSafeInputMode(true);
        // A selected patch must not silently expand into an adjacent coplanar
        // patch. Keep its boundary; other redundant Boolean seams may merge.
        for(TopExp_Explorer e(copiedTarget,TopAbs_EDGE);e.More();e.Next()) unify.KeepShape(e.Current());
        unify.Build();
        const auto candidate=unify.Shape();
        double normalizedVolume=0;
        if(!validSolid(candidate,normalizedVolume)) {
            error=QStringLiteral("B-Rep normalization failed.");return false;
        }
        GProp_GProps before,after;
        BRepGProp::VolumeProperties(source,before);
        BRepGProp::VolumeProperties(candidate,after);
        if(before.CentreOfMass().Distance(after.CentreOfMass())>1.e-6 || std::abs(before.Mass()-after.Mass())>1.e-7*std::max(1.0,std::abs(before.Mass()))) {
            error=QStringLiteral("B-Rep normalization changed the volume.");return false;
        }
        for(TopExp_Explorer f(source,TopAbs_FACE);f.More();f.Next()) {
            FaceMapping mapping{TopoDS::Face(f.Current()),{}};
            const auto copied=copy.ModifiedShape(f.Current());
            const auto& modified=unify.History()->Modified(copied);
            for(TopExp_Explorer c(candidate,TopAbs_FACE);c.More();c.Next()) {
                bool matches=c.Current().IsSame(copied);
                for(TopTools_ListIteratorOfListOfShape it(modified);it.More();it.Next())
                    matches=matches||c.Current().IsSame(it.Value());
                if(matches) mapping.modified.push_back(TopoDS::Face(c.Current()));
            }
            if(mapping.modified.empty()) {
                error=QStringLiteral("An original face was lost during B-Rep normalization.");return false;
            }
            if(f.Current().IsSame(target)) {
                if(mapping.modified.size()!=1) {
                    error=QStringLiteral("The selected face has an ambiguous reconstruction history.");return false;
                }
                result.target=mapping.modified.front();
            }
            result.history.push_back(mapping);
        }
        if(result.target.IsNull()) {error=QStringLiteral("Selected face was lost during normalization.");return false;}
        result.solid=candidate;
        return true;
    } catch(const Standard_Failure&) {
        error=QStringLiteral("Open CASCADE could not normalize this B-Rep.");return false;
    }
}
bool BRepReconstruction::supports(const TopoDS_Shape& source,const TopoDS_Shape& result,
        const TopoDS_Face& target,const gp_Pln& expected,QString& error) {
    // Bidirectional support coverage permits edge splits and coplanar merging,
    // but neither new transition surfaces nor disappearing analytic supports.
    for(int pass=0;pass<2;++pass) {
        const auto& from=pass==0?source:result;
        const auto& to=pass==0?result:source;
        for(TopExp_Explorer f(from,TopAbs_FACE);f.More();f.Next()) {
            bool found=false;
            for(TopExp_Explorer g(to,TopAbs_FACE);g.More();g.Next()) {
                const auto a=TopoDS::Face(pass==0?f.Current():g.Current());
                const auto b=TopoDS::Face(pass==0?g.Current():f.Current());
                if(sameSupport(a,b,a.IsSame(target)?&expected:nullptr)) {found=true;break;}
            }
            if(!found) {
                error=QStringLiteral("Reconstruction lost an original support or introduced an unintended transition surface.");
                return false;
            }
        }
    }
    return true;
}
