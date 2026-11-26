/* swapon.c - Enable region for swapping
 *
 * Copyright 2012 Elie De Brauwer <eliedebrauwer@gmail.com>

USE_SWAPON(NEWTOY(swapon, "ap#<0>32767dv", TOYFLAG_SBIN|TOYFLAG_NEEDROOT|TOYFLAG_LINEBUF))

config SWAPON
  bool "swapon"
  default y
  help
    usage: swapon [-adv] [-p PRIORITY] [FILE...]

    Enable swapping on given devices/files.

    -a	All swap entries in /etc/fstab (skipping noauto and already active)
    -d	Discard freed SSD pages
    -p	Priority (highest priority areas allocated first)
    -v	Verbose

    Options in /etc/fstab: noauto, nofail (no error if missing), pri=PRIORITY,
    discard[=once|pages]. DEVICE can be UUID=uuid or LABEL=label.
*/

#define FOR_swapon
#include "toys.h"

GLOBALS(
  long p;
)

// Kernel swapon() flags (from linux/swap.h)
#define DISCARD 0x10000
#define DISCARD_ONCE 0x20000
#define DISCARD_PAGES 0x40000

// Is this file/device already in /proc/swaps?
static int active(char *file)
{
  struct stat st, st2;
  char *line, *s;
  FILE *fp;
  int found = 0;

  if (stat(file, &st) || !(fp = fopen("/proc/swaps", "r"))) return 0;
  while (!found && (line = xgetline(fp))) {
    // Filename is first, with spaces escaped as \040
    if (*line == '/' && (s = strchr(line, ' '))) {
      *s = 0;
      octal_deslash(line);
      if (!stat(line, &st2)) found = S_ISBLK(st.st_mode)
        ? S_ISBLK(st2.st_mode) && st.st_rdev == st2.st_rdev
        : same_file(&st, &st2);
    }
    free(line);
  }
  fclose(fp);

  return found;
}

static void xswapon(char *file, int flags)
{
  if (FLAG(v)) printf("swapon %s\n", file);
  if (swapon(file, flags)) perror_msg("Couldn't swapon '%s'", file);
}

// Apply one fstab option (len bytes at opt) to *flags. Returns 0 to skip entry.
static int swap_opt(char *opt, int len, int *flags, int *nofail, char *dev)
{
  char *end;
  long pri;

  if (len == 6 && !strncmp(opt, "noauto", 6)) return 0;
  if (len == 6 && !strncmp(opt, "nofail", 6)) *nofail = 1;
  else if (strstart(&opt, "pri=")) {
    pri = strtol(opt, &end, 10);
    if (end != opt+len-4 || pri < 0 || pri > 32767) {
      error_msg("%s: bad pri", dev);
      return 0;
    }
    *flags = (*flags & ~SWAP_FLAG_PRIO_MASK) | SWAP_FLAG_PREFER
      | (pri << SWAP_FLAG_PRIO_SHIFT);
  } else if (len == 7 && strstart(&opt, "discard")) *flags |= DISCARD;
  else if (len == 12 && strstart(&opt, "discard=once"))
    *flags |= DISCARD|DISCARD_ONCE;
  else if (len == 13 && strstart(&opt, "discard=pages"))
    *flags |= DISCARD|DISCARD_PAGES;

  return 1;
}

// -a: everything of type swap in /etc/fstab. Options there override -p/-d.
static void swapon_all(int flags)
{
  struct mtab_list *mm, *mtl = xgetmountlist("/etc/fstab");
  char *dev, *opt, *s, *blkid;
  int fl, len, nofail;

  dlist_terminate(mtl);   // (returns the last entry, mtl is still the first)

  for (mm = mtl; mm; mm = mm->next) {
    if (strcmp(mm->type, "swap")) continue;
    fl = flags;
    nofail = 0;
    for (s = mm->opts; (opt = comma_iterate(&s, &len));)
      if (!swap_opt(opt, len, &fl, &nofail, mm->device)) break;
    if (opt) continue;

    // Like mount, look up UUID= and LABEL= with blkid
    dev = mm->device;
    blkid = strstart(&dev, "UUID=") ? "-U"
      : strstart(&dev, "LABEL=") ? "-L" : 0;
    if (blkid) {
      if (!(dev = chomp(xrunread((char *[]){"blkid", blkid, dev, 0}, 0)))
          || !*dev) {
        if (!nofail) error_msg("no %s", mm->device);
        free(dev);
        continue;
      }
    } else dev = xstrdup(dev);

    // nofail: quietly skip devices that don't exist
    if (!(nofail && access(dev, F_OK)) && !active(dev)) xswapon(dev, fl);
    free(dev);
  }
  llist_traverse(mtl, free);
}

void swapon_main(void)
{
  char **args;
  int flags = FLAG(d)*(DISCARD|DISCARD_ONCE|DISCARD_PAGES);

  if (!FLAG(a) && !toys.optc) help_exit("Needs FILE or -a");
  if (FLAG(p)) flags |= SWAP_FLAG_PREFER | (TT.p << SWAP_FLAG_PRIO_SHIFT);
  if (FLAG(a)) swapon_all(flags);
  for (args = toys.optargs; *args; args++) xswapon(*args, flags);
}
