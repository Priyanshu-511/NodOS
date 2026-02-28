#pragma once

// vi — modal text editor for NodOS.
//
// Modes
//   NORMAL : hjkl / arrow movement, dd delete line, o open line,
//             x delete char, G last line, gg first line, :commands
//   INSERT : type to insert, Esc → NORMAL
//   COMMAND: typed after pressing ':' in NORMAL mode
//
// Commands (:)
//   :w           save to current file
//   :q           quit (fails if unsaved changes)
//   :q!          force quit without saving
//   :wq / :x     save and quit
//   :w <name>    save to a different file
//
// Returns 0 on clean exit, -1 on error.
int vi_open(const char* filename);   // open existing or new file
