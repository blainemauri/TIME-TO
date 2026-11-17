// Small string helpers shared by the core sources. Not part of the public API.
#pragma once

#include "tt_core.h"

//! Append a string at out[pos], never writing past cap, always NUL terminating.
//! Returns the new position (clamped to cap - 1).
int tt_app(char *out, size_t cap, int pos, const char *s);
int tt_app_char(char *out, size_t cap, int pos, char c);
int tt_app_int(char *out, size_t cap, int pos, int32_t value);
//! Append an integer zero-padded to `width` digits.
int tt_app_pad(char *out, size_t cap, int pos, int32_t value, int width);

//! Parse an optionally signed decimal integer at *p and advance past it.
bool tt_scan_int(const char **p, int32_t *value);
//! Case-insensitive prefix match. Returns the length of the prefix or 0.
int tt_starts(const char *text, const char *prefix);
char tt_lower(char c);
