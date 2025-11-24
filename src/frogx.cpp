/*
___________                   ____  ___
\_   _____/______  ____   ____\   \/  /
 |    __) \_  __ \/  _ \ / ___\\     / 
 |     \   |  | \(  <_> ) /_/  >     \ 
 \___  /   |__|   \____/\___  /___/\  \
     \/                /_____/      \_/

                FrogX - nano but not stupid

   Part of FrogTools.
   Created by Victor Duarte (Porquinho) + SapoGPT 🐸
*/

// frogx.cpp - FrogX: minimal nano-like editor (C++23, English UI)
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

#include <ncurses.h>
#include <vector>
#include <string>
#include <string_view>
#include <fstream>
#include <locale.h>
#include <algorithm>
#include <ranges>
#include <format>
#include <cctype>
#include <cstdint>
#include <cstring>

struct Editor {
    std::vector<std::string> lines;
    std::string filename{"unnamed.txt"};
    std::string cut_buffer;
    std::string last_message;
    int cursor_row{0};
    int cursor_col{0};
    int view_row_offset{0};
    bool modified{false};
    bool select_all{false};

    void ensure_non_empty() {
        if (lines.empty())
            lines.emplace_back();
    }

    void clamp_cursor() {
        ensure_non_empty();
        cursor_row = std::clamp(cursor_row, 0, (int)lines.size() - 1);
        cursor_col = std::clamp(cursor_col, 0, (int)lines[cursor_row].size());
    }

    std::string& current_line() {
        return lines[cursor_row];
    }

    const std::string& current_line() const {
        return lines[cursor_row];
    }
};

static Editor ed;

// === Splash Screen ===
static void splash() {
    clear();
    mvprintw(0, 0,
R"(___________                   ____  ___
\_   _____/______  ____   ____\   \/  /
 |    __) \_  __ \/  _ \ / ___\\     / 
 |     \   |  | \(  <_> ) /_/  >     \ 
 \___  /   |__|   \____/\___  /___/\  \
     \/                /_____/      \_/

                FrogX - nano but not stupid
)");
    refresh();
    napms(400); // 0.4 seconds
}

// === File Operations ===
static void load_file() {
    ed.lines.clear();

    if (std::ifstream in{ed.filename}) {
        std::string line;
        while (std::getline(in, line)) {
            ed.lines.push_back(line);
        }
    }

    ed.ensure_non_empty();
    ed.cursor_row = 0;
    ed.cursor_col = 0;
    ed.view_row_offset = 0;
    ed.modified = false;
    ed.select_all = false;
    ed.cut_buffer.clear();
    ed.last_message.clear();
}

static void save_file() {
    std::ofstream out{ed.filename};
    if (!out) {
        ed.last_message = "Error: could not save file.";
        return;
    }

    std::size_t written_lines = 0;
    std::size_t written_bytes = 0;

    for (std::size_t i = 0; i < ed.lines.size(); ++i) {
        const auto &line = ed.lines[i];
        out << line;
        written_bytes += line.size();
        if (i + 1 < ed.lines.size()) {
            out << '\n';
            written_bytes += 1;
        }
        ++written_lines;
    }

    ed.modified = false;
    ed.select_all = false;
    ed.last_message = std::format("Wrote {} lines ({} bytes).",
                                  written_lines, written_bytes);
}

// === Cursor & View ===
static void ensure_cursor_visible(int screen_rows) {
    // reserve 2 lines: status + help
    int text_rows = std::max(screen_rows - 2, 1);

    if (ed.cursor_row < ed.view_row_offset) {
        ed.view_row_offset = ed.cursor_row;
    } else if (ed.cursor_row >= ed.view_row_offset + text_rows) {
        ed.view_row_offset = ed.cursor_row - text_rows + 1;
    }

    int max_offset = std::max(0, (int)ed.lines.size() - 1);
    ed.view_row_offset = std::clamp(ed.view_row_offset, 0, max_offset);
}

static void clear_all() {
    ed.lines.assign(1, std::string{});
    ed.cursor_row = 0;
    ed.cursor_col = 0;
    ed.view_row_offset = 0;
    ed.modified = true;
    ed.select_all = false;
    ed.cut_buffer.clear();
    ed.last_message = "Buffer cleared.";
}

// === Text Editing ===
static void insert_char(int ch) {
    ed.ensure_non_empty();
    auto& line = ed.current_line();

    if (ed.cursor_col > (int)line.size())
        ed.cursor_col = (int)line.size();

    line.insert(line.begin() + ed.cursor_col, static_cast<char>(ch));
    ++ed.cursor_col;
    ed.modified = true;
    ed.select_all = false;
}

static void insert_newline() {
    ed.ensure_non_empty();
    auto& cur = ed.current_line();

    if (ed.cursor_col > (int)cur.size())
        ed.cursor_col = (int)cur.size();

    std::string tail = cur.substr(ed.cursor_col);
    cur.erase(ed.cursor_col);

    ed.lines.insert(ed.lines.begin() + ed.cursor_row + 1, std::move(tail));
    ++ed.cursor_row;
    ed.cursor_col = 0;
    ed.modified = true;
    ed.select_all = false;
}

static void backspace_char() {
    ed.ensure_non_empty();

    if (ed.select_all) {
        clear_all();
        return;
    }

    if (ed.cursor_row == 0 && ed.cursor_col == 0)
        return;

    if (ed.cursor_col > 0) {
        auto& line = ed.current_line();
        line.erase(ed.cursor_col - 1, 1);
        --ed.cursor_col;
    } else {
        auto& prev = ed.lines[ed.cursor_row - 1];
        prev += ed.current_line();
        ed.lines.erase(ed.lines.begin() + ed.cursor_row);
        --ed.cursor_row;
        ed.cursor_col = (int)prev.size();
    }

    ed.modified = true;
    ed.select_all = false;
}

static void delete_char() {
    ed.ensure_non_empty();

    if (ed.select_all) {
        clear_all();
        return;
    }

    auto& line = ed.current_line();
    if (ed.cursor_col < (int)line.size()) {
        line.erase(ed.cursor_col, 1);
        ed.modified = true;
        return;
    }

    if (ed.cursor_row + 1 < (int)ed.lines.size()) {
        line += ed.lines[ed.cursor_row + 1];
        ed.lines.erase(ed.lines.begin() + ed.cursor_row + 1);
        ed.modified = true;
    }
}

// === Navigation ===
static void move_cursor(int row_delta, int col_delta) {
    if (ed.select_all) ed.select_all = false;
    ed.cursor_row += row_delta;
    ed.cursor_col += col_delta;
}

static void page_scroll(int direction, int screen_rows) {
    int text_rows = std::max(screen_rows - 2, 1);
    ed.cursor_row = std::clamp(
        ed.cursor_row + direction * text_rows,
        0,
        (int)ed.lines.size() - 1
    );
}

// === Cut / Paste ===
static void cut_line() {
    ed.ensure_non_empty();
    if (ed.lines.empty()) return;

    ed.cut_buffer = std::move(ed.current_line());
    ed.lines.erase(ed.lines.begin() + ed.cursor_row);

    if (ed.lines.empty())
        ed.lines.emplace_back();

    if (ed.cursor_row >= (int)ed.lines.size())
        ed.cursor_row = (int)ed.lines.size() - 1;

    ed.cursor_col = 0;
    ed.modified = true;
    ed.select_all = false;
    ed.last_message = "Line cut.";
}

static void uncut_line() {
    if (ed.cut_buffer.empty()) {
        ed.last_message = "Nothing to paste.";
        return;
    }
    ed.ensure_non_empty();

    ed.lines.insert(ed.lines.begin() + ed.cursor_row + 1, ed.cut_buffer);
    ++ed.cursor_row;
    ed.cursor_col = (int)ed.cut_buffer.size();
    ed.modified = true;
    ed.select_all = false;
    ed.last_message = "Line pasted.";
}

// === Prompt Input (fixed, using getnstr) ===
static std::string prompt_input(std::string_view label) {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    move(rows - 1, 0);
    clrtoeol();

    std::string prompt{label};
    if ((int)prompt.size() > cols - 1)
        prompt.resize(cols - 1);

    echo();
    curs_set(1);

    addstr(prompt.c_str());

    int maxlen = cols - (int)prompt.size() - 1;
    if (maxlen < 1) maxlen = 1;

    char buf[512];
    int limit = std::min(maxlen, (int)sizeof(buf) - 1);

    getnstr(buf, limit);
    buf[limit] = '\0';

    noecho();
    curs_set(0);

    return std::string(buf);
}

// === Search ===
static void search_forward(std::string_view term) {
    if (term.empty()) return;
    ed.ensure_non_empty();

    int start_row = ed.cursor_row;
    int start_col = ed.cursor_col;

    for (int r = start_row; r < (int)ed.lines.size(); ++r) {
        int cstart = (r == start_row) ? start_col : 0;
        const auto& line = ed.lines[r];
        auto pos = line.find(term, (std::size_t)cstart);
        if (pos != std::string::npos) {
            ed.cursor_row = r;
            ed.cursor_col = (int)pos;
            ed.last_message = "Match found.";
            return;
        }
    }

    ed.last_message = "Not found.";
}

// === Display ===
static void draw_screen() {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    erase();

    ensure_cursor_visible(rows);
    int text_rows = std::max(rows - 2, 1); // reserve 2 lines

    auto visible_lines = ed.lines
                       | std::views::drop(ed.view_row_offset)
                       | std::views::take(text_rows);

    int row = 0;
    for (const auto& line : visible_lines) {
        mvaddnstr(row++, 0, line.c_str(), cols);
    }

    // Status bar (second-to-last line)
    std::string status = std::format(
        "{}{}  Ln {}, Col {}",
        ed.filename,
        ed.modified ? " *" : "",
        ed.cursor_row + 1,
        ed.cursor_col + 1
    );

    if (ed.select_all)
        status += "  [ALL SELECTED]";
    if (!ed.last_message.empty())
        status += "  | " + ed.last_message;

    if ((int)status.size() > cols)
        status.resize(cols);

    mvaddnstr(rows - 2, 0, status.c_str(), cols);

    // Help bar (last line, nano-style)
    std::string help =
        "^O WriteOut  ^X Exit  ^A SelectAll  ^K CutLine  ^U UnCut  ^W WhereIs";
    if ((int)help.size() > cols)
        help.resize(cols);
    mvaddnstr(rows - 1, 0, help.c_str(), cols);

    int screen_row = std::clamp(ed.cursor_row - ed.view_row_offset, 0, text_rows - 1);
    int screen_col = std::min(ed.cursor_col, cols - 1);
    move(screen_row, screen_col);

    refresh();
}

// === Key Handling ===
static bool handle_key(int ch) {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    switch (ch) {
        case 24: // Ctrl+X handled in main
            return false;

        case 15: { // Ctrl+O - ask for filename, then save
            std::string name = prompt_input("Write file name: ");
            if (!name.empty()) {
                ed.filename = name;
            }
            save_file();
            break;
        }

        case 1: // Ctrl+A
            ed.select_all = true;
            ed.last_message = "All selected. Backspace/Delete will clear the buffer.";
            break;

        case 11: // Ctrl+K
            cut_line();
            break;

        case 21: // Ctrl+U
            uncut_line();
            break;

        case 23: { // Ctrl+W
            auto term = prompt_input("Search: ");
            if (!term.empty()) {
                search_forward(term);
            } else {
                ed.last_message = "Search canceled.";
            }
            break;
        }

        case KEY_LEFT:
            move_cursor(0, -1);
            break;

        case KEY_RIGHT:
            move_cursor(0, 1);
            break;

        case KEY_UP:
            move_cursor(-1, 0);
            break;

        case KEY_DOWN:
            move_cursor(1, 0);
            break;

        case KEY_HOME:
            ed.select_all = false;
            ed.cursor_col = 0;
            break;

        case KEY_END:
            ed.select_all = false;
            ed.cursor_col = (int)ed.current_line().size();
            break;

        case KEY_PPAGE:
            ed.select_all = false;
            page_scroll(-1, rows);
            break;

        case KEY_NPAGE:
            ed.select_all = false;
            page_scroll(1, rows);
            break;

        case KEY_BACKSPACE:
        case 127:
            backspace_char();
            break;

        case KEY_DC:
            delete_char();
            break;

        case '\n':
        case '\r':
            insert_newline();
            break;

        default:
            if (ch >= 0 && ch <= 255 && std::isprint((unsigned char)ch)) {
                insert_char(ch);
            }
            break;
    }

    ed.clamp_cursor();
    return true;
}

// === Main ===
int main(int argc, char* argv[]) {
    if (argc > 1)
        ed.filename = argv[1];

    load_file();

    std::setlocale(LC_ALL, "");
    initscr();
    raw();
    noecho();
    keypad(stdscr, TRUE);

    splash();

    bool quit_pending = false;

    while (true) {
        draw_screen();
        int ch = getch();

        if (ch == 24) { // Ctrl+X
            if (ed.modified && !quit_pending) {
                quit_pending = true;
                ed.last_message = "Modified. Press Ctrl+X again to exit without saving.";
                continue;
            }
            break;
        }

        quit_pending = false;
        if (!handle_key(ch))
            break;
    }

    endwin();
    return 0;
}
