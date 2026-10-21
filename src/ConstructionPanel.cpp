#include "ConstructionPanel.h"
#include <QFormLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPushButton>
#include <QSignalBlocker>

ConstructionPanel::ConstructionPanel(const Construction::Definition& d,QWidget* parent)
    : QDialog(parent,Qt::Tool),method_(d.method) {
    setObjectName("constructionPanel");setWindowTitle(d.name);setModal(false);
    auto* layout=new QVBoxLayout(this);auto* form=new QFormLayout;layout->addLayout(form);
    name=new QLineEdit(this);name->setObjectName("constructionName");form->addRow(tr("Name"),name);
    for(size_t i=0;i<d.roles.size();++i) {
        auto* row=new QWidget(this);auto* line=new QHBoxLayout(row);line->setContentsMargins(0,0,0,0);
        auto* combo=new QComboBox(row);combo->setObjectName(QString("constructionRole%1").arg(i));
        combo->setMinimumContentsLength(20);combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        combo->addItem(tr("Select a reference…"),QString());roles.push_back(combo);line->addWidget(combo);
        auto* pick=new QPushButton(tr("Pick"),row);pick->setObjectName(QString("constructionPick%1").arg(i));line->addWidget(pick);
        form->addRow(d.roles[i].name,row);
        connect(pick,&QPushButton::clicked,this,[this,i] {if(pickRequested) pickRequested(i);});
        connect(combo,qOverload<int>(&QComboBox::currentIndexChanged),this,[this] {if(changed) changed();});
    }
    const auto addVector=[&](const QString& label,std::array<QDoubleSpinBox*,3>& fields,const char* prefix) {
        if(label.isEmpty()) return;
        auto* row=new QWidget(this);auto* line=new QHBoxLayout(row);line->setContentsMargins(0,0,0,0);
        for(int axis=0;axis<3;++axis) {
            line->addWidget(new QLabel(QString(QChar('X'+axis)),row));
            auto* input=new QDoubleSpinBox(row);input->setRange(-1.e9,1.e9);input->setDecimals(6);input->setKeyboardTracking(false);
            input->setObjectName(QString::fromLatin1(prefix)+QChar('X'+axis));fields[axis]=input;line->addWidget(input);
            connect(input,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this] {if(changed) changed();});
        }
        form->addRow(label,row);
    };
    addVector(d.vectorLabel,vector,"constructionVector");addVector(d.anglesLabel,angles,"constructionAngle");
    if(!d.valueLabel.isEmpty()) {
        value=new QDoubleSpinBox(this);value->setRange(-1.e9,1.e9);value->setDecimals(6);value->setKeyboardTracking(false);
        value->setObjectName("constructionValue");form->addRow(d.valueLabel,value);
        connect(value,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this] {if(changed) changed();});
    }
    if(d.method==Construction::Method::PlaneMidplane) {
        midplaneSolution=new QComboBox(this);midplaneSolution->setObjectName("midplaneSolution");
        midplaneSolution->addItems({tr("Between planes / smaller angle"),tr("Other angle bisector")});
        midplaneSolution->setToolTip(tr("Angled planes have two perpendicular bisectors. Preview both to choose the intended wedge sector. Parallel planes have only one midplane."));
        form->addRow(tr("Solution"),midplaneSolution);
        connect(midplaneSolution,qOverload<int>(&QComboBox::currentIndexChanged),this,[this] {if(changed) changed();});
    }
    reverse=new QCheckBox(tr("Reverse normal / direction"),this);reverse->setVisible(d.kind!=Construction::Kind::Point && !d.edit);
    form->addRow(reverse);connect(reverse,&QCheckBox::toggled,this,[this] {if(changed) changed();});
    status_=new QLabel(this);status_->setWordWrap(true);status_->setObjectName("constructionStatus");layout->addWidget(status_);
    buttons_=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,this);layout->addWidget(buttons_);
    connect(buttons_,&QDialogButtonBox::accepted,this,[this] {if(confirmed) confirmed();});
    connect(buttons_,&QDialogButtonBox::rejected,this,&ConstructionPanel::reject);
    connect(name,&QLineEdit::textEdited,this,[this] {if(changed) changed();});
}
Construction::Request ConstructionPanel::request() const {
    Construction::Request result;result.method=method_;
    for(int i=0;i<3;++i) {if(vector[i]) result.vector.SetCoord(i+1,vector[i]->value());if(angles[i]) result.angles.SetCoord(i+1,angles[i]->value());}
    if(value) result.value=value->value();result.reverse=reverse->isChecked();
    result.alternativeBisector=midplaneSolution && midplaneSolution->currentIndex()==1;return result;
}
void ConstructionPanel::setValid(bool valid,const QString& message) {
    buttons_->button(QDialogButtonBox::Ok)->setEnabled(valid);status_->setText(message);
}
void ConstructionPanel::setVector(const gp_Vec& v) {
    for(int i=0;i<3;++i) if(vector[i]) {QSignalBlocker blocker(vector[i]);vector[i]->setValue(v.Coord(i+1));}
    if(changed) changed();
}
void ConstructionPanel::setValue(double v) {if(value) {QSignalBlocker blocker(value);value->setValue(v);}if(changed) changed();}
void ConstructionPanel::reject() {if(cancelled) cancelled();}

void ConstructionPanel::finishEditing() {
    for(auto* input:vector) if(input) {QSignalBlocker blocker(input);input->interpretText();}
    for(auto* input:angles) if(input) {QSignalBlocker blocker(input);input->interpretText();}
    if(value) {QSignalBlocker blocker(value);value->interpretText();}
}
