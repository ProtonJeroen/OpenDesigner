#pragma once
#include <QJsonObject>
#include <QString>
#include <TopoDS_Shape.hxx>
#include <vector>

// Diagnostic snapshots only; deliberately independent of document loading.
namespace DebugModel {
struct Object { QJsonObject metadata; TopoDS_Shape shape; };
struct Snapshot { QJsonObject context; std::vector<Object> objects; };
QJsonObject diagnostics(const TopoDS_Shape& shape);
QJsonObject meshDiagnostics(const TopoDS_Shape& shape);
TopoDS_Shape remesh(const TopoDS_Shape& shape);
bool equivalent(const TopoDS_Shape& a,const TopoDS_Shape& b);
bool write(const QString& path,const Snapshot& snapshot,QString& error);
bool read(const QString& path,Snapshot& snapshot,QString& error);
}
