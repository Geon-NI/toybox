/* swapoff.c - Disable region for swapping
 *
 * Copyright 2012 Elie De Brauwer <eliedebrauwer@gmail.com>

USE_SWAPOFF(NEWTOY(swapoff, "av", TOYFLAG_SBIN|TOYFLAG_NEEDROOT|TOYFLAG_LINEBUF))

config SWAPOFF
  bool "swapoff"
  default y
  help
    usage: swapoff [-av] [FILE...]

    Disable swapping on devices or files.

    -a	All active swap areas (from /proc/swaps)
    -v	Verbose
*/

#define FOR_swapoff
#include "toys.h"

static void xswapoff(char *str)
{
  if (FLAG(v)) printf("swapoff %s\n", str);
  if (swapoff(str)) perror_msg("failed to remove swaparea '%s'", str);
}

void swapoff_main(void)
{
  char *ss, *line, **args;
  FILE *fp;

  if (!FLAG(a) && !toys.optc) help_exit("Needs FILE or -a");
  if (FLAG(a) && (fp = fopen("/proc/swaps", "r"))) {
    while ((line = xgetline(fp))) {
      // Filename is first, with spaces escaped as \040 (skip header line)
      if (*line == '/' && (ss = strchr(line, ' '))) {
        *ss = 0;
        octal_deslash(line);
        xswapoff(line);
      }
      free(line);
    }
    fclose(fp);
  }
  for (args = toys.optargs; *args; args++) xswapoff(*args);
}
