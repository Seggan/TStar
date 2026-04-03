// ============================================================================
// TaskManager.cpp
// Central task scheduler: submits tasks to a private QThreadPool, manages
// lifecycle signals, and provides per-task and global cancellation.
// ============================================================================

#include "TaskManager.h"
#include "ResourceManager.h"
#include "Logger.h"

#include <QMutexLocker>
#include <QThread>

namespace Threading {

// ============================================================================
// Singleton Access
// ============================================================================

TaskManager& TaskManager::instance()
{
    static TaskManager _instance;
    return _instance;
}

// ============================================================================
// Construction / Destruction
// ============================================================================

TaskManager::TaskManager(QObject* parent)
    : QObject(parent)
{
    // Sensible default before init() is called
    m_pool.setMaxThreadCount(
        std::max(1, QThread::idealThreadCount() - 1));
}

TaskManager::~TaskManager()
{
    // Best-effort cleanup: cancel pending work and wait for running tasks
    cancelAll();
    m_pool.waitForDone(5000);
}

// ============================================================================
// Initialization
// ============================================================================

void TaskManager::init()
{
    const int n = ResourceManager::instance().maxThreads();
    m_pool.setMaxThreadCount(n);

    Logger::info(
        QString("TaskManager: pool size = %1 thread(s)").arg(n),
        "Threading");
}

// ============================================================================
// Task Submission
// ============================================================================

Task::Id TaskManager::submit(std::shared_ptr<Task> task)
{
    Q_ASSERT(task);

    const Task::Id id = task->id();

    // -- Register the task under lock -----------------------------------------
    {
        QMutexLocker lk(&m_mutex);
        m_tasks.insert(id, task);
    }

    // -- Wire per-task signals to manager-level signals -----------------------
    // Qt::AutoConnection routes these as queued connections since the Task
    // QObject lives in the GUI thread while run() executes in a pool thread.

    connect(task.get(), &Task::started,
            this, [this](quint64 tid) {
                emit taskStarted(tid);
            });

    connect(task.get(), &Task::progress,
            this, [this](quint64 tid, int pct, const QString& msg) {
                emit taskProgress(tid, pct, msg);
            });

    connect(task.get(), &Task::finished,
            this, [this](quint64 tid) {
                emit taskFinished(tid);
                onTaskDone(tid);
            });

    connect(task.get(), &Task::failed,
            this, [this](quint64 tid, const QString& err) {
                emit taskFailed(tid, err);
                onTaskDone(tid);
            });

    connect(task.get(), &Task::cancelled,
            this, [this](quint64 tid) {
                emit taskCancelled(tid);
                onTaskDone(tid);
            });

    // -- Submit to thread pool ------------------------------------------------
    // Higher enum value = higher QThreadPool scheduling priority
    m_pool.start(task.get(), static_cast<int>(task->priority()));

    return id;
}

// ============================================================================
// Cancellation
// ============================================================================

void TaskManager::cancel(Task::Id id)
{
    QMutexLocker lk(&m_mutex);
    auto it = m_tasks.find(id);
    if (it != m_tasks.end()) {
        it.value()->cancel();
    }
}

void TaskManager::cancelAll()
{
    // Take a snapshot under lock, then cancel without holding it
    // (Task::cancel() is lock-free and thread-safe)
    QList<std::shared_ptr<Task>> snapshot;
    {
        QMutexLocker lk(&m_mutex);
        snapshot.reserve(m_tasks.size());
        for (auto& t : m_tasks) {
            snapshot.append(t);
        }
    }

    for (auto& t : snapshot) {
        t->cancel();
    }
}

// ============================================================================
// Internal: Task Completion Cleanup
// ============================================================================

void TaskManager::onTaskDone(quint64 id)
{
    bool empty = false;
    {
        QMutexLocker lk(&m_mutex);
        m_tasks.remove(id);
        empty = m_tasks.isEmpty();
    }

    if (empty) {
        emit allTasksDone();
    }
}

// ============================================================================
// Status Queries
// ============================================================================

int TaskManager::activeCount() const
{
    return m_pool.activeThreadCount();
}

int TaskManager::trackedCount() const
{
    QMutexLocker lk(&m_mutex);
    return m_tasks.size();
}

bool TaskManager::waitForAll(int msTimeout)
{
    return m_pool.waitForDone(msTimeout);
}

int TaskManager::maxThreads() const
{
    return m_pool.maxThreadCount();
}

} // namespace Threading