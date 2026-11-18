// Heuristica — raw ANSI terminal I/O. See Terminal.h.
#include "render/Terminal.h"

#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <exception>

namespace heur {

namespace {

// ---- Process-wide terminal state (touched by signal handlers => sig_atomic_t / POD only) ----------
termios g_orig{};                    // terminal attributes before we touched them
termios g_raw{};                     // our raw-mode attributes (re-applied after Ctrl+Z / fg)
volatile std::sig_atomic_t g_haveOrig = 0;
volatile std::sig_atomic_t g_rawActive = 0;
volatile std::sig_atomic_t g_interrupt = 0;
volatile std::sig_atomic_t g_winch = 0;
volatile std::sig_atomic_t g_resumed = 0;
bool g_hooksInstalled = false;
std::terminate_handler g_prevTerminate = nullptr;

// Alternate screen, hide cursor, disable auto-wrap, clear.
constexpr char kEnterSeq[] = "\x1b[?1049h\x1b[?25l\x1b[?7l\x1b[0m\x1b[2J\x1b[H";
// Reset colours, re-enable auto-wrap, show cursor, leave alternate screen.
constexpr char kLeaveSeq[] = "\x1b[0m\x1b[?7h\x1b[?25h\x1b[?1049l";

constexpr int kHandledSignals[] = {SIGINT, SIGTERM, SIGHUP, SIGQUIT};
constexpr int kFatalSignals[] = {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT};
struct sigaction g_oldHandled[sizeof(kHandledSignals) / sizeof(int)];
struct sigaction g_oldFatal[sizeof(kFatalSignals) / sizeof(int)];
struct sigaction g_oldWinch, g_oldTstp;

// Async-signal-safe full write.
void rawWrite(const char* s, std::size_t n) {
    while (n > 0) {
        const ssize_t w = ::write(STDOUT_FILENO, s, n);
        if (w < 0) {
            if (errno == EINTR) continue;
            return;
        }
        s += w;
        n -= std::size_t(w);
    }
}

// Async-signal-safe: only write() and tcsetattr().
void restoreTerminalSignalSafe() {
    if (!g_rawActive) return;
    g_rawActive = 0;
    rawWrite(kLeaveSeq, sizeof(kLeaveSeq) - 1);
    if (g_haveOrig) ::tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_orig);
}

extern "C" void onInterruptSignal(int sig) {
    const int savedErrno = errno;
    if (g_interrupt) {
        // Second signal while the main loop did not react: restore and bail out immediately.
        restoreTerminalSignalSafe();
        ::_exit(128 + sig);
    }
    g_interrupt = 1;
    errno = savedErrno;
}

extern "C" void onWinchSignal(int) { g_winch = 1; }

extern "C" void onStopSignal(int) {
    const int savedErrno = errno;
    const bool wasRaw = g_rawActive != 0;
    if (wasRaw) {
        rawWrite(kLeaveSeq, sizeof(kLeaveSeq) - 1);
        ::tcsetattr(STDIN_FILENO, TCSANOW, &g_orig);
    }
    ::raise(SIGSTOP);  // actually suspend; returns after SIGCONT (fg)
    if (wasRaw && g_rawActive) {
        ::tcsetattr(STDIN_FILENO, TCSANOW, &g_raw);
        rawWrite(kEnterSeq, sizeof(kEnterSeq) - 1);
        g_resumed = 1;
        g_winch = 1;
    }
    errno = savedErrno;
}

extern "C" void onFatalSignal(int sig, siginfo_t* info, void* ctx) {
    restoreTerminalSignalSafe();
    // Chain to a previously installed handler (e.g. sanitizers) if there was one.
    for (std::size_t i = 0; i < sizeof(kFatalSignals) / sizeof(int); ++i) {
        if (kFatalSignals[i] != sig) continue;
        const struct sigaction& old = g_oldFatal[i];
        if (old.sa_flags & SA_SIGINFO) {
            if (old.sa_sigaction) {
                old.sa_sigaction(sig, info, ctx);
                return;
            }
        } else if (old.sa_handler != SIG_DFL && old.sa_handler != SIG_IGN) {
            old.sa_handler(sig);
            return;
        }
    }
    // SA_RESETHAND restored the default disposition; SA_NODEFER lets this raise take effect now.
    ::raise(sig);
}

void onTerminate() {
    restoreTerminalSignalSafe();
    // The previous handler (libstdc++'s verbose one) prints the exception on the restored terminal.
    if (g_prevTerminate) g_prevTerminate();
    std::abort();
}

void onExit() { restoreTerminalSignalSafe(); }

void installSignal(int sig, void (*handler)(int), struct sigaction* old, int flags) {
    struct sigaction sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = flags;
    ::sigaction(sig, &sa, old);
}

void installHooks() {
    for (std::size_t i = 0; i < sizeof(kHandledSignals) / sizeof(int); ++i)
        installSignal(kHandledSignals[i], onInterruptSignal, &g_oldHandled[i], 0);
    for (std::size_t i = 0; i < sizeof(kFatalSignals) / sizeof(int); ++i) {
        struct sigaction sa;
        std::memset(&sa, 0, sizeof(sa));
        sa.sa_sigaction = onFatalSignal;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_SIGINFO | SA_RESETHAND | SA_NODEFER;
        ::sigaction(kFatalSignals[i], &sa, &g_oldFatal[i]);
    }
    installSignal(SIGWINCH, onWinchSignal, &g_oldWinch, SA_RESTART);
    installSignal(SIGTSTP, onStopSignal, &g_oldTstp, SA_RESTART);
    if (!g_hooksInstalled) {
        // These two cannot be uninstalled; they are no-ops once the terminal is restored.
        std::atexit(onExit);
        g_prevTerminate = std::set_terminate(onTerminate);
        g_hooksInstalled = true;
    }
}

void uninstallHooks() {
    for (std::size_t i = 0; i < sizeof(kHandledSignals) / sizeof(int); ++i)
        ::sigaction(kHandledSignals[i], &g_oldHandled[i], nullptr);
    for (std::size_t i = 0; i < sizeof(kFatalSignals) / sizeof(int); ++i)
        ::sigaction(kFatalSignals[i], &g_oldFatal[i], nullptr);
    ::sigaction(SIGWINCH, &g_oldWinch, nullptr);
    ::sigaction(SIGTSTP, &g_oldTstp, nullptr);
}

bool isCsiParam(unsigned char c) { return c >= 0x20 && c <= 0x3F; }  // parameter + intermediate bytes

Key keyOf(KeyCode code) { return Key{code, 0}; }

// Decodes the final byte (+ numeric parameter) of a CSI/SS3 sequence.
KeyCode decodeEscape(char final, int param) {
    switch (final) {
        case 'A': return KeyCode::Up;
        case 'B': return KeyCode::Down;
        case 'C': return KeyCode::Right;
        case 'D': return KeyCode::Left;
        case 'H': return KeyCode::Home;
        case 'F': return KeyCode::End;
        case 'Z': return KeyCode::BackTab;
        case 'M': return KeyCode::Enter;  // keypad enter (SS3 M)
        case '~':
            switch (param) {
                case 1: case 7: return KeyCode::Home;
                case 4: case 8: return KeyCode::End;
                case 3: return KeyCode::Delete;
                case 5: return KeyCode::PageUp;
                case 6: return KeyCode::PageDown;
                default: return KeyCode::None;
            }
        default: return KeyCode::None;
    }
}

}  // namespace

// ---------------------------------------------------------------------------------------------------
// Key parsing
// ---------------------------------------------------------------------------------------------------
std::vector<Key> parseKeys(std::string& buf, bool final) {
    std::vector<Key> keys;
    std::size_t i = 0;
    const std::size_t n = buf.size();
    while (i < n) {
        const unsigned char c = static_cast<unsigned char>(buf[i]);
        if (c == 0x1b) {
            if (i + 1 >= n) {
                if (!final) break;
                keys.push_back(keyOf(KeyCode::Escape));
                ++i;
                continue;
            }
            const unsigned char next = static_cast<unsigned char>(buf[i + 1]);
            if (next == '[' || next == 'O') {
                std::size_t j = i + 2;
                while (j < n && isCsiParam(static_cast<unsigned char>(buf[j]))) ++j;
                if (j >= n) {
                    if (!final) break;
                    keys.push_back(keyOf(KeyCode::Escape));  // truncated sequence: swallow it
                    i = n;
                    continue;
                }
                int param = 0;
                for (std::size_t k = i + 2; k < j && buf[k] >= '0' && buf[k] <= '9'; ++k)
                    param = param * 10 + (buf[k] - '0');
                const KeyCode code = decodeEscape(buf[j], param);
                if (code != KeyCode::None) keys.push_back(keyOf(code));
                i = j + 1;
            } else if (next == 0x1b) {
                keys.push_back(keyOf(KeyCode::Escape));  // Esc Esc
                ++i;
            } else {
                // Alt+key arrives as ESC + key: drop the ESC so Alt combos never quit the game.
                ++i;
            }
            continue;
        }
        if (c == '\r' || c == '\n') {
            keys.push_back(keyOf(KeyCode::Enter));
            if (c == '\r' && i + 1 < n && buf[i + 1] == '\n') ++i;
            ++i;
            continue;
        }
        if (c == '\t') { keys.push_back(keyOf(KeyCode::Tab)); ++i; continue; }
        if (c == 0x7f || c == 0x08) { keys.push_back(keyOf(KeyCode::Backspace)); ++i; continue; }
        if (c == 0x03) { keys.push_back(keyOf(KeyCode::CtrlC)); ++i; continue; }
        if (c < 0x20) { ++i; continue; }  // other control bytes: ignore
        if (c < 0x80) {
            keys.push_back(Key{KeyCode::Char, char32_t(c)});
            ++i;
            continue;
        }
        // UTF-8 multi-byte sequence.
        int len = (c >= 0xF0) ? 4 : (c >= 0xE0) ? 3 : (c >= 0xC0) ? 2 : 1;
        if (len == 1) { ++i; continue; }  // stray continuation byte
        if (i + std::size_t(len) > n) {
            if (!final) break;
            i = n;
            continue;
        }
        char32_t cp = c & (len == 2 ? 0x1F : len == 3 ? 0x0F : 0x07);
        for (int k = 1; k < len; ++k) cp = (cp << 6) | (static_cast<unsigned char>(buf[i + std::size_t(k)]) & 0x3F);
        keys.push_back(Key{KeyCode::Char, cp});
        i += std::size_t(len);
    }
    buf.erase(0, i);
    return keys;
}

// ---------------------------------------------------------------------------------------------------
// Terminal
// ---------------------------------------------------------------------------------------------------
Terminal::~Terminal() { close(); }

bool Terminal::stdinIsTty() { return ::isatty(STDIN_FILENO) == 1; }
bool Terminal::stdoutIsTty() { return ::isatty(STDOUT_FILENO) == 1; }

bool Terminal::open() {
    if (open_) return true;
    if (!stdinIsTty() || !stdoutIsTty()) return false;
    if (::tcgetattr(STDIN_FILENO, &g_orig) != 0) return false;
    g_haveOrig = 1;

    termios raw = g_orig;
    raw.c_iflag &= ~tcflag_t(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
    raw.c_lflag &= ~tcflag_t(ECHO | ICANON | IEXTEN);  // keep ISIG: Ctrl+C -> SIGINT -> clean exit
    raw.c_cflag |= CS8;
    raw.c_cc[VMIN] = 0;   // non-blocking reads without O_NONBLOCK (which would leak into stdout)
    raw.c_cc[VTIME] = 0;
    g_raw = raw;

    g_interrupt = 0;
    installHooks();
    if (::tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) {
        uninstallHooks();
        return false;
    }
    g_rawActive = 1;
    rawWrite(kEnterSeq, sizeof(kEnterSeq) - 1);
    open_ = true;
    querySize();
    return true;
}

void Terminal::close() {
    if (!open_) return;
    restoreTerminalSignalSafe();
    uninstallHooks();
    open_ = false;
}

void Terminal::querySize() {
    winsize ws{};
    if (::ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
        cols_ = ws.ws_col;
        rows_ = ws.ws_row;
        return;
    }
    if (const char* c = std::getenv("COLUMNS")) cols_ = std::max(20, std::atoi(c));
    if (const char* r = std::getenv("LINES")) rows_ = std::max(10, std::atoi(r));
}

bool Terminal::pollResize() {
    const int oldC = cols_, oldR = rows_;
    g_winch = 0;
    querySize();  // cheap; doing it every frame also covers terminals that never send SIGWINCH
    return cols_ != oldC || rows_ != oldR;
}

bool Terminal::consumeResumed() {
    if (!g_resumed) return false;
    g_resumed = 0;
    return true;
}

bool Terminal::interruptRequested() { return g_interrupt != 0; }

std::vector<Key> Terminal::readKeys() {
    char buf[512];
    auto drain = [&] {
        for (;;) {
            const ssize_t r = ::read(STDIN_FILENO, buf, sizeof(buf));
            if (r > 0) {
                pending_.append(buf, std::size_t(r));
                if (std::size_t(r) < sizeof(buf)) break;
            } else if (r < 0 && errno == EINTR) {
                continue;
            } else {
                break;
            }
        }
    };
    drain();
    if (pending_.empty()) return {};
    // A lone ESC might be the start of an escape sequence split across reads: wait briefly.
    std::vector<Key> keys = parseKeys(pending_, false);
    if (!pending_.empty()) {
        pollfd pfd{STDIN_FILENO, POLLIN, 0};
        if (::poll(&pfd, 1, 25) > 0) drain();
        std::vector<Key> more = parseKeys(pending_, true);
        keys.insert(keys.end(), more.begin(), more.end());
    }
    return keys;
}

void Terminal::write(std::string_view bytes) { rawWrite(bytes.data(), bytes.size()); }

}  // namespace heur
