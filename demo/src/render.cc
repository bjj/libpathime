#include "render.h"

#include <algorithm>
#include <cstdio>
#include <vector>

#include <cpp-terminal/color.hpp>
#include <cpp-terminal/cursor.hpp>
#include <cpp-terminal/screen.hpp>
#include <cpp-terminal/style.hpp>

#include "text.h"

namespace demo {
namespace {

using Term::Color;
using Term::Style;

std::string fg(Color::Name name, const std::string &s)
{
    return Term::color_fg(name) + s + Term::color_fg(Color::Name::Default);
}

std::string styled(Style style, const std::string &s)
{
    return Term::style(style) + s + Term::style(Style::Reset);
}

std::string dim(const std::string &s) { return styled(Style::Dim, s); }

/** A section heading: the one piece of chrome this screen has. */
std::string heading(const std::string &text)
{
    return " " + fg(Color::Name::Cyan, Term::style(Style::Bold) + text +
                                           Term::style(Style::Reset));
}

std::string rule(std::size_t columns)
{
    std::string bar;
    for (std::size_t i = 0; i < columns; i++) bar += "\xE2\x94\x80";  /* ─ */
    return dim(bar);
}

/* The engine strip: which input methods this build supplied, and which one has
 * focus. Function keys pick one directly; Ctrl+E cycles. */
std::string engine_strip(const App &app)
{
    std::string line = " ";
    for (std::size_t i = 0; i < app.engines().size(); i++) {
        const std::string entry =
            "F" + std::to_string(i + 2) + " " + app.engines()[i].name;
        if (i == app.active_index()) {
            line += Term::style(Style::Reversed) + " " + entry + " " +
                    Term::style(Style::Reset);
        } else {
            line += dim(" " + entry + " ");
        }
        line += " ";
    }
    return line;
}

/*
 * The document, with the composition drawn into it the way a real text field
 * shows a preedit: settled text — what the engine considers decided — in one
 * style, the still-changing tail in another.
 *
 * The line shows the *tail* of the text, so the caret is always at its right
 * hand end and there is no scroll state to keep. @a caret_column receives the
 * 1-based column the terminal cursor should be left in.
 */
std::string document_line(const App &app, std::size_t columns,
                          std::size_t *caret_column)
{
    const std::string &doc = app.document();
    const pathime_composition_t *comp = app.composition();

    std::string preedit;
    std::size_t settled_bytes = 0;
    if (comp != nullptr) {
        preedit.assign(comp->preedit.bytes, comp->preedit.len);
        settled_bytes = byte_offset_of_scalar(preedit, comp->preedit_settled);
    }
    const std::string settled = preedit.substr(0, settled_bytes);
    const std::string active = preedit.substr(settled_bytes);

    const std::size_t indent = 3;
    const std::size_t room = columns > indent + 2 ? columns - indent - 2 : 1;
    const std::size_t start = tail_start_byte(doc + preedit, room);

    /* Each piece clipped to whatever of it survives the tail cut, given where
     * it begins in the concatenation. */
    const auto visible = [start](const std::string &part, std::size_t base) {
        if (base + part.size() <= start) return std::string();
        return part.substr(start > base ? start - base : 0);
    };
    const std::string doc_vis = visible(doc, 0);
    const std::string settled_vis = visible(settled, doc.size());
    const std::string active_vis = visible(active, doc.size() + settled.size());

    std::string line = std::string(indent, ' ');
    line += doc_vis;
    if (!settled_vis.empty())
        line += Term::color_fg(Color::Name::Green) + Term::style(Style::Underline) +
                settled_vis + Term::style(Style::Reset);
    if (!active_vis.empty())
        line += Term::color_fg(Color::Name::Yellow) + Term::style(Style::Underline) +
                active_vis + Term::style(Style::Reset);

    *caret_column = indent + 1 +
                    display_width(doc_vis + settled_vis + active_vis);

    /* An empty field says what to type into it, the way a placeholder does.
     * The caret stays where it is, so the hint sits behind it. */
    if (doc.empty() && preedit.empty()) line += dim(app.active().hint);
    return line;
}

void composition_lines(const App &app, std::vector<std::string> *out)
{
    const pathime_composition_t *comp = app.composition();
    const std::string preedit =
        comp != nullptr ? std::string(comp->preedit.bytes, comp->preedit.len) : "";
    out->push_back("   preedit      " + (preedit.empty() ? dim("empty") : preedit));

    char buf[96];
    std::snprintf(buf, sizeof(buf), "   settled      %zu of %zu scalars",
                  comp != nullptr ? comp->preedit_settled : 0,
                  scalar_count(preedit));
    out->push_back(buf);
}

/* The candidate list: one page of it, numbered the way the digits are bound. */
void candidate_lines(const App &app, std::size_t columns,
                     std::vector<std::string> *out)
{
    const pathime_composition_t *comp = app.composition();
    const std::size_t count = comp != nullptr ? comp->candidate_count : 0;
    if (count == 0) {
        out->push_back("   candidates   " + dim("none"));
        return;
    }

    const std::size_t per_page = app.page_size();
    const std::size_t pages = (count + per_page - 1) / per_page;
    /*
     * The entry the composition currently reflects. Highlighting it is not
     * decoration: on an engine that previews its candidates the hovered one
     * *is* what the preedit above is showing, so the highlight and the text
     * field are two views of the same fact, and watching them move together is
     * most of what this panel is for.
     *
     * It comes out of the composition rather than out of anything this program
     * remembers asking for, which is what the header asks of a client — the
     * engine moves this cursor too.
     */
    const std::size_t cursor = comp->candidate_cursor;
    char buf[160];
    std::snprintf(buf, sizeof(buf),
                  "   candidates   %zu   page %zu of %zu   at %zu", count,
                  app.page() + 1, pages, cursor);
    out->push_back(std::string(buf) + dim("   Up/Dn, PgUp/PgDn"));

    std::string line = "   ";
    for (std::size_t i = 0; i < per_page; i++) {
        const std::size_t index = app.page() * per_page + i;
        if (index >= count) break;

        const std::string text = app.candidate(index);
        const bool hovered = index == cursor;
        const std::string entry =
            fg(Color::Name::BrightBlack, std::to_string(i + 1) + ".") + " " +
            (hovered ? styled(Style::Reversed, text) : text) + "  ";

        if (display_width(line) + display_width(text) + 6 > columns) {
            out->push_back(line);
            line = "   ";
        }
        line += entry;
    }
    if (display_width(line) > 3) out->push_back(line);
}

/*
 * The event log: everything that crossed the API boundary, oldest first, with
 * the direction of travel in the margin. The three kinds are styled apart
 * because the point of reading downward is seeing a call, then the callbacks it
 * produced, then the next call — and undifferentiated text hides exactly that.
 */
void log_lines(const App &app, std::size_t budget, std::vector<std::string> *out)
{
    if (budget == 0) return;
    out->push_back(heading("event log") +
                   dim("   ") + fg(Color::Name::Cyan, "\xE2\x86\x92 calls in") +
                   dim("   ") + fg(Color::Name::Magenta, "\xE2\x86\x90 callbacks out") +
                   dim("   \xC2\xB7 this program"));

    const std::deque<LogEntry> &log = app.log();
    const std::size_t skip = log.size() > budget - 1 ? log.size() - (budget - 1) : 0;
    for (std::size_t i = skip; i < log.size(); i++) {
        switch (log[i].kind) {
        case LogKind::Call:
            /* Into the library. Bright, because it is the thing that caused
             * whatever follows it. */
            out->push_back("   " + fg(Color::Name::Cyan, "\xE2\x86\x92 ") +
                           Term::style(Style::Bold) + log[i].text +
                           Term::style(Style::Reset));
            break;
        case LogKind::Callback:
            /* Out of the library, indented under the call that caused it. */
            out->push_back("     " + fg(Color::Name::Magenta, "\xE2\x86\x90 ") +
                           log[i].text);
            break;
        case LogKind::Info:
            out->push_back("   " + dim("\xC2\xB7 " + log[i].text));
            break;
        }
    }
}

void option_lines(const App &app, std::size_t budget, std::vector<std::string> *out)
{
    if (budget < 2) return;

    const bool focused = app.pane() == Pane::Options;
    std::string title = heading("options");
    title += app.engine_level() ? "  engine level" : "  context level";
    title += dim(focused
                     ? "    Up/Down  Left/Right change  Space toggle  r reset  l level  Esc done"
                     : "    Tab to edit");
    out->push_back(title);

    const std::vector<OptionRow> &rows = app.options();
    if (rows.empty()) {
        out->push_back("   " + dim("this engine implements none"));
        return;
    }

    /* Scroll so the selection stays on screen. */
    const std::size_t room = budget - 1;
    std::size_t first = 0;
    if (app.option_index() >= room) first = app.option_index() - room + 1;

    for (std::size_t i = first; i < rows.size() && i - first < room; i++) {
        const OptionRow &row = rows[i];
        const bool selected = focused && i == app.option_index();
        const bool overridden = app.is_option_set_here(i);

        std::string text = " ";
        text += selected ? "\xE2\x96\xB8 " : "  ";  /* ▸ */
        /* Wide enough for the longest name in the inventory and the widest
         * value a flags option prints, with a column between them. */
        text += pad_to_width(pathime_option_name(row.option), 30);
        text += pad_to_width(app.option_value_text(i), 30);

        /* What a client acts on: where the value came from, whether a context
         * value is shadowing the engine one being edited, and whether changing
         * it throws the composition away. */
        std::string flags;
        if (overridden) flags += "set here  ";
        if (app.engine_level() && app.is_option_shadowed(i))
            flags += "context overrides  ";
        if (row.info.resets_composition) flags += "resets";
        text += flags;

        if (selected) text = Term::style(Style::Reversed) + text + Term::style(Style::Reset);
        else if (!overridden) text = dim(text);
        out->push_back(text);
    }
}

/*
 * Deliberately short enough to fit an 80x24 terminal with the footer. What to
 * type in each engine is not here: it is in the text field itself, where an
 * empty document shows the active engine's hint as a placeholder.
 */
std::vector<std::string> help_page()
{
    std::vector<std::string> lines;
    lines.push_back(heading("libpathime demo — keys"));
    lines.push_back("");
    lines.push_back("   typing          every printable key is offered to the engine");
    lines.push_back("   1 - 9           pick a candidate from the page on screen");
    lines.push_back("   Alt + 1 - 9     the same — and the only form Bopomofo leaves");
    lines.push_back("                   free, since there the digits are the tone keys");
    lines.push_back("   Up / Down       move the highlight without choosing; the preedit");
    lines.push_back("                   follows it on an engine that previews candidates");
    lines.push_back("   PgUp / PgDn     page the list; past the end raises max-candidates");
    lines.push_back("   Space           convert: what advances a composition");
    lines.push_back("   Return          end it without applying a conversion the user");
    lines.push_back("                   did not choose, so it may commit something other");
    lines.push_back("                   than the preedit on screen");
    lines.push_back("   Backspace       the engine's while composing; this program's own");
    lines.push_back("                   when the engine declines it");
    lines.push_back("");
    lines.push_back("   Tab             move between the text field and the options");
    lines.push_back("   Ctrl+E / F2..   next input method / pick one directly");
    lines.push_back("   Ctrl+T          commit: end the composition, keep the text");
    lines.push_back("   Ctrl+R          reset: discard the composition, commit nothing");
    lines.push_back("   Ctrl+O          leave the field: commit, then reset");
    lines.push_back("   Ctrl+U          cycle how much surrounding text is supplied");
    lines.push_back("   Ctrl+D  Ctrl+L  clear the document / clear the event log");
    lines.push_back("   Ctrl+Y          copy the document to the clipboard (OSC 52)");
    lines.push_back("   Ctrl+Q          quit");
    lines.push_back("");
    lines.push_back("   " + dim("any key returns"));
    return lines;
}

/* Base64, for OSC 52's payload. Small enough that pulling in a dependency to
 * avoid writing it would be the larger cost. */
std::string base64(const std::string &in)
{
    static const char *kAlphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((in.size() + 2) / 3 * 4);
    for (std::size_t i = 0; i < in.size(); i += 3) {
        const unsigned char b0 = static_cast<unsigned char>(in[i]);
        const unsigned char b1 =
            i + 1 < in.size() ? static_cast<unsigned char>(in[i + 1]) : 0;
        const unsigned char b2 =
            i + 2 < in.size() ? static_cast<unsigned char>(in[i + 2]) : 0;
        out += kAlphabet[b0 >> 2];
        out += kAlphabet[((b0 & 0x03u) << 4) | (b1 >> 4)];
        out += i + 1 < in.size() ? kAlphabet[((b1 & 0x0Fu) << 2) | (b2 >> 6)] : '=';
        out += i + 2 < in.size() ? kAlphabet[b2 & 0x3Fu] : '=';
    }
    return out;
}

}  // namespace

std::string clipboard_copy(const std::string &text)
{
    /* OSC 52, selection "c" for the clipboard proper, terminated with BEL —
     * the form the widest set of terminals accepts. */
    return "\x1b]52;c;" + base64(text) + "\a";
}

std::string disable_mouse_reporting()
{
    return "\x1b[?1006l\x1b[?1003l\x1b[?1002l";
}

std::string render(const App &app, std::size_t rows, std::size_t columns)
{
    std::vector<std::string> lines;
    std::size_t caret_row = 0;
    std::size_t caret_column = 1;

    if (rows < 12 || columns < 50) {
        lines.push_back(" terminal too small — 50x12 is the minimum");
    } else if (app.help_visible()) {
        lines = help_page();
    } else {
        char buf[192];
        std::snprintf(buf, sizeof(buf), " libpathime %s demo",
                      pathime_version_string());
        std::string header = Term::style(Style::Bold) + buf + Term::style(Style::Reset);
        header += dim("    F1 help   Ctrl+Q quit");
        lines.push_back(header);
        lines.push_back(engine_strip(app));
        lines.push_back(rule(columns));

        /* --- the text field ------------------------------------------- */
        std::string req;
        const std::uint32_t bits = app.requirements();
        if (bits & PATHIME_REQUIRES_SURROUNDING_TEXT) req += " surrounding-text";
        if (bits & PATHIME_REQUIRES_DELETE_SURROUNDING) req += " delete-surrounding";
        /* What this program supplies is as much a part of the picture as what
         * the engine asks for, and it is the one input the user cannot see on
         * screen — so it is shown next to the requirement it answers. */
        std::string supplied;
        switch (app.surrounding()) {
        case Surrounding::Full:     supplied = "whole document"; break;
        case Surrounding::Fragment: supplied = "1 scalar";       break;
        case Surrounding::None:     supplied = "none";           break;
        }
        lines.push_back(heading("document") +
                        dim("   engine requires:" + (req.empty() ? " nothing" : req)) +
                        dim("   supplying: " + supplied + " (Ctrl+U)"));
        std::size_t caret_col = 1;
        const std::string doc = document_line(app, columns, &caret_col);
        caret_row = lines.size() + 1;
        caret_column = caret_col;
        lines.push_back(doc);
        lines.push_back("");

        /* --- what the engine says it is doing -------------------------- */
        lines.push_back(heading("composition"));
        composition_lines(app, &lines);
        candidate_lines(app, columns, &lines);
        lines.push_back("");

        /* --- the rest of the screen, split between the log and options ---
         *
         * The log gets what it can actually fill, up to two thirds of what is
         * left; the options panel takes the remainder and scrolls, since it
         * keeps the selected row on screen anyway. So a quiet log leaves the
         * options room to show themselves in full, and a busy one — which is
         * when the log is the interesting panel — takes the space it needs.
         *
         * The shares invert while the options panel has the keyboard: whatever
         * the user is working in is what should be legible, and nothing is
         * being added to the log while they are in there anyway. */
        const std::size_t body = rows - 2;  /* the footer takes two rows */
        const std::size_t left = body > lines.size() ? body - lines.size() : 0;
        const std::size_t log_wanted = app.log().size() + 1;  /* + its heading */
        const std::size_t log_share =
            app.pane() == Pane::Options ? left / 3 : left * 2 / 3;
        const std::size_t log_budget =
            std::min<std::size_t>(log_wanted, left > 6 ? log_share : 0);
        const std::size_t option_budget = left > log_budget + 1
                                              ? left - log_budget - 1 : 0;

        log_lines(app, log_budget, &lines);
        if (log_budget > 0) lines.push_back("");
        option_lines(app, option_budget, &lines);
    }

    /* --- the footer: what just happened ------------------------------- */
    std::vector<std::string> footer;
    footer.push_back(rule(columns));
    if (!app.status().empty()) {
        footer.push_back(" " + fg(Color::Name::Yellow, app.status()));
    } else if (!app.last_key().empty()) {
        footer.push_back(" " + dim("last key  ") + app.last_key());
    } else {
        footer.push_back("");
    }

    /* --- one write, clipped to the terminal --------------------------- */
    std::string out;
    for (std::size_t row = 1; row + 2 <= rows; row++) {
        out += Term::cursor_move(row, 1);
        if (row - 1 < lines.size()) out += clip_to_width(lines[row - 1], columns);
        out += Term::clear_eol();
    }
    for (std::size_t i = 0; i < footer.size(); i++) {
        out += Term::cursor_move(rows - 1 + i, 1);
        out += clip_to_width(footer[i], columns);
        out += Term::clear_eol();
    }

    if (caret_row != 0 && !app.help_visible())
        out += Term::cursor_move(caret_row, caret_column);
    else
        out += Term::cursor_move(rows, 1);
    return out;
}

}  // namespace demo
