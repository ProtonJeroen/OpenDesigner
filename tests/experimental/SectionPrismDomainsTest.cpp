#include "SectionPrismDomains.h"
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <BRep_Builder.hxx>
#include <Geom_Plane.hxx>
#include <Geom2d_Line.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Shell.hxx>
#include <algorithm>
#include <iostream>
#include <sstream>
using namespace SectionExperiment;
namespace {
void require(bool x,const char* why){if(!x)throw std::runtime_error(why);}
std::string stamp(const TopoDS_Shape& s){std::ostringstream out;BRepTools::Write(s,out);return out.str();}
struct Fixture {TopoDS_Shape solid;TopoDS_Face top;};
TopoDS_Wire wire(const std::vector<gp_Pnt>& p){BRepBuilderAPI_MakePolygon w;for(auto q:p)w.Add(q);w.Close();return w.Wire();}
Fixture prism(const std::vector<gp_Pnt>& p,bool hole=false) {
    BRepBuilderAPI_MakeFace f(gp_Pln(gp_Pnt(),gp_Dir(0,0,1)),wire(p),true);
    if(hole){auto inner=wire({{1,1,0},{2,1,0},{2,2,0},{1,2,0}});inner.Reverse();f.Add(inner);}
    BRepPrimAPI_MakePrism sweep(f.Face(),gp_Vec(0,0,20),true,true);return {sweep.Shape(),TopoDS::Face(sweep.LastShape())};
}
Fixture clone(const Fixture& f){BRepBuilderAPI_Copy c(f.solid,true,false);auto top=TopoDS::Face(c.ModifiedShape(f.top));top.Orientation(f.top.Orientation());return {c.Shape(),top};}
TopoDS_Face wall(const Fixture& f){for(TopExp_Explorer it(f.solid,TopAbs_FACE);it.More();it.Next()){TopLoc_Location l;const auto p=Handle(Geom_Plane)::DownCast(BRep_Tool::Surface(TopoDS::Face(it.Current()),l));if(!p.IsNull()&&std::abs(p->Axis().Direction().Z())<.5)return TopoDS::Face(it.Current());}throw std::runtime_error("wall not found");}
double median(std::vector<double> x){std::sort(x.begin(),x.end());return x[x.size()/2];}
void positive(const char* name,const Fixture& f,size_t faces,size_t cells) {
    const auto before=stamp(f.solid);const PrismInput input(f.solid,f.top);std::vector<PrismDomainVerdict> samples;
    for(int trial=0;trial<3;++trial) {
        const auto e=producePrismDomains(input);const auto r=consumePrismDomains(input,e);
        if(r.nominalDomains!=Outcome::Certified||r.materialSides!=Outcome::Certified)std::cout<<"DOMAIN_FAILURE "<<name<<" proposal="<<e.reason<<" consumer="<<r.reason<<'\n';
        require(r.outcome==Outcome::Unresolved&&r.nominalDomains==Outcome::Certified&&r.materialSides==Outcome::Certified,"positive nominal domain/side verification failed");
        require(r.actualDomains==Outcome::Unresolved&&r.material==Outcome::Unresolved,"unsupported actual material interpretation certified");
        require(r.faces==faces&&r.cells==cells&&r.kernelChecks==faces+cells,"domain-cell/kernel consistency coverage");samples.push_back(r);
        if(trial==0) {
            unsigned refused=0;
            const auto corrupt=[&](const char* label,auto change){auto bad=e;change(bad);const auto v=consumePrismDomains(input,bad);std::cout<<"DOMAIN_CORRUPTION "<<name<<'/'<<label<<" reason="<<v.reason<<'\n';require(v.outcome!=Outcome::Certified&&!(v.nominalDomains==Outcome::Certified&&v.materialSides==Outcome::Certified),"corrupt domain/side evidence accepted");++refused;};
            corrupt("missing-domain-cell",[](auto& x){x.domains[x.boundary.top].cells.pop_back();});
            corrupt("wrong-domain-cell",[](auto& x){x.domains[x.boundary.top].cells[0].low+=1;});
            corrupt("missing-X-slab",[](auto& x){x.domains[x.boundary.top].breaks.pop_back();});
            corrupt("wrong-domain-area",[](auto& x){x.domains[x.boundary.top].area+=1;});
            corrupt("wrong-Face-orientation",[](auto& x){x.domains[x.boundary.top].faceOrientation=int(TopAbs_REVERSED);});
            corrupt("wrong-wire-orientation",[](auto& x){auto& o=x.domains[x.boundary.top].wireOrientation;o=o==int(TopAbs_FORWARD)?int(TopAbs_REVERSED):int(TopAbs_FORWARD);});
            corrupt("alternative-domain-flag",[](auto& x){x.domains[x.boundary.top].naturalRestriction=true;});
            corrupt("misclassified-interior-side",[](auto& x){x.sides[x.boundary.top].negativeSideIsInterior=false;});
            corrupt("swapped-inward-outward",[](auto& x){auto& s=x.sides[x.boundary.bottom];std::swap(s.inward,s.outward);});
            corrupt("reversed-side-normal",[](auto& x){auto& s=x.sides[x.boundary.walls[0].face];for(auto& q:s.normal)q=-q;});
            corrupt("wrong-material-role",[](auto& x){x.sides[x.boundary.top].role=FaceRole::Wall;});
            corrupt("missing-side-witness",[](auto& x){x.sides.pop_back();});
            auto fake=e;fake.proposalOutcome=Outcome::Rejected;for(auto& d:fake.domains)d.proposalOutcome=Outcome::Rejected;
            const auto independent=consumePrismDomains(input,fake);require(independent.nominalDomains==Outcome::Certified&&independent.materialSides==Outcome::Certified,"producer domain flags trusted");
            std::cout<<"DOMAIN_WITNESS_SUMMARY "<<name<<" refused="<<refused<<" status_flags_ignored=1\n";
        }
    }
    const auto m=[&](auto member){std::vector<double> values;for(const auto& r:samples)values.push_back(r.*member);return median(values);};
    std::cout<<"DOMAIN_MEDIAN "<<name<<" faces="<<faces<<" cells="<<cells
      <<" domain_checks_ms="<<m(&PrismDomainVerdict::faceDomainChecksMs)<<" material_side_ms="<<m(&PrismDomainVerdict::materialSideChecksMs)
      <<" repeated_binding_ms="<<m(&PrismDomainVerdict::repeatedBindingMs)<<" independent_added_ms="<<m(&PrismDomainVerdict::independentAddedMs)
      <<" baseline_ms="<<m(&PrismDomainVerdict::baselineMs)<<" total_ms="<<m(&PrismDomainVerdict::totalMs)<<'\n';
    require(stamp(f.solid)==before,"domain/side verification mutated source");
}
}
void runSectionPrismDomainTests() {
    const std::vector<gp_Pnt> rectangle={{0,0,0},{6,0,0},{6,4,0},{0,4,0}},polygon={{0,0,0},{6,0,0},{6,2,0},{2,2,0},{2,4,0},{0,4,0}};
    const auto box=prism(rectangle),nonconvex=prism(polygon);positive("rectangle",box,6,6);positive("nonconvex",nonconvex,8,10);
    gp_Trsf t;t.SetValues(1,0,0,16,0,0,-1,-8,0,1,0,32);TopLoc_Location l(t);positive("90-X-translated",{nonconvex.solid.Moved(l),TopoDS::Face(nonconvex.top.Moved(l))},8,10);
    unsigned negatives=0;
    const auto refuse=[&](const char* label,const Fixture& f){const auto before=stamp(f.solid);const PrismInput input(f.solid,f.top);const auto e=producePrismDomains(input);const auto r=consumePrismDomains(input,e);std::cout<<"DOMAIN_GEOMETRY_REFUSAL "<<label<<" outcome="<<int(r.outcome)<<" reason="<<r.reason<<'\n';require(r.outcome!=Outcome::Certified&&!(r.nominalDomains==Outcome::Certified&&r.materialSides==Outcome::Certified),"invalid/unsupported Face domain certified");require(stamp(f.solid)==before,"negative source mutated");++negatives;};
    BRep_Builder b;
    auto cap=clone(box);auto sh=TopoDS::Shell(TopExp_Explorer(cap.solid,TopAbs_SHELL).Current());sh.Free(true);b.Remove(sh,cap.top);cap.top=TopoDS::Face(cap.top.Reversed());b.Add(sh,cap.top);refuse("reversed-cap",cap);
    auto side=clone(box);sh=TopoDS::Shell(TopExp_Explorer(side.solid,TopAbs_SHELL).Current());sh.Free(true);auto w=wall(side);b.Remove(sh,w);b.Add(sh,w.Reversed());refuse("reversed-sidewall",side);
    auto outer=clone(box);auto ow=TopoDS::Wire(TopExp_Explorer(outer.top,TopAbs_WIRE).Current());outer.top.Free(true);b.Remove(outer.top,ow);b.Add(outer.top,ow.Reversed());refuse("incorrect-outer-wire-orientation",outer);
    auto pcurve=clone(box);auto e=TopoDS::Edge(TopExp_Explorer(pcurve.top,TopAbs_EDGE).Current());double first,last;auto line=Handle(Geom2d_Line)::DownCast(BRep_Tool::CurveOnSurface(e,pcurve.top,first,last));require(!line.IsNull(),"missing pcurve fixture");Handle(Geom2d_Line) reverse=new Geom2d_Line(line->Location(),line->Direction().Reversed());b.UpdateEdge(e,reverse,pcurve.top,BRep_Tool::Tolerance(e));refuse("inconsistent-pcurve-orientation",pcurve);
    auto incomplete=clone(box);ow=TopoDS::Wire(TopExp_Explorer(incomplete.top,TopAbs_WIRE).Current());ow.Free(true);b.Remove(ow,TopExp_Explorer(incomplete.top,TopAbs_EDGE).Current());refuse("incomplete-Face-domain",incomplete);
    refuse("unintended-inner-wire",prism(rectangle,true));
    auto natural=clone(box);b.NaturalRestriction(natural.top,true);refuse("unproved-natural-domain",natural);
    std::cout<<"DOMAIN_SUMMARY actual_geometry_refusals="<<negatives<<" positive_fixtures=3 trials_each=3 whole_material=Unresolved\n";
}
