#pragma once
#include "BRepMeasurement.h"

namespace BRepCertification { bool singleSolid(const TopoDS_Shape& shape,double& volume); }
namespace BooleanGeometry { bool validSolid(const TopoDS_Shape& shape,double& volume); }
namespace BRepMeasurement::detail {
// Internal lexical handoff, NOT a reusable shape-validity record. Only
// singleSolid and BooleanGeometry::validSolid may construct/consume it after
// their successful, identical default-options validBRep check.
// It borrows that exact argument through the following read-only traversals.
// No caller can pass another shape, flags, policy, or a claimed success Boolean.
class ValidatedInputVolume final {
    friend bool BRepCertification::singleSolid(const TopoDS_Shape&,double&);
    friend bool BooleanGeometry::validSolid(const TopoDS_Shape&,double&);
    const TopoDS_Shape& input;
    explicit ValidatedInputVolume(const TopoDS_Shape& shape):input(shape) {}
    ~ValidatedInputVolume() = default;
    ValidatedInputVolume(const ValidatedInputVolume&) = delete;
    ValidatedInputVolume(ValidatedInputVolume&&) = delete;
    ValidatedInputVolume& operator=(const ValidatedInputVolume&) = delete;
    ValidatedInputVolume& operator=(ValidatedInputVolume&&) = delete;
    bool compute(Result& result,QString& error) const;
};
}
