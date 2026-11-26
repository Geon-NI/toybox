/* pwdx.c - report current directory of a process.
 *
 * Copyright 2013 Lukasz Skalski <l.skalski@partner.samsung.com>

USE_PWDX(NEWTOY(pwdx, "<1a", TOYFLAG_USR|TOYFLAG_BIN))

config PWDX
  bool "pwdx"
  default y
  help
    usage: pwdx PID...

    Print working directory of processes listed on command line.
*/

#include "toys.h"

void pwdx_main(void)
{
  char **optargs;

  for (optargs = toys.optargs; *optargs; optargs++) {
    char *path = toybuf, *s;

    // The whole argument must be a PID ("1x" isn't PID 1)
    for (s = *optargs; isdigit(*s); s++);
    if (*s || s == *optargs || s-*optargs > 10) path = strerror(ESRCH);
    else {
      sprintf(toybuf, "/proc/%ld/cwd", atol(*optargs));
      if (!readlink0(toybuf, toybuf, sizeof(toybuf))) path = strerror(errno);
    }
    if (path != toybuf) toys.exitval = 1;

    xprintf("%s: %s\n", *optargs, path);
  }
}
