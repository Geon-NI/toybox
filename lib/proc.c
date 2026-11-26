/* proc.c - Helpers for reading per-process FDs and /proc/net socket tables.
 *
 * Copyright 2026 Kim Geonwoo
 *
 * Shared by fdstat, lsof and netstat, which used to each parse these
 * files on their own.
 */

#include "toys.h"

static int cmp_long(const void *a, const void *b)
{
  long x = *(long *)a, y = *(long *)b;

  return (x > y) - (x < y);
}

// Call fn(dirfd, fd, data) for each open fd of pid, in ascending fd order.
// dirfd is /proc/PID/fd (for readlinkat()/fstatat()), fd is the number as a
// string. With fn NULL just count. Returns the number of fds, or -1 with errno
// set if /proc/PID/fd can't be opened (no such process, permission denied).
long proc_fds(long pid, void (*fn)(int dirfd, char *fd, void *data), void *data)
{
  char buf[32];
  struct dirent *de;
  long *fds = 0, n = 0, i;
  DIR *dir;

  sprintf(buf, "/proc/%ld/fd", pid);
  if (!(dir = opendir(buf))) return -1;
  while ((de = readdir(dir))) {
    if (!isdigit(*de->d_name)) continue;
    if (fn) {
      if (!(n & 255)) fds = xrealloc(fds, (n+256)*sizeof(long));
      fds[n] = atol(de->d_name);
    }
    n++;
  }
  if (fn) {
    qsort(fds, n, sizeof(long), cmp_long);
    for (i = 0; i < n; i++) {
      sprintf(buf, "%ld", fds[i]);
      fn(dirfd(dir), buf, data);
    }
    free(fds);
  }
  closedir(dir);

  return n;
}

// Parse one line of /proc/net/NAME into ps. Returns 0 if it didn't parse.
// Column layouts (first line of each file is a header):
//   tcp/udp/raw: sl local:port remote:port st tx:rx tr:when retr uid tmo inode
//                (addresses are 8 hex digits, or 32 in the "6" files)
//   unix:        Num RefCount Protocol Flags Type St Inode [Path]
//   netlink:     sk Eth Pid Groups Rmem Wmem Dump Locks Drops Inode
static int parse_sock(struct proc_sock *ps, char *line)
{
  unsigned *l = ps->laddr, *r = ps->raddr;
  int len = 0;

  if (ps->af == AF_UNIX) {
    if (sscanf(line, "%*p: %lX %*X %lX %X %X %llu %n", &ps->refcount,
        &ps->flags, &ps->type, &ps->state, &ps->inode, &len) < 5) return 0;
    // Path follows the inode (unnamed sockets have none)
    if (len && line[len]) ps->path = line+len;

    return 1;
  }
  if (ps->af == AF_NETLINK)
    return 2 == sscanf(line, "%*p %d %*u %*x %*u %*u %*u %*u %*u %llu",
      &ps->protocol, &ps->inode);

  ps->type = *ps->name=='t' ? SOCK_STREAM : *ps->name=='u' ? SOCK_DGRAM
    : SOCK_RAW;
  if (ps->af == AF_INET) return 9 == sscanf(line,
    " %*d: %x:%x %x:%x %x %x:%x %*X:%*X %*X %u %*d %llu",
    l, &ps->lport, r, &ps->rport, &ps->state, &ps->txq, &ps->rxq,
    &ps->uid, &ps->inode);

  return 15 == sscanf(line,
    " %*d: %8x%8x%8x%8x:%x %8x%8x%8x%8x:%x %x %x:%x %*X:%*X %*X %u %*d %llu",
    l, l+1, l+2, l+3, &ps->lport, r, r+1, r+2, r+3, &ps->rport,
    &ps->state, &ps->txq, &ps->rxq, &ps->uid, &ps->inode);
}

// Call fn(ps, data) for each socket in /proc/net/NAME, where NAME is one of
// tcp tcp6 udp udp6 raw raw6 unix netlink. ps (and ps->path) are only valid
// during the call. Entries with inode 0 (TIME_WAIT etc) are included,
// lines that don't parse are skipped.
// Returns -1 with errno set if the file can't be opened (IPv6 off etc), else 0.
int proc_net(char *name, void (*fn)(struct proc_sock *ps, void *data),
  void *data)
{
  struct proc_sock ps;
  char buf[32], *line;
  FILE *fp;
  int af = AF_INET;

  if (!strcmp(name, "unix")) af = AF_UNIX;
  else if (!strcmp(name, "netlink")) af = AF_NETLINK;
  else if (name[strlen(name)-1] == '6') af = AF_INET6;

  sprintf(buf, "/proc/net/%.20s", name);
  if (!(fp = fopen(buf, "r"))) return -1;
  free(xgetline(fp));
  while ((line = xgetline(fp))) {
    memset(&ps, 0, sizeof(ps));
    ps.name = name;
    ps.af = af;
    if (parse_sock(&ps, line)) fn(&ps, data);
    free(line);
  }
  fclose(fp);

  return 0;
}

// Socket cache for proc_sock(): every table, sorted by inode
static struct proc_sock *socks;
static long nsock;
static char sockread;

static void add_sock(struct proc_sock *ps, void *unused)
{
  if (!ps->inode) return;
  if (!(nsock & 255)) socks = xrealloc(socks, (nsock+256)*sizeof(*ps));
  socks[nsock] = *ps;
  if (ps->path) socks[nsock].path = xstrdup(ps->path);
  nsock++;
}

static int cmp_sock(const void *a, const void *b)
{
  long long x = ((struct proc_sock *)a)->inode,
            y = ((struct proc_sock *)b)->inode;

  return (x > y) - (x < y);
}

// Find a socket by inode (from readlink "socket:[INODE]" or stat's st_ino)
// in all /proc/net tables, or NULL. The tables are read once on first use;
// call proc_sock_free() to make the next call read them again.
struct proc_sock *proc_sock(long long inode)
{
  char *tabs[] = {"tcp", "tcp6", "udp", "udp6", "raw", "raw6", "unix",
    "netlink"};
  struct proc_sock key;
  int i;

  if (!sockread) {
    for (i = 0; i < ARRAY_LEN(tabs); i++) proc_net(tabs[i], add_sock, 0);
    qsort(socks, nsock, sizeof(*socks), cmp_sock);
    sockread = 1;
  }
  key.inode = inode;

  return bsearch(&key, socks, nsock, sizeof(*socks), cmp_sock);
}

// Drop the proc_sock() cache (for repeated sampling, or before exit)
void proc_sock_free(void)
{
  while (nsock) free(socks[--nsock].path);
  free(socks);
  socks = 0;
  sockread = 0;
}
