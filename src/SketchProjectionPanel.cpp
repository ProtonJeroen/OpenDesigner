#include "SketchProjectionPanel.h"
#include <QCloseEvent>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

SketchProjectionPanel::SketchProjectionPanel(const QString& workplaneName,QWidget* parent):QDialog(parent,Qt::Tool) {
    setObjectName("sketchProjectionPanel");setWindowTitle(tr("Project 3D Geometry"));setModal(false);
    auto* layout=new QVBoxLayout(this);
    auto* form=new QFormLayout;layout->addLayout(form);
    auto* target=new QLabel(workplaneName,this);target->setObjectName("projectionWorkplane");target->setTextFormat(Qt::PlainText);target->setWordWrap(true);
    form->addRow(tr("Target Workplane"),target);
    mode=new QComboBox(this);mode->setObjectName("projectionMode");
    for(const auto& item:{std::pair{tr("Vertex"),Mode::Vertex},std::pair{tr("Edge"),Mode::Edge},std::pair{tr("Face"),Mode::Face},std::pair{tr("Mixed"),Mode::Mixed}})
        mode->addItem(item.first,int(item.second));
    mode->setCurrentIndex(3);form->addRow(tr("Source selection"),mode);
    auto* help=new QLabel(tr("Click replaces; Ctrl+click adds/removes. Mixed accepts vertices, edges and faces. Blue geometry is a temporary live preview."),this);
    help->setWordWrap(true);help->setMinimumWidth(350);help->setMaximumWidth(440);layout->addWidget(help);
    sources=new QListWidget(this);sources->setObjectName("projectionSources");sources->setSelectionMode(QAbstractItemView::ExtendedSelection);sources->setFixedHeight(85);layout->addWidget(sources);
    auto* row=new QHBoxLayout;layout->addLayout(row);
    remove=new QPushButton(tr("Remove selected sources"),this);remove->setObjectName("projectionRemove");row->addWidget(remove);
    clear=new QPushButton(tr("Clear sources"),this);clear->setObjectName("projectionClear");row->addWidget(clear);
    counts_=new QLabel(this);counts_->setObjectName("projectionCounts");layout->addWidget(counts_);
    status_=new QLabel(this);status_->setObjectName("projectionStatus");status_->setTextFormat(Qt::PlainText);status_->setWordWrap(true);status_->setMaximumWidth(440);layout->addWidget(status_);
    diagnostics_=new QPlainTextEdit(this);diagnostics_->setObjectName("projectionDiagnostics");diagnostics_->setReadOnly(true);diagnostics_->setFixedHeight(85);diagnostics_->setPlaceholderText(tr("Projection diagnostics appear here."));layout->addWidget(diagnostics_);
    buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,this);layout->addWidget(buttons);
    for(auto* button:findChildren<QPushButton*>()) {button->setAutoDefault(false);button->setDefault(false);}
    connect(mode,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{if(modeChanged) modeChanged();});
    connect(clear,&QPushButton::clicked,this,[this]{if(clearSources) clearSources();});
    connect(remove,&QPushButton::clicked,this,[this]{if(removeSources) removeSources();});
    connect(sources,&QListWidget::itemSelectionChanged,this,[this]{remove->setEnabled(!sources->selectedItems().empty());});
    connect(buttons,&QDialogButtonBox::accepted,this,[this]{if(confirmed) confirmed();});
    connect(buttons,&QDialogButtonBox::rejected,this,&SketchProjectionPanel::reject);
    setResult({},0,tr("Choose 3D sources in the viewport."),{},false);
}
void SketchProjectionPanel::setResult(const QStringList& labels,int entities,const QString& message,const QStringList& diagnostics,bool valid) {
    sources->clear();sources->addItems(labels);clear->setEnabled(!labels.empty());remove->setEnabled(false);
    counts_->setText(tr("%1 selected sources — %2 Construction entities").arg(labels.size()).arg(entities));
    status_->setText(message);diagnostics_->setPlainText(diagnostics.join('\n'));buttons->button(QDialogButtonBox::Ok)->setEnabled(valid);
    // CommandPanelPositioner observes layout requests and sizes the natural form.
    // Do not show()/move()/adjustSize() on each selection or preview update.
}
void SketchProjectionPanel::reject() {if(cancelled) cancelled();else QDialog::reject();}
void SketchProjectionPanel::closeEvent(QCloseEvent* event) {event->ignore();reject();}
