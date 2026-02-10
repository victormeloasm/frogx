# 🐸 **FrogX — nano, but not stupid**

<div align="center">
  <img src="assets/logo.png" width="280">
</div>

<p align="center">
  <b>A minimal terminal text editor that refuses to be dumb.</b><br>
  Clean. Fast. Pure C. No bloat. No colors. No bullshit.<br>
</p>

---

## 🚀 **What’s New in 2.0 (C Edition)**

FrogX 2.0 is a **full rewrite in C**, designed to be faster, smaller and safer:

* 🧬 **Rewritten from scratch in pure C** (no C++, no classes, no runtime overhead)
* ⚡ **Low-level I/O** using `read()` and `write()` for maximum speed and control
* 📦 **Tiny static binary (~15 KB)** with aggressive compiler/linker optimizations
* 🔄 **Zero flicker** — fully stable ncurses UI with correct screen redraw logic
* 🧼 **Clean architecture**: single file, zero abstractions, zero magic
* 🧠 **Robust internal buffer** with safe cursor movement and boundaries
* ✂️ **Cut/Uncut mechanics fully fixed** (Ctrl+K / Ctrl+U)
* 🖱️ **Right-click paste working across terminals**
* 🔍 **Search improved** (Ctrl+W with stable viewport updates)
* 📌 **More reliable status & help bars**
* 🐸 **Optimized ASCII splash**, no delay and no visual artifacts
* 🧱 **No dependencies except ncurses**
* 🔐 **Safer memory handling**, no leaks, no undefined behavior

FrogX is now **as small as nano**, but without its limitations — and without its weight.

---

## 📥 **Download**

🎉 **Latest release (v2.0, C edition):**

👉 **[https://github.com/victormeloasm/frogx/releases/download/v2.0/frogx-2.0.0-linux-x86_64.tar.gz](https://github.com/victormeloasm/frogx/releases/download/v2.0/frogx-2.0.0-linux-x86_64.tar.gz)**

Extract and place the executable wherever you prefer.

---

## 🔧 **Build From Source**

### **Requirements**

* `gcc` or `clang` with full C11 support
* `ncurses` development headers
* `lld` (optional) for extreme binary size reduction

### **Compile**

```bash
gcc -std=c11 -O3 -ffunction-sections -fdata-sections -flto -fuse-ld=lld \
    -s -o frogx frogx.c -lncurses
```

Optional flags to reach the ~15 KB binary:

```bash
-Wl,--gc-sections,--strip-all
```

### **Install globally**

```bash
sudo mv frogx /usr/local/bin/
sudo chmod 755 /usr/local/bin/frogx
```

---

## 🧠 **Keyboard Shortcuts**

| Shortcut             | Action                         |
| -------------------- | ------------------------------ |
| **Ctrl+X**           | Exit (double-press if unsaved) |
| **Ctrl+O**           | Save with filename prompt      |
| **Ctrl+A**           | Select all                     |
| **Delete/Backspace** | Delete characters              |
| **Ctrl+K**           | Cut line                       |
| **Ctrl+U**           | Uncut (paste)                  |
| **Ctrl+W**           | Search forward                 |
| **Arrow Keys**       | Move cursor                    |
| **Home / End**       | Jump to line start/end         |
| **Page Up/Down**     | Scroll viewport                |
| **Right-Click**      | Paste                          |

---

## 📝 **Changelog (v2.0.0)**

* Complete rewrite in C
* Low-level I/O (`read`, `write`)
* Stable redraw (no flicker, no UI glitches)
* Faster search, cut, paste, and navigation
* Improved status/help bar
* Fixed splash screen timing and positioning
* Global install support
* ~15 KB optimized release binary
* Overall: cleaner, faster, safer, smaller

---

## 🐸 **Why FrogX Exists**

Because the world doesn’t need another bloated editor.

Sometimes you just need:

* instant startup
* predictable behavior
* zero dependencies
* zero configs
* zero surprises

**Open → edit → save → quit.**
Everything else is noise.

---

## 🐸💚 **Part of the FrogTools ecosystem**

Security. Cryptography. Editors. Compression.
Everything light, clean and open-source — from the Porquinho.

