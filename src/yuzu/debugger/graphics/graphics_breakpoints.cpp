// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <QLabel>
#include <QMetaType>
#include <QPushButton>
#include <QTreeView>
#include <QVBoxLayout>
#include "common/assert.h"
#include "yuzu/debugger/graphics/graphics_breakpoints.h"
#include "yuzu/debugger/graphics/graphics_breakpoints_p.h"

BreakPointModel::BreakPointModel(std::shared_ptr<Tegra::DebugContext> debug_context,
                                 QObject* parent)
    : QAbstractListModel(parent), context_weak(debug_context),
      at_breakpoint(debug_context->at_breakpoint),
      active_breakpoint(debug_context->active_breakpoint) {}

int BreakPointModel::columnCount(const QModelIndex& parent) const {
    return 1;
}

int BreakPointModel::rowCount(const QModelIndex& parent) const {
    return static_cast<int>(Tegra::DebugContext::Event::NumEvents);
}

QVariant BreakPointModel::data(const QModelIndex& index, int role) const {
    const auto event = static_cast<Tegra::DebugContext::Event>(index.row());

    switch (role) {
    case Qt::DisplayRole: {
        if (index.column() == 0) {
            static const std::map<Tegra::DebugContext::Event, QString> map = {
                {Tegra::DebugContext::Event::MaxwellCommandLoaded, tr("Maxwell command loaded")},
                {Tegra::DebugContext::Event::MaxwellCommandProcessed,
                 tr("Maxwell command processed")},
                {Tegra::DebugContext::Event::IncomingPrimitiveBatch,
                 tr("Incoming primitive batch")},
                {Tegra::DebugContext::Event::FinishedPrimitiveBatch,
                 tr("Finished primitive batch")},
            };

            DEBUG_ASSERT(map.size() == static_cast<size_t>(Tegra::DebugContext::Event::NumEvents));
            return (map.find(event) != map.end()) ? map.at(event) : QString();
        }

        break;
    }

    case Qt::CheckStateRole: {
        if (index.column() == 0)
            return data(index, Role_IsEnabled).toBool() ? Qt::Checked : Qt::Unchecked;
        break;
    }

    case Qt::BackgroundRole: {
        if (at_breakpoint && index.row() == static_cast<int>(active_breakpoint)) {
            return QBrush(QColor(0xE0, 0xE0, 0x10));
        }
        break;
    }

    case Role_IsEnabled: {
        auto context = context_weak.lock();
        return context && context->breakpoints[(int)event].enabled;
    }

    default:
        break;
    }
    return QVariant();
}

Qt::ItemFlags BreakPointModel::flags(const QModelIndex& index) const {
    if (!index.isValid())
        return 0;

    Qt::ItemFlags flags = Qt::ItemIsEnabled;
    if (index.column() == 0)
        flags |= Qt::ItemIsUserCheckable;
    return flags;
}

bool BreakPointModel::setData(const QModelIndex& index, const QVariant& value, int role) {
    const auto event = static_cast<Tegra::DebugContext::Event>(index.row());

    switch (role) {
    case Qt::CheckStateRole: {
        if (index.column() != 0)
            return false;

        auto context = context_weak.lock();
        if (!context)
            return false;

        context->breakpoints[(int)event].enabled = value == Qt::Checked;
        QModelIndex changed_index = createIndex(index.row(), 0);
        emit dataChanged(changed_index, changed_index);
        return true;
    }
    }

    return false;
}

void BreakPointModel::OnBreakPointHit(Tegra::DebugContext::Event event) {
    auto context = context_weak.lock();
    if (!context)
        return;

    active_breakpoint = context->active_breakpoint;
    at_breakpoint = context->at_breakpoint;
    emit dataChanged(createIndex(static_cast<int>(event), 0),
                     createIndex(static_cast<int>(event), 0));
}

void BreakPointModel::OnResumed() {
    auto context = context_weak.lock();
    if (!context)
        return;

    at_breakpoint = context->at_breakpoint;
    emit dataChanged(createIndex(static_cast<int>(active_breakpoint), 0),
                     createIndex(static_cast<int>(active_breakpoint), 0));
    active_breakpoint = context->active_breakpoint;
}

GraphicsBreakPointsWidget::GraphicsBreakPointsWidget(
    std::shared_ptr<Tegra::DebugContext> debug_context, QWidget* parent)
    : QDockWidget(tr("Maxwell Breakpoints"), parent), Tegra::DebugContext::BreakPointObserver(
                                                          debug_context) {
    setObjectName("TegraBreakPointsWidget");

    status_text = new QLabel(tr("Emulation running"));
    resume_button = new QPushButton(tr("Resume"));
    resume_button->setEnabled(false);

    breakpoint_model = new BreakPointModel(debug_context, this);
    breakpoint_list = new QTreeView;
    breakpoint_list->setRootIsDecorated(false);
    breakpoint_list->setHeaderHidden(true);
    breakpoint_list->setModel(breakpoint_model);

    qRegisterMetaType<Tegra::DebugContext::Event>("Tegra::DebugContext::Event");

    connect(breakpoint_list, SIGNAL(doubleClicked(const QModelIndex&)), this,
            SLOT(OnItemDoubleClicked(const QModelIndex&)));

    connect(resume_button, SIGNAL(clicked()), this, SLOT(OnResumeRequested()));

    connect(this, SIGNAL(BreakPointHit(Tegra::DebugContext::Event, void*)), this,
            SLOT(OnBreakPointHit(Tegra::DebugContext::Event, void*)), Qt::BlockingQueuedConnection);
    connect(this, SIGNAL(Resumed()), this, SLOT(OnResumed()));

    connect(this, SIGNAL(BreakPointHit(Tegra::DebugContext::Event, void*)), breakpoint_model,
            SLOT(OnBreakPointHit(Tegra::DebugContext::Event)), Qt::BlockingQueuedConnection);
    connect(this, SIGNAL(Resumed()), breakpoint_model, SLOT(OnResumed()));

    connect(this, SIGNAL(BreakPointsChanged(const QModelIndex&, const QModelIndex&)),
            breakpoint_model, SIGNAL(dataChanged(const QModelIndex&, const QModelIndex&)));

    QWidget* main_widget = new QWidget;
    auto main_layout = new QVBoxLayout;
    {
        auto sub_layout = new QHBoxLayout;
        sub_layout->addWidget(status_text);
        sub_layout->addWidget(resume_button);
        main_layout->addLayout(sub_layout);
    }
    main_layout->addWidget(breakpoint_list);
    main_widget->setLayout(main_layout);

    setWidget(main_widget);
}

void GraphicsBreakPointsWidget::OnMaxwellBreakPointHit(Event event, void* data) {
    // Process in GUI thread
    emit BreakPointHit(event, data);
}

void GraphicsBreakPointsWidget::OnBreakPointHit(Tegra::DebugContext::Event event, void* data) {
    status_text->setText(tr("Emulation halted at breakpoint"));
    resume_button->setEnabled(true);
}

void GraphicsBreakPointsWidget::OnMaxwellResume() {
    // Process in GUI thread
    emit Resumed();
}

void GraphicsBreakPointsWidget::OnResumed() {
    status_text->setText(tr("Emulation running"));
    resume_button->setEnabled(false);
}

void GraphicsBreakPointsWidget::OnResumeRequested() {
    if (auto context = context_weak.lock())
        context->Resume();
}

void GraphicsBreakPointsWidget::OnItemDoubleClicked(const QModelIndex& index) {
    if (!index.isValid())
        return;

    QModelIndex check_index = breakpoint_list->model()->index(index.row(), 0);
    QVariant enabled = breakpoint_list->model()->data(check_index, Qt::CheckStateRole);
    QVariant new_state = Qt::Unchecked;
    if (enabled == Qt::Unchecked)
        new_state = Qt::Checked;
    breakpoint_list->model()->setData(check_index, new_state, Qt::CheckStateRole);
}
