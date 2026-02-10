# 🐸 **FrogX — nano, but not stupid**

<div align="center">
  <img src="assets/logo.png" width="280">
</div>

<p align="center">
  <b>A minimal terminal text editor that refuses to be dumb.</b><br>
  Clean. Fast. Pure C. Low-level Linux terminal API. No bloat. No colors. No bullshit.<br>
</p>

---

## 🚀 **FrogX 2.0 is pure low-level**

FrogX 2.0 is a **full rewrite in C** focused on **minimalism and control**.

No ncurses. No UI libraries. No dependencies.

It runs directly on the Linux terminal stack using:

* `termios` for raw mode input
* `ioctl(TIOCGWINSZ)` for terminal size
* ANSI escape sequences for rendering
* low-level I/O via `read()` and `write()`

This is the kind of software that makes you remember why Linux is beautiful.

---

## ✅ **Features**

* Pure **C11** codebase
* **Raw terminal mode** with deterministic input handling
* **Fast screen refresh** using ANSI escape sequences
* **Status bar** with filename, modified flag, Ln/Col, and messages
* **Help bar** with the core shortcuts
* Opens missing files as an empty buffer
* **Ctrl+O** save with filename prompt (nano-style)
* **Ctrl+X** exit (double press if modified)
* **Ctrl+K** cut line, **Ctrl+U** uncut
* **Ctrl+W** forward search
* **Ctrl+A** select all (Backspace/Delete clears the entire buffer)
* Arrow keys, Home/End, Page Up/Down
* Tiny binary when built with LTO + section GC + strip

Note: input insertion is **printable ASCII** (32..126). This is intentional for simplicity and speed.

---

## 📥 **Download (v2.0)**

Latest release:

👉 **[https://github.com/victormeloasm/frogx/releases/download/v2.0/frogx-2.0.0-linux-x86_64.tar.gz](https://github.com/victormeloasm/frogx/releases/download/v2.0/frogx-2.0.0-linux-x86_64.tar.gz)**

### Install from release

```bash
curl -L -o frogx.tar.gz https://github.com/victormeloasm/frogx/releases/download/v2.0/frogx-2.0.0-linux-x86_64.tar.gz
tar -xzf frogx.tar.gz

sudo install -m 755 frogx /usr/local/bin/frogx 2>/dev/null || sudo install -m 755 */frogx /usr/local/bin/frogx
```

Run:

```bash
frogx file.txt
```

---

## 🔧 **Build From Source**

### Requirements

* Linux
* `gcc` or `clang` (C11)

That’s it. No libraries.

### Compile (simple)

```bash
gcc -std=c11 -O2 -Wall -Wextra -o frogx frogx.c
```

### Compile (tiny and fast)

This is the build style used to shrink the binary aggressively:

```bash
clang -std=c11 -Os -ffunction-sections -fdata-sections -flto -fuse-ld=lld \
  -Wl,--gc-sections,--strip-all \
  -o frogx frogx.c
```

### Install globally

```bash
sudo install -m 755 frogx /usr/local/bin/frogx
```

### Uninstall

```bash
sudo rm -f /usr/local/bin/frogx
```

---

## 🧠 **Keyboard Shortcuts**

| Shortcut           | Action                                          |
| ------------------ | ----------------------------------------------- |
| **Ctrl+X**         | Exit (double press if modified)                 |
| **Ctrl+O**         | Save (prompts for filename)                     |
| **Ctrl+A**         | Select all (Backspace/Delete clears everything) |
| **Ctrl+K**         | Cut line                                        |
| **Ctrl+U**         | Uncut (paste cut line)                          |
| **Ctrl+W**         | Search forward                                  |
| **Arrow Keys**     | Move cursor                                     |
| **Home / End**     | Line start / end                                |
| **Page Up / Down** | Scroll by screen                                |
| **Backspace**      | Delete left                                     |
| **Delete**         | Delete under cursor                             |

---

## 📝 **Changelog (v2.0.0)**

* Full rewrite in pure C
* Removed UI libraries entirely
* Raw terminal mode input (`termios`)
* Terminal sizing via `ioctl`
* Rendering via ANSI escape sequences
* Low-level `read()` and `write()` I/O
* Stable refresh and cursor logic
* Better save prompt and status messages
* Smaller, faster, cleaner

---

## 🐸 **Why FrogX Exists**

Because sometimes you want a text editor that does exactly this:

Open file
Edit
Save
Quit

No plugins. No config maze. No delays.

Just a tiny editor that stays close to the metal.

---

## 🐸💚 **Part of the FrogTools ecosystem**

Everything minimal. Everything fast. Everything open source.
Built by Victor Duarte with Love <3


