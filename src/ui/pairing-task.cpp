#include "pairing-task.h"
#include <QThread>
#include <algorithm>

PairingTask::PairingTask(QObject *parent) : QObject(parent)
{
    timer_.setInterval(25);
    connect(&timer_, &QTimer::timeout, this, [this] { poll(); });
}

PairingTask::~PairingTask() { cancel(); }

void PairingTask::closeProcess(bool terminate)
{
    if (process_) {
        if (terminate && WaitForSingleObject(process_, 0) == WAIT_TIMEOUT)
            sc_process_terminate(process_);
        sc_process_close(process_);
        process_ = SC_PROCESS_NONE;
    }
    if (output_) {
        sc_pipe_close(output_);
        output_ = SC_PROCESS_NONE;
    }
}

void PairingTask::cancel()
{
    Q_ASSERT(thread() == QThread::currentThread());
    timer_.stop();
    completion_ = {};
    ownerAlive_ = {};
    closeProcess(true);
    text_.clear();
}

bool PairingTask::launch(const std::vector<std::string> &arguments)
{
    std::vector<std::string> command{("\"" + program_ + "\"").toUtf8().toStdString()};
    command.insert(command.end(), arguments.begin(), arguments.end());
    sc_pid process = SC_PROCESS_NONE;
    sc_pipe output = SC_PROCESS_NONE;
    // Exactly one redirected pipe: drain it even after the retained text cap.
    if (sc_process_execute_p(command, &process, SC_PROCESS_NO_STDERR,
                             nullptr, &output, nullptr) != SC_PROCESS_SUCCESS)
        return false; // execute_p owns cleanup on failure.
    process_ = process;
    output_ = output;
    text_.clear();
    elapsed_.restart();
    timer_.start();
    return true;
}

void PairingTask::start(const QString &program, const QString &address, const QString &code,
                        const QString &connectAddress, Completion completion,
                        std::function<bool()> ownerAlive, int timeoutMs)
{
    cancel(); // A new run cannot inherit the old completion or process.
    program_ = program;
    if (program_.startsWith('"') && program_.endsWith('"'))
        program_ = program_.mid(1, program_.size() - 2);
    connectAddress_ = connectAddress;
    completion_ = std::move(completion);
    ownerAlive_ = std::move(ownerAlive);
    connecting_ = false;
    timeoutMs_ = timeoutMs;
    if (!launch({"pair", address.toStdString(), code.toStdString()}))
        finish(false, false, QStringLiteral("Could not start pairing process"));
}

void PairingTask::finish(bool paired, bool connected, const QString &error)
{
    auto callback = std::move(completion_);
    cancel();
    // State is fully retired before user code (which may destroy us or start again).
    if (callback) callback(paired, connected, error);
}

void PairingTask::poll()
{
    if (ownerAlive_ && !ownerAlive_()) {
        cancel();
        return;
    }
    if (!process_) return;
    // PeekNamedPipe + a single reader: ReadFile never requests unavailable bytes.
    // Bound work per event and retained diagnostics so a noisy child cannot starve UI.
    for (int budget = 64 * 1024; budget > 0;) {
        DWORD available = 0;
        if (!PeekNamedPipe(output_, nullptr, 0, nullptr, &available, nullptr) || !available)
            break;
        char buffer[4096];
        DWORD amount = std::min<DWORD>(available, sizeof(buffer));
        ssize_t read = sc_pipe_read(output_, buffer, amount);
        if (read <= 0) break;
        int keep = std::min<int>(int(read), 64 * 1024 - int(text_.size()));
        if (keep > 0) text_.append(buffer, keep);
        budget -= int(read);
    }
    DWORD wait = WaitForSingleObject(process_, 0);
    if (wait == WAIT_TIMEOUT && elapsed_.elapsed() < timeoutMs_) return;
    if (wait != WAIT_OBJECT_0) {
        finish(connecting_, false, QStringLiteral("Pairing/connect process timed out or failed"));
        return;
    }
    DWORD exitCode = 1;
    GetExitCodeProcess(process_, &exitCode);
    bool success = exitCode == 0 && (connecting_
        ? (text_.startsWith("connected") || text_.startsWith("already connected"))
        : text_.contains("Successfully paired"));
    QString error = QString::fromUtf8(text_);
    closeProcess(false);
    if (!connecting_ && success && !connectAddress_.isEmpty()) {
        connecting_ = true;
        if (!launch({"connect", connectAddress_.toStdString()}))
            finish(true, false, QStringLiteral("Could not start connect process"));
        return;
    }
    finish(connecting_ || success, connecting_ && success, error);
}
