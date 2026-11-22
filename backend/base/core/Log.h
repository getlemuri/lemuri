#pragma once

/**
 * The backend's technical log, to attach to bug reports: what it writes on standard error (its messages, libssh's)
 * goes on there and also, each line with its time, into ~/.lemuri/logs/lemuri-0.log, three rotating files of 1 MB
 * each, readable by the user only. The first Lemuri keeps its own lemuri-N.log in the same folder. Never passwords,
 * tokens or keys in the messages.
 */
namespace Log {
/** Once, at startup, before the other threads. */
void start();
/** Once, at the end, after the other threads: standard error goes back to the real one and the log is closed. */
void stop();
}  // namespace Log
