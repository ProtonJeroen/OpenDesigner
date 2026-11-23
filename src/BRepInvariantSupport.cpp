#include "BRepTiming.h"
#include "BRepTopology.h"
#include "BRepCertification.h"
#include "BRepMeasurement.h"
#include <BRepAdaptor_Curve2d.hxx>
#include <BRepTools.hxx>
#include <GeomConvert_BSplineCurveToBezierCurve.hxx>
#include <Geom_BezierCurve.hxx>
#include <Adaptor3d_Curve.hxx>
#include <Precision.hxx>
#include <Standard_Failure.hxx>
#include <algorithm>
#include <cmath>
#include <limits>

namespace BRepReconstruction {
namespace {
bool finite(double x) {return std::isfinite(x)&&!Precision::IsInfinite(x);}
bool parameterEqual(double a,double b) {
    return std::abs(a-b)<=64*std::numeric_limits<double>::epsilon()*std::max({1.,std::abs(a),std::abs(b)});
}
struct Segment {TopoDS_Edge edge;gp_Pnt2d first,last;bool constantU;};
// Bounded sufficient proof, never a sampled curve-intersection test. A Bezier
// curve and its derivative lie in their control hulls. Locally positive
// derivative projection proves regularity/injectivity. Pairwise separated hulls
// exclude intersections; adjacent intervals may share only their endpoint.
struct BezierPiece { std::vector<gp_Pnt> poles; };
struct CurveProof {
    unsigned work=0;
    static constexpr unsigned budget=100000;
    static constexpr int depthLimit=18;
    double guard=Precision::Confusion();
    std::pair<BezierPiece,BezierPiece> split(const BezierPiece& p) const {
        auto row=p.poles;BezierPiece a,b;a.poles.push_back(row.front());b.poles.push_back(row.back());
        for(size_t n=row.size()-1;n>0;--n) {
            for(size_t i=0;i<n;++i) row[i]=gp_Pnt((row[i].XYZ()+row[i+1].XYZ())*.5);
            a.poles.push_back(row.front());b.poles.push_back(row[n-1]);
        }
        std::reverse(b.poles.begin(),b.poles.end());return {a,b};
    }
    bool positive(const BezierPiece& p,const gp_Dir& axis) const {
        for(size_t i=1;i<p.poles.size();++i) {
            const double projection=gp_Vec(p.poles[i-1],p.poles[i]).Dot(gp_Vec(axis));
            if(!finite(projection)||projection<=guard) return false;
        }
        return true;
    }
    bool monotone(const BezierPiece& p) const {
        gp_Vec chord(p.poles.front(),p.poles.back());
        return finite(chord.Magnitude())&&chord.Magnitude()>guard&&positive(p,gp_Dir(chord));
    }
    bool regularPieces(const BezierPiece& p,int depth,std::vector<BezierPiece>& leaves) {
        if(++work>budget||leaves.size()>=256) return false;
        if(monotone(p)) {leaves.push_back(p);return true;}
        if(depth==depthLimit) return false;
        auto [a,b]=split(p);return regularPieces(a,depth+1,leaves)&&regularPieces(b,depth+1,leaves);
    }
    bool separated(const BezierPiece& a,const BezierPiece& b) const {
        for(int axis=1;axis<=3;++axis) {
            double amin=INFINITY,amax=-INFINITY,bmin=INFINITY,bmax=-INFINITY;
            for(const auto& p:a.poles) {amin=std::min(amin,p.Coord(axis));amax=std::max(amax,p.Coord(axis));}
            for(const auto& p:b.poles) {bmin=std::min(bmin,p.Coord(axis));bmax=std::max(bmax,p.Coord(axis));}
            if(amax+2*guard<bmin||bmax+2*guard<amin) return true;
        }
        return false;
    }
    bool disjoint(const BezierPiece& a,const BezierPiece& b,bool adjacent,int depth) {
        if(++work>budget) return false;
        if(separated(a,b)) return true;
        if(adjacent) {
            gp_Vec chord(a.poles.front(),b.poles.back());
            if(finite(chord.Magnitude())&&chord.Magnitude()>guard&&positive(a,gp_Dir(chord))&&positive(b,gp_Dir(chord))) return true;
        }
        if(depth==depthLimit) return false;
        // Split both, preserving the one parameter-adjacent endpoint pair.
        auto [a0,a1]=split(a);auto [b0,b1]=split(b);
        return disjoint(a0,b0,false,depth+1)&&disjoint(a0,b1,false,depth+1)
            &&disjoint(a1,b0,adjacent,depth+1)&&disjoint(a1,b1,false,depth+1);
    }
    bool certify(const Handle(Geom_BSplineCurve)& curve) {
        BRepTiming::Scope timing("BRepInvariantSupport.bezierCertificate");
        double scale=1;
        for(int i=1;i<=curve->NbPoles();++i)for(int axis=1;axis<=3;++axis)
            scale=std::max(scale,std::abs(curve->Pole(i).Coord(axis)));
        // Strict margins dominate floating point roundoff for this bounded
        // conversion/subdivision workload. They tighten, never relax, acceptance.
        guard=std::max(guard,4096*std::numeric_limits<double>::epsilon()*scale);
        if(curve->NbKnots()>257||curve->NbPoles()>1024) return false;
        for(int i=2;i<=curve->NbKnots();++i) {
            const double a=curve->Knot(i-1),b=curve->Knot(i);
            if(!finite(a)||!finite(b)||b-a<=128*std::numeric_limits<double>::epsilon()*std::max({1.,std::abs(a),std::abs(b)}))return false;
        }
        GeomConvert_BSplineCurveToBezierCurve spans(curve);
        if(spans.NbArcs()!=curve->NbKnots()-1||spans.NbArcs()>256) return false;
        std::vector<BezierPiece> leaves;
        for(int i=1;i<=spans.NbArcs();++i) {
            BezierPiece p;auto arc=spans.Arc(i);
            if(arc.IsNull()||arc->IsRational()||arc->NbPoles()<2||arc->NbPoles()>4)return false;
            for(int j=1;j<=arc->NbPoles();++j) {
                const auto pole=arc->Pole(j);
                if(!finite(pole.X())||!finite(pole.Y())||!finite(pole.Z()))return false;
                p.poles.push_back(pole);
            }
            if(!regularPieces(p,0,leaves))return false;
        }
        for(size_t i=0;i<leaves.size();++i)for(size_t j=i+1;j<leaves.size();++j)
            if(!disjoint(leaves[i],leaves[j],j==i+1,0))return false;
        return true;
    }
};

}
bool SupportDescriptor::invariantSpline(InvariantSplineSupport& result,QString& error,const TopologyIndex* context) const {
    BRepTiming::Scope timingScope("BRepInvariantSupport.SupportDescriptor::invariantSpline");
    result={};error.clear();
    const auto fail=[&](const QString& message){result={};error=message;return false;};
    try {
        if(surface.GetType()==GeomAbs_BSplineSurface)
            return fail(QStringLiteral("A generic B-spline surface has no certified linear-extrusion proof; tensor-product equivalence is not implemented."));
        if(surface.GetType()!=GeomAbs_SurfaceOfExtrusion)
            return fail(QStringLiteral("Invariant spline support requires an exact surface of linear extrusion."));
        if(!BRepCertification::validBRep(face)||!BRepCertification::withinTolerance(face,1.e-6))
            return fail(QStringLiteral("Invariant support requires a valid Face within the existing 1e-6 topology tolerance ceiling."));
        if(surface.Trsf().IsNegative()||std::abs(surface.Trsf().ScaleFactor()-1)>1.e-12)
            return fail(QStringLiteral("Only positive rigid carrier locations are certified."));
        const auto curve=surface.BasisCurve();
        if(curve.IsNull()||curve->GetType()!=GeomAbs_BSplineCurve)
            return fail(QStringLiteral("The extrusion basis is not an exact B-spline curve."));
        const auto full=curve->BSpline();
        if(full.IsNull()||full->IsRational()||full->Degree()>3||full->IsPeriodic())
            return fail(QStringLiteral("Initial spline support requires a nonperiodic, nonrational B-spline of degree at most 3."));
        TopologyIndex own;
        if(!context) {if(!TopologyIndex::build(face,own,error)) return false;context=&own;}
        const auto* record=context->face(face);
        if(!record||!record->support.face.IsEqual(face))
            return fail(QStringLiteral("The current oriented Face is not in the supplied topology context."));
        if(record->wires.size()!=1||!record->wires.front().outer||record->wires.front().edges.size()!=4)
            return fail(QStringLiteral("Initial spline support requires one four-edge rectangular UV wire; holes, seams and split boundaries are not certified."));
        double u0,u1,v0,v1;BRepTools::UVBounds(face,u0,u1,v0,v1);
        for(double x:{u0,u1,v0,v1}) if(!finite(x)) return fail(QStringLiteral("Invariant support needs finite trim domains."));
        std::vector<Segment> segments;std::vector<double> us,vs;
        TopTools_IndexedMapOfShape unique;
        for(const auto& edge:record->wires.front().edges) {
            if(!unique.Add(edge)||unique.Extent()!=int(segments.size()+1))
                return fail(QStringLiteral("Repeated or periodic edge uses are ambiguous for this bounded proof."));
            const auto* indexed=context->edge(edge);
            if(!indexed||indexed->faces.size()>2) return fail(QStringLiteral("Ambiguous or nonmanifold trim adjacency."));
            const BRepAdaptor_Curve2d trim(edge,face);
            if(trim.GetType()!=GeomAbs_Line||!finite(trim.FirstParameter())||!finite(trim.LastParameter())
               ||trim.LastParameter()<=trim.FirstParameter())
                return fail(QStringLiteral("Only finite exact isoparametric line trims are certified."));
            const auto direction=trim.Line().Direction();
            const bool constantU=direction.X()==0,constantV=direction.Y()==0;
            if(constantU==constantV) return fail(QStringLiteral("Oblique/non-isoparametric trimming is unsupported."));
            const auto first=trim.Value(trim.FirstParameter()),last=trim.Value(trim.LastParameter());
            for(double x:{first.X(),first.Y(),last.X(),last.Y()})
                if(!finite(x)) return fail(QStringLiteral("Nonfinite trim parameters."));
            segments.push_back({edge,first,last,constantU});
            (constantU?us:vs).push_back(constantU?first.X():first.Y());
        }
        if(us.size()!=2||vs.size()!=2) return fail(QStringLiteral("The trims do not determine one rectangular region."));
        std::sort(us.begin(),us.end());std::sort(vs.begin(),vs.end());
        if(us[1]<=us[0]||vs[1]<=vs[0]) return fail(QStringLiteral("Degenerate or duplicate trim boundaries."));
        InvariantSplineSupport proof;proof.face=face;proof.axis=surface.Direction();
        proof.firstU=us[0];proof.lastU=us[1];proof.firstV=vs[0];proof.lastV=vs[1];
        if(proof.firstU<curve->FirstParameter()&&!parameterEqual(proof.firstU,curve->FirstParameter()))
            return fail(QStringLiteral("Trim begins outside the current basis domain."));
        if(proof.lastU>curve->LastParameter()&&!parameterEqual(proof.lastU,curve->LastParameter()))
            return fail(QStringLiteral("Trim ends outside the current basis domain."));
        for(const auto& segment:segments) {
            const double lo=segment.constantU?vs[0]:us[0],hi=segment.constantU?vs[1]:us[1];
            const double a=segment.constantU?segment.first.Y():segment.first.X();
            const double b=segment.constantU?segment.last.Y():segment.last.X();
            if(!parameterEqual(std::min(a,b),lo)||!parameterEqual(std::max(a,b),hi))
                return fail(QStringLiteral("Partial, overlapping or inconsistent trim intervals are ambiguous."));
            const double constant=segment.constantU?segment.first.X():segment.first.Y();
            const bool first=parameterEqual(constant,segment.constantU?us[0]:vs[0]);
            InvariantSplineSupport::Trim boundary;
            boundary.boundary=segment.constantU?(first?InvariantSplineSupport::Boundary::FirstU:InvariantSplineSupport::Boundary::LastU)
                :(first?InvariantSplineSupport::Boundary::FirstV:InvariantSplineSupport::Boundary::LastV);
            boundary.edge=segment.edge;
            for(const auto& owner:context->edge(segment.edge)->faces) if(!owner.IsSame(face)) boundary.neighbors.push_back(owner);
            proof.trims.push_back(std::move(boundary));
        }
        proof.basis=Handle(Geom_BSplineCurve)::DownCast(full->Copy());
        proof.trimmedBasis=Handle(Geom_BSplineCurve)::DownCast(full->Copy());
        proof.trimmedBasis->Segment(proof.firstU,proof.lastU);
        for(int i=2;i<proof.trimmedBasis->NbKnots();++i)
            if(proof.trimmedBasis->Multiplicity(i)>proof.trimmedBasis->Degree())
                return fail(QStringLiteral("Discontinuous basis knot spans are not certified."));
        const auto origin=proof.trimmedBasis->Pole(1);
        proof.basisPlane=gp_Pln(origin,proof.axis);
        const gp_Vec chord(proof.trimmedBasis->StartPoint(),proof.trimmedBasis->EndPoint());
        if(chord.Magnitude()<=Precision::Confusion()) return fail(QStringLiteral("Closed/degenerate basis intervals are not certified."));
        const gp_Dir monotone(chord);
        bool curved=false,strictMonotone=true;
        for(int i=1;i<=proof.trimmedBasis->NbPoles();++i) {
            const auto pole=proof.trimmedBasis->Pole(i);
            if(!finite(pole.X())||!finite(pole.Y())||!finite(pole.Z())) return fail(QStringLiteral("Nonfinite basis geometry."));
            const double distance=proof.basisPlane.Distance(pole);
            if(gp_Vec(origin,pole).Crossed(gp_Vec(monotone)).Magnitude()>Precision::Confusion()) curved=true;
            proof.planarityResidual=std::max(proof.planarityResidual,distance);
            if(distance>Precision::Confusion()) return fail(QStringLiteral("The trimmed control polygon does not prove a planar section perpendicular to the extrusion axis."));
            // Derivative control coefficients have positive denominators. Strict
            // projection of every pole difference proves a regular injective
            // section over all knot spans, without derivative sampling.
            if(i>1&&gp_Vec(proof.trimmedBasis->Pole(i-1),pole).Dot(gp_Vec(monotone))<=Precision::Confusion())
                strictMonotone=false;
        }
        if(!strictMonotone&&!CurveProof{}.certify(proof.trimmedBasis))
            return fail(QStringLiteral("The bounded Bezier regularity/self-intersection exclusion proof is inconclusive or exceeds its budget."));
        if(!curved) return fail(QStringLiteral("Collinear spline bases need an analytic-plane canonicalization proof, which is not implemented in this scoped classifier."));
        proof.firstSection=proof.basisPlane;proof.lastSection=proof.basisPlane;
        proof.firstSection.Translate(gp_Vec(proof.axis)*proof.firstV);
        proof.lastSection.Translate(gp_Vec(proof.axis)*proof.lastV);
        for(auto& trim:proof.trims) {
            if(trim.boundary!=InvariantSplineSupport::Boundary::FirstV&&trim.boundary!=InvariantSplineSupport::Boundary::LastV) continue;
            const auto& section=trim.boundary==InvariantSplineSupport::Boundary::FirstV?proof.firstSection:proof.lastSection;
            for(const auto& neighbor:trim.neighbors) {
                const BRepAdaptor_Surface cap(neighbor);
                if(cap.GetType()==GeomAbs_Plane&&cap.Plane().Axis().Direction().IsParallel(proof.axis,1.e-8)
                   &&cap.Plane().Distance(section.Location())<=1.e-6) trim.planarCaps.push_back(neighbor);
            }
        }
        BRepMeasurement::Result measured;
        if(!BRepMeasurement::area(face,measured,error)) return false;
        if(measured.properties.Mass()<=1.e-12) return fail(QStringLiteral("Degenerate support area."));
        proof.axialContinuation=true;result=std::move(proof);return true;
    } catch(const Standard_Failure& failure) {return fail(QString::fromUtf8(failure.GetMessageString()));}
}
}
