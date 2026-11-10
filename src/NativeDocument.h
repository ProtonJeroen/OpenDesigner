#pragma once

#include <QColor>
#include <QJsonObject>
#include <QString>
#include <TopoDS_Shape.hxx>
#include <gp_Trsf.hxx>
#include <gp_Pnt.hxx>
#include <gp_Dir.hxx>
#include <vector>
#include "ConstructionGeometry.h"
#include "SketchGeometry.h"

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
    QJsonObject edgeBlendRecipe;
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
    int schemaVersion = 5; // Capability; saves choose v3/v4/v5 by exact sketch content.
    std::vector<NativePartState> parts;
    QString activePartId;
    std::vector<Construction::Object> constructions;
    std::array<int,3> constructionNameCounters{};
    std::vector<Sketch::Entity> sketchEntities;
    NativeCameraState camera;
};

class NativeDocumentStorage final {
public:
    static constexpr int CurrentSchemaVersion = 5;
    // Shared by storage and snapshot import. Empty/legacy curves require v3,
    // points v4, ellipses v5; older document-content guards remain separate.
    static int sketchSchemaVersion(const std::vector<Sketch::Entity>& entities);
    static bool save(const QString& path, const NativeDocumentSnapshot& snapshot, QString& error);
    static bool load(const QString& path, NativeDocumentSnapshot& snapshot, QString& error);
};
