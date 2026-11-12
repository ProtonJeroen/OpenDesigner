#pragma once
#include <QString>
#include <QJsonObject>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Edge.hxx>
#include <gp_Pln.hxx>
#include <gp_Cylinder.hxx>
#include <gp_Cone.hxx>
#include <optional>
#include <gp_Ax1.hxx>
#include <array>
#include <vector>

// A small reconstruction recipe, not an authoritative feature tree. Supports are
// geometric planes, cylinders or cones (one curved support per transition, which
// must then meet the plane in one complete circle). Face handles below are an
// ephemeral certified cache.
namespace EdgeBlendFeatures {
enum class Kind { Fillet, Chamfer };
struct Feature {
    QString id;
    Kind kind=Kind::Fillet;
    double size=0;
    std::array<gp_Pln,2> supports;
    std::array<std::optional<gp_Cylinder>,2> cylinders;
    std::array<std::optional<gp_Cone>,2> cones;
    std::vector<TopoDS_Face> faces;
};
struct State {
    TopoDS_Shape sharp;
    std::vector<Feature> features;
    TopoDS_Shape certified;
};
// True when one support is curved (cylinder/cone): a circular transition.
bool circular(const Feature& feature);
bool resolve(const TopoDS_Shape& current,State& state,QString& error);
int selected(const State& state,const TopoDS_Face& face);
bool create(const TopoDS_Shape& current,const State& prior,const TopoDS_Edge& edge,
            Kind kind,double size,State& after,TopoDS_Shape& result,QString& error);
bool edit(const TopoDS_Shape& current,const State& prior,const QString& id,double size,
          State& after,TopoDS_Shape& result,QString& error);
bool remove(const TopoDS_Shape& current,const State& prior,const std::vector<TopoDS_Face>& faces,
            State& after,TopoDS_Shape& result,QString& error);
bool move(const TopoDS_Shape& current,const State& prior,const TopoDS_Face& face,double distance,
          bool keep,State& after,TopoDS_Shape& result,QString& error);
bool rotate(const TopoDS_Shape& current,const State& prior,const TopoDS_Face& face,
            const gp_Ax1& axis,double degrees,bool keep,State& after,TopoDS_Shape& result,QString& error);
QJsonObject encode(const State& state);
State decode(const QJsonObject& object); // Untrusted; resolve must certify against current.
}
