#include "SketchGeometry.h"
#include <Geom2dAPI_Interpolate.hxx>
#include <TColgp_HArray1OfPnt2d.hxx>
#include <TColgp_Array1OfVec2d.hxx>
#include <TColgp_Array1OfPnt2d.hxx>
#include <TColStd_Array1OfReal.hxx>
#include <TColStd_Array1OfInteger.hxx>
#include <TColStd_HArray1OfBoolean.hxx>
#include <Standard_Failure.hxx>
#include <cmath>
#include <stdexcept>

namespace Sketch {
Handle(Geom2d_BSplineCurve) bsplineCurve(const BSpline& s) {
    if(s.degree<1 || s.degree>25 || s.poles.size()<2 || s.poles.size()>10000 || s.knots.size()<2 || s.knots.size()>10002
       || s.weights.size()!=s.poles.size() || s.multiplicities.size()!=s.knots.size()
       || !std::isfinite(s.first) || !std::isfinite(s.last) || s.last<=s.first)
        throw std::invalid_argument("Invalid exact B-spline coefficient counts, degree or interval.");
    TColgp_Array1OfPnt2d poles(1,int(s.poles.size()));TColStd_Array1OfReal weights(1,int(s.poles.size())),knots(1,int(s.knots.size()));
    TColStd_Array1OfInteger multiplicities(1,int(s.knots.size()));
    for(int i=1;i<=poles.Length();++i) {
        const auto p=s.poles[size_t(i-1)];const double w=s.weights[size_t(i-1)];
        if(!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(w) || w<=0) throw std::invalid_argument("Invalid B-spline pole/weight.");
        poles.SetValue(i,gp_Pnt2d(p.x,p.y));weights.SetValue(i,w);
    }
    for(int i=1;i<=knots.Length();++i) {
        const auto index=size_t(i-1);if(!std::isfinite(s.knots[index]) || (index && s.knots[index]<=s.knots[index-1]) || s.multiplicities[index]<1 || s.multiplicities[index]>s.degree+1)
            throw std::invalid_argument("Invalid B-spline knot/multiplicity.");
        knots.SetValue(i,s.knots[index]);multiplicities.SetValue(i,s.multiplicities[index]);
    }
    Handle(Geom2d_BSplineCurve) c=new Geom2d_BSplineCurve(poles,weights,knots,multiplicities,s.degree,s.periodic);
    if(s.first<c->FirstParameter() || s.last>c->LastParameter()) throw std::invalid_argument("B-spline trim is outside the knot domain.");
    return c;
}
bool validSpline(const Spline& s,QString& error) {
    const auto finite=[](Point p){return std::isfinite(p.x) && std::isfinite(p.y);};
    if(s.points.size()<(s.closed?3:2) || s.points.size()>10000) {error="A spline needs 2–10000 points (at least 3 when closed).";return false;}
    if(s.closed && (s.startTangent || s.endTangent)) {error="Periodic splines derive the seam tangent; end tangents require an open spline.";return false;}
    for(const auto& t:{s.startTangent,s.endTangent}) if(t && (!finite(*t) || std::hypot(t->x,t->y)<=1.e-7)) {error="Spline tangents must be finite nonzero vectors.";return false;}
    for(size_t i=0;i<s.points.size();++i) {
        if(!finite(s.points[i])) {error="Spline points must be finite.";return false;}
        if(i && std::hypot(s.points[i].x-s.points[i-1].x,s.points[i].y-s.points[i-1].y)<=1.e-7) {error="Consecutive spline points must be separated by more than 1e-7 mm.";return false;}
    }
    if(s.closed && std::hypot(s.points.front().x-s.points.back().x,s.points.front().y-s.points.back().y)<=1.e-7) {error="Closed splines omit the repeated first point.";return false;}
    error.clear();return true;
}
Handle(Geom2d_BSplineCurve) splineCurve(const Spline& s) {
    QString error;if(!validSpline(s,error)) throw std::invalid_argument(error.toStdString());
    Handle(TColgp_HArray1OfPnt2d) points=new TColgp_HArray1OfPnt2d(1,int(s.points.size()));
    for(int i=1;i<=points->Length();++i) points->SetValue(i,gp_Pnt2d(s.points[size_t(i-1)].x,s.points[size_t(i-1)].y));
    Geom2dAPI_Interpolate interpolation(points,s.closed,1.e-7);
    if(s.startTangent || s.endTangent) {
        TColgp_Array1OfVec2d tangents(1,points->Length());
        Handle(TColStd_HArray1OfBoolean) flags=new TColStd_HArray1OfBoolean(1,points->Length());flags->Init(false);
        if(s.startTangent) {tangents.SetValue(1,gp_Vec2d(s.startTangent->x,s.startTangent->y));flags->SetValue(1,true);}
        if(s.endTangent) {tangents.SetValue(points->Length(),gp_Vec2d(s.endTangent->x,s.endTangent->y));flags->SetValue(points->Length(),true);}
        interpolation.Load(tangents,flags,false);
    }
    interpolation.Perform();
    if(!interpolation.IsDone()) throw std::runtime_error("Spline interpolation failed; no geometry changed.");
    return interpolation.Curve();
}
}
