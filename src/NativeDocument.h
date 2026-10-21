#pragma once

#include <QColor>
#include <QString>
#include <TopoDS_Shape.hxx>
#include <gp_Trsf.hxx>
#include <gp_Pnt.hxx>
#include <gp_Dir.hxx>
#include <vector>
#include "ConstructionGeometry.h"

struct NativeCameraState {
    gp_Pnt eye{0.0, 0.0, 1000.0};
    gp_Pnt center{0.0, 0.0, 0.0};
    gp_Dir up{0.0, 1.0, 0.0};
    double scale = 1.0;
    int projectionType = 0;
};

struct NativePartState {
    QString id;
    QString name;
    TopoDS_Shape shape;
    gp_Trsf transform;
    QColor color = QColor(211, 211, 211);
    bool visible = true;
    int geometryKind = 1;
    bool cylinderPrimitive = false;
    bool spherePrimitive = false;
    bool conePrimitive = false;
    double sphereRadius = 0.0;
    double cylinderRadius = 0.0;
    double cylinderHeight = 0.0;
    double cylinderBaseOffset = 0.0;
    double coneBottomRadius = 40.0;
    double coneTopRadius = 20.0;
    double coneHeight = 80.0;
    double coneBaseOffset = 0.0;
};

struct NativeDocumentSnapshot {
    int schemaVersion = 2;
    std::vector<NativePartState> parts;
    std::vector<Construction::Object> constructions;
    std::array<int,3> constructionNameCounters{};
    NativeCameraState camera;
};

class NativeDocumentStorage final {
public:
    static constexpr int CurrentSchemaVersion = 2;
    static bool save(const QString& path, const NativeDocumentSnapshot& snapshot, QString& error);
    static bool load(const QString& path, NativeDocumentSnapshot& snapshot, QString& error);
};
