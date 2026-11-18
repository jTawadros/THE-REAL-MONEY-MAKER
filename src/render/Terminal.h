// Heuristica — raw ANSI terminal I/O (no ncurses).
//
// Terminal owns the TTY state: raw mode (no echo, non-canonical, non-blocking reads), the alternate
// screen buffer and cursor visibility. Everything is restored by the destructor, by an atexit hook, by
// std::terminate and by the SIGINT/SIGTERM handlers (which only use async-signal-safe calls).
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace heur {

enum class KeyCode : std::uint8_t {
    None,
    Char,       // printable character in Key::ch (ASCII or decoded UTF-8)
    Up, Down, Left, Right,
    PageUp, PageDown, Home, End,
    Enter, Escape, Tab, BackTab, Backspace, Delete,
    CtrlC,
};

struct Key {
    KeyCode code = KeyCode::None;
    char32_t ch = 0;

    bool is(char c) const { return code == KeyCode::Char && ch == char32_t(static_cast<unsigned char>(c)); }
};

// Parses a raw byte stream into keys. Exposed separately so it can be tested without a TTY.
// `final` = no more bytes are expected right now (a trailing lone ESC is then reported as Escape).
// Unconsumed trailing bytes (an incomplete escape/UTF-8 sequence) are left in `buf`.
std::vector<Key> parseKeys(std::string& buf, bool final);

class Terminal {
public:
    Terminal() = default;
    ~Terminal();
    Terminal(const Terminal&) = delete;
    Terminal& operator=(const Terminal&) = delete;

    static bool stdinIsTty();
    static bool stdoutIsTty();

    // Enter raw mode + alternate screen + hidden cursor; installs signal/atexit/terminate hooks.
    // Returns false (and leaves the terminal untouched) if stdin/stdout is not a TTY.
    bool open();
    // Restore the terminal (idempotent).
    void close();
    bool isOpen() const { return open_; }

    int cols() const { return cols_; }
    int rows() const { return rows_; }
    // Re-queries the window size. Returns true when the size changed since the last call (SIGWINCH).
    bool pollResize();
    // True once after the process resumed from Ctrl+Z (the screen must be fully redrawn).
    bool consumeResumed();
    // SIGINT / SIGTERM was received: the main loop should exit cleanly.
    static bool interruptRequested();

    // Non-blocking: returns every key currently buffered (possibly none).
    std::vector<Key> readKeys();

    // Writes all bytes (handles partial writes / EINTR). One call per frame.
    void write(std::string_view bytes);

private:
    void querySize();

    bool open_ = false;
    int cols_ = 80;
    int rows_ = 24;
    std::string pending_;  // incomplete input sequence carried to the next read
};

}  // namespace heur
