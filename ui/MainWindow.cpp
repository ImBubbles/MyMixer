#include "MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QBoxLayout>
#include <QCheckBox>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScrollArea>
#include <QStatusBar>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QToolBar>
#include <QToolButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <cmath>
#include <deque>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>

#include "../config/Config.h"
#include "../environment/Environment.h"
#include "../logger/Log.h"
#include "../pipewire/PipeWireContext.h"
#include "../pipewire/VirtualChannel.h"
#include "FlowLayout.h"

class StereoLevelGraph final : public QWidget {
public:
    explicit StereoLevelGraph(QWidget* parent = nullptr)
        : QWidget(parent), history_(90, {-60.0, -60.0}) {
        setMinimumHeight(140);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setAccessibleName("Stereo input level graph in decibels");
    }

    void addSample(const float left, const float right) {
        currentLeft_ = toDecibels(left);
        currentRight_ = toDecibels(right);
        history_.emplace_back(currentLeft_, currentRight_);
        while (history_.size() > 90) {
            history_.pop_front();
        }
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), QColor("#252a2d"));

        const QRectF graphRect(39.0, 24.0, std::max(1, width() - 48), std::max(1, height() - 43));
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor("#1e2325"));
        painter.drawRoundedRect(graphRect, 3.0, 3.0);

        QFont labelFont = painter.font();
        labelFont.setPointSize(8);
        painter.setFont(labelFont);
        const QColor labelColor("#a1b2ae");
        const std::array<int, 6> decibelTicks{0, -12, -24, -36, -48, -60};
        for (const int decibels : decibelTicks) {
            const qreal y = yForDecibels(decibels, graphRect);
            painter.setPen(QPen(QColor("#3c4548"), decibels == -60 ? 1.0 : 0.7));
            painter.drawLine(QPointF(graphRect.left(), y), QPointF(graphRect.right(), y));
            painter.setPen(labelColor);
            painter.drawText(QRectF(0.0, y - 7.0, 34.0, 14.0), Qt::AlignRight | Qt::AlignVCenter,
                             QString::number(decibels));
        }

        const QString leftText = QString("L  %1").arg(formatDecibels(currentLeft_));
        const QString rightText = QString("R  %1").arg(formatDecibels(currentRight_));
        painter.setPen(colorForDecibels(currentLeft_));
        painter.drawText(QRectF(0.0, 2.0, width() / 2.0, 18.0), Qt::AlignLeft | Qt::AlignVCenter, leftText);
        painter.setPen(colorForDecibels(currentRight_));
        painter.drawText(QRectF(width() / 2.0, 2.0, width() / 2.0 - 2.0, 18.0), Qt::AlignRight | Qt::AlignVCenter, rightText);

        if (history_.size() < 2) {
            return;
        }
        const qreal xStep = graphRect.width() / static_cast<qreal>(history_.size() - 1);
        for (int channel = 0; channel < 2; ++channel) {
            for (std::size_t index = 1; index < history_.size(); ++index) {
                const double previous = channel == 0 ? history_[index - 1].first : history_[index - 1].second;
                const double current = channel == 0 ? history_[index].first : history_[index].second;
                const QPointF start(graphRect.left() + static_cast<qreal>(index - 1) * xStep,
                                    yForDecibels(previous, graphRect));
                const QPointF end(graphRect.left() + static_cast<qreal>(index) * xStep,
                                  yForDecibels(current, graphRect));
                QPen tracePen(colorForDecibels(std::max(previous, current)), channel == 0 ? 2.0 : 1.6);
                if (channel == 1) {
                    tracePen.setStyle(Qt::DashLine);
                }
                painter.setPen(tracePen);
                painter.drawLine(start, end);
            }
        }
    }

private:
    std::deque<std::pair<double, double>> history_;
    double currentLeft_ = -60.0;
    double currentRight_ = -60.0;

    static double toDecibels(const float level) {
        if (!std::isfinite(level) || level <= 0.0f) {
            return -60.0;
        }
        return std::clamp(20.0 * std::log10(static_cast<double>(level)), -60.0, 0.0);
    }

    static QString formatDecibels(const double decibels) {
        return decibels <= -60.0 ? QString("-inf dB") : QString("%1 dB").arg(decibels, 0, 'f', 1);
    }

    static qreal yForDecibels(const double decibels, const QRectF& graphRect) {
        const double normalized = (std::clamp(decibels, -60.0, 0.0) + 60.0) / 60.0;
        return graphRect.bottom() - normalized * graphRect.height();
    }

    static QColor colorForDecibels(const double decibels) {
        const QColor green("#39c878");
        const QColor yellow("#f0d84a");
        const QColor red("#f04e4e");
        if (decibels <= -18.0) {
            return green;
        }
        if (decibels < -6.0) {
            const qreal amount = (decibels + 18.0) / 12.0;
            return QColor::fromRgbF(green.redF() + (yellow.redF() - green.redF()) * amount,
                                    green.greenF() + (yellow.greenF() - green.greenF()) * amount,
                                    green.blueF() + (yellow.blueF() - green.blueF()) * amount);
        }
        const qreal amount = std::clamp((decibels + 6.0) / 6.0, 0.0, 1.0);
        return QColor::fromRgbF(yellow.redF() + (red.redF() - yellow.redF()) * amount,
                                yellow.greenF() + (red.greenF() - yellow.greenF()) * amount,
                                yellow.blueF() + (red.blueF() - yellow.blueF()) * amount);
    }
};

MainWindow::MainWindow(PipeWireContext& context)
    : context_(context), operationWatcher_(new QFutureWatcher<UiOperationResult>(this)) {
    setWindowTitle("MyMixer");
    resize(1040, 680);
    setStyleSheet(
        "QMainWindow, QDialog, QMessageBox { background: #252a2d; color: #e5eeeb; }"
        "QWidget { color: #e5eeeb; }"
        "QToolBar, QStatusBar { background: #303639; border: 0; border-bottom: 1px solid #454d50; spacing: 6px; padding: 6px; }"
        "QStatusBar { border-top: 1px solid #454d50; border-bottom: 0; }"
        "QScrollArea, QAbstractScrollArea::viewport, QWidget#channelCanvas { background: #252a2d; border: 0; }"
        "QFrame#channelColumn { background: #303639; border: 1px solid #485154; border-radius: 6px; }"
        "QFrame#virtualChannel { background: transparent; border: 0; }"
        "QLabel#channelName { font-size: 16px; font-weight: 600; color: #f0f5f3; }"
        "QLabel#technicalName { color: #a1b2ae; font-family: monospace; }"
        "QLabel#mutedLabel { color: #a1b2ae; }"
        "QFrame#inputMeter { background: #252a2d; border: 1px solid #485154; border-radius: 4px; }"
        "QLabel#meterChannelLabel { color: #a1b2ae; font-size: 11px; font-weight: 600; }"
        "QToolBar QToolButton, QPushButton { background: #394245; color: #e5eeeb; border: 1px solid #505b5e; border-radius: 4px; padding: 6px 10px; }"
        "QToolBar QToolButton:hover, QPushButton:hover { background: #465154; border-color: #36b9a5; }"
        "QToolBar QToolButton:pressed, QPushButton:pressed { background: #1e887a; border-color: #36b9a5; }"
        "QToolBar QToolButton:focus, QPushButton:focus, QLineEdit:focus { border: 1px solid #36b9a5; }"
        "QToolBar QToolButton#primaryToolbarAction { background: #167f73; color: #f3fffc; border-color: #36b9a5; font-weight: 600; }"
        "QToolBar QToolButton#primaryToolbarAction:hover { background: #1b9989; }"
        "QToolButton { text-align: left; }"
        "QPushButton:disabled { color: #7f8a8d; background: #2b3033; border-color: #3e4649; }"
        "QPushButton#primaryAction { background: #167f73; color: #f3fffc; border: 1px solid #36b9a5; font-weight: 600; }"
        "QPushButton#primaryAction:hover { background: #1b9989; }"
        "QLineEdit { background: #1e2325; color: #e5eeeb; border: 1px solid #505b5e; border-radius: 4px; padding: 6px; selection-background-color: #168f81; }"
        "QMenu { background: #303639; color: #e5eeeb; border: 1px solid #505b5e; padding: 4px; }"
        "QMenu::item { padding: 6px 28px 6px 8px; border-radius: 3px; }"
        "QMenu::item:selected { background: #414b4e; }"
        "QMenu::item:checked { background: #164b46; color: #baf4e8; }"
        "QMenu::indicator { width: 14px; height: 14px; }"
        "QMenu::indicator:checked { background: #36b9a5; border: 1px solid #73ddc9; border-radius: 2px; }"
        "QScrollBar:vertical { background: #252a2d; width: 12px; margin: 0; }"
        "QScrollBar::handle:vertical { background: #505b5e; min-height: 28px; border-radius: 5px; }"
        "QScrollBar::handle:vertical:hover { background: #36b9a5; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        "QToolTip { background: #394245; color: #f0f5f3; border: 1px solid #36b9a5; padding: 4px; }"
    );

    toolbar_ = addToolBar("Mixer");
    toolbar_->setMovable(false);
    toolbar_->addWidget(new QLabel("Config"));
    QAction* importAction = toolbar_->addAction("Import");
    QAction* exportAction = toolbar_->addAction("Export");
    QAction* setDefaultAction = toolbar_->addAction("Set Default");
    setDefaultAction->setToolTip("Choose the config file loaded at startup");
    toolbar_->addSeparator();
    QAction* autostartAction = toolbar_->addAction("");
    autostartAction->setCheckable(true);
    const auto refreshAutostart = [autostartAction] {
        const bool enabled = Environment::isAutostartEnabled();
        autostartAction->setChecked(enabled);
        autostartAction->setText(enabled ? "\u2714 Autostart" : "\u2716 Autostart");
        autostartAction->setToolTip(enabled ? "Autostart is enabled" : "Autostart is disabled");
    };
    refreshAutostart();
    toolbar_->addSeparator();
    QAction* quitAction = toolbar_->addAction("Quit");
    auto* content = new QWidget(this);
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);
    auto* scrollArea = new QScrollArea(content);
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    columnsWidget_ = new QWidget(scrollArea);
    columnsWidget_->setObjectName("channelCanvas");
    auto* canvasLayout = new QVBoxLayout(columnsWidget_);
    canvasLayout->setContentsMargins(16, 16, 16, 16);
    canvasLayout->setSpacing(0);
    workspaceLayout_ = new QBoxLayout(QBoxLayout::LeftToRight);
    workspaceLayout_->setSpacing(12);
    hardwareHost_ = new QWidget(columnsWidget_);
    hardwareHost_->setFixedWidth(260);
    auto* hardwareHostLayout = new QVBoxLayout(hardwareHost_);
    hardwareHostLayout->setContentsMargins(0, 0, 0, 0);
    virtualHost_ = new QFrame(columnsWidget_);
    virtualHost_->setObjectName("channelColumn");
    virtualHost_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto* virtualHostLayout = new QVBoxLayout(virtualHost_);
    virtualHostLayout->setContentsMargins(14, 12, 14, 14);
    virtualHostLayout->setSpacing(14);
    auto* virtualHeader = new QWidget(virtualHost_);
    auto* virtualHeaderLayout = new QHBoxLayout(virtualHeader);
    virtualHeaderLayout->setContentsMargins(0, 0, 0, 0);
    auto* virtualTitle = new QLabel("Virtual", virtualHeader);
    virtualTitle->setObjectName("channelName");
    virtualHeaderLayout->addWidget(virtualTitle);
    virtualHeaderLayout->addStretch();
    auto* addAction = new QPushButton("Add channel", virtualHeader);
    addAction->setObjectName("primaryAction");
    virtualHeaderLayout->addWidget(addAction);
    virtualHostLayout->addWidget(virtualHeader);
    auto* virtualCards = new QWidget(virtualHost_);
    virtualCards->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    virtualLayout_ = new FlowLayout(virtualCards, 0, 12);
    virtualLayout_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    virtualHostLayout->addWidget(virtualCards);
    workspaceLayout_->addWidget(hardwareHost_, 0);
    workspaceLayout_->addWidget(virtualHost_, 1);
    workspaceLayout_->setAlignment(hardwareHost_, Qt::AlignTop);
    workspaceLayout_->setAlignment(virtualHost_, Qt::AlignTop);
    canvasLayout->addLayout(workspaceLayout_);
    canvasLayout->addStretch(1);
    scrollArea->setWidget(columnsWidget_);
    contentLayout->addWidget(scrollArea, 1);
    setCentralWidget(content);

    connect(addAction, &QPushButton::clicked, this, [this] { addChannel(); });
    connect(importAction, &QAction::triggered, this, [this] { importConfig(); });
    connect(exportAction, &QAction::triggered, this, [this] { exportConfig(); });
    connect(setDefaultAction, &QAction::triggered, this, [this] { setDefaultConfig(); });
    connect(autostartAction, &QAction::triggered, this, [this, refreshAutostart](bool checked) {
        if (!Environment::setAutostart(checked, QCoreApplication::applicationFilePath().toStdString())) {
            QMessageBox::warning(this, "Autostart failed", "Could not update the autostart entry.");
        }
        refreshAutostart();
    });
    connect(quitAction, &QAction::triggered, qApp, &QApplication::quit);
    connect(operationWatcher_, &QFutureWatcher<UiOperationResult>::finished, this, [this] {
        const UiOperationResult result = operationWatcher_->result();
        toolbar_->setEnabled(true);
        columnsWidget_->setEnabled(true);
        meterTimer_->start();
        statusBar()->clearMessage();
        if (operationCompletion_) {
            Completion completion = std::move(operationCompletion_);
            completion(result);
        } else if (!result.success && !result.message.isEmpty()) {
            QMessageBox::warning(this, "Operation failed", result.message);
        }
    });

    meterTimer_ = new QTimer(this);
    meterTimer_->setInterval(33);
    connect(meterTimer_, &QTimer::timeout, this, [this] { updateMeters(); });
    meterTimer_->start();

    trayIcon_ = new QSystemTrayIcon(style()->standardIcon(QStyle::SP_ComputerIcon), this);
    trayIcon_->setToolTip("MyMixer is running in the background");
    auto* trayMenu = new QMenu(this);
    QAction* showAction = trayMenu->addAction("Show MyMixer");
    QAction* trayQuitAction = trayMenu->addAction("Quit");
    trayIcon_->setContextMenu(trayMenu);
    connect(showAction, &QAction::triggered, this, [this] {
        showNormal();
        raise();
        activateWindow();
    });
    connect(trayQuitAction, &QAction::triggered, qApp, &QApplication::quit);
    connect(trayIcon_, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
            showNormal();
            raise();
            activateWindow();
        }
    });
    trayIcon_->show();

    refreshChannels();
}

MainWindow::~MainWindow() {
    meterTimer_->stop();
    if (operationWatcher_->isRunning()) {
        operationWatcher_->future().waitForFinished();
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    hide();
    event->ignore();
}

void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    if (workspaceLayout_ != nullptr) {
        const bool stacked = event->size().width() < 1000;
        workspaceLayout_->setDirection(stacked ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
        const Qt::Alignment alignment = stacked ? Qt::Alignment() : Qt::AlignTop;
        workspaceLayout_->setAlignment(hardwareHost_, alignment);
        workspaceLayout_->setAlignment(virtualHost_, alignment);
    }
}

void MainWindow::refreshChannels() {
    meters_.clear();
    while (QLayoutItem* item = virtualLayout_->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            delete widget;
        }
        delete item;
    }
    delete hardwareColumn_;
    hardwareColumn_ = nullptr;

    const std::vector<VirtualChannel*> channels = context_.getChannels();
    const std::vector<HardwareAudioDevice> hardwareDevices = context_.getHardwareDevices();
    const std::vector<AudioConnection> activeHardwareConnections = context_.getHardwareConnections();
    const auto deviceExists = [&hardwareDevices](const AudioEndpoint& endpoint) {
        return !endpoint.isHardware || std::any_of(hardwareDevices.begin(), hardwareDevices.end(), [&endpoint](const HardwareAudioDevice& device) {
            return device.name == endpoint.name && device.leftPort != PW_ID_ANY && device.rightPort != PW_ID_ANY;
        });
    };
    selectedHardware_.erase(std::remove_if(selectedHardware_.begin(), selectedHardware_.end(), [&deviceExists](const AudioEndpoint& endpoint) {
        return !deviceExists(endpoint);
    }), selectedHardware_.end());
    for (const AudioConnection& connection : activeHardwareConnections) {
        for (const AudioEndpoint* endpoint : {&connection.source, &connection.destination}) {
            if (endpoint->isHardware && deviceExists(*endpoint) &&
                std::none_of(selectedHardware_.begin(), selectedHardware_.end(), [endpoint](const AudioEndpoint& selected) {
                    return selected.name == endpoint->name;
                })) {
                selectedHardware_.push_back(*endpoint);
            }
        }
    }

    const auto routeIsActive = [this, &channels, &activeHardwareConnections](const AudioEndpoint& source, const AudioEndpoint& destination) {
        if (!source.isHardware && !destination.isHardware) {
            const auto channel = std::find_if(channels.begin(), channels.end(), [&source](const VirtualChannel* candidate) {
                return candidate != nullptr && candidate->name == source.name;
            });
            return channel != channels.end() && std::any_of((*channel)->outputLinks.begin(), (*channel)->outputLinks.end(), [&destination](const auto& link) {
                return link != nullptr && link->to == destination.name;
            });
        }
        return std::any_of(activeHardwareConnections.begin(), activeHardwareConnections.end(), [&source, &destination](const AudioConnection& connection) {
            return connection.source.name == source.name && connection.source.isHardware == source.isHardware &&
                   connection.destination.name == destination.name && connection.destination.isHardware == destination.isHardware;
        });
    };

    const auto addConnectionChecklist = [this, &routeIsActive, &channels, &hardwareDevices](QWidget* parent, const QString& title,
                                                               const AudioEndpoint& fixedEndpoint,
                                                               const std::vector<AudioEndpoint>& candidates,
                                                               const bool fixedIsSource) {
        auto* checklistWidget = new QWidget(parent);
        auto* checklistLayout = new QVBoxLayout(checklistWidget);
        checklistLayout->setContentsMargins(0, 0, 0, 0);
        checklistLayout->setSpacing(6);
        auto* button = new QToolButton(checklistWidget);
        int connectedCount = 0;
        for (const AudioEndpoint& candidate : candidates) {
            const AudioEndpoint& source = fixedIsSource ? fixedEndpoint : candidate;
            const AudioEndpoint& destination = fixedIsSource ? candidate : fixedEndpoint;
            connectedCount += routeIsActive(source, destination) ? 1 : 0;
        }
        button->setText(QString("%1 (%2)").arg(title).arg(connectedCount));
        button->setCheckable(true);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        checklistLayout->addWidget(button);
        auto* options = new QFrame(checklistWidget);
        options->setObjectName("routeChecklist");
        auto* optionsLayout = new QVBoxLayout(options);
        optionsLayout->setContentsMargins(8, 6, 8, 6);
        optionsLayout->setSpacing(4);
        checklistLayout->addWidget(options);

        const std::string checklistKey = fixedEndpoint.name + (fixedEndpoint.isHardware ? "|hardware|" : "|virtual|") +
            title.toStdString() + (fixedIsSource ? "|source" : "|destination");
        button->setChecked(expandedChecklists_.contains(checklistKey));
        options->setVisible(button->isChecked());
        connect(button, &QToolButton::toggled, checklistWidget, [this, options, checklistKey](const bool expanded) {
            options->setVisible(expanded);
            if (expanded) {
                expandedChecklists_.insert(checklistKey);
            } else {
                expandedChecklists_.erase(checklistKey);
            }
        });

        for (const AudioEndpoint& candidate : candidates) {
            const AudioEndpoint source = fixedIsSource ? fixedEndpoint : candidate;
            const AudioEndpoint destination = fixedIsSource ? candidate : fixedEndpoint;
            QString label;
            if (candidate.isHardware) {
                const auto device = std::find_if(hardwareDevices.begin(), hardwareDevices.end(), [&candidate](const HardwareAudioDevice& entry) {
                    return entry.name == candidate.name;
                });
                label = device == hardwareDevices.end() ? QString::fromStdString(candidate.name)
                                                        : QString::fromStdString(device->description);
            } else {
                const auto channel = std::find_if(channels.begin(), channels.end(), [&candidate](const VirtualChannel* entry) {
                    return entry != nullptr && entry->name == candidate.name;
                });
                label = channel == channels.end() ? QString::fromStdString(candidate.name)
                                                  : QString::fromStdString((*channel)->description);
            }

            auto* checkBox = new QCheckBox(label, options);
            checkBox->setChecked(routeIsActive(source, destination));
            checkBox->setToolTip(QString::fromStdString(candidate.name));
            optionsLayout->addWidget(checkBox);
            connect(checkBox, &QCheckBox::toggled, this, [this, source, destination](const bool connected) {
                runOperation([this, source, destination, connected] {
                    bool success = false;
                    if (!source.isHardware && !destination.isHardware) {
                        const std::vector<VirtualChannel*> currentChannels = context_.getChannels();
                        const auto from = std::find_if(currentChannels.begin(), currentChannels.end(), [&source](const VirtualChannel* entry) {
                            return entry != nullptr && entry->name == source.name;
                        });
                        const auto to = std::find_if(currentChannels.begin(), currentChannels.end(), [&destination](const VirtualChannel* entry) {
                            return entry != nullptr && entry->name == destination.name;
                        });
                        success = from != currentChannels.end() && to != currentChannels.end() &&
                                  context_.setChannelConnection(*from, *to, connected);
                    } else {
                        success = context_.setAudioConnection(source, destination, connected);
                    }
                    if (!success) {
                        return UiOperationResult{false, QString("Could not %1 route %2 -> %3.")
                            .arg(connected ? "connect" : "disconnect")
                            .arg(QString::fromStdString(source.name), QString::fromStdString(destination.name)), {}};
                    }
                    return UiOperationResult{true, connected ? "Connection added." : "Connection removed.", {}};
                }, [this](const UiOperationResult& result) {
                    refreshChannels();
                    if (result.success) {
                        statusBar()->showMessage(result.message, 2500);
                    } else {
                        QMessageBox::warning(this, "Connection change failed", result.message);
                    }
                });
            });
        }
        optionsLayout->addStretch();
        return checklistWidget;
    };

    auto* hardwareColumn = new QFrame(hardwareHost_);
    hardwareColumn_ = hardwareColumn;
    hardwareColumn->setObjectName("channelColumn");
    hardwareColumn->setMinimumWidth(230);
    hardwareColumn->setMaximumWidth(280);
    hardwareColumn->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto* hardwareLayout = new QVBoxLayout(hardwareColumn);
    hardwareLayout->setContentsMargins(14, 12, 14, 14);
    hardwareLayout->setSpacing(14);
    auto* hardwareTitle = new QLabel("Hardware", hardwareColumn);
    hardwareTitle->setObjectName("channelName");
    hardwareLayout->addWidget(hardwareTitle);
    auto* addHardwareButton = new QPushButton("Add Hardware", hardwareColumn);
    addHardwareButton->setObjectName("primaryAction");
    connect(addHardwareButton, &QPushButton::clicked, this, [this] { addHardware(); });
    hardwareLayout->addWidget(addHardwareButton);

    for (const AudioEndpoint& endpoint : selectedHardware_) {
        const auto device = std::find_if(hardwareDevices.begin(), hardwareDevices.end(), [&endpoint](const HardwareAudioDevice& candidate) {
            return candidate.name == endpoint.name;
        });
        if (device == hardwareDevices.end()) {
            continue;
        }
        auto* deviceName = new QLabel(QString::fromStdString(device->description), hardwareColumn);
        deviceName->setWordWrap(true);
        hardwareLayout->addWidget(deviceName);
        auto* deviceType = new QLabel(device->isInput ? "Input device" : "Output device", hardwareColumn);
        deviceType->setObjectName("technicalName");
        hardwareLayout->addWidget(deviceType);
        auto* removeHardwareButton = new QPushButton("Remove device", hardwareColumn);
        connect(removeHardwareButton, &QPushButton::clicked, this, [this, endpoint, description = device->description, activeHardwareConnections] {
            const auto choice = QMessageBox::warning(
                this,
                "Remove hardware device",
                "Remove \"" + QString::fromStdString(description) + "\" and disconnect its mixer routes?",
                QMessageBox::Cancel | QMessageBox::Yes,
                QMessageBox::Cancel
            );
            if (choice != QMessageBox::Yes) {
                return;
            }

            runOperation([this, endpoint, activeHardwareConnections] {
                for (const AudioConnection& connection : activeHardwareConnections) {
                    const bool usesEndpoint =
                        (connection.source.isHardware && connection.source.name == endpoint.name) ||
                        (connection.destination.isHardware && connection.destination.name == endpoint.name);
                    if (usesEndpoint && !context_.setAudioConnection(connection.source, connection.destination, false)) {
                        return UiOperationResult{false, "Could not disconnect all routes for this hardware device.", {}};
                    }
                }

                return UiOperationResult{true, "Hardware device removed.", {}};
            }, [this, endpoint](const UiOperationResult& result) {
                if (result.success) {
                    selectedHardware_.erase(std::remove_if(selectedHardware_.begin(), selectedHardware_.end(), [&endpoint](const AudioEndpoint& selected) {
                        return selected.isHardware && selected.name == endpoint.name;
                    }), selectedHardware_.end());
                }
                refreshChannels();
                if (result.success) {
                    statusBar()->showMessage(result.message, 2500);
                } else {
                    QMessageBox::warning(this, "Hardware removal failed", result.message);
                }
            });
        });
        hardwareLayout->addWidget(removeHardwareButton);

        std::vector<AudioEndpoint> candidates;
        bool endpointIsSource = device->isInput;
        if (device->isInput) {
            for (const VirtualChannel* channel : channels) {
                if (channel != nullptr) {
                    candidates.push_back({channel->name, false});
                }
            }
            for (const AudioEndpoint& selected : selectedHardware_) {
                const auto other = std::find_if(hardwareDevices.begin(), hardwareDevices.end(), [&selected](const HardwareAudioDevice& candidate) {
                    return candidate.name == selected.name;
                });
                if (other != hardwareDevices.end() && !other->isInput) {
                    candidates.push_back(selected);
                }
            }
        } else {
            endpointIsSource = false;
            for (const VirtualChannel* channel : channels) {
                if (channel != nullptr) {
                    candidates.push_back({channel->name, false});
                }
            }
            for (const AudioEndpoint& selected : selectedHardware_) {
                const auto other = std::find_if(hardwareDevices.begin(), hardwareDevices.end(), [&selected](const HardwareAudioDevice& candidate) {
                    return candidate.name == selected.name;
                });
                if (other != hardwareDevices.end() && other->isInput) {
                    candidates.push_back(selected);
                }
            }
        }
        hardwareLayout->addWidget(addConnectionChecklist(hardwareColumn,
            endpointIsSource ? "Connect to" : "Connected from", endpoint, candidates, endpointIsSource));
    }
    hardwareLayout->addStretch();
    static_cast<QVBoxLayout*>(hardwareHost_->layout())->addWidget(hardwareColumn);

    for (const VirtualChannel* channel : channels) {
        if (channel == nullptr) {
            continue;
        }

        QWidget* virtualCards = virtualHost_->layout()->itemAt(1)->widget();
        auto* column = new QFrame(virtualCards);
        column->setObjectName("virtualChannel");
        column->setMinimumWidth(230);
        column->setMaximumWidth(280);
        column->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        auto* layout = new QVBoxLayout(column);

        auto* displayName = new QLabel(QString::fromStdString(channel->description), column);
        displayName->setObjectName("channelName");
        displayName->setWordWrap(true);
        layout->addWidget(displayName);

        auto* technicalName = new QLabel(QString::fromStdString(channel->name), column);
        technicalName->setObjectName("technicalName");
        layout->addWidget(technicalName);

        auto* meterModule = new QFrame(column);
        meterModule->setObjectName("inputMeter");
        meterModule->setFixedHeight(172);
        auto* meterLayout = new QVBoxLayout(meterModule);
        meterLayout->setContentsMargins(6, 5, 6, 5);
        auto* levelGraph = new StereoLevelGraph(meterModule);
        meterLayout->addWidget(levelGraph);
        layout->addWidget(meterModule);
        meters_.push_back({channel->name, levelGraph, 0.0f, 0.0f});

        auto* routeTitle = new QLabel("Connected to", column);
        layout->addWidget(routeTitle);
        bool hasRoutes = false;
        for (const auto& outputLink : channel->outputLinks) {
            if (outputLink == nullptr) {
                continue;
            }
            hasRoutes = true;
            QString destination = QString::fromStdString(outputLink->to);
            const auto destinationChannel = std::find_if(channels.begin(), channels.end(), [&outputLink](const VirtualChannel* candidate) {
                return candidate != nullptr && candidate->name == outputLink->to;
            });
            if (destinationChannel != channels.end()) {
                destination = QString::fromStdString((*destinationChannel)->description);
            }
            auto* routeLabel = new QLabel(destination, column);
            routeLabel->setWordWrap(true);
            layout->addWidget(routeLabel);
        }
        for (const AudioConnection& connection : activeHardwareConnections) {
            if (!connection.source.isHardware && connection.source.name == channel->name && connection.destination.isHardware) {
                const auto device = std::find_if(hardwareDevices.begin(), hardwareDevices.end(), [&connection](const HardwareAudioDevice& entry) {
                    return entry.name == connection.destination.name;
                });
                auto* routeLabel = new QLabel(device == hardwareDevices.end()
                    ? QString::fromStdString(connection.destination.name)
                    : QString::fromStdString(device->description), column);
                routeLabel->setWordWrap(true);
                layout->addWidget(routeLabel);
                hasRoutes = true;
            }
        }
        if (!hasRoutes) {
            auto* noRoutes = new QLabel("No connections", column);
            noRoutes->setObjectName("mutedLabel");
            layout->addWidget(noRoutes);
        }

        layout->addSpacing(8);
        std::vector<AudioEndpoint> destinations;
        for (const VirtualChannel* candidate : channels) {
            if (candidate != nullptr && candidate->name != channel->name) {
                destinations.push_back({candidate->name, false});
            }
        }
        for (const HardwareAudioDevice& device : hardwareDevices) {
            const AudioEndpoint endpoint{device.name, true};
            if (!device.isInput && std::any_of(selectedHardware_.begin(), selectedHardware_.end(), [&endpoint](const AudioEndpoint& selected) {
                    return selected.name == endpoint.name && selected.isHardware;
                })) {
                destinations.push_back(endpoint);
            }
        }
        layout->addWidget(addConnectionChecklist(column, "Connections", {channel->name, false}, destinations, true));
        auto* removeButton = new QPushButton("Remove channel", column);
        connect(removeButton, &QPushButton::clicked, this, [this, name = channel->name, display = channel->description] {
            const auto choice = QMessageBox::warning(
                this,
                "Remove channel",
                "Remove \"" + QString::fromStdString(display) + "\" (" + QString::fromStdString(name) + ") and all its connections?",
                QMessageBox::Cancel | QMessageBox::Yes,
                QMessageBox::Cancel
            );
            if (choice == QMessageBox::Yes) {
                removeChannel(name);
            }
        });
        layout->addWidget(removeButton);
        layout->addStretch();
        virtualLayout_->addWidget(column);
    }

    if (channels.empty()) {
        QWidget* virtualCards = virtualHost_->layout()->itemAt(1)->widget();
        auto* emptyState = new QLabel("No virtual channels yet. Add a channel to get started.", virtualCards);
        emptyState->setObjectName("emptyState");
        emptyState->setStyleSheet("color: #a1b2ae; font-size: 15px; padding: 8px;");
        virtualLayout_->addWidget(emptyState);
    }
}

void MainWindow::addHardware() {
    const std::vector<HardwareAudioDevice> devices = context_.getHardwareDevices();
    if (devices.empty()) {
        QMessageBox::information(this, "Add Hardware", "No physical PipeWire audio devices are currently available.");
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle("Add Hardware");
    auto* form = new QFormLayout(&dialog);
    auto* devicePicker = new QComboBox(&dialog);
    for (const HardwareAudioDevice& device : devices) {
        const bool alreadyAdded = std::any_of(selectedHardware_.begin(), selectedHardware_.end(), [&device](const AudioEndpoint& selected) {
            return selected.isHardware && selected.name == device.name;
        });
        if (alreadyAdded || device.leftPort == PW_ID_ANY || device.rightPort == PW_ID_ANY) {
            continue;
        }
        const QString direction = device.isInput ? "Input" : "Output";
        devicePicker->addItem(QString("%1  ·  %2").arg(direction, QString::fromStdString(device.description)),
                              QString::fromStdString(device.name));
    }
    if (devicePicker->count() == 0) {
        const bool hasReadyDevice = std::any_of(devices.begin(), devices.end(), [](const HardwareAudioDevice& device) {
            return device.leftPort != PW_ID_ANY && device.rightPort != PW_ID_ANY;
        });
        QMessageBox::information(this, "Add Hardware", hasReadyDevice
            ? "All available stereo hardware devices are already added."
            : "No stereo hardware ports are ready yet. Try again in a moment.");
        return;
    }
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText("Add device");
    buttons->button(QDialogButtonBox::Ok)->setObjectName("primaryAction");
    form->addRow("Input or output device", devicePicker);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const std::string selectedName = devicePicker->currentData().toString().toStdString();
    const auto selected = std::find_if(devices.begin(), devices.end(), [&selectedName](const HardwareAudioDevice& device) {
        return device.name == selectedName;
    });
    if (selected != devices.end() && std::none_of(selectedHardware_.begin(), selectedHardware_.end(), [selectedName](const AudioEndpoint& endpoint) {
            return endpoint.isHardware && endpoint.name == selectedName;
        })) {
        selectedHardware_.push_back({selectedName, true});
        refreshChannels();
    }
}

void MainWindow::updateMeters() {
    Log::drainAsync(); // flush any log messages queued by the realtime PipeWire thread
    const std::vector<VirtualChannel*> channels = context_.getChannels();
    for (const VirtualChannel* channel : channels) {
        const auto diagnostics = const_cast<VirtualChannel*>(channel)->consumeDiagnostics();
        if (diagnostics.missedDeadlines > 0) {
            Log::warning("Channel \"" + channel->name + "\" missed " +
                std::to_string(diagnostics.missedDeadlines) + " process deadline(s), max gap " +
                std::to_string(diagnostics.maxGapNs / 1'000'000) + "ms");
        }
    }
    for (MeterDisplay& meter : meters_) {
        const auto channel = std::find_if(channels.begin(), channels.end(), [&meter](const VirtualChannel* candidate) {
            return candidate != nullptr && candidate->name == meter.channelName;
        });
        const auto peaks = channel == channels.end()
            ? std::pair<float, float>{0.0f, 0.0f}
            : (*channel)->consumeInputPeaks();
        const float targetLeft = std::clamp(peaks.first, 0.0f, 1.0f);
        const float targetRight = std::clamp(peaks.second, 0.0f, 1.0f);
        meter.displayedLeft = targetLeft >= meter.displayedLeft ? targetLeft : std::max(targetLeft, meter.displayedLeft * 0.88f);
        meter.displayedRight = targetRight >= meter.displayedRight ? targetRight : std::max(targetRight, meter.displayedRight * 0.88f);
        meter.graph->addSample(meter.displayedLeft, meter.displayedRight);
    }
}

void MainWindow::removeChannel(const std::string& channelName) {
    runOperation([this, channelName] {
        if (!context_.removeChannel(channelName)) {
            return UiOperationResult{false, "The channel could not be removed.", {}};
        }
        return UiOperationResult{true, "Channel removed.", {}};
    }, [this](const UiOperationResult& result) {
        if (result.success) {
            refreshChannels();
            statusBar()->showMessage(result.message, 3500);
        } else {
            QMessageBox::warning(this, "Removal failed", result.message);
        }
    });
}

void MainWindow::addChannel() {
    QDialog dialog(this);
    dialog.setWindowTitle("Add virtual channel");
    auto* form = new QFormLayout(&dialog);
    auto* nameField = new QLineEdit(&dialog);
    auto* displayField = new QLineEdit(&dialog);
    auto* validation = new QLabel(&dialog);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
    QPushButton* createButton = buttons->addButton("Create", QDialogButtonBox::AcceptRole);
    createButton->setObjectName("primaryAction");
    createButton->setDefault(true);

    nameField->setPlaceholderText("music_output");
    displayField->setPlaceholderText("Music Output");
    validation->setWordWrap(true);
    validation->setStyleSheet("color: #ff8278;");
    form->addRow("PipeWire name", nameField);
    form->addRow("Display name", displayField);
    form->addRow(validation);
    form->addRow(buttons);

    const QRegularExpression namePattern("^[a-z][a-z0-9_-]{0,62}$");
    const auto validate = [this, nameField, displayField, validation, createButton, namePattern] {
        const QString name = nameField->text().trimmed();
        const QString displayName = displayField->text().trimmed();
        bool valid = namePattern.match(name).hasMatch() && !displayName.isEmpty();
        QString reason;
        if (!namePattern.match(name).hasMatch()) {
            reason = "Use lowercase ASCII letters, digits, hyphens, or underscores; start with a letter.";
        } else if (displayName.isEmpty()) {
            reason = "A display name is required.";
        }
        for (const VirtualChannel* channel : context_.getChannels()) {
            if (channel != nullptr && QString::fromStdString(channel->name).compare(name, Qt::CaseInsensitive) == 0) {
                valid = false;
                reason = "That PipeWire name is already in use.";
                break;
            }
        }
        validation->setText(reason);
        createButton->setEnabled(valid);
    };
    connect(nameField, &QLineEdit::textChanged, &dialog, [validate] { validate(); });
    connect(displayField, &QLineEdit::textChanged, &dialog, [validate] { validate(); });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&dialog] { dialog.accept(); });
    validate();

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const std::string name = nameField->text().trimmed().toStdString();
    const std::string displayName = displayField->text().trimmed().toStdString();
    runOperation([this, name, displayName] {
        if (context_.createChannel(name, displayName) == nullptr) {
            return UiOperationResult{false, "PipeWire could not create the channel. Check the application log for details."};
        }
        return UiOperationResult{true, "Channel created."};
    }, [this](const UiOperationResult& result) {
        if (result.success) {
            refreshChannels();
            statusBar()->showMessage(result.message, 3500);
        } else {
            QMessageBox::warning(this, "Channel creation failed", result.message);
        }
    });
}

static QString chooseConfigFile(QWidget* parent, const QString& title, const QString& startPath) {
    QFileDialog dialog(parent, title, startPath.isEmpty() ? QDir::homePath() : startPath, "JSON config (*.json);;All files (*)");
    dialog.setOption(QFileDialog::DontUseNativeDialog);
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    dialog.setFileMode(QFileDialog::ExistingFile);
    dialog.setFilter(QDir::AllEntries | QDir::AllDirs | QDir::Hidden | QDir::NoDotAndDotDot);
    dialog.setLabelText(QFileDialog::FileName, "File name or full path:");
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty()) {
        return {};
    }
    return dialog.selectedFiles().front();
}

void MainWindow::importConfig() {
    const QString path = chooseConfigFile(this, "Import Config", QFileInfo(QString::fromStdString(Environment::settings.config)).absolutePath());
    if (path.isEmpty()) {
        return;
    }

    Config config;
    try {
        std::ifstream input(path.toStdString());
        if (!input.is_open()) {
            throw std::runtime_error("Could not open the selected file.");
        }
        const std::string json((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        config = ConfigHandler::fromJson(json);
    } catch (const std::exception& error) {
        QMessageBox::warning(this, "Import failed", QString::fromUtf8(error.what()));
        return;
    }

    runOperation([this, config = std::move(config)]() mutable {
        if (!ConfigHandler::loadConfig(&context_, std::move(config))) {
            return UiOperationResult{false, "The config could not be applied. The previous session was restored when possible."};
        }
        return UiOperationResult{true, "Config imported."};
    }, [this](const UiOperationResult& result) {
        if (result.success) {
            refreshChannels();
            statusBar()->showMessage(result.message, 3500);
        } else {
            QMessageBox::warning(this, "Import failed", result.message);
        }
    });
}

void MainWindow::setDefaultConfig() {
    const QString path = chooseConfigFile(this, "Select Default Config", QFileInfo(QString::fromStdString(Environment::settings.config)).absolutePath());
    if (path.isEmpty()) {
        return;
    }
    try {
        std::ifstream input(path.toStdString());
        if (!input.is_open()) {
            throw std::runtime_error("Could not open the selected file.");
        }
        const std::string json((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        ConfigHandler::fromJson(json);
    } catch (const std::exception& error) {
        QMessageBox::warning(this, "Set Default failed", QString::fromUtf8(error.what()));
        return;
    }
    if (!Environment::setDefaultConfigPath(path.toStdString())) {
        QMessageBox::warning(this, "Set Default failed", "Could not update settings.json.");
        return;
    }
    statusBar()->showMessage("Default config set to " + path, 3500);
}

void MainWindow::exportConfig() {
    QFileDialog dialog(this, "Export Config", QDir::homePath(), "JSON config (*.json)");
    dialog.setOption(QFileDialog::DontUseNativeDialog);
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setFilter(QDir::AllEntries | QDir::AllDirs | QDir::Hidden | QDir::NoDotAndDotDot);
    dialog.setLabelText(QFileDialog::FileName, "File name or full path:");
    dialog.selectFile("mymixer-config.json");
    QString path;
    if (dialog.exec() == QDialog::Accepted && !dialog.selectedFiles().isEmpty()) {
        path = dialog.selectedFiles().front();
    }
    if (path.isEmpty()) {
        return;
    }
    if (QFileInfo(path).suffix().compare("json", Qt::CaseInsensitive) != 0) {
        path += ".json";
    }

    runOperation([this, path] {
        try {
            const Config config = ConfigHandler::toConfig(&context_);
            const std::string json = ConfigHandler::toJson(config);
            std::ofstream output(path.toStdString(), std::ios::binary | std::ios::trunc);
            if (!output.is_open()) {
                return UiOperationResult{false, "Could not open the selected path for writing."};
            }
            output << json << '\n';
            if (!output) {
                return UiOperationResult{false, "Failed while writing the config file."};
            }
            return UiOperationResult{true, "Config exported."};
        } catch (const std::exception& error) {
            return UiOperationResult{false, QString::fromUtf8(error.what())};
        }
    }, [this](const UiOperationResult& result) {
        if (result.success) {
            statusBar()->showMessage(result.message, 3500);
        } else {
            QMessageBox::warning(this, "Export failed", result.message);
        }
    });
}

void MainWindow::runOperation(std::function<UiOperationResult()> operation, Completion completion) {
    if (operationWatcher_->isRunning()) {
        return;
    }
    operationCompletion_ = std::move(completion);
    meterTimer_->stop();
    toolbar_->setEnabled(false);
    columnsWidget_->setEnabled(false);
    statusBar()->showMessage("Working with PipeWire...");
    operationWatcher_->setFuture(QtConcurrent::run([operation = std::move(operation)]() mutable {
        try {
            return operation();
        } catch (const std::exception& error) {
            return UiOperationResult{false, QString::fromUtf8(error.what())};
        }
    }));
}
