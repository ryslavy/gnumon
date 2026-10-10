#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <linux/input.h>
#include <dlfcn.h>
#include <cstring>
#include "../common/Clock.h"

namespace gnumon::layer {

struct HotkeyChord {
    bool ctrl = false;
    bool shift = false;
    bool alt = false;
    int evdevKey = 0;
    unsigned long keysym = 0;
    std::string rawStr;

    static std::string ToUpper(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::toupper(c); });
        return s;
    }

    static HotkeyChord Parse(const std::string& str) {
        HotkeyChord c;
        c.rawStr = str;
        if (str.empty()) return c;

        // Split by '+'
        std::vector<std::string> parts;
        size_t start = 0;
        while (start < str.size()) {
            size_t plus = str.find('+', start);
            if (plus == std::string::npos) {
                parts.push_back(str.substr(start));
                break;
            }
            parts.push_back(str.substr(start, plus - start));
            start = plus + 1;
        }

        for (auto& p : parts) {
            // Trim whitespace
            p.erase(0, p.find_first_not_of(" \t\r\n"));
            p.erase(p.find_last_not_of(" \t\r\n") + 1);
            std::string u = ToUpper(p);

            if (u == "CTRL" || u == "CONTROL") {
                c.ctrl = true;
            } else if (u == "SHIFT") {
                c.shift = true;
            } else if (u == "ALT") {
                c.alt = true;
            } else if (u == "F1") { c.evdevKey = KEY_F1; c.keysym = 0xffbe; }
            else if (u == "F2") { c.evdevKey = KEY_F2; c.keysym = 0xffbf; }
            else if (u == "F3") { c.evdevKey = KEY_F3; c.keysym = 0xffc0; }
            else if (u == "F4") { c.evdevKey = KEY_F4; c.keysym = 0xffc1; }
            else if (u == "F5") { c.evdevKey = KEY_F5; c.keysym = 0xffc2; }
            else if (u == "F6") { c.evdevKey = KEY_F6; c.keysym = 0xffc3; }
            else if (u == "F7") { c.evdevKey = KEY_F7; c.keysym = 0xffc4; }
            else if (u == "F8") { c.evdevKey = KEY_F8; c.keysym = 0xffc5; }
            else if (u == "F9") { c.evdevKey = KEY_F9; c.keysym = 0xffc6; }
            else if (u == "F10") { c.evdevKey = KEY_F10; c.keysym = 0xffc7; }
            else if (u == "F11") { c.evdevKey = KEY_F11; c.keysym = 0xffc8; }
            else if (u == "F12") { c.evdevKey = KEY_F12; c.keysym = 0xffc9; }
            else if (u == "SPACE") { c.evdevKey = KEY_SPACE; c.keysym = 0x0020; }
            else if (u == "TAB") { c.evdevKey = KEY_TAB; c.keysym = 0xff09; }
            else if (u.size() == 1) {
                char ch = u[0];
                if (ch >= 'A' && ch <= 'Z') {
                    static const int letterToEvdev[26] = {
                        KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I,
                        KEY_J, KEY_K, KEY_L, KEY_M, KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R,
                        KEY_S, KEY_T, KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z
                    };
                    c.evdevKey = letterToEvdev[ch - 'A'];
                    c.keysym = static_cast<unsigned long>(std::tolower(ch));
                } else if (ch >= '0' && ch <= '9') {
                    if (ch == '0') c.evdevKey = KEY_0;
                    else c.evdevKey = KEY_1 + (ch - '1');
                    c.keysym = static_cast<unsigned long>(ch);
                }
            }
        }
        return c;
    }
};

class InGameHotkeyManager {
public:
    InGameHotkeyManager() {
        SetDefaultChords();
    }

    ~InGameHotkeyManager() {
        CloseEvdev();
        if (dpy_ && pXCloseDisplay_) {
            pXCloseDisplay_(dpy_);
            dpy_ = nullptr;
        }
        if (x11Lib_) {
            dlclose(x11Lib_);
            x11Lib_ = nullptr;
        }
    }

    void SetDefaultChords() {
        chordOverlay_ = HotkeyChord::Parse("Ctrl+Shift+O");
        chordPreset_ = HotkeyChord::Parse("Ctrl+Shift+P");
        chordCapture_ = HotkeyChord::Parse("Ctrl+Shift+K");
    }

    void UpdateChords(const std::string& overlayStr, const std::string& presetStr, const std::string& captureStr) {
        if (!overlayStr.empty() && overlayStr != chordOverlay_.rawStr) {
            chordOverlay_ = HotkeyChord::Parse(overlayStr);
        }
        if (!presetStr.empty() && presetStr != chordPreset_.rawStr) {
            chordPreset_ = HotkeyChord::Parse(presetStr);
        }
        if (!captureStr.empty() && captureStr != chordCapture_.rawStr) {
            chordCapture_ = HotkeyChord::Parse(captureStr);
        }
    }

    struct HotkeyEvents {
        bool toggleOverlay = false;
        bool cyclePreset = false;
        bool toggleCapture = false;
    };

    HotkeyEvents Poll(uint64_t nowNs) {
        HotkeyEvents ev;
        if (nowNs < lastCheckNs_ + 25'000'000ULL) {
            return ev; // 40 Hz polling throttle
        }
        lastCheckNs_ = nowNs;

        // 1. Evdev polling
        PollEvdev(ev, nowNs);

        // 2. X11 fallback (if evdev didn't trigger)
        if (!ev.toggleOverlay && !ev.cyclePreset && !ev.toggleCapture) {
            PollX11(ev, nowNs);
        }

        return ev;
    }

private:
    void CloseEvdev() {
        for (int fd : evdevFds_) {
            if (fd >= 0) close(fd);
        }
        evdevFds_.clear();
    }

    void ScanEvdev(uint64_t nowNs) {
        if (nowNs < lastEvdevScanNs_ + 3'000'000'000ULL && !evdevFds_.empty()) {
            return;
        }
        lastEvdevScanNs_ = nowNs;

        CloseEvdev();
        DIR* dir = opendir("/dev/input");
        if (!dir) return;

        struct dirent* ent;
        while ((ent = readdir(dir)) != nullptr) {
            if (strncmp(ent->d_name, "event", 5) == 0) {
                std::string path = std::string("/dev/input/") + ent->d_name;
                int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
                if (fd >= 0) {
                    // Check if device supports EV_KEY
                    unsigned long evBits[(EV_MAX + 7) / 8]{};
                    if (ioctl(fd, EVIOCGBIT(0, sizeof(evBits)), evBits) >= 0) {
                        if (evBits[EV_KEY / 8] & (1 << (EV_KEY % 8))) {
                            evdevFds_.push_back(fd);
                            continue;
                        }
                    }
                    close(fd);
                }
            }
        }
        closedir(dir);
    }

    void PollEvdev(HotkeyEvents& out, uint64_t nowNs) {
        ScanEvdev(nowNs);

        for (int efd : evdevFds_) {
            struct input_event iev[16];
            ssize_t n = read(efd, iev, sizeof(iev));
            if (n <= 0) continue;

            size_t count = n / sizeof(struct input_event);
            for (size_t i = 0; i < count; ++i) {
                if (iev[i].type != EV_KEY) continue;

                int code = iev[i].code;
                int val = iev[i].value; // 0 = release, 1 = press, 2 = repeat

                if (code == KEY_LEFTCTRL || code == KEY_RIGHTCTRL) {
                    ctrlDown_ = (val != 0);
                } else if (code == KEY_LEFTSHIFT || code == KEY_RIGHTSHIFT) {
                    shiftDown_ = (val != 0);
                } else if (code == KEY_LEFTALT || code == KEY_RIGHTALT) {
                    altDown_ = (val != 0);
                }

                if (val == 1) { // Key down edge
                    // Overlay toggle check (chord OR F9 fallback)
                    if ((chordOverlay_.evdevKey > 0 && code == chordOverlay_.evdevKey &&
                         ctrlDown_ == chordOverlay_.ctrl && shiftDown_ == chordOverlay_.shift && altDown_ == chordOverlay_.alt) ||
                        (code == KEY_F9)) {
                        out.toggleOverlay = true;
                    }
                    // Preset cycle check (chord OR F8/F11 fallback)
                    else if ((chordPreset_.evdevKey > 0 && code == chordPreset_.evdevKey &&
                              ctrlDown_ == chordPreset_.ctrl && shiftDown_ == chordPreset_.shift && altDown_ == chordPreset_.alt) ||
                             (code == KEY_F8 || code == KEY_F11)) {
                        out.cyclePreset = true;
                    }
                    // Capture toggle check (chord OR F10 fallback)
                    else if ((chordCapture_.evdevKey > 0 && code == chordCapture_.evdevKey &&
                              ctrlDown_ == chordCapture_.ctrl && shiftDown_ == chordCapture_.shift && altDown_ == chordCapture_.alt) ||
                             (code == KEY_F10)) {
                        out.toggleCapture = true;
                    }
                }
            }
        }
    }

    void EnsureX11() {
        if (x11Tried_) return;
        x11Tried_ = true;

        x11Lib_ = dlopen("libX11.so.6", RTLD_LAZY);
        if (!x11Lib_) x11Lib_ = dlopen("libX11.so", RTLD_LAZY);
        if (!x11Lib_) return;

        pXOpenDisplay_ = (XOpenDisplay_fn)dlsym(x11Lib_, "XOpenDisplay");
        pXCloseDisplay_ = (XCloseDisplay_fn)dlsym(x11Lib_, "XCloseDisplay");
        pXQueryKeymap_ = (XQueryKeymap_fn)dlsym(x11Lib_, "XQueryKeymap");
        pXKeysymToKeycode_ = (XKeysymToKeycode_fn)dlsym(x11Lib_, "XKeysymToKeycode");

        if (pXOpenDisplay_) {
            dpy_ = pXOpenDisplay_(nullptr);
        }
    }

    bool CheckX11Chord(void* dpy, const char keys[32], const HotkeyChord& chord, unsigned long fallbackKeysym) {
        if (!dpy || !pXKeysymToKeycode_) return false;

        auto isSymDown = [&](unsigned long ks) -> bool {
            if (ks == 0) return false;
            uint8_t kc = pXKeysymToKeycode_(dpy, ks);
            if (kc == 0) return false;
            return (keys[kc >> 3] & (1 << (kc & 7))) != 0;
        };

        // Check fallback direct single key
        if (fallbackKeysym != 0 && isSymDown(fallbackKeysym)) {
            return true;
        }

        if (chord.keysym == 0) return false;

        bool target = isSymDown(chord.keysym);
        if (!target) return false;

        if (chord.ctrl) {
            bool cDown = isSymDown(0xffe3 /* XK_Control_L */) || isSymDown(0xffe4 /* XK_Control_R */);
            if (!cDown) return false;
        }
        if (chord.shift) {
            bool sDown = isSymDown(0xffe1 /* XK_Shift_L */) || isSymDown(0xffe2 /* XK_Shift_R */);
            if (!sDown) return false;
        }
        if (chord.alt) {
            bool aDown = isSymDown(0xffe9 /* XK_Alt_L */) || isSymDown(0xffea /* XK_Alt_R */);
            if (!aDown) return false;
        }

        return true;
    }

    void PollX11(HotkeyEvents& out, uint64_t nowNs) {
        EnsureX11();
        if (!dpy_ && nowNs > lastX11RetryNs_ + 2'000'000'000ULL) {
            lastX11RetryNs_ = nowNs;
            if (pXOpenDisplay_) dpy_ = pXOpenDisplay_(nullptr);
        }
        if (!dpy_ || !pXQueryKeymap_) return;

        char keys[32]{};
        pXQueryKeymap_(dpy_, keys);

        bool curOverlay = CheckX11Chord(dpy_, keys, chordOverlay_, 0xffc6 /* XK_F9 */);
        if (curOverlay && !wasX11Overlay_) {
            out.toggleOverlay = true;
        }
        wasX11Overlay_ = curOverlay;

        bool curPreset = CheckX11Chord(dpy_, keys, chordPreset_, 0xffc5 /* XK_F8 */);
        if (!curPreset) curPreset = CheckX11Chord(dpy_, keys, chordPreset_, 0xffc8 /* XK_F11 */);
        if (curPreset && !wasX11Preset_) {
            out.cyclePreset = true;
        }
        wasX11Preset_ = curPreset;

        bool curCapture = CheckX11Chord(dpy_, keys, chordCapture_, 0xffc7 /* XK_F10 */);
        if (curCapture && !wasX11Capture_) {
            out.toggleCapture = true;
        }
        wasX11Capture_ = curCapture;
    }

    HotkeyChord chordOverlay_;
    HotkeyChord chordPreset_;
    HotkeyChord chordCapture_;

    uint64_t lastCheckNs_ = 0;
    uint64_t lastEvdevScanNs_ = 0;
    std::vector<int> evdevFds_;

    bool ctrlDown_ = false;
    bool shiftDown_ = false;
    bool altDown_ = false;

    // X11 dynamic loader
    typedef void* (*XOpenDisplay_fn)(const char*);
    typedef int (*XCloseDisplay_fn)(void*);
    typedef int (*XQueryKeymap_fn)(void*, char[32]);
    typedef unsigned char (*XKeysymToKeycode_fn)(void*, unsigned long);

    void* x11Lib_ = nullptr;
    XOpenDisplay_fn pXOpenDisplay_ = nullptr;
    XCloseDisplay_fn pXCloseDisplay_ = nullptr;
    XQueryKeymap_fn pXQueryKeymap_ = nullptr;
    XKeysymToKeycode_fn pXKeysymToKeycode_ = nullptr;
    void* dpy_ = nullptr;
    bool x11Tried_ = false;
    uint64_t lastX11RetryNs_ = 0;

    bool wasX11Overlay_ = false;
    bool wasX11Preset_ = false;
    bool wasX11Capture_ = false;
};

} // namespace gnumon::layer
