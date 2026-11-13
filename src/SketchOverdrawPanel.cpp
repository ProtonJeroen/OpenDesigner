#include "SketchOverdrawPanel.h"
#include <QLabel>
#include <QComboBox>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QVBoxLayout>
#include <QSignalBlocker>
SketchOverdrawPanel::SketchOverdrawPanel(const QString& planeName,QWidget* parent):QDialog(parent,Qt::Tool) {
    setObjectName("overdrawPanel");setWindowTitle(tr("Overdraw — %1 — Geometry").arg(planeName));setModal(false);
    auto* layout=new QVBoxLayout(this);
    prompt_=new QLabel(this);prompt_->setObjectName("overdrawPrompt");prompt_->setWordWrap(true);prompt_->setMinimumWidth(310);layout->addWidget(prompt_);
    notice_=new QLabel(this);notice_->setObjectName("overdrawNotice");notice_->setWordWrap(true);layout->addWidget(notice_);
    candidates=new QComboBox(this);candidates->setObjectName("overdrawCandidates");candidates->setMinimumContentsLength(32);layout->addWidget(candidates);
    chooseButton=new QPushButton(tr("Choose green piece"),this);chooseButton->setObjectName("overdrawChoose");layout->addWidget(chooseButton);
    backButton=new QPushButton(tr("Back one piece"),this);backButton->setObjectName("overdrawBack");layout->addWidget(backButton);
    newStartButton=new QPushButton(tr("New start (keep chosen pieces)"),this);newStartButton->setObjectName("overdrawNewStart");layout->addWidget(newStartButton);
    count_=new QLabel(this);layout->addWidget(count_);
    buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,this);layout->addWidget(buttons);
    for(auto* b:{chooseButton,backButton,newStartButton,buttons->button(QDialogButtonBox::Ok),buttons->button(QDialogButtonBox::Cancel)}) {b->setAutoDefault(false);b->setDefault(false);}
    connect(candidates,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int i){if(candidateChanged) candidateChanged(i-1);});
    connect(chooseButton,&QPushButton::clicked,this,[this]{if(choose) choose();});
    connect(backButton,&QPushButton::clicked,this,[this]{if(back) back();});
    connect(newStartButton,&QPushButton::clicked,this,[this]{if(newStart) newStart();});
    connect(buttons,&QDialogButtonBox::accepted,this,[this]{if(confirmed) confirmed();});
    connect(buttons,&QDialogButtonBox::rejected,this,&SketchOverdrawPanel::reject);
}
void SketchOverdrawPanel::reject() {if(cancelled) cancelled();else QDialog::reject();}
void SketchOverdrawPanel::update(const QString& prompt,const QString& notice,const QStringList& labels,int selected,int chosen,bool hasStart,bool valid) {
    prompt_->setText(prompt);notice_->setText(notice);notice_->setVisible(!notice.isEmpty());
    const QSignalBlocker block(candidates);candidates->clear();candidates->addItem(labels.size()>1?tr("Ambiguous — explicitly choose a piece…"):tr("Move along a construction curve…"));
    candidates->addItems(labels);candidates->setCurrentIndex(selected+1);candidates->setEnabled(!labels.empty());
    chooseButton->setText(hasStart?tr("Choose green piece"):tr("Use start point"));
    chooseButton->setEnabled(selected>=0);backButton->setEnabled(chosen>0 || hasStart);newStartButton->setEnabled(hasStart);
    count_->setText(tr("%1 piece(s) chosen. OK adds new portions and reuses existing Profile geometry.").arg(chosen));
    buttons->button(QDialogButtonBox::Ok)->setEnabled(valid);
    adjustSize();
}
