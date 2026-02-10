/*
___________                   ____  ___
\_   _____/______  ____   ____\   \/  /
 |    __) \_  __ \/  _ \ / ___\\     /
 |     \   |  | \(  <_> ) /_/  >     \
 \___  /   |__|   \____/\___  /___/\  \
     \/                /_____/      \_/

                FrogX - nano but not stupid (v2.0 C lowlevel)

   Part of FrogTools.
   Created by Victor Duarte (Porquinho) + SapoGPT 
*/

// frogx.c - FrogX: minimal nano-like editor (C11, English UI, low-level terminal)
// Controls:
//   Ctrl+O  -> prompt for file name then save
//   Ctrl+X  -> exit (twice if modified, exits without saving)
//   Ctrl+A  -> mark "ALL SELECTED"
//   Backspace/Delete with ALL SELECTED -> clear entire buffer
//   Ctrl+K  -> cut current line
//   Ctrl+U  -> paste last cut line
//   Ctrl+W  -> search text (forward)
//   Arrows  -> move cursor
//   Home/End -> line start/end
//   PageUp/PageDown -> scroll
//
// No syntax highlight, no colors. Status bar + nano-like help bar at the bottom.

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/* ===================== Low-level macros ===================== */

#define CTRL_KEY(k) ((k) & 0x1f)

enum EditorKey {
    KEY_NULL = 0,
    KEY_ESC  = 27,
    KEY_BACKSPACE = 127,

    KEY_ARROW_LEFT = 1000,
    KEY_ARROW_RIGHT,
    KEY_ARROW_UP,
    KEY_ARROW_DOWN,
    KEY_DELETE,
    KEY_HOME,
    KEY_END,
    KEY_PAGE_UP,
    KEY_PAGE_DOWN
};

/* ===================== Terminal raw mode ===================== */

static struct termios g_orig_termios;

static void die(const char *msg) {
    (void)msg;
    /* best effort: restore screen + cursor */
    write(STDOUT_FILENO, "\x1b[2J\x1b[H\x1b[?25h", 13);
    _exit(1);
}

static void disable_raw_mode(void) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_orig_termios);
}

static void enable_raw_mode(void) {
    if (tcgetattr(STDIN_FILENO, &g_orig_termios) == -1) die("tcgetattr");
    atexit(disable_raw_mode);

    struct termios raw = g_orig_termios;

    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

    raw.c_cc[VMIN]  = 0;
    raw.c_cc[VTIME] = 1; /* 100ms */

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

static void sleep_us(long usec) {
    if (usec <= 0) return;
    struct timespec ts;
    ts.tv_sec  = usec / 1000000L;
    ts.tv_nsec = (usec % 1000000L) * 1000L;
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR) {}
}

static int get_window_size(int *rows, int *cols) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) return -1;
    *cols = (int)ws.ws_col;
    *rows = (int)ws.ws_row;
    return 0;
}

static int read_key(void) {
    char c;
    while (1) {
        ssize_t n = read(STDIN_FILENO, &c, 1);
        if (n == 1) break;
        if (n == -1 && errno != EAGAIN) die("read");
    }

    if (c == '\x1b') {
        char seq[4];
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return KEY_ESC;
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return KEY_ESC;

        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                if (read(STDIN_FILENO, &seq[2], 1) != 1) return KEY_ESC;
                if (seq[2] == '~') {
                    switch (seq[1]) {
                        case '1': return KEY_HOME;
                        case '3': return KEY_DELETE;
                        case '4': return KEY_END;
                        case '5': return KEY_PAGE_UP;
                        case '6': return KEY_PAGE_DOWN;
                        case '7': return KEY_HOME;
                        case '8': return KEY_END;
                    }
                }
            } else {
                switch (seq[1]) {
                    case 'A': return KEY_ARROW_UP;
                    case 'B': return KEY_ARROW_DOWN;
                    case 'C': return KEY_ARROW_RIGHT;
                    case 'D': return KEY_ARROW_LEFT;
                    case 'H': return KEY_HOME;
                    case 'F': return KEY_END;
                }
            }
        } else if (seq[0] == 'O') {
            switch (seq[1]) {
                case 'H': return KEY_HOME;
                case 'F': return KEY_END;
            }
        }
        return KEY_ESC;
    }

    return (unsigned char)c;
}

/* ===================== Tiny append buffer (single write per frame) ===================== */

struct abuf {
    char *b;
    size_t len;
};

static void ab_init(struct abuf *ab) {
    ab->b = NULL;
    ab->len = 0;
}

static void ab_append(struct abuf *ab, const char *s, size_t n) {
    char *newb = (char*)realloc(ab->b, ab->len + n);
    if (!newb) die("realloc");
    memcpy(newb + ab->len, s, n);
    ab->b = newb;
    ab->len += n;
}

static void ab_append_cstr(struct abuf *ab, const char *s) {
    if (!s) return;
    ab_append(ab, s, strlen(s));
}

static void ab_free(struct abuf *ab) {
    free(ab->b);
}

/* ===================== Minimal string helpers ===================== */

static char *xstrdup(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s);
    char *p = (char*)malloc(n + 1);
    if (!p) die("malloc");
    memcpy(p, s, n + 1);
    return p;
}

static void str_set(char *dst, size_t cap, const char *src) {
    if (!dst || cap == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    size_t n = strlen(src);
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static void u64_to_dec(char *out, size_t cap, uint64_t v) {
    if (!out || cap == 0) return;
    char tmp[32];
    size_t i = 0;
    if (v == 0) tmp[i++] = '0';
    while (v && i < sizeof(tmp)) {
        tmp[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    size_t n = (i < cap - 1) ? i : (cap - 1);
    for (size_t j = 0; j < n; j++) out[j] = tmp[i - 1 - j];
    out[n] = '\0';
}

/* ===================== Editor data ===================== */

typedef struct {
    int len;
    char *chars; /* NUL-terminated */
} Row;

typedef struct {
    Row *rows;
    int numrows;
    int caprows;

    int cx, cy;      /* cursor x,y in text */
    int rowoff;      /* vertical scroll offset */
    int coloff;      /* horizontal scroll offset */

    int screenrows;
    int screencols;

    int modified;
    int select_all;
    int quit_pending;

    char *filename;
    char *cutbuf;

    char statusmsg[160];
} EditorState;

static EditorState E;

/* forward */
static void editor_refresh_screen(void);

/* ===================== Row ops ===================== */

static void row_free(Row *r) {
    if (!r) return;
    free(r->chars);
    r->chars = NULL;
    r->len = 0;
}

static void row_set(Row *r, const char *s, int len) {
    if (!r) return;
    if (len < 0) len = 0;
    r->chars = (char*)realloc(r->chars, (size_t)len + 1);
    if (!r->chars) die("realloc");
    if (len > 0 && s) memcpy(r->chars, s, (size_t)len);
    r->chars[len] = '\0';
    r->len = len;
}

static void row_insert_char(Row *r, int at, int c) {
    if (!r) return;
    if (at < 0) at = 0;
    if (at > r->len) at = r->len;

    r->chars = (char*)realloc(r->chars, (size_t)r->len + 2);
    if (!r->chars) die("realloc");

    memmove(&r->chars[at + 1], &r->chars[at], (size_t)(r->len - at + 1));
    r->chars[at] = (char)c;
    r->len++;
}

static void row_del_char(Row *r, int at) {
    if (!r) return;
    if (at < 0 || at >= r->len) return;
    memmove(&r->chars[at], &r->chars[at + 1], (size_t)(r->len - at));
    r->len--;
}

/* ===================== Editor core helpers ===================== */

static void editor_set_status(const char *msg) {
    str_set(E.statusmsg, sizeof(E.statusmsg), msg ? msg : "");
}

static void editor_ensure_non_empty(void) {
    if (E.numrows != 0) return;

    E.rows = (Row*)realloc(E.rows, sizeof(Row));
    if (!E.rows) die("realloc");
    E.caprows = 1;
    E.numrows = 1;
    E.rows[0].chars = NULL;
    E.rows[0].len = 0;
    row_set(&E.rows[0], "", 0);
}

static void editor_grow_rows(void) {
    int newcap = (E.caprows == 0) ? 8 : (E.caprows * 2);
    E.rows = (Row*)realloc(E.rows, (size_t)newcap * sizeof(Row));
    if (!E.rows) die("realloc");
    for (int i = E.caprows; i < newcap; i++) {
        E.rows[i].chars = NULL;
        E.rows[i].len = 0;
    }
    E.caprows = newcap;
}

static void editor_insert_row(int at, const char *s, int len) {
    if (at < 0) at = 0;
    if (at > E.numrows) at = E.numrows;

    if (E.numrows == E.caprows) editor_grow_rows();

    memmove(&E.rows[at + 1], &E.rows[at], (size_t)(E.numrows - at) * sizeof(Row));
    E.rows[at].chars = NULL;
    E.rows[at].len = 0;
    row_set(&E.rows[at], s, len);
    E.numrows++;
    E.modified = 1;
}

static void editor_del_row(int at) {
    if (at < 0 || at >= E.numrows) return;
    row_free(&E.rows[at]);
    memmove(&E.rows[at], &E.rows[at + 1], (size_t)(E.numrows - at - 1) * sizeof(Row));
    E.numrows--;
    if (E.numrows == 0) editor_ensure_non_empty();
    E.modified = 1;
}

static void editor_clamp_cursor(void) {
    editor_ensure_non_empty();
    if (E.cy < 0) E.cy = 0;
    if (E.cy >= E.numrows) E.cy = E.numrows - 1;
    if (E.cy < 0) E.cy = 0;

    Row *row = &E.rows[E.cy];
    if (E.cx < 0) E.cx = 0;
    if (E.cx > row->len) E.cx = row->len;
}

/* ===================== File I/O: open/read/write ===================== */

static void editor_clear_all(void) {
    for (int i = 0; i < E.numrows; i++) row_free(&E.rows[i]);
    free(E.rows);
    E.rows = NULL;
    E.numrows = 0;
    E.caprows = 0;

    editor_ensure_non_empty();
    E.cx = E.cy = 0;
    E.rowoff = E.coloff = 0;

    E.modified = 1;
    E.select_all = 0;

    free(E.cutbuf);
    E.cutbuf = NULL;

    editor_set_status("Buffer cleared.");
}

static void editor_load_file(const char *fname) {
    if (!fname || !fname[0]) fname = "unnamed.txt";

    free(E.filename);
    E.filename = xstrdup(fname);

    /* wipe current buffer */
    for (int i = 0; i < E.numrows; i++) row_free(&E.rows[i]);
    free(E.rows);
    E.rows = NULL;
    E.numrows = 0;
    E.caprows = 0;

    int fd = open(E.filename, O_RDONLY);
    if (fd >= 0) {
        struct stat st;
        if (fstat(fd, &st) == 0 && st.st_size > 0) {
            size_t sz = (size_t)st.st_size;
            char *buf = (char*)malloc(sz + 1);
            if (!buf) die("malloc");

            size_t off = 0;
            while (off < sz) {
                ssize_t n = read(fd, buf + off, sz - off);
                if (n <= 0) break;
                off += (size_t)n;
            }
            buf[off] = '\0';

            char *p = buf;
            char *line = p;
            for (; *p; p++) {
                if (*p == '\n') {
                    *p = '\0';
                    editor_insert_row(E.numrows, line, (int)strlen(line));
                    line = p + 1;
                }
            }
            editor_insert_row(E.numrows, line, (int)strlen(line));

            E.modified = 0;
            free(buf);
        }
        close(fd);
    }

    editor_ensure_non_empty();
    E.cx = E.cy = 0;
    E.rowoff = E.coloff = 0;
    E.select_all = 0;
    E.quit_pending = 0;
    E.modified = 0;
    E.statusmsg[0] = '\0';
}

static void editor_save_file(void) {
    if (!E.filename) {
        editor_set_status("Error: no file name.");
        return;
    }

    int fd = open(E.filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        editor_set_status("Error: could not save file.");
        return;
    }

    uint64_t written_lines = 0;
    uint64_t written_bytes = 0;

    for (int i = 0; i < E.numrows; i++) {
        Row *r = &E.rows[i];

        if (r->len > 0) {
            size_t off = 0;
            while (off < (size_t)r->len) {
                ssize_t n = write(fd, r->chars + off, (size_t)r->len - off);
                if (n <= 0) break;
                off += (size_t)n;
                written_bytes += (uint64_t)n;
            }
        }
        if (i + 1 < E.numrows) {
            ssize_t n = write(fd, "\n", 1);
            if (n == 1) written_bytes += 1;
        }
        written_lines++;
    }

    close(fd);

    E.modified = 0;
    E.select_all = 0;

    char a[32], b[32];
    u64_to_dec(a, sizeof(a), written_lines);
    u64_to_dec(b, sizeof(b), written_bytes);

    char msg[160];
    size_t pos = 0;

    const char *p1 = "Wrote ";
    const char *p2 = " lines (";
    const char *p3 = " bytes).";

    size_t n1 = strlen(p1), n2 = strlen(p2), n3 = strlen(p3);
    size_t na = strlen(a), nb = strlen(b);

    if (pos + n1 < sizeof(msg)) { memcpy(msg + pos, p1, n1); pos += n1; }
    if (pos + na < sizeof(msg)) { memcpy(msg + pos, a, na); pos += na; }
    if (pos + n2 < sizeof(msg)) { memcpy(msg + pos, p2, n2); pos += n2; }
    if (pos + nb < sizeof(msg)) { memcpy(msg + pos, b, nb); pos += nb; }
    if (pos + n3 < sizeof(msg)) { memcpy(msg + pos, p3, n3); pos += n3; }

    if (pos >= sizeof(msg)) pos = sizeof(msg) - 1;
    msg[pos] = '\0';

    editor_set_status(msg);
}

/* ===================== Prompt (bottom line) ===================== */

static char *editor_prompt(const char *label) {
    size_t cap = 128, len = 0;
    char *buf = (char*)malloc(cap);
    if (!buf) die("malloc");
    buf[0] = '\0';

    for (;;) {
        char tmp[160];
        size_t pos = 0;
        str_set(tmp, sizeof(tmp), "");
        if (label) {
            size_t nl = strlen(label);
            if (nl > sizeof(tmp) - 1) nl = sizeof(tmp) - 1;
            memcpy(tmp, label, nl);
            pos = nl;
        }
        size_t nb = strlen(buf);
        if (pos + nb >= sizeof(tmp)) nb = sizeof(tmp) - 1 - pos;
        memcpy(tmp + pos, buf, nb);
        tmp[pos + nb] = '\0';

        editor_set_status(tmp);
        editor_refresh_screen();

        int c = read_key();

        if (c == KEY_ESC) {
            free(buf);
            editor_set_status("Canceled.");
            return NULL;
        } else if (c == '\r' || c == '\n') {
            return buf;
        } else if (c == KEY_BACKSPACE || c == KEY_DELETE || c == 8) {
            if (len > 0) {
                buf[--len] = '\0';
            }
        } else if (c >= 32 && c <= 126) {
            if (len + 1 >= cap) {
                cap *= 2;
                buf = (char*)realloc(buf, cap);
                if (!buf) die("realloc");
            }
            buf[len++] = (char)c;
            buf[len] = '\0';
        }
    }
}

/* ===================== Search forward ===================== */

static void editor_search_forward(const char *term) {
    if (!term || !term[0]) return;

    for (int r = E.cy; r < E.numrows; r++) {
        Row *row = &E.rows[r];
        int start = (r == E.cy) ? E.cx : 0;
        if (start < 0) start = 0;
        if (start > row->len) start = row->len;

        const char *hay = row->chars + start;
        const char *pos = strstr(hay, term);
        if (pos) {
            E.cy = r;
            E.cx = (int)(pos - row->chars);
            editor_set_status("Match found.");
            return;
        }
    }
    editor_set_status("Not found.");
}

/* ===================== Editing ===================== */

static void editor_insert_char(int c) {
    editor_ensure_non_empty();
    Row *row = &E.rows[E.cy];
    if (E.cx < 0) E.cx = 0;
    if (E.cx > row->len) E.cx = row->len;

    row_insert_char(row, E.cx, c);
    E.cx++;
    E.modified = 1;
    E.select_all = 0;
}

static void editor_insert_newline(void) {
    editor_ensure_non_empty();
    Row *row = &E.rows[E.cy];

    if (E.cx < 0) E.cx = 0;
    if (E.cx > row->len) E.cx = row->len;

    int tail_len = row->len - E.cx;
    const char *tail = row->chars + E.cx;

    row->chars[E.cx] = '\0';
    row->len = E.cx;

    editor_insert_row(E.cy + 1, tail, tail_len);
    E.cy++;
    E.cx = 0;

    E.modified = 1;
    E.select_all = 0;
}

static void editor_backspace(void) {
    editor_ensure_non_empty();

    if (E.select_all) {
        editor_clear_all();
        return;
    }

    if (E.cy == 0 && E.cx == 0) return;

    Row *row = &E.rows[E.cy];

    if (E.cx > 0) {
        row_del_char(row, E.cx - 1);
        E.cx--;
        E.modified = 1;
        E.select_all = 0;
        return;
    }

    Row *prev = &E.rows[E.cy - 1];
    int prev_len = prev->len;

    prev->chars = (char*)realloc(prev->chars, (size_t)prev->len + (size_t)row->len + 1);
    if (!prev->chars) die("realloc");
    memcpy(prev->chars + prev->len, row->chars, (size_t)row->len);
    prev->len += row->len;
    prev->chars[prev->len] = '\0';

    editor_del_row(E.cy);
    E.cy--;
    E.cx = prev_len;

    E.modified = 1;
    E.select_all = 0;
}

static void editor_delete(void) {
    editor_ensure_non_empty();

    if (E.select_all) {
        editor_clear_all();
        return;
    }

    Row *row = &E.rows[E.cy];

    if (E.cx < row->len) {
        row_del_char(row, E.cx);
        E.modified = 1;
        return;
    }

    if (E.cy + 1 < E.numrows) {
        Row *next = &E.rows[E.cy + 1];
        row->chars = (char*)realloc(row->chars, (size_t)row->len + (size_t)next->len + 1);
        if (!row->chars) die("realloc");
        memcpy(row->chars + row->len, next->chars, (size_t)next->len);
        row->len += next->len;
        row->chars[row->len] = '\0';
        editor_del_row(E.cy + 1);
        E.modified = 1;
    }
}

/* ===================== Cut / Uncut line ===================== */

static void editor_cut_line(void) {
    editor_ensure_non_empty();

    free(E.cutbuf);
    E.cutbuf = xstrdup(E.rows[E.cy].chars ? E.rows[E.cy].chars : "");

    editor_del_row(E.cy);
    if (E.cy >= E.numrows) E.cy = E.numrows - 1;
    if (E.cy < 0) E.cy = 0;

    E.cx = 0;
    E.select_all = 0;
    editor_set_status("Line cut.");
}

static void editor_uncut_line(void) {
    if (!E.cutbuf || !E.cutbuf[0]) {
        editor_set_status("Nothing to paste.");
        return;
    }

    int at = E.cy + 1;
    if (at > E.numrows) at = E.numrows;

    editor_insert_row(at, E.cutbuf, (int)strlen(E.cutbuf));
    E.cy = at;
    E.cx = (int)strlen(E.cutbuf);

    E.select_all = 0;
    editor_set_status("Line pasted.");
}

/* ===================== Scrolling & drawing ===================== */

static void editor_scroll(void) {
    if (get_window_size(&E.screenrows, &E.screencols) == -1) die("winsize");
    int textrows = E.screenrows - 2;
    if (textrows < 1) textrows = 1;

    if (E.cy < E.rowoff) E.rowoff = E.cy;
    if (E.cy >= E.rowoff + textrows) E.rowoff = E.cy - textrows + 1;

    if (E.rowoff < 0) E.rowoff = 0;
    if (E.rowoff > E.numrows - 1) E.rowoff = (E.numrows > 0) ? (E.numrows - 1) : 0;

    if (E.cx < E.coloff) E.coloff = E.cx;
    if (E.cx >= E.coloff + E.screencols) E.coloff = E.cx - E.screencols + 1;
    if (E.coloff < 0) E.coloff = 0;
}

static void editor_draw_rows(struct abuf *ab) {
    int textrows = E.screenrows - 2;
    if (textrows < 1) textrows = 1;

    for (int y = 0; y < textrows; y++) {
        int filerow = y + E.rowoff;

        if (filerow >= E.numrows) {
            ab_append(ab, "~", 1);
        } else {
            Row *r = &E.rows[filerow];
            int len = r->len - E.coloff;
            if (len < 0) len = 0;
            if (len > E.screencols) len = E.screencols;
            if (len > 0) ab_append(ab, r->chars + E.coloff, (size_t)len);
        }

        ab_append(ab, "\x1b[K", 3);
        ab_append(ab, "\r\n", 2);
    }
}

static void editor_draw_status_bar(struct abuf *ab) {
    const char *fname = E.filename ? E.filename : "unnamed.txt";

    char ln[32], col[32];
    u64_to_dec(ln, sizeof(ln), (uint64_t)(E.cy + 1));
    u64_to_dec(col, sizeof(col), (uint64_t)(E.cx + 1));

    struct abuf sb;
    ab_init(&sb);

    ab_append_cstr(&sb, fname);
    if (E.modified) ab_append_cstr(&sb, " *");
    ab_append_cstr(&sb, "  Ln ");
    ab_append_cstr(&sb, ln);
    ab_append_cstr(&sb, ", Col ");
    ab_append_cstr(&sb, col);

    if (E.select_all) ab_append_cstr(&sb, "  [ALL SELECTED]");
    if (E.statusmsg[0]) {
        ab_append_cstr(&sb, "  | ");
        ab_append_cstr(&sb, E.statusmsg);
    }

    if ((int)sb.len > E.screencols) sb.len = (size_t)E.screencols;

    ab_append(ab, sb.b ? sb.b : "", sb.len);
    ab_append(ab, "\x1b[K", 3);
    ab_append(ab, "\r\n", 2);

    ab_free(&sb);
}

static void editor_draw_help_bar(struct abuf *ab) {
    const char *help =
        "^O WriteOut  ^X Exit  ^A SelectAll  ^K CutLine  ^U UnCut  ^W WhereIs";
    size_t len = strlen(help);
    if ((int)len > E.screencols) len = (size_t)E.screencols;
    ab_append(ab, help, len);
    ab_append(ab, "\x1b[K", 3);
}

static void editor_refresh_screen(void) {
    editor_scroll();

    struct abuf ab;
    ab_init(&ab);

    ab_append(&ab, "\x1b[?25l", 6);
    ab_append(&ab, "\x1b[H", 3);

    editor_draw_rows(&ab);
    editor_draw_status_bar(&ab);
    editor_draw_help_bar(&ab);

    int textrows = E.screenrows - 2;
    if (textrows < 1) textrows = 1;

    int cx = E.cx - E.coloff;
    int cy = E.cy - E.rowoff;

    if (cy < 0) cy = 0;
    if (cy >= textrows) cy = textrows - 1;
    if (cx < 0) cx = 0;
    if (cx >= E.screencols) cx = E.screencols - 1;

    struct abuf cb;
    ab_init(&cb);

    ab_append(&cb, "\x1b[", 2);

    char rbuf[32], cbuf2[32];
    u64_to_dec(rbuf, sizeof(rbuf), (uint64_t)(cy + 1));
    u64_to_dec(cbuf2, sizeof(cbuf2), (uint64_t)(cx + 1));
    ab_append_cstr(&cb, rbuf);
    ab_append(&cb, ";", 1);
    ab_append_cstr(&cb, cbuf2);
    ab_append(&cb, "H", 1);

    ab_append(&ab, cb.b, cb.len);
    ab_free(&cb);

    ab_append(&ab, "\x1b[?25h", 6);

    if (write(STDOUT_FILENO, ab.b, ab.len) == -1) die("write");
    ab_free(&ab);
}

/* ===================== Movement & paging ===================== */

static void editor_move_cursor(int key) {
    if (E.select_all) E.select_all = 0;

    switch (key) {
        case KEY_ARROW_LEFT:
            if (E.cx > 0) E.cx--;
            else if (E.cy > 0) { E.cy--; E.cx = E.rows[E.cy].len; }
            break;

        case KEY_ARROW_RIGHT: {
            Row *row = &E.rows[E.cy];
            if (E.cx < row->len) E.cx++;
            else if (E.cx == row->len && E.cy + 1 < E.numrows) { E.cy++; E.cx = 0; }
        } break;

        case KEY_ARROW_UP:
            if (E.cy > 0) E.cy--;
            break;

        case KEY_ARROW_DOWN:
            if (E.cy + 1 < E.numrows) E.cy++;
            break;
    }
    editor_clamp_cursor();
}

static void editor_page_move(int key) {
    if (E.select_all) E.select_all = 0;

    int textrows = E.screenrows - 2;
    if (textrows < 1) textrows = 1;

    if (key == KEY_PAGE_UP) {
        E.cy -= textrows;
        if (E.cy < 0) E.cy = 0;
    } else if (key == KEY_PAGE_DOWN) {
        E.cy += textrows;
        if (E.cy >= E.numrows) E.cy = E.numrows - 1;
        if (E.cy < 0) E.cy = 0;
    }
    editor_clamp_cursor();
}

/* ===================== Key processing ===================== */

static int editor_process_keypress(void) {
    int c = read_key();

    switch (c) {
        case CTRL_KEY('x'):
            if (E.modified && !E.quit_pending) {
                E.quit_pending = 1;
                editor_set_status("Modified. Press Ctrl+X again to exit without saving.");
                return 1;
            }
            return 0;

        case CTRL_KEY('o'): {
            char *name = editor_prompt("Write file name: ");
            if (name) {
                if (name[0]) {
                    free(E.filename);
                    E.filename = xstrdup(name);
                }
                free(name);
                editor_save_file();
            }
        } break;

        case CTRL_KEY('a'):
            E.select_all = 1;
            editor_set_status("All selected. Backspace/Delete will clear the buffer.");
            break;

        case CTRL_KEY('k'):
            editor_cut_line();
            break;

        case CTRL_KEY('u'):
            editor_uncut_line();
            break;

        case CTRL_KEY('w'): {
            char *term = editor_prompt("Search: ");
            if (term) {
                if (term[0]) editor_search_forward(term);
                else editor_set_status("Search canceled.");
                free(term);
            }
        } break;

        case KEY_HOME:
            E.select_all = 0;
            E.cx = 0;
            break;

        case KEY_END:
            E.select_all = 0;
            E.cx = E.rows[E.cy].len;
            break;

        case KEY_PAGE_UP:
        case KEY_PAGE_DOWN:
            editor_page_move(c);
            break;

        case KEY_ARROW_LEFT:
        case KEY_ARROW_RIGHT:
        case KEY_ARROW_UP:
        case KEY_ARROW_DOWN:
            editor_move_cursor(c);
            break;

        case KEY_DELETE:
            editor_delete();
            break;

        case KEY_BACKSPACE:
        case 8:
            editor_backspace();
            break;

        case '\r':
        case '\n':
            editor_insert_newline();
            break;

        case KEY_ESC:
            break;

        default:
            if (c >= 32 && c <= 126) {
                editor_insert_char(c);
            }
            break;
    }

    E.quit_pending = 0;
    editor_clamp_cursor();
    return 1;
}

/* ===================== Init & main ===================== */

static void editor_init(void) {
    memset(&E, 0, sizeof(E));

    E.rows = NULL;
    E.numrows = 0;
    E.caprows = 0;

    E.cx = E.cy = 0;
    E.rowoff = E.coloff = 0;

    E.modified = 0;
    E.select_all = 0;
    E.quit_pending = 0;

    E.filename = xstrdup("unnamed.txt");
    E.cutbuf = NULL;
    E.statusmsg[0] = '\0';

    if (get_window_size(&E.screenrows, &E.screencols) == -1) die("winsize");
    editor_ensure_non_empty();
}

int main(int argc, char **argv) {
    enable_raw_mode();
    editor_init();

    if (argc > 1) editor_load_file(argv[1]);
    else editor_load_file("unnamed.txt");

    /* splash (CRLF fix: use \r\n because OPOST is disabled) */
    write(STDOUT_FILENO, "\x1b[2J\x1b[H\x1b[?25l", 13);

    const char *splash =
"___________                   ____  ___\r\n"
"\\_   _____/______  ____   ____\\   \\/  /\r\n"
" |    __) \\_  __ \\/  _ \\ / ___\\\\     / \r\n"
" |     \\   |  | \\(  <_> ) /_/  >     \\\r\n"
" \\___  /   |__|   \\____/\\___  /___/\\  \\\r\n"
"     \\/                /_____/      \\_/\r\n"
"\r\n"
"                FrogX - nano but not stupid\r\n";

    write(STDOUT_FILENO, splash, strlen(splash));
    sleep_us(400000);

    while (1) {
        editor_refresh_screen();
        if (!editor_process_keypress()) break;
    }

    /* clear + show cursor */
    write(STDOUT_FILENO, "\x1b[2J\x1b[H\x1b[?25h", 13);
    return 0;
}
