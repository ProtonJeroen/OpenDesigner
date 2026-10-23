#include "SketchDrawingPanel.h"
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QDialogButtonBox>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QSignalBlocker>

SketchDrawingPanel::SketchDrawingPanel(Sketch::DrawTool tool,Sketch::Role initialRole,const QString& planeName,QWidget* parent)
    : QDialog(parent,Qt::Tool),tool_(tool) {
    setObjectName("sketchDrawingPanel");setWindowTitle(tr("2D Drawing — %1").arg(planeName));setModal(false);
    auto* layout=new QVBoxLayout(this);auto* form=new QFormLayout;layout->addLayout(form);
    role=new QComboBox(this);role->setObjectName("sketchRole");
    if(tool!=Sketch::DrawTool::InfiniteLine) role->addItem(tr("Profile"),int(Sketch::Role::Profile));
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
    if(tool==Sketch::DrawTool::Arc) form->addRow(tr("Arc direction"),direction);else direction->hide();
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
    setStep(0,false,false,{});
}
void SketchDrawingPanel::reject() {if(cancelled) cancelled();else QDialog::reject();}
void SketchDrawingPanel::finishEditing() {for(auto* spin:{x,y,distance,angle}) spin->interpretText();}
void SketchDrawingPanel::setCursor(Sketch::Point p,double length,double degrees) {
    const auto update=[](QDoubleSpinBox* s,double v){if(!s->hasFocus()) {const QSignalBlocker blocker(s);s->setValue(v);}};
    update(x,p.x);update(y,p.y);if(length>0) update(distance,length);update(angle,degrees);
}
void SketchDrawingPanel::setArcSweep(double degrees) {const QSignalBlocker block(angle);angle->setValue(degrees);}
void SketchDrawingPanel::setStep(int points,bool complete,bool valid,const QString& message) {
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
