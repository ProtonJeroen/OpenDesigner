#include "CadViewport.h"
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableWidget>
#include <QHeaderView>
#include <QPushButton>
#include <QCheckBox>
#include <QLineEdit>
#include <QLabel>
#include <Standard_Failure.hxx>
#include <algorithm>
#include <numbers>

void CadViewport::editSketchCurve(bool createSpline,Sketch::Role role) {
    const auto* owner=constructionObject(activeSketchWorkplaneId_);
    if(!owner || !owner->visible || !owner->selectable) {emit statusMessage(tr("Edit a visible, unlocked Workplane first."));return;}
    Sketch::Entity candidate;candidate.workplaneId=owner->id;candidate.role=role;
    if(createSpline) candidate.geometry=Sketch::Spline{{{0,0},{10,10},{20,0}}};
    else {
        if(selectedSketchIds_.size()!=1) {emit statusMessage(tr("Select one ellipse, elliptical arc or interpolating spline."));return;}
        const auto it=std::find_if(sketchEntities_.begin(),sketchEntities_.end(),[&](const auto& e){return e.id==selectedSketchIds_.front();});
        if(it==sketchEntities_.end() || it->workplaneId!=owner->id || !it->visible || !it->selectable
           || (!Sketch::isElliptical(it->geometry) && !std::holds_alternative<Sketch::Spline>(it->geometry))) {
            emit statusMessage(tr("Choose an editable curve on the active Workplane."));return;
        }
        candidate=*it;
    }
    const auto originalGeometry=candidate.geometry;
    cancelCommandSession();
    QDialog dialog(this);dialog.setObjectName("sketchCurveEditor");dialog.setWindowTitle(createSpline?tr("Interpolating Spline"):tr("Edit Curve"));
    auto* layout=new QVBoxLayout(&dialog);auto* form=new QFormLayout;layout->addLayout(form);
    std::vector<QDoubleSpinBox*> values;std::vector<double> displayedValues;
    const auto number=[&](const QString& label,double value) {
        auto* spin=new QDoubleSpinBox(&dialog);spin->setDecimals(9);spin->setRange(-1.e9,1.e9);spin->setValue(value);
        spin->setKeyboardTracking(false);form->addRow(label,spin);values.push_back(spin);displayedValues.push_back(spin->value());return spin;
    };
    QTableWidget* points=nullptr;QCheckBox* closed=nullptr;QLineEdit* startTangent=nullptr;QLineEdit* endTangent=nullptr;
    if(const auto* s=std::get_if<Sketch::Spline>(&candidate.geometry)) {
        layout->addWidget(new QLabel(tr("Workplane coordinates in mm. Insert, move or remove through-points by editing the table."),&dialog));
        points=new QTableWidget(int(s->points.size()),2,&dialog);points->setObjectName("splinePoints");
        points->setHorizontalHeaderLabels({tr("X (mm)"),tr("Y (mm)")});points->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        for(int i=0;i<points->rowCount();++i) {points->setItem(i,0,new QTableWidgetItem(QString::number(s->points[size_t(i)].x,'g',17)));points->setItem(i,1,new QTableWidgetItem(QString::number(s->points[size_t(i)].y,'g',17)));}
        layout->addWidget(points);auto* row=new QHBoxLayout;layout->addLayout(row);
        auto* add=new QPushButton(tr("Insert point"),&dialog);auto* remove=new QPushButton(tr("Remove point"),&dialog);row->addWidget(add);row->addWidget(remove);
        connect(add,&QPushButton::clicked,&dialog,[&]{const int at=points->currentRow()<0?points->rowCount():points->currentRow()+1;points->insertRow(at);points->setItem(at,0,new QTableWidgetItem("0"));points->setItem(at,1,new QTableWidgetItem("0"));points->setCurrentCell(at,0);});
        connect(remove,&QPushButton::clicked,&dialog,[&]{if(points->currentRow()>=0) points->removeRow(points->currentRow());});
        closed=new QCheckBox(tr("Closed (periodic; omit repeated first point)"),&dialog);closed->setChecked(s->closed);layout->addWidget(closed);
        const auto tangentText=[](const std::optional<Sketch::Point>& p){return p?QString::number(p->x,'g',17)+", "+QString::number(p->y,'g',17):QString();};
        startTangent=new QLineEdit(tangentText(s->startTangent),&dialog);endTangent=new QLineEdit(tangentText(s->endTangent),&dialog);
        form->addRow(tr("Start tangent X, Y (blank = automatic)"),startTangent);form->addRow(tr("End tangent X, Y (blank = automatic)"),endTangent);
    } else {
        const auto e=std::holds_alternative<Sketch::Ellipse>(candidate.geometry)?std::get<Sketch::Ellipse>(candidate.geometry):std::get<Sketch::EllipticalArc>(candidate.geometry).ellipse;
        number(tr("Center X (mm)"),e.center.x);number(tr("Center Y (mm)"),e.center.y);
        number(tr("Major radius (mm)"),e.majorRadius);number(tr("Minor radius (mm)"),e.minorRadius);number(tr("Axis angle (degrees)"),e.axisAngle*180/std::numbers::pi);
        if(const auto* a=std::get_if<Sketch::EllipticalArc>(&candidate.geometry)) {number(tr("Start parameter (degrees)"),a->startParameter*180/std::numbers::pi);number(tr("Signed sweep (degrees)"),a->sweepParameter*180/std::numbers::pi);}
    }
    auto* diagnostic=new QLabel(&dialog);diagnostic->setWordWrap(true);layout->addWidget(diagnostic);
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);buttons->button(QDialogButtonBox::Ok)->setText(tr("Preview"));layout->addWidget(buttons);
    connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    connect(buttons,&QDialogButtonBox::accepted,&dialog,[&] {
        QString error;
        if(points) {
            Sketch::Spline s;s.closed=closed->isChecked();
            for(int i=0;i<points->rowCount();++i) {
                bool x=false,y=false;const auto* xi=points->item(i,0);const auto* yi=points->item(i,1);
                const double px=xi?xi->text().toDouble(&x):0,py=yi?yi->text().toDouble(&y):0;
                if(!x || !y) {diagnostic->setText(tr("Enter numeric X and Y coordinates for every point."));return;}s.points.push_back({px,py});
            }
            const auto tangent=[&](QLineEdit* input,std::optional<Sketch::Point>& target) {
                if(input->text().trimmed().isEmpty()) return true;
                const auto parts=input->text().split(',');if(parts.size()!=2) return false;
                bool x=false,y=false;const auto px=parts[0].toDouble(&x),py=parts[1].toDouble(&y);if(!x || !y) return false;
                target=Sketch::Point{px,py};return true;
            };
            if(!tangent(startTangent,s.startTangent) || !tangent(endTangent,s.endTangent)) {diagnostic->setText(tr("Tangents use two numbers separated by a comma."));return;}
            candidate.geometry=s;
        } else {
            const double radians=std::numbers::pi/180;
            auto e=std::holds_alternative<Sketch::Ellipse>(originalGeometry)?std::get<Sketch::Ellipse>(originalGeometry):std::get<Sketch::EllipticalArc>(originalGeometry).ellipse;
            // Display precision is not document precision. Preserve every
            // untouched value, including radians and values outside UI ranges.
            const auto changed=[&](size_t i){return values[i]->value()!=displayedValues[i];};
            if(changed(0)) e.center.x=values[0]->value();if(changed(1)) e.center.y=values[1]->value();
            if(changed(2)) e.majorRadius=values[2]->value();if(changed(3)) e.minorRadius=values[3]->value();
            if(changed(4)) e.axisAngle=values[4]->value()*radians;
            if(values.size()==7) {
                auto arc=std::get<Sketch::EllipticalArc>(originalGeometry);arc.ellipse=e;
                if(changed(5)) arc.startParameter=values[5]->value()*radians;
                if(changed(6)) arc.sweepParameter=values[6]->value()*radians;
                candidate.geometry=arc;
            } else candidate.geometry=e;
        }
        if(!Sketch::valid(candidate,error)) {diagnostic->setText(error);return;}
        try {if(const auto* s=std::get_if<Sketch::Spline>(&candidate.geometry)) Sketch::splineCurve(*s);}
        catch(const Standard_Failure& e) {diagnostic->setText(QString::fromUtf8(e.GetMessageString()));return;}
        catch(const std::exception& e) {diagnostic->setText(QString::fromUtf8(e.what()));return;}
        dialog.accept();
    });
    dialog.resize(540,440);if(dialog.exec()!=QDialog::Accepted) return;
    auto after=sketchEntities_;
    if(createSpline) after.push_back(candidate);
    else for(auto& e:after) if(e.id==candidate.id) e=candidate;
    QString error;if(!previewSketchEntities(after,error)) emit statusMessage(error);
}
