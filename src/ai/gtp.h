#pragma once

#include <string>

#include "go/go_types.h"

namespace vgo {

// GTP coordinate <-> internal (x, y). Internal y grows downward (0 = top edge);
// GTP row numbers grow upward (1 = bottom edge). Column letters skip 'I'.

std::string gtpPoint(const Point& p, int size);
std::string gtpPoint(int x, int y, int size);

// Parses a GTP coordinate like "D4" into an internal point. Returns false on
// malformed input or out-of-range coordinates.
bool gtpParsePoint(const std::string& s, int size, Point* out);

}  // namespace vgo
