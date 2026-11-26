/* lsof.c - list open files.
 *
 * Copyright 2015 The Android Open Source Project

USE_LSOF(NEWTOY(lsof, "lp*t", TOYFLAG_USR|TOYFLAG_BIN))

config LSOF
  bool "lsof"
  default n
  help
    usage: lsof [-lt] [-p PID1,PID2,...] [FILE...]

    List all open files belonging to all active processes, or processes using
    listed FILE(s).

    -l	list uids numerically
    -p	for given comma-separated pids only (default all pids)
    -t	terse (pid only) output
*/

#define FOR_lsof
#include "toys.h"

GLOBALS(
  struct arg_list *p;

  struct stat *sought_files;
  struct double_list *files;
  int last_shown_pid, shown_header;
)

struct proc_info {
  char cmd[17];
  int pid, uid;
};

struct file_info {
  char *next, *prev;

  // For output.
  struct proc_info pi;
  char *name, fd[12], rw, locks, type[10], device[32], size_off[32], node[32];

  // For filtering.
  struct dev_ino di;
};

static void print_info(void *data)
{
  struct file_info *fi = data;
  char *user;

  // Filter matches
  if (toys.optc) {
    int i;

    for (i = 0; i<toys.optc; i++)
      if (same_dev_ino(TT.sought_files+i, &fi->di)) break;
    if (i==toys.optc) return;
  }

  if (FLAG(t)) {
    if (fi->pi.pid != TT.last_shown_pid)
      printf("%d\n", TT.last_shown_pid = fi->pi.pid);
  } else {
    if (!TT.shown_header) {
      // TODO: llist_traverse to measure the columns first.
      printf("%-9s %5s %10.10s %4s   %7s %18s %9s %10s %s\n", "COMMAND", "PID",
        "USER", "FD", "TYPE", "DEVICE", "SIZE/OFF", "NODE", "NAME");
      TT.shown_header = 1;
    }

    // -l: numeric uid instead of user name
    if (FLAG(l)) sprintf(user = toybuf, "%d", fi->pi.uid);
    else user = getusername(fi->pi.uid);
    printf("%-9s %5d %10.10s %4s%c%c %7s %18s %9s %10s %s\n",
           fi->pi.cmd, fi->pi.pid, user,
           fi->fd, fi->rw, fi->locks, fi->type, fi->device, fi->size_off,
           fi->node, fi->name);
  }
}

static void free_info(void *data)
{
  free(((struct file_info *)data)->name);
  free(data);
}

// Fill rw mode and offset from /proc/PID/fdinfo/FD
static void fill_flags(struct file_info *fi)
{
  char path[64];
  FILE *fp;
  long long pos;
  unsigned flags;

  sprintf(path, "/proc/%d/fdinfo/%s", fi->pi.pid, fi->fd);
  if (!(fp = fopen(path, "r"))) return;

  if (fscanf(fp, "pos: %lld flags: %o", &pos, &flags) == 2) {
    flags &= O_ACCMODE;
    if (flags == O_RDONLY) fi->rw = 'r';
    else if (flags == O_WRONLY) fi->rw = 'w';
    else fi->rw = 'u';

    snprintf(fi->size_off, sizeof(fi->size_off), "0t%lld", pos);
  }
  fclose(fp);
}

// Fill TYPE and NAME of a socket from the /proc/net tables (lib/proc.c).
// Sockets not in any table keep TYPE "sock" and NAME from readlink.
static void find_socket(struct file_info *fi, long long inode)
{
  char *tcp_states[] = {
    "UNKNOWN", "ESTABLISHED", "SYN_SENT", "SYN_RECV", "FIN_WAIT1", "FIN_WAIT2",
    "TIME_WAIT", "CLOSE", "CLOSE_WAIT", "LAST_ACK", "LISTEN", "CLOSING"
  };
  char *netlink_states[] = {
    "ROUTE", "UNUSED", "USERSOCK", "FIREWALL", "SOCK_DIAG", "NFLOG", "XFRM",
    "SELINUX", "ISCSI", "AUDIT", "FIB_LOOKUP", "CONNECTOR", "NETFILTER",
    "IP6_FW", "DNRTMSG", "KOBJECT_UEVENT", "GENERIC", "DM", "SCSITRANSPORT",
    "ENCRYPTFS", "RDMA", "CRYPTO"
  };
  char lip[INET6_ADDRSTRLEN], rip[INET6_ADDRSTRLEN];
  struct proc_sock *ps = proc_sock(inode);
  int v6;

  if (!ps) return;
  if (ps->af == AF_UNIX) {
    strcpy(fi->type, "unix");
    fi->name = xstrdup(ps->path ? : "socket");
  } else if (ps->af == AF_NETLINK) {
    strcpy(fi->type, "netlink");
    fi->name = xstrdup((unsigned)ps->protocol < ARRAY_LEN(netlink_states)
      ? netlink_states[ps->protocol] : "?");
  } else {
    v6 = ps->af == AF_INET6;
    strcpy(fi->type, v6 ? "IPv6" : "IPv4");
    inet_ntop(ps->af, ps->laddr, lip, sizeof(lip));
    inet_ntop(ps->af, ps->raddr, rip, sizeof(rip));
    if (ps->type == SOCK_STREAM)
      fi->name = xmprintf(v6 ? "TCP [%s]:%u->[%s]:%u (%s)"
        : "TCP %s:%u->%s:%u (%s)", lip, ps->lport, rip, ps->rport,
        tcp_states[ps->state > TCP_CLOSING ? 0 : ps->state]);
    else fi->name = xmprintf(v6 ? "%s [%s]:%u->[%s]:%u" : "%s %s:%u->%s:%u",
      ps->type == SOCK_DGRAM ? "UDP" : "RAW", lip, ps->lport, rip, ps->rport);
  }
}

// Fill TYPE, DEVICE, SIZE/OFF and NODE by following path
static void fill_stat(struct file_info *fi, char *path)
{
  struct stat sb;
  long dev;

  if (stat(path, &sb)) return;

  // Fill TYPE.
  switch ((sb.st_mode & S_IFMT)) {
    case S_IFBLK: strcpy(fi->type, "BLK"); break;
    case S_IFCHR: strcpy(fi->type, "CHR"); break;
    case S_IFDIR: strcpy(fi->type, "DIR"); break;
    case S_IFIFO: strcpy(fi->type, "FIFO"); break;
    case S_IFLNK: strcpy(fi->type, "LINK"); break;
    case S_IFREG: strcpy(fi->type, "REG"); break;
    case S_IFSOCK: strcpy(fi->type, "sock"); break;
    default:
      snprintf(fi->type, sizeof(fi->type), "%04o", sb.st_mode & S_IFMT);
      break;
  }

  if (S_ISSOCK(sb.st_mode)) find_socket(fi, sb.st_ino);

  // Fill DEVICE.
  dev = (S_ISBLK(sb.st_mode) || S_ISCHR(sb.st_mode)) ? sb.st_rdev : sb.st_dev;
  if (!S_ISSOCK(sb.st_mode))
    snprintf(fi->device, sizeof(fi->device), "%d,%d",
             dev_major(dev), dev_minor(dev));

  // Fill SIZE/OFF.
  if (S_ISREG(sb.st_mode) || S_ISDIR(sb.st_mode))
    snprintf(fi->size_off, sizeof(fi->size_off), "%lld",
             (long long)sb.st_size);

  // Fill NODE.
  snprintf(fi->node, sizeof(fi->node), "%ld", (long)sb.st_ino);

  // Stash st_dev and st_ino for filtering.
  fi->di.dev = sb.st_dev;
  fi->di.ino = sb.st_ino;
}

static struct file_info *new_file_info(struct proc_info *pi, char *fd)
{
  struct file_info *fi = xzalloc(sizeof(struct file_info));

  dlist_add_nomalloc(&TT.files, (struct double_list *)fi);

  fi->pi = *pi;

  // Defaults.
  snprintf(fi->fd, sizeof(fi->fd), "%s", fd);
  strcpy(fi->type, "unknown");
  fi->rw = fi->locks = ' ';

  return fi;
}

// One /proc/PID/LINK entry: cwd, root and exe (fd = their FD column name),
// or fd/N (fd NULL)
static void visit_symlink(struct proc_info *pi, char *fd, char *link)
{
  struct file_info *fi = new_file_info(pi, fd ? : link);
  char path[64];

  sprintf(path, "/proc/%d/%s%s", pi->pid, fd ? "" : "fd/", link);
  if (!fd) fill_flags(fi);
  fill_stat(fi, path);
  if (!fi->name && !(fi->name = xreadlink(path))) // Sockets already have one
    fi->name = xmprintf("%s (readlink: %s)", path, strerror(errno));
}

// Files mapped into memory (executable, shared libraries...), FD column "mem"
static void visit_maps(struct proc_info *pi)
{
  FILE *fp;
  unsigned long long offset;
  long inode;
  char path[64], *line, device[10]; // xxx:xxxxx\0
  struct file_info *fi;
  int pos;

  sprintf(path, "/proc/%d/maps", pi->pid);
  if (!(fp = fopen(path, "r"))) return;
  while ((line = xgetline(fp))) {
    // Only file maps (not anonymous/heap/stack), once each (at offset 0)
    // TODO: show unique maps even if they have a non-zero offset?
    pos = 0;
    if (sscanf(line, "%*x-%*x %*s %llx %9s %ld %n", &offset, device, &inode,
        &pos) >= 3 && pos && inode && strcmp(device, "00:00") && !offset) {
      fi = new_file_info(pi, "mem");
      fill_stat(fi, fi->name = xstrdup(line+pos));
    }
    free(line);
  }
  fclose(fp);
}

static void visit_fd(int dirfd, char *fd, void *pi)
{
  visit_symlink(pi, NULL, fd);
}

static void visit_fds(struct proc_info *pi)
{
  struct file_info *fi;
  int err;

  if (proc_fds(pi->pid, visit_fd, pi) >= 0) return;
  err = errno;
  fi = new_file_info(pi, "NOFD");
  fi->name = xmprintf("/proc/%d/fd (opendir: %s)", pi->pid, strerror(err));
}

static void lsof_pid(int pid, struct stat *st)
{
  struct proc_info pi;
  struct stat sb;
  char *s;

  pi.pid = pid;

  // Skip nonexistent pids
  sprintf(toybuf, "/proc/%d/stat", pid);
  if (!readfile(toybuf, toybuf, sizeof(toybuf)-1) || !(s = strchr(toybuf, '(')))
    return;
  memcpy(pi.cmd, s+1, sizeof(pi.cmd)-1);
  pi.cmd[sizeof(pi.cmd)-1] = 0;
  if ((s = strrchr(pi.cmd, ')'))) *s = 0;

  // Get USER.
  if (!st) {
    snprintf(toybuf, sizeof(toybuf), "/proc/%d", pid);
    if (stat(toybuf, st = &sb)) return;
  }
  pi.uid = st->st_uid;

  visit_symlink(&pi, "cwd", "cwd");
  visit_symlink(&pi, "rtd", "root");
  visit_symlink(&pi, "txt", "exe");
  visit_maps(&pi);
  visit_fds(&pi);
}

static int scan_proc(struct dirtree *node)
{
  int pid;

  if (!node->parent) return DIRTREE_RECURSE|DIRTREE_SHUTUP;
  if ((pid = atol(node->name))) lsof_pid(pid, &node->st);

  return 0;
}

void lsof_main(void)
{
  struct arg_list *pp;
  int i, pid;

  // lsof will only filter on paths it can stat (because it filters by inode).
  if (toys.optc) {
    TT.sought_files = xmalloc(toys.optc*sizeof(struct stat));
    for (i = 0; i<toys.optc; ++i) xstat(toys.optargs[i], TT.sought_files+i);
  }

  if (!TT.p) dirtree_read("/proc", scan_proc);
  else for (pp = TT.p; pp; pp = pp->next) {
    char *start, *end, *next = pp->arg;

    while ((start = comma_iterate(&next, &i))) {
      // The whole entry must be a positive number ("12x" isn't PID 12)
      if ((pid = strtol(start, &end, 10))<1 || end != start+i)
        error_msg("bad -p '%.*s'", i, start);
      else lsof_pid(pid, 0);
    }
  }

  llist_traverse(TT.files, print_info);

  if (CFG_TOYBOX_FREE) {
    llist_traverse(TT.files, free_info);
    proc_sock_free();
  }
}
