/* column.c - Columnate lists.
 *
 * Copyright 2026 Kim Geonwoo
 *
 * No standard. See util-linux column(1): this does the common subset.
 * Not implemented: JSON, trees, -O -C -l -E -d -m -e -H -T -W -n -J -r -i -p.

USE_COLUMN(NEWTOY(column, "txLs:o:c#<0N:R:", TOYFLAG_USR|TOYFLAG_BIN))

config COLUMN
  bool "column"
  default n
  help
    usage: column [-txL] [-c WIDTH] [-s SEP] [-o SEP] [-N NAMES] [-R COLS] [FILE...]

    Format lines of input into columns. Without -t, fill columns with whole
    lines (tab separated) as many as fit in WIDTH.

    -c	Output WIDTH (default: terminal width or 80)
    -x	Fill rows before columns
    -L	Keep empty lines
    -t	Table: split each line into fields and line up the fields
    -s	Field separators for -t (any of these characters, default whitespace)
    -o	Output separator for -t (default two spaces)
    -N	Comma separated column names for -t (adds a header line)
    -R	Right align these -t columns (comma separated numbers or names)
*/

#define FOR_column
#include "toys.h"

GLOBALS(
  char *R, *N;
  long c;
  char *o, *s;

  char **lines;
  long nlines;
)

// Display width of UTF-8 string (invalid bytes and control characters
// count as one column, as they're printed as-is)
static int width(char *s)
{
  unsigned wc;
  int w = 0, len, col;

  while (*s) {
    if ((len = utf8towc(&wc, s, 4)) < 1) len = col = 1;
    else if ((col = wcwidth(wc)) < 0) col = 1;
    w += col;
    s += len;
  }

  return w;
}

// loopfiles_lines() callback: keep each line (without newline)
static void do_line(char **pline, long len)
{
  if (!pline) return;
  chomp(*pline);
  if (!**pline && !FLAG(L)) return;
  if (!(TT.nlines & 255))
    TT.lines = xrealloc(TT.lines, (TT.nlines+256)*sizeof(char *));
  TT.lines[TT.nlines++] = *pline;
  *pline = 0;
}

// Print up to tab stop at column "to" (with "*at" columns already printed)
static void tab_to(int *at, int to)
{
  int next;

  while ((next = (*at+8)&~7) <= to) {
    xputc('\t');
    *at = next;
  }
}

// Fill columns (or rows with -x) with whole lines, like util-linux and BSD.
// Each column is the longest line rounded up to the next tab stop.
static void fill(void)
{
  unsigned w = 80;
  long i, row, col, cols, rows, max = 0;
  int at;

  if (FLAG(c)) w = TT.c;
  else terminal_size(&w, 0);
  for (i = 0; i < TT.nlines; i++) if (width(TT.lines[i]) > max)
    max = width(TT.lines[i]);

  // A line is wider than the output: just print them
  if (max >= w) {
    for (i = 0; i < TT.nlines; i++) xprintf("%s\n", TT.lines[i]);
    return;
  }
  max = (max+8)&~7;
  if (!(cols = w/max)) cols = 1;
  rows = (TT.nlines+cols-1)/cols;

  // Line i goes in row i/cols (-x) or row i%rows, then pad to next column
  for (row = 0; row < rows; row++) {
    for (at = col = 0; col < cols; col++) {
      i = FLAG(x) ? row*cols+col : col*rows+row;
      if (i >= TT.nlines) break;
      if (col) tab_to(&at, col*max);
      xprintf("%s", TT.lines[i]);
      at += width(TT.lines[i]);
    }
    xputc('\n');
  }
}

// Split line into fields at -s characters (empty fields kept), or at runs
// of whitespace (leading/trailing ignored). Returns field count.
static int split(char *line, char ***fields)
{
  char *sep = TT.s ? : " \t", *s;
  int n = 0;

  *fields = 0;
  if (!TT.s) line += strspn(line, sep);
  while (*line || (TT.s && n)) {
    if (!(n & 15)) *fields = xrealloc(*fields, (n+16)*sizeof(char *));
    (*fields)[n++] = line;
    s = line + strcspn(line, sep);
    if (!*s) break;
    *s++ = 0;
    line = TT.s ? s : s+strspn(s, sep);
  }

  return n;
}

// Table (-t): rows of fields, each column padded to its widest field.
// The last column isn't padded unless right aligned.
static void table(void)
{
  char ***rows, **names = 0, *hdr = 0, *s, *ss, *end;
  int *nf, *wide, *right, cols = 0, nn = 0, i, w;
  long r, n = TT.nlines;

  // No input, no output (not even the -N header)
  if (!n) return;

  // -N: header row of comma separated names (util-linux skips empty ones)
  if (TT.N) {
    names = xmalloc((strlen(TT.N)/2+1)*sizeof(char *));
    for (s = hdr = xstrdup(TT.N); s; s = ss) {
      if ((ss = strchr(s, ','))) *ss++ = 0;
      if (*s) names[nn++] = s;
    }
  }

  // Row 0 is the header (if any), then one row per input line
  rows = xmalloc((n+1)*sizeof(*rows));
  nf = xmalloc((n+1)*sizeof(int));
  if (names) {
    rows[0] = names;
    nf[0] = nn;
  }
  for (r = 0; r < n; r++) nf[r+!!names] = split(TT.lines[r], rows+r+!!names);
  n += !!names;

  for (r = 0; r < n; r++) if (nf[r] > cols) cols = nf[r];
  wide = xzalloc(cols*sizeof(int));
  right = xzalloc(cols*sizeof(int));
  for (r = 0; r < n; r++) for (i = 0; i < nf[r]; i++)
    if ((w = width(rows[r][i])) > wide[i]) wide[i] = w;

  // -R: each entry is a -N name or a column number (from 1)
  for (s = TT.R; s && *s; s = ss+!!*ss) {
    ss = s+strcspn(s, ",");
    for (i = 0; i < nn; i++)
      if (strlen(names[i]) == ss-s && !strncmp(names[i], s, ss-s)) break;
    if (i == nn) {
      i = strtol(s, &end, 10)-1;
      if (end != ss) i = -1;
    }
    if (i < 0 || i >= cols) error_exit("bad -R '%.*s'", (int)(ss-s), s);
    right[i] = 1;
  }

  for (r = 0; r < n; r++) {
    for (i = 0; i < cols; i++) {
      s = i < nf[r] ? rows[r][i] : "";
      w = wide[i]-width(s);
      // Empty last field isn't padded even when right aligned
      if (right[i] && (*s || i < cols-1)) xprintf("%*s", w, "");
      xprintf("%s", s);
      if (i == cols-1) break;
      if (!right[i]) xprintf("%*s", w, "");
      xprintf("%s", TT.o ? : "  ");
    }
    xputc('\n');
  }

  if (CFG_TOYBOX_FREE) {
    free(hdr);
    for (r = 0; r < n; r++) free(rows[r]);
    free(rows);
    free(nf);
    free(wide);
    free(right);
  }
}

void column_main(void)
{
  if ((TT.N || TT.R) && !FLAG(t)) error_exit("-N and -R need -t");

  loopfiles_lines(toys.optargs, do_line);
  if (FLAG(t)) table();
  else fill();

  if (CFG_TOYBOX_FREE) {
    while (TT.nlines) free(TT.lines[--TT.nlines]);
    free(TT.lines);
  }
}
