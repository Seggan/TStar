// ============================================================================
// Task.cpp
// Background task lifecycle management: state machine, cancellation,
// progress reporting, and exception-safe execution wrapper.
// ============================================================================

#include "Task.h"
#include "ThreadState.h"

#include <QMetaObject>
#include <stdexcept>

namespace Threading {

// ============================================================================
// Static ID Counter
// ============================================================================

std::atomic<Task::Id> Task::s_nextId { 1 };

// ============================================================================
// Construction
// ============================================================================

Task::Task(Priority priority, QObject* parent)
    : QObject(parent)
    , QRunnable()
    , m_id(s_nextId.fetch_add(1, std::memory_order_relaxed))
    , m_priority(priority)
{
    // TaskManager keeps us alive via shared_ptr; disable QRunnable auto-delete
    setAutoDelete(false);
}

// ============================================================================
// Cancellation
// ============================================================================

void Task::cancel()
{
    m_cancelRequested.store(true, std::memory_order_release);
}

/**
 * @brief Composite cancellation check.
 *
 * Returns false if either:
 *   - This task's cancel() was called, OR
 *   - The global ThreadState flag requests shutdown
 */
bool Task::shouldContinue() const noexcept
{
    if (m_cancelRequested.load(std::memory_order_acquire))
        return false;
    return ThreadState::shouldRun();
}

// ============================================================================
// Status Queries
// ============================================================================

bool Task::isActive() const noexcept
{
    const Status s = status();
    return s == Status::Pending || s == Status::Running;
}

// ============================================================================
// Progress Reporting
// ============================================================================

void Task::reportProgress(int percent, const QString& message)
{
    emit progress(m_id, qBound(0, percent, 100), message);
}

// ============================================================================
// Core Execution (QRunnable Entry Point)
// ============================================================================

/**
 * @brief Called by QThreadPool in a worker thread.
 *
 * Manages the Pending -> Running -> Completed/Cancelled/Failed state machine.
 * Wraps execute() in a try/catch to convert exceptions into failed() signals.
 */
void Task::run()
{
    // -- Transition: Pending -> Running ---------------------------------------
    {
        Status expected = Status::Pending;
        if (!m_status.compare_exchange_strong(expected, Status::Running,
                                              std::memory_order_acq_rel)) {
            // Already cancelled before the pool started us
            emit cancelled(m_id);
            return;
        }
    }

    emit started(m_id);

    // -- Execute the task body ------------------------------------------------
    try {
        execute();
    }
    catch (const std::exception& ex) {
        m_status.store(Status::Failed, std::memory_order_release);
        emit failed(m_id, QString::fromUtf8(ex.what()));
        return;
    }
    catch (...) {
        m_status.store(Status::Failed, std::memory_order_release);
        emit failed(m_id,
            QStringLiteral("Unknown exception in task %1").arg(m_id));
        return;
    }

    // -- Determine final status -----------------------------------------------
    if (m_cancelRequested.load(std::memory_order_acquire)) {
        m_status.store(Status::Cancelled, std::memory_order_release);
        emit cancelled(m_id);
    } else {
        m_status.store(Status::Completed, std::memory_order_release);
        emit finished(m_id);
    }
}

} // namespace Threading