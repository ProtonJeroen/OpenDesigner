#pragma once

#include <BRepAdaptor_Surface.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Wire.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <QString>
#include <Geom_BSplineCurve.hxx>
#include <gp_Pln.hxx>
#include <vector>

namespace BRepReconstruction {
class TopologyIndex;
struct InvariantSplineSupport;
// Exact current carrier and oriented trim owner, not a feature classification.
// The adaptor exposes analytic supports, extrusion basis curves, B-spline
// carriers, trimmed UV bounds and the carrier transform without approximation.
// Cylinder/torus/plane alone never establishes Fillet/Chamfer identity. Material
// side and continuity require oriented trims and an independent local certificate.
struct SupportDescriptor {
    TopoDS_Face face;
    BRepAdaptor_Surface surface;
    explicit SupportDescriptor(const TopoDS_Face& owner) : face(owner), surface(owner) {}
    // Opt-in, read-only bounded support proof; no command eligibility change.
    bool invariantSpline(InvariantSplineSupport& result, QString& error,
                         const TopologyIndex* context = nullptr) const;
};

struct InvariantSplineSupport {
    enum class Boundary { FirstU, LastU, FirstV, LastV };
    struct Trim {
        Boundary boundary;
        TopoDS_Edge edge; // current oriented use, not persistent identity
        std::vector<TopoDS_Face> neighbors;
        std::vector<TopoDS_Face> planarCaps; // actual perpendicular cap neighbors
    };
    TopoDS_Face face;
    Handle(Geom_BSplineCurve) basis, trimmedBasis; // isolated exact geometry in B-Rep coordinates
    gp_Dir axis;
    gp_Pln basisPlane, firstSection, lastSection;
    double firstU=0, lastU=0, firstV=0, lastV=0;
    double planarityResidual=0;
    std::vector<Trim> trims;
    // The linear carrier has unbounded V continuation. This certifies only
    // support geometry over the recorded finite U interval, not a safe material
    // edit, cap extension, collision clearance, or arbitrary numerical distance.
    bool axialContinuation=false;
};

// Read-only analysis of one current shape. Handles retain exact curves, pcurves,
// continuity data, locations and tolerances; consumers must not mutate them.
// This index neither normalizes nor validates/heals the shape. Existing callers
// retain their own solid, complexity and numerical acceptance policies.
class TopologyIndex {
public:
    struct Wire {
        TopoDS_Wire wire;
        bool outer = false;
        // Topological enumeration, not a certified connected traversal order.
        std::vector<TopoDS_Edge> edges; // oriented occurrences, including seams
    };
    struct Face {
        SupportDescriptor support;
        std::vector<Wire> wires;
        explicit Face(const TopoDS_Face& owner) : support(owner) {}
    };
    struct EdgeUse {
        int faceIndex; // zero-based indices into faces() and Face::wires
        int wireIndex;
        TopoDS_Edge edge; // use orientation, not the unique edge's orientation
    };
    struct Edge {
        TopoDS_Edge edge;
        std::vector<TopoDS_Face> faces; // distinct owners; a seam has one owner
        std::vector<EdgeUse> uses;     // a periodic seam retains both uses
    };

    // Clears result on failure. No document/recipe/primitive inputs exist.
    static bool build(const TopoDS_Shape& shape, TopologyIndex& result, QString& error);
    const std::vector<Face>& faces() const { return faces_; }
    const std::vector<Edge>& edges() const { return edges_; }
    const Face* face(const TopoDS_Face& owner) const;
    const Edge* edge(const TopoDS_Edge& owner) const;
    std::vector<TopoDS_Face> neighbors(const TopoDS_Face& owner) const;
private:
    TopTools_IndexedMapOfShape faceMap_, edgeMap_;
    std::vector<Face> faces_;
    std::vector<Edge> edges_;
};
}
