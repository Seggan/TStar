#ifndef TSTAR_TASK_H
#define TSTAR_TASK_H

// ============================================================================
// Task.h
// Abstract background task with unique ID, per-task cancellation, priority
// levels, and progress reporting. Designed for use with TaskManager.
// ============================================================================

#include <QObject>
#include <QRunnable>
#include <QString>
#include <atomic>

namespace Threading {

/**
 * @brief Abstract base class for background tasks.
 *
 * Subclass and implement execute() to define the work. Use shouldContinue()
 * in tight loops for responsive cancellation. Call reportProgress() to emit
 * progress updates.
 *
 * Usage:
 * @code
 *   class MyTask : public Threading::Task {
 *   protected:
 *       void execute() override {
 *           for (int i = 0; i < 100 && shouldContinue(); ++i) {
 *               doWork(i);
 *               reportProgress(i + 1, tr("Processing %1/100").arg(i + 1));
 *           }
 *       }
 *   };
 *
 *   auto task = std::make_shared<MyTask>();
 *   connect(task.get(), &Task::progress, this, &MyDialog::onProgress);
 *   TaskManager::instance().submit(task);
 * @endcode
 *
 * Key properties:
 *   - Every task has a unique quint64 ID assigned on construction.
 *   - cancel() only affects this task; other tasks continue unaffected.
 *   - shouldContinue() also respects the global ThreadState flag.
 *   - Signals are delivered via Qt::AutoConnection (typically queued to GUI).
 *   - Lifetime is managed by std::shared_ptr inside TaskManager.
 */
class Task : public QObject, public QRunnable
{
    Q_OBJECT
    Q_DISABLE_COPY(Task)

public:
    using Id = quint64;

    /**
     * @brief Task priority levels.
     * Higher numeric value = higher scheduling priority in QThreadPool.
     */
    enum class Priority : int {
        Background = 0,     ///< Housekeeping, downloads
        Low        = 1,     ///< Bulk batch processing
        Normal     = 2,     ///< Standard user-triggered operations
        High       = 3,     ///< Interactive / time-critical operations
        Critical   = 4      ///< Must execute immediately
    };
    Q_ENUM(Priority)

    /** Task lifecycle states. */
    enum class Status {
        Pending,            ///< Submitted but not yet started
        Running,            ///< execute() is in progress
        Cancelled,          ///< Stopped early via cancel()
        Failed,             ///< execute() threw an exception
        Completed           ///< execute() returned normally
    };
    Q_ENUM(Status)

    explicit Task(Priority priority = Priority::Normal,
                  QObject* parent = nullptr);
    ~Task() override = default;

    // -- Accessors ------------------------------------------------------------

    /** Unique task ID, assigned on construction. Never reused within a session. */
    Id       id()       const noexcept { return m_id; }
    Priority priority() const noexcept { return m_priority; }
    Status   status()   const noexcept { return m_status.load(std::memory_order_acquire); }

    /** True while status is Pending or Running. */
    bool isActive()    const noexcept;

    /** True once cancel() has been called (task may still be running). */
    bool isCancelled() const noexcept
    {
        return m_cancelRequested.load(std::memory_order_acquire);
    }

    // -- Cancellation ---------------------------------------------------------

    /**
     * @brief Request graceful cancellation.
     *
     * Thread-safe. Sets the cancellation flag so that shouldContinue()
     * returns false. Does NOT forcibly terminate the thread.
     */
    void cancel();

signals:
    /** Emitted immediately before execute() begins. */
    void started(quint64 id);

    /**
     * @brief Periodic progress update from within execute().
     * @param id      Task ID.
     * @param percent Completion estimate [0, 100].
     * @param message Human-readable status string (may be empty).
     */
    void progress(quint64 id, int percent, const QString& message);

    /** Emitted when execute() completes successfully. */
    void finished(quint64 id);

    /** Emitted when execute() throws an exception. */
    void failed(quint64 id, const QString& errorMessage);

    /** Emitted when execution stopped early due to cancellation. */
    void cancelled(quint64 id);

protected:
    /**
     * @brief Implement this to define the task's work.
     * Runs in a pool thread. Check shouldContinue() frequently.
     */
    virtual void execute() = 0;

    /**
     * @brief Emit a progress update from within execute().
     * @param percent Completion estimate [0, 100] (clamped internally).
     * @param message Optional status description.
     */
    void reportProgress(int percent, const QString& message = {});

    /**
     * @brief Check whether execution should continue.
     * Returns false when cancel() has been called on this task
     * or when the global ThreadState::shouldRun() flag is cleared.
     */
    bool shouldContinue() const noexcept;

private:
    /** QRunnable entry point; manages state transitions around execute(). */
    void run() override final;

    const Id       m_id;
    const Priority m_priority;

    std::atomic<Status> m_status          { Status::Pending };
    std::atomic<bool>   m_cancelRequested { false };

    static std::atomic<Id> s_nextId;
};

} // namespace Threading

#endif // TSTAR_TASK_H