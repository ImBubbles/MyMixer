#pragma once

#include <QFutureWatcher>
#include <QMainWindow>
#include <QString>
#include <QStringList>

#include <functional>
#include <set>
#include <string>
#include <vector>

#include "../pipewire/PipeWireContext.h"

class QToolBar;
class QWidget;
class QBoxLayout;
class PipeWireContext;
class FlowLayout;
class QCloseEvent;
class QSystemTrayIcon;
class QTimer;
class StereoLevelGraph;

struct UiOperationResult {
    bool success;
    QString message;
    QStringList failures;
};

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(PipeWireContext& context);
    ~MainWindow() override;

private:
    using Completion = std::function<void(const UiOperationResult&)>;

    PipeWireContext& context_;
    QToolBar* toolbar_ = nullptr;
    QWidget* columnsWidget_ = nullptr;
    QWidget* hardwareHost_ = nullptr;
    QWidget* virtualHost_ = nullptr;
    QWidget* hardwareColumn_ = nullptr;
    QBoxLayout* workspaceLayout_ = nullptr;
    FlowLayout* virtualLayout_ = nullptr;
    QFutureWatcher<UiOperationResult>* operationWatcher_ = nullptr;
    QSystemTrayIcon* trayIcon_ = nullptr;
    QTimer* meterTimer_ = nullptr;
    std::vector<AudioEndpoint> selectedHardware_;
    std::set<std::string> expandedChecklists_;
    Completion operationCompletion_;
    struct MeterDisplay {
        std::string channelName;
        StereoLevelGraph* graph = nullptr;
        float displayedLeft = 0.0f;
        float displayedRight = 0.0f;
    };
    std::vector<MeterDisplay> meters_;

    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void refreshChannels();
    void updateMeters();
    void removeChannel(const std::string& channelName);
    void addChannel();
    void addHardware();
    void importConfig();
    void exportConfig();
    void runOperation(std::function<UiOperationResult()> operation, Completion completion = {});
};
