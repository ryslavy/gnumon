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

        bool isOverlayDown = false;
        bool isPresetDown = false;
        bool isCaptureDown = false;

        // 1. Evdev polling
        CheckEvdevState(nowNs, isOverlayDown, isPresetDown, isCaptureDown);

        // 2. X11 fallback / check
        CheckX11State(nowNs, isOverlayDown, isPresetDown, isCaptureDown);

        // 3. Unified edge-triggered transitions with minimum debounce cooldown
        if (isOverlayDown && !wasOverlayDown_ && (nowNs >= lastOverlayToggleNs_ + 300'000'000ULL)) {
            ev.toggleOverlay = true;
            lastOverlayToggleNs_ = nowNs;
        }
        wasOverlayDown_ = isOverlayDown;

        if (isPresetDown && !wasPresetDown_ && (nowNs >= lastPresetCycleNs_ + 250'000'000ULL)) {
            ev.cyclePreset = true;
            lastPresetCycleNs_ = nowNs;
        }
        wasPresetDown_ = isPresetDown;

        if (isCaptureDown && !wasCaptureDown_ && (nowNs >= lastCaptureToggleNs_ + 500'000'000ULL)) {
            ev.toggleCapture = true;
            lastCaptureToggleNs_ = nowNs;
        }
        wasCaptureDown_ = isCaptureDown;

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
        if (!evdevFds_.empty() && nowNs < lastEvdevScanNs_ + 10'000'000'000ULL) {
            return; // Already initialized keyboard fds
        }
        lastEvdevScanNs_ = nowNs;

        if (evdevFds_.empty()) {
            DIR* dir = opendir("/dev/input");
            if (!dir) return;

            struct dirent* ent;
            while ((ent = readdir(dir)) != nullptr) {
                if (strncmp(ent->d_name, "event", 5) == 0) {
                    std::string path = std::string("/dev/input/") + ent->d_name;
                    int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
                    if (fd >= 0) {
                        // Check if device supports EV_KEY and has keyboard keys
                        unsigned long evBits[(EV_MAX + 7) / 8]{};
                        if (ioctl(fd, EVIOCGBIT(0, sizeof(evBits)), evBits) >= 0) {
                            if (evBits[EV_KEY / 8] & (1 << (EV_KEY % 8))) {
                                unsigned long keyBits[(KEY_MAX + 7) / 8]{};
                                if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keyBits)), keyBits) >= 0) {
                                    // Must support standard keyboard keys (KEY_A, KEY_SPACE, KEY_F1, KEY_ESC)
                                    bool isKbd = (keyBits[KEY_A / 8] & (1 << (KEY_A % 8))) ||
                                                 (keyBits[KEY_SPACE / 8] & (1 << (KEY_SPACE % 8))) ||
                                                 (keyBits[KEY_F1 / 8] & (1 << (KEY_F1 % 8))) ||
                                                 (keyBits[KEY_ESC / 8] & (1 << (KEY_ESC % 8)));
                                    if (isKbd) {
                                        evdevFds_.push_back(fd);
                                        continue;
                                    }
                                }
                            }
                        }
                        close(fd);
                    }
                }
            }
            closedir(dir);
        }
    }

    void CheckEvdevState(uint64_t nowNs, bool& overlayDown, bool& presetDown, bool& captureDown) {
        ScanEvdev(nowNs);
        if (evdevFds_.empty()) return;

        uint8_t keyStates[(KEY_MAX + 7) / 8]{};
        for (int efd : evdevFds_) {
            uint8_t devKeyStates[(KEY_MAX + 7) / 8]{};
            if (ioctl(efd, EVIOCGKEY(sizeof(devKeyStates)), devKeyStates) >= 0) {
                for (size_t i = 0; i < sizeof(keyStates); ++i) {
                    keyStates[i] |= devKeyStates[i];
                }
            }
        }

        auto isDown = [&](int k) -> bool {
            if (k <= 0 || k > KEY_MAX) return false;
            return (keyStates[k / 8] & (1 << (k % 8))) != 0;
        };

        bool cDown = isDown(KEY_LEFTCTRL) || isDown(KEY_RIGHTCTRL);
        bool sDown = isDown(KEY_LEFTSHIFT) || isDown(KEY_RIGHTSHIFT);
        bool aDown = isDown(KEY_LEFTALT) || isDown(KEY_RIGHTALT);

        auto testChord = [&](const HotkeyChord& chord, int fb1, int fb2 = 0) -> bool {
            if (fb1 > 0 && isDown(fb1)) return true;
            if (fb2 > 0 && isDown(fb2)) return true;
            if (chord.evdevKey <= 0) return false;
            if (!isDown(chord.evdevKey)) return false;
            if (chord.ctrl && !cDown) return false;
            if (chord.shift && !sDown) return false;
            if (chord.alt && !aDown) return false;
            return true;
        };

        if (testChord(chordOverlay_, KEY_F9)) overlayDown = true;
        if (testChord(chordPreset_, KEY_F8, KEY_F11)) presetDown = true;
        if (testChord(chordCapture_, KEY_F10)) captureDown = true;
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

    void CheckX11State(uint64_t nowNs, bool& overlayDown, bool& presetDown, bool& captureDown) {
        EnsureX11();
        if (!dpy_ && nowNs > lastX11RetryNs_ + 2'000'000'000ULL) {
            lastX11RetryNs_ = nowNs;
            if (pXOpenDisplay_) dpy_ = pXOpenDisplay_(nullptr);
        }
        if (!dpy_ || !pXQueryKeymap_) return;

        char keys[32]{};
        pXQueryKeymap_(dpy_, keys);

        if (!overlayDown) {
            overlayDown = CheckX11Chord(dpy_, keys, chordOverlay_, 0xffc6 /* XK_F9 */);
        }
        if (!presetDown) {
            presetDown = CheckX11Chord(dpy_, keys, chordPreset_, 0xffc5 /* XK_F8 */) ||
                         CheckX11Chord(dpy_, keys, chordPreset_, 0xffc8 /* XK_F11 */);
        }
        if (!captureDown) {
            captureDown = CheckX11Chord(dpy_, keys, chordCapture_, 0xffc7 /* XK_F10 */);
        }
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

    bool wasOverlayDown_ = false;
    bool wasPresetDown_ = false;
    bool wasCaptureDown_ = false;
    uint64_t lastOverlayToggleNs_ = 0;
    uint64_t lastPresetCycleNs_ = 0;
    uint64_t lastCaptureToggleNs_ = 0;
};

} // namespace gnumon::layer
