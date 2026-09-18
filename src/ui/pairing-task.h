#pragma once
#include "util/sc_process.h"
#include <QObject>
#include <QTimer>
#include <QElapsedTimer>
#include <functional>

// Windows process handles and the timer are owned and used on the QObject thread.
// No detached worker, queued raw UI pointer, or blocking process destructor.
class PairingTask : public QObject {
public:
    using Completion = std::function<void(bool paired, bool connected, const QString &error)>;
    explicit PairingTask(QObject *parent = nullptr);
    ~PairingTask() override;
    void start(const QString &program, const QString &address, const QString &code,
               const QString &connectAddress, Completion completion,
               std::function<bool()> ownerAlive = {}, int timeoutMs = 30000);
    void cancel(); // Silent, idempotent, never waits for process exit.
    bool active() const { return process_ != SC_PROCESS_NONE; }

private:
    bool launch(const std::vector<std::string> &arguments);
    void poll();
    void finish(bool paired, bool connected, const QString &error);
    void closeProcess(bool terminate);
    QTimer timer_;
    QElapsedTimer elapsed_;
    sc_pid process_ = SC_PROCESS_NONE;
    sc_pipe output_ = SC_PROCESS_NONE;
    QByteArray text_;
    QString program_, connectAddress_;
    bool connecting_ = false;
    int timeoutMs_ = 30000;
    Completion completion_;
    std::function<bool()> ownerAlive_;
};
