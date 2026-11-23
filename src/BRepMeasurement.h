#pragma once
#include <TopoDS_Shape.hxx>
#include <GProp_GProps.hxx>
#include <QString>

// Numerical properties of the current exact B-Rep, independent of modeling
// eligibility. No repair, tessellation, history, or positive-solid policy.
namespace BRepMeasurement {
enum class Integration { Automatic, Adaptive };
struct Result {
    GProp_GProps properties;
    double relativeError = 0;
    bool adaptive = false;
};
// Non-null valid empty sets and lower-dimensional contacts have zero volume.
// Solids must be valid and closed; positivity/single-shell/manifold/tolerance
// requirements remain with their existing callers. Outputs clear on failure.
bool volume(const TopoDS_Shape& shape, Result& result, QString& error);
bool area(const TopoDS_Shape& shape, Result& result, QString& error,
          Integration policy = Integration::Automatic);
// Convenience for existing exception-based geometry adapters.
GProp_GProps volumeProperties(const TopoDS_Shape& shape);
GProp_GProps areaProperties(const TopoDS_Shape& shape, Integration policy = Integration::Automatic);
}
