#pragma once

#include <string>

#include "record.h"

namespace vgo {

// Standard SGF (FF[4]) coordinate <-> internal Point. SGF uses a top-left
// origin with no skipped letters and no row flip: the first letter is the
// column ('a' = left), the second is the row ('a' = top). This is unlike GTP
// (which skips 'I' and numbers rows from the bottom), so it lives separately
// from gtp.h.

// Encodes a point as two lowercase letters, e.g. "pd" (top-right 4-4 on 19x19).
std::string sgfPoint(const Point& p);

// Parses an SGF coordinate like "pd" into an internal point. Returns false on
// malformed input or out-of-range coordinates.
bool sgfParsePoint(const std::string& s, int size, Point* out);

// Serialize a record to standard SGF (FF[4], GM[1]). Pass moves serialize as
// empty move values (B[] / W[]). Blunders and notes are preserved as root-level
// C[] comments (structured blunders use a "VGO-BAD:<move>:<coord>:<text>"
// marker) so they survive a round-trip without breaking other SGF readers.
std::string serializeSgf(const GameRecord& r);

// Parse standard SGF (FF[4]) into a record. Only the mainline is read (variation
// subtrees are skipped); board size defaults to 19 when SZ is absent. Returns
// false on malformed input.
bool parseSgf(const std::string& text, GameRecord* out);

}  // namespace vgo
