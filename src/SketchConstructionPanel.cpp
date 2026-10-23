#include "SketchConstructionPanel.h"
#include <QComboBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QSignalBlocker>
#include <cmath>

SketchConstructionPanel::SketchConstructionPanel(Sketch::ConstructionTool tool,const QString& planeName,QWidget* parent):QDialog(parent,Qt::Tool) {
    const bool offset=tool==Sketch::ConstructionTool::OffsetLine;
    setObjectName("sketchConstructionPanel");setWindowTitle((offset?tr("Offset Construction Line — %1"):tr("Common Tangent Construction Line — %1")).arg(planeName));setModal(false);
    auto* layout=new QVBoxLayout(this);
    prompt_=new QLabel(this);prompt_->setObjectName("constructionLinePrompt");prompt_->setWordWrap(true);prompt_->setMinimumWidth(350);layout->addWidget(prompt_);
    auto* form=new QFormLayout;layout->addLayout(form);
    for(int i=0;i<(offset?1:2);++i) {
        auto* row=new QWidget(this);auto* horizontal=new QHBoxLayout(row);horizontal->setContentsMargins(0,0,0,0);
        references[i]=new QComboBox(row);references[i]->setObjectName(QString("constructionLineReference%1").arg(i+1));references[i]->setMinimumContentsLength(24);
        references[i]->addItem(tr("Choose a reference…"),QString());horizontal->addWidget(references[i]);
        pickButtons[i]=new QPushButton(tr("Pick"),row);horizontal->addWidget(pickButtons[i]);
        form->addRow(offset?tr("Straight reference"):tr("Circle / arc %1").arg(i+1),row);
        connect(references[i],qOverload<int>(&QComboBox::currentIndexChanged),this,[this,i]{if(referenceChanged) referenceChanged(i);});
        connect(pickButtons[i],&QPushButton::clicked,this,[this,i]{if(pickReference) pickReference(i);});
    }
    output=new QComboBox(this);output->setObjectName("constructionLineOutput");output->addItem(tr("Infinite"),int(Sketch::LineOutput::Infinite));output->addItem(tr("Finite"),int(Sketch::LineOutput::Finite));form->addRow(tr("Output"),output);
    connect(output,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{if(parametersChanged) parametersChanged();});
    const auto number=[&](const char* name,double low,double high,double value) {
        auto* field=new QDoubleSpinBox(this);field->setObjectName(name);field->setDecimals(9);field->setRange(low,high);field->setValue(value);field->setKeyboardTracking(false);return field;
    };
    if(offset) {
        distance=number("constructionLineOffset",-1.e9,1.e9,10);form->addRow(tr("Signed offset (mm)"),distance);
        side=new QComboBox(this);side->addItems({tr("Left of source direction (+)"),tr("Right of source direction (−)")});form->addRow(tr("Side"),side);
        mouseSide=new QCheckBox(tr("Choose side with mouse; click to lock"),this);form->addRow(mouseSide);
        intervalGroup_=new QWidget(this);auto* intervalForm=new QFormLayout(intervalGroup_);intervalForm->setContentsMargins(0,0,0,0);
        explicitInterval=new QCheckBox(tr("Use explicit position and length"),intervalGroup_);intervalForm->addRow(explicitInterval);
        intervalStart=number("constructionLineIntervalStart",-1.e9,1.e9,0);intervalLength=number("constructionLineIntervalLength",1.e-6,1.e9,100);
        intervalForm->addRow(tr("Start along source (mm)"),intervalStart);intervalForm->addRow(tr("Length (mm)"),intervalLength);layout->addWidget(intervalGroup_);
        connect(distance,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this](double value) {const QSignalBlocker block(side);side->setCurrentIndex(value<0?1:0);if(parametersChanged) parametersChanged();});
        connect(side,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int index) {const QSignalBlocker block(distance);distance->setValue((index==0?1:-1)*std::abs(distance->value()));if(parametersChanged) parametersChanged();});
        connect(mouseSide,&QCheckBox::toggled,this,[this]{if(parametersChanged) parametersChanged();});
        connect(explicitInterval,&QCheckBox::toggled,this,[this]{if(parametersChanged) parametersChanged();});
        for(auto* field:{intervalStart,intervalLength}) connect(field,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this]{if(parametersChanged) parametersChanged();});
        setIntervalVisible(false);
    } else {
        solutions=new QComboBox(this);solutions->setObjectName("constructionLineSolution");solutions->setMinimumContentsLength(35);form->addRow(tr("Explicit solution"),solutions);
        connect(solutions,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int index){if(solutionChanged) solutionChanged(index-1);});
        auto* legend=new QLabel(tr("Green: available tangents. Blue: chosen solution. Orange: contact points.\nSelect a solution in the list or click a separate green line."),this);legend->setWordWrap(true);layout->addWidget(legend);
    }
    buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,this);layout->addWidget(buttons);
    for(auto* button:findChildren<QPushButton*>()) {button->setAutoDefault(false);button->setDefault(false);}
    connect(buttons,&QDialogButtonBox::accepted,this,[this]{if(confirmed) confirmed();});
    connect(buttons,&QDialogButtonBox::rejected,this,&SketchConstructionPanel::reject);
}
void SketchConstructionPanel::finishEditing() {
    for(auto* field:{distance,intervalStart,intervalLength}) if(field) field->interpretText();
}
void SketchConstructionPanel::setIntervalVisible(bool visible) {
    if(!intervalGroup_) return;
    intervalGroup_->setVisible(visible);intervalStart->setEnabled(visible && explicitInterval->isChecked());intervalLength->setEnabled(visible && explicitInterval->isChecked());
}
void SketchConstructionPanel::setStatus(const QString& message,bool valid) {prompt_->setText(message);buttons->button(QDialogButtonBox::Ok)->setEnabled(valid);adjustSize();}
void SketchConstructionPanel::reject() {if(cancelled) cancelled();else QDialog::reject();}
