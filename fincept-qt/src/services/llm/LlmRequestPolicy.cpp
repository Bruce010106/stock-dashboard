#include "services/llm/LlmRequestPolicy.h"

namespace fincept::ai_chat::detail {

// Define non-trivial TLS objects once. MinGW can emit duplicate TLS
// initialization functions for inline variables shared across translation units.
thread_local ProgressEmitter t_progress_emitter;
thread_local QString t_chat_session_id;

} // namespace fincept::ai_chat::detail
