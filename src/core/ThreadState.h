#ifndef THREAD_STATE_H
#define THREAD_STATE_H

// ============================================================================
// ThreadState.h
// Global and per-task cancellation primitives for background operations.
// ============================================================================

#include <atomic>

namespace Threading {

/**
 * @brief Application-wide "keep running" flag for global cancellation.
 *
 * This is a GLOBAL flag -- calling requestCancel() stops ALL tasks at once.
 * For per-task cancellation, use:
 *   - Threading::CancelToken  (for existing QThread-based workers)
 *   - Threading::Task + TaskManager (preferred for new work)
 *
 * Typical usage: call requestCancel() only on application shutdown or when
 * the user requests all background activity to stop simultaneously.
 */
class ThreadState {
public:
    /** @return false if global cancellation has been requested. */
    static bool shouldRun()
    {
        return s_shouldRun.load(std::memory_order_acquire);
    }

    static void setRun(bool run)
    {
        s_shouldRun.store(run, std::memory_order_release);
    }

    /** Request cancellation of ALL running tasks (application-wide). */
    static void requestCancel() { setRun(false); }

    /** Re-arm the global flag after cancellation has been resolved. */
    static void reset() { setRun(true); }

private:
    static std::atomic<bool> s_shouldRun;
};

/**
 * @brief Lightweight per-operation cancellation token.
 *
 * Provides per-task cancellation that also respects the global ThreadState
 * flag. shouldContinue() returns false when either this token's cancel()
 * has been called OR ThreadState::shouldRun() is false.
 *
 * Usage:
 * @code
 *   Threading::CancelToken m_cancelToken;
 *   // In worker loop:
 *   if (!m_cancelToken.shouldContinue()) return;
 *   // From cancel button:
 *   m_cancelToken.cancel();
 * @endcode
 */
class CancelToken {
public:
    /** Request cancellation of this specific operation. Thread-safe. */
    void cancel() { m_cancelled.store(true, std::memory_order_release); }

    /** Re-arm for a fresh operation on the same token. */
    void reset() { m_cancelled.store(false, std::memory_order_release); }

    /** @return true once cancel() has been called on this token. */
    bool isCancelled() const
    {
        return m_cancelled.load(std::memory_order_acquire);
    }

    /**
     * @return false when either this token or the global flag requests stop.
     * Use this in tight inner loops for responsive cancellation.
     */
    bool shouldContinue() const
    {
        return !isCancelled() && ThreadState::shouldRun();
    }

private:
    std::atomic<bool> m_cancelled{false};
};

// -- Backward-compatible free functions ---------------------------------------
inline bool getThreadRun()         { return ThreadState::shouldRun(); }
inline void setThreadRun(bool run) { ThreadState::setRun(run); }

} // namespace Threading

#endif // THREAD_STATE_H