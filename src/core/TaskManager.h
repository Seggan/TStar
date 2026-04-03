#ifndef TSTAR_TASKMANAGER_H
#define TSTAR_TASKMANAGER_H

// ============================================================================
// TaskManager.h
// Central manager for all background tasks. Owns a private QThreadPool
// sized according to ResourceManager's 90% CPU policy.
// ============================================================================

#include "Task.h"

#include <QObject>
#include <QMutex>
#include <QThreadPool>
#include <QHash>
#include <memory>

namespace Threading {

/**
 * @brief Centralized background task scheduler and lifecycle manager.
 *
 * All long-running background work should be submitted through TaskManager
 * rather than creating raw QThread objects. This ensures:
 *   - Thread count is bounded by the 90% CPU rule
 *   - Tasks are prioritized (Critical > High > Normal > Low > Background)
 *   - Individual tasks can be cancelled by ID
 *   - Global progress and completion signals are available for status UI
 *
 * Initialization:
 *   Call TaskManager::instance().init() once after
 *   ResourceManager::instance().init().
 *
 * Submitting work:
 * @code
 *   auto task = std::make_shared<MyTask>();
 *   connect(task.get(), &Task::progress, this, &MyDialog::onProgress);
 *   Task::Id id = TaskManager::instance().submit(task);
 * @endcode
 *
 * Cancellation:
 * @code
 *   TaskManager::instance().cancel(id);   // Cancel one task
 *   TaskManager::instance().cancelAll();  // Cancel everything
 * @endcode
 *
 * Thread safety: All public methods are thread-safe.
 */
class TaskManager : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY(TaskManager)

public:
    static TaskManager& instance();

    /**
     * @brief Initialize the thread pool size.
     * Must be called once after ResourceManager::instance().init().
     */
    void init();

    /**
     * @brief Submit a task for execution on the private thread pool.
     *
     * TaskManager holds a shared_ptr to the task until it finishes.
     * The task is started with its declared priority.
     *
     * @param task Shared task to run (must not be null).
     * @return The task's unique ID for later cancellation.
     */
    Task::Id submit(std::shared_ptr<Task> task);

    /**
     * @brief Request cancellation of a single task by ID.
     * Safe to call after the task has already completed (no-op).
     */
    void cancel(Task::Id id);

    /**
     * @brief Request cancellation of ALL tracked tasks.
     */
    void cancelAll();

    /** Number of tasks currently executing in the pool. */
    int activeCount() const;

    /** Total number of tasks known to the manager (running + queued). */
    int trackedCount() const;

    /**
     * @brief Block until all running tasks finish or timeout expires.
     * @param msTimeout Maximum wait in milliseconds (-1 = wait forever).
     * @return true if all tasks completed within the timeout.
     */
    bool waitForAll(int msTimeout = -1);

    /** Maximum thread count of the internal pool. */
    int maxThreads() const;

signals:
    // -- Global mirrors of per-task signals -----------------------------------

    /** Emitted when any task starts executing. */
    void taskStarted(quint64 id);

    /** Emitted for periodic progress from any running task. */
    void taskProgress(quint64 id, int percent, const QString& message);

    /** Emitted when any task completes successfully. */
    void taskFinished(quint64 id);

    /** Emitted when any task fails with an exception. */
    void taskFailed(quint64 id, const QString& errorMessage);

    /** Emitted when any task is cancelled. */
    void taskCancelled(quint64 id);

    /**
     * @brief Emitted when the last tracked task finishes/cancels/fails.
     * Useful for hiding a global busy indicator.
     */
    void allTasksDone();

private:
    explicit TaskManager(QObject* parent = nullptr);
    ~TaskManager() override;

    /** Remove a completed/failed/cancelled task from the registry. */
    void onTaskDone(quint64 id);

    QThreadPool  m_pool;        ///< Private pool (NOT QThreadPool::globalInstance())
    mutable QMutex m_mutex;     ///< Guards m_tasks
    QHash<Task::Id, std::shared_ptr<Task>> m_tasks;  ///< All tracked tasks
};

} // namespace Threading

#endif // TSTAR_TASKMANAGER_H