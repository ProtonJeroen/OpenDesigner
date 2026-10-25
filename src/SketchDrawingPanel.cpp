#include "SketchDrawingPanel.h"
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QDialogButtonBox>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QSignalBlocker>
#include <QPainter>
#include <QPixmap>
#include <QLineF>
#include <QPalette>
#include <QIcon>
#include <QApplication>

QIcon Sketch::orthogonalLineIcon(DrawTool tool) {
    QIcon icon;
    for(int scale:{1,2}) {
        QPixmap image(24*scale,24*scale);image.setDevicePixelRatio(scale);image.fill(Qt::transparent);
        QPainter painter(&image);painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(QColor(255,102,204),1.8,Qt::DashLine));
        const bool horizontal=tool==DrawTool::HorizontalInfiniteLine;
        painter.drawLine(horizontal?QLineF(2,6,22,6):QLineF(6,2,6,22));
        painter.setPen(QApplication::palette().color(QPalette::WindowText));
        auto font=painter.font();font.setPixelSize(12);font.setBold(true);painter.setFont(font);
        painter.drawText(QRect(10,10,14,14),Qt::AlignCenter,horizontal?QStringLiteral("H"):QStringLiteral("V"));
        painter.end();icon.addPixmap(image);
    }
    return icon;
}

SketchDrawingPanel::SketchDrawingPanel(Sketch::DrawTool tool,Sketch::Role initialRole,const QString& planeName,QWidget* parent)
    : QDialog(parent,Qt::Tool),tool_(tool) {
    setObjectName("sketchDrawingPanel");setWindowTitle(tr("2D Drawing — %1").arg(planeName));setModal(false);
    auto* layout=new QVBoxLayout(this);auto* form=new QFormLayout;form_=form;layout->addLayout(form);
    role=new QComboBox(this);role->setObjectName("sketchRole");
    if(!Sketch::infiniteLineTool(tool)) role->addItem(tr("Profile"),int(Sketch::Role::Profile));
    role->addItem(tr("Construction"),int(Sketch::Role::Construction));role->setCurrentIndex(role->findData(int(initialRole)));
    form->addRow(tr("Geometry"),role);
    const auto spin=[this](const char* name,double min,double max) {
        auto* s=new QDoubleSpinBox(this);s->setObjectName(name);s->setRange(min,max);s->setDecimals(9);s->setKeyboardTracking(false);return s;
    };
    x=spin("sketchX",-1.e9,1.e9);y=spin("sketchY",-1.e9,1.e9);
    form->addRow(tr("X (mm)"),x);form->addRow(tr("Y (mm)"),y);
    xyButton=new QPushButton(tr("Use XY point"),this);xyButton->setObjectName("sketchUseXY");layout->addWidget(xyButton);
    distance=spin("sketchDistance",0.000001,1.e9);distance->setValue(10);
    angle=spin("sketchAngle",-359.999999999,359.999999999);
    distanceLabel_=new QLabel(this);angleLabel_=new QLabel(this);
    form->addRow(distanceLabel_,distance);form->addRow(angleLabel_,angle);
    direction=new QComboBox(this);direction->setObjectName("sketchArcDirection");direction->addItems({tr("Counterclockwise (CCW)"),tr("Clockwise (CW)")});
    if(tool==Sketch::DrawTool::Arc || tool==Sketch::DrawTool::EllipticalArc) form->addRow(tr("Arc direction"),direction);else direction->hide();
    if(Sketch::ellipseTool(tool)) {
        x->setDecimals(12);y->setDecimals(12);
        majorRadius=spin("sketchEllipseMajorRadius",0,1.e9);majorRadius->setValue(10);
        axisAngle=spin("sketchEllipseAxisAngle",-360000,360000);
        minorRadius=spin("sketchEllipseMinorRadius",0,1.e9);minorRadius->setValue(5);
        startParameter=spin("sketchEllipseStartParameter",-360000,360000);
        sweepParameter=spin("sketchEllipseSweep",-360,360);sweepParameter->setValue(90);
        form->addRow(tr("Major radius (mm)"),majorRadius);form->addRow(tr("Major axis angle (degrees)"),axisAngle);
        form->addRow(tr("Minor radius (mm)"),minorRadius);form->addRow(tr("Start parameter (degrees)"),startParameter);
        form->addRow(tr("Signed sweep (degrees)"),sweepParameter);
        for(auto* s:{x,y,majorRadius,axisAngle,minorRadius,startParameter,sweepParameter}) {
            s->setDecimals(12);
            connect(s,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this,s] {
                ellipseEdits_.insert(s);if(ellipseParametersChanged) ellipseParametersChanged();
            });
        }
    }
    polarButton=new QPushButton(this);polarButton->setObjectName("sketchUsePolar");layout->addWidget(polarButton);
    backButton=new QPushButton(tr("Back one point"),this);backButton->setObjectName("sketchBack");layout->addWidget(backButton);
    status_=new QLabel(this);status_->setWordWrap(true);status_->setMinimumWidth(260);layout->addWidget(status_);
    buttons_=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,this);layout->addWidget(buttons_);
    for(auto* b:{xyButton,polarButton,backButton,buttons_->button(QDialogButtonBox::Ok),buttons_->button(QDialogButtonBox::Cancel)}) {b->setAutoDefault(false);b->setDefault(false);}
    connect(xyButton,&QPushButton::clicked,this,[this]{finishEditing();if(useXY) useXY();});
    connect(polarButton,&QPushButton::clicked,this,[this]{finishEditing();if(usePolar) usePolar();});
    connect(backButton,&QPushButton::clicked,this,[this]{if(back) back();});
    connect(buttons_,&QDialogButtonBox::accepted,this,[this]{finishEditing();if(confirmed) confirmed();});
    connect(buttons_,&QDialogButtonBox::rejected,this,&SketchDrawingPanel::reject);
    connect(role,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{if(roleChanged) roleChanged();});
    connect(direction,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{if(directionChanged) directionChanged();});
    if(Sketch::orthogonalLineTool(tool)) {
        const bool horizontal=tool==Sketch::DrawTool::HorizontalInfiniteLine;
        setWindowTitle((horizontal?tr("Horizontal Infinite Construction Line — %1"):tr("Vertical Infinite Construction Line — %1")).arg(planeName));
        auto* coordinate=horizontal?y:x;coordinate->setKeyboardTracking(true);
        connect(coordinate,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this]{if(orthogonalPositionChanged) orthogonalPositionChanged();});
    }
    setStep(0,false,false,{});
}
void SketchDrawingPanel::reject() {if(cancelled) cancelled();else QDialog::reject();}
void SketchDrawingPanel::finishEditing() {for(auto* spin:{x,y,distance,angle,majorRadius,axisAngle,minorRadius,startParameter,sweepParameter}) if(spin) spin->interpretText();}
bool SketchDrawingPanel::parameterWidget(const QWidget* widget) const {
    for(const auto* s:{distance,angle,majorRadius,axisAngle,minorRadius,startParameter,sweepParameter})
        if(s && widget && (s==widget || s->isAncestorOf(widget))) return true;
    return false;
}
void SketchDrawingPanel::setEllipseValues(const Sketch::Ellipse& e,double start,double sweep,bool force) {
    if(!majorRadius || (hasEllipseEdits() && !force)) return;
    constexpr double degrees=180/3.14159265358979323846;
    const auto put=[&](QDoubleSpinBox* s,double value) {if(force || !s->hasFocus()) {const QSignalBlocker block(s);s->setValue(value);}};
    put(x,e.center.x);put(y,e.center.y);put(majorRadius,e.majorRadius);put(axisAngle,e.axisAngle*degrees);
    put(minorRadius,e.minorRadius);put(startParameter,start*degrees);put(sweepParameter,sweep*degrees);
}
void SketchDrawingPanel::setEllipseStep(int points,bool complete,bool valid,const QString& message) {
    const bool arc=tool_==Sketch::DrawTool::EllipticalArc;
    const auto row=[&](QWidget* field,bool visible) {field->setVisible(visible);if(auto* label=form_->labelForField(field)) label->setVisible(visible);};
    row(x,points==0 || complete);row(y,points==0 || complete);
    row(distance,false);row(angle,false);
    row(majorRadius,points==1 || complete);row(axisAngle,points==1 || complete);
    row(minorRadius,points==2 || complete);
    row(startParameter,arc && (points==3 || complete));row(sweepParameter,arc && (points==4 || complete));
    if(arc) row(direction,points>=3);
    xyButton->setVisible(points==0);xyButton->setEnabled(!complete);
    polarButton->setVisible(points>0 && !complete);polarButton->setEnabled(points>0 && !complete);
    polarButton->setText(points==1?tr("Use major radius / angle"):points==2?tr("Use minor radius"):
        points==3?tr("Use start parameter"):tr("Use signed sweep"));
    backButton->setEnabled(points>0);buttons_->button(QDialogButtonBox::Ok)->setEnabled(complete && valid);
    const auto prompt=complete?tr("Preview — edit parameters, then OK/Enter; Cancel/Esc discards."):
        points==0?tr("Pick center or enter exact XY."):points==1?tr("Pick major-axis endpoint or enter radius / angle."):
        points==2?tr("Pick minor radius perpendicular to the fixed major axis. Do not exceed the major radius."):
        points==3?tr("Pick start on the ellipse or enter its parameter (not polar angle)."):
        tr("Pick end on the ellipse or enter signed sweep. CW/CCW preserves endpoints.");
    status_->setText(prompt+(message.isEmpty()?QString():"\n"+message));
}
void SketchDrawingPanel::setCursor(Sketch::Point p,double length,double degrees) {
    const auto update=[](QDoubleSpinBox* s,double v){if(!s->hasFocus()) {const QSignalBlocker blocker(s);s->setValue(v);}};
    update(x,p.x);update(y,p.y);if(length>0) update(distance,length);update(angle,degrees);
}
void SketchDrawingPanel::setArcSweep(double degrees) {const QSignalBlocker block(angle);angle->setValue(degrees);}
void SketchDrawingPanel::setStep(int points,bool complete,bool valid,const QString& message) {
    if(Sketch::ellipseTool(tool_)) {setEllipseStep(points,complete,valid,message);return;}
    if(Sketch::orthogonalLineTool(tool_)) {
        const bool horizontal=tool_==Sketch::DrawTool::HorizontalInfiniteLine;
        const auto row=[&](QWidget* field,bool visible) {field->setVisible(visible);if(auto* label=form_->labelForField(field)) label->setVisible(visible);};
        row(x,!horizontal);row(y,horizontal);row(distance,false);row(angle,false);
        polarButton->hide();xyButton->setText(horizontal?tr("Use Y position"):tr("Use X position"));xyButton->setEnabled(true);
        backButton->setEnabled(points>0);buttons_->button(QDialogButtonBox::Ok)->setEnabled(complete && valid);
        const auto prompt=complete?tr("Preview — edit the offset, then OK/Enter; Cancel/Esc discards.")
            :horizontal?tr("Click once or enter local Y (mm). Direction is exactly (1,0).")
                       :tr("Click once or enter local X (mm). Direction is exactly (0,1).");
        status_->setText(prompt+(message.isEmpty()?QString():"\n"+message));return;
    }
    const bool arc=tool_==Sketch::DrawTool::Arc;
    distanceLabel_->setText(tool_==Sketch::DrawTool::Circle || arc?tr("Radius (mm)"):tr("Length (mm)"));
    angleLabel_->setText(arc && points>=2?tr("Sweep (degrees)"):tr("Angle (degrees)"));
    polarButton->setText(arc && points>=2?tr("Use sweep angle"):tool_==Sketch::DrawTool::Circle || arc?tr("Use radius / angle"):tr("Use length / angle"));
    polarButton->setEnabled(points>0 && !complete);xyButton->setEnabled(!complete);backButton->setEnabled(points>0);
    const bool fixedRadius=arc && points>=2;distance->setEnabled(!fixedRadius);
    buttons_->button(QDialogButtonBox::Ok)->setEnabled(complete && valid);
    const QString prompt=complete?tr("Preview — OK/Enter confirms; Cancel/Esc discards.")
        :points==0?(arc || tool_==Sketch::DrawTool::Circle?tr("Pick center or enter XY."):tr("Pick start point or enter XY."))
        :fixedRadius?tr("Choose CW/CCW, then pick arc end or enter a sweep magnitude. Negative input selects CW."):arc?tr("Pick arc start or enter radius / start angle.")
        :tool_==Sketch::DrawTool::Circle?tr("Pick radius point or enter radius."):tr("Pick end point or enter length / angle.");
    status_->setText(prompt+(message.isEmpty()?QString():"\n"+message));
}
