// ============================================================================
// ThreadState.cpp
// Static member initialization for the global cancellation flag.
// ============================================================================

#include "ThreadState.h"

namespace Threading {

std::atomic<bool> ThreadState::s_shouldRun{true};

} // namespace Threading