/* netstat.c - Display Linux networking subsystem.
 *
 * Copyright 2012 Ranjan Kumar <ranjankumar.bth@gmail.com>
 * Copyright 2013 Kyungwan Han <asura321@gmail.com>
 *
 * Not in SUSv4.
 *
USE_NETSTAT(NEWTOY(netstat, "pWrxwutneal", TOYFLAG_BIN))
config NETSTAT
  bool "netstat"
  default y
  help
    usage: netstat [-pWrxwutneal]

    Display networking information. Default is netstat -tuwx

    -r	Routing table
    -a	All sockets (not just connected)
    -l	Listening server sockets
    -t	TCP sockets
    -u	UDP sockets
    -w	Raw sockets
    -x	Unix sockets
    -e	Extended info
    -n	Don't resolve names
    -W	Wide display
    -p	Show PID/program name of sockets
*/

#define FOR_netstat
#include "toys.h"
#include <net/route.h>

GLOBALS(
  struct num_cache *inodes;
  int wpad;
)

struct num_cache {
  struct num_cache *next;
  long long num;
  char data[];
};

// Find num in cache
static struct num_cache *get_num_cache(struct num_cache *cache, long long num)
{
  while (cache) {
    if (num==cache->num) return cache;
    cache = cache->next;
  }

  return 0;
}

// Uniquely add num+data to cache. Updates *cache, returns pointer to existing
// entry if it was already there.
static struct num_cache *add_num_cache(struct num_cache **cache, long long num,
  void *data, int len)
{
  struct num_cache *old = get_num_cache(*cache, num);

  if (old) return old;

  old = xzalloc(sizeof(struct num_cache)+len);
  old->next = *cache;
  old->num = num;
  memcpy(old->data, data, len);

  *cache = old;

  return 0;
}

static void addr2str(int af, void *addr, unsigned port, char *buf, int len,
  char *proto)
{
  char pres[INET6_ADDRSTRLEN];
  struct servent *se = 0;
  int pos, count;

  if (!inet_ntop(af, addr, pres, sizeof(pres))) perror_exit("inet_ntop");

  if (FLAG(n) || !port) {
    strcpy(buf, pres);
  } else {
    struct addrinfo hints, *result, *rp;
    char cut[4];

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = af;

    if (!getaddrinfo(pres, NULL, &hints, &result)) {
      socklen_t sock_len = (af == AF_INET) ? sizeof(struct sockaddr_in)
        : sizeof(struct sockaddr_in6);

      // We assume that a failing getnameinfo dosn't stomp "buf" here.
      for (rp = result; rp; rp = rp->ai_next)
        if (!getnameinfo(rp->ai_addr, sock_len, buf, 256, 0, 0, 0)) break;
      freeaddrinfo(result);
      buf[len] = 0;
    }

    // getservbyport() doesn't understand proto "tcp6", so truncate
    memcpy(cut, proto, 3);
    cut[3] = 0;
    se = getservbyport(htons(port), cut);
  }

  if (!strcmp(buf, "::")) strcpy(buf, "[::]");

  // Append :service or :* if port == 0.
  if (se) {
    count = snprintf(0, 0, ":%s", se->s_name);
    // NI_MAXSERV == 32, which is greater than our minimum field width.
    // (Although the longest service name on Debian in 2021 is only 16 bytes.)
    if (count>=len) {
      count = len-1;
      se->s_name[count] = 0;
    }
  } else count = port ? snprintf(0, 0, ":%u", port) : 2;
  // We always show the port, even if that means clobbering the end of the host.
  pos = strlen(buf);
  if (len-pos<count) pos = len-count;
  if (se) sprintf(buf+pos, ":%s", se->s_name);
  else sprintf(buf+pos, port ? ":%u" : ":*", port);
}

// Display info for tcp/udp/raw (proc_net() callback)
static void show_ip(struct proc_sock *ps, void *unused)
{
  char *ss_state = "UNKNOWN", buf[12], *s, *label = ps->name;
  char lip[256], rip[256];
  char *state_label[] = {"", "ESTABLISHED", "SYN_SENT", "SYN_RECV", "FIN_WAIT1",
                         "FIN_WAIT2", "TIME_WAIT", "CLOSE", "CLOSE_WAIT",
                         "LAST_ACK", "LISTEN", "CLOSING", "UNKNOWN"};
  unsigned state = ps->state;

  // Should we display this? (listening or all or TCP/UDP/RAW)
  if (!(FLAG(l) && (!ps->rport && (state&0xA))) && !FLAG(a)
      && !(ps->rport&0x70)) return;

  addr2str(ps->af, ps->laddr, ps->lport, lip, TT.wpad, label);
  addr2str(ps->af, ps->raddr, ps->rport, rip, TT.wpad, label);

  // Display data
  s = label;
  if (strstart(&s, "tcp")) {
    int sz = ARRAY_LEN(state_label);
    if (!state || state >= sz) state = sz-1;
    ss_state = state_label[state];
  } else if (strstart(&s, "udp")) {
    if (state == 1) ss_state = state_label[state];
    else if (state == 7) ss_state = "";
  } else if (strstart(&s, "raw")) sprintf(ss_state = buf, "%u", state);

  printf("%-6s%6d%7d %*.*s %*.*s %-11s", label, ps->rxq, ps->txq, -TT.wpad,
    TT.wpad, lip, -TT.wpad, TT.wpad, rip, ss_state);
  if (FLAG(e)) {
    if (FLAG(n)) sprintf(s = toybuf, "%d", ps->uid);
    else s = getusername(ps->uid);
    printf(" %-10s %-11lld", s, ps->inode);
  }
  if (FLAG(p)) {
    struct num_cache *nc = get_num_cache(TT.inodes, ps->inode);

    printf(" %s", nc ? nc->data : "-");
  }
  xputc('\n');
}

// Display info for unix sockets (proc_net() callback)
static void show_unix(struct proc_sock *ps, void *unused)
{
  char *types[] = {"","STREAM","DGRAM","RAW","RDM","SEQPACKET","DCCP","PACKET"},
       *states[] = {"","LISTENING","CONNECTING","CONNECTED","DISCONNECTING"};
  unsigned long flags = ps->flags, type = ps->type, state = ps->state;

  // Linux exports only SO_ACCEPTCON since 2.3.15pre3 in 1999, but let's
  // filter in case they add more someday.
  flags &= 1<<16;

  // Only show unconnected listening sockets with -a or -l.
  if (state==1 && flags && !(FLAG(a) || FLAG(l))) return;

  if (type==10) type = 7; // move SOCK_PACKET into line
  if (type>=ARRAY_LEN(types)) type = 0;
  if (state>=ARRAY_LEN(states) || (state==1 && !flags)) state = 0;

  if (state!=1 && FLAG(l)) return;

  sprintf(toybuf, "[ %s]", flags ? "ACC " : "");
  printf("unix  %-6ld %-11s %-10s %-13s %-8llu ",
    ps->refcount, toybuf, types[type], states[state], ps->inode);
  if (FLAG(p)) {
    struct num_cache *nc = get_num_cache(TT.inodes, ps->inode);

    printf("%-19.19s ", nc ? nc->data : "-");
  }
  if (ps->path) printf(" %s\n"+!FLAG(p), ps->path);
  else xputc('\n');
}

static void show_net(char *name, void (*fn)(struct proc_sock *, void *))
{
  if (proc_net(name, fn, 0)) perror_exit("No file /proc/net/%s", name);
}

// proc_fds() callback: remember which PID/program owns each socket inode
static void add_inode(int dirfd, char *fd, void *name)
{
  char *s = toybuf;

  if (!readlinkat0(dirfd, fd, s, sizeof(toybuf))) return;
  // Can the "[0000]:" happen in a modern kernel?
  if (strstart(&s, "socket:[") || strstart(&s, "[0000]:"))
    add_num_cache(&TT.inodes, atoll(s), name, strlen(name)+1);
}

static int scan_pids(struct dirtree *node)
{
  char *name;
  int pid;

  if (!node->parent) return DIRTREE_RECURSE;
  if (!(pid = atol(node->name))) return 0;

  sprintf(toybuf, "/proc/%d/cmdline", pid);
  if (!(readfile(toybuf, toybuf, 256))) return 0;

  name = xmprintf("%d/%s", pid, getbasename(toybuf));
  proc_fds(pid, add_inode, name);
  free(name);

  return 0;
}

// extract inet4 route info from /proc/net/route file and display it.
static void display_routes(void)
{
  static const char flagchars[] = "GHRDMDAC";
  static const unsigned flagarray[] = {
    RTF_GATEWAY, RTF_HOST, RTF_REINSTATE, RTF_DYNAMIC, RTF_MODIFIED
  };
  unsigned dest, gate, mask;
  int flags, ref, use, metric, mss, win, irtt;
  char *out = toybuf, *flag_val;
  char iface[64]={0};
  FILE *fp = xfopen("/proc/net/route", "r");

  // Skip header.
  (void)fgets(toybuf, sizeof(toybuf), fp);

  printf("Kernel IP routing table\n"
          "Destination\tGateway \tGenmask \tFlags %s Iface\n",
          !FLAG(e) ? "  MSS Window  irtt" : "Metric Ref    Use");

  while (fscanf(fp, "%63s%x%x%X%d%d%d%x%d%d%d", iface, &dest, &gate, &flags,
                &ref, &use, &metric, &mask, &mss, &win, &irtt) == 11) {
    char *destip = 0, *gateip = 0, *maskip = 0;

    // skip down interfaces.
    if (!(flags & RTF_UP)) continue;

// TODO /proc/net/ipv6_route

    if (dest) {
      if (inet_ntop(AF_INET, &dest, out, 16)) destip = out;
    } else destip = FLAG(n) ? "0.0.0.0" : "default";
    out += 16;

    if (gate) {
      if (inet_ntop(AF_INET, &gate, out, 16)) gateip = out;
    } else gateip = FLAG(n) ? "0.0.0.0" : "*";
    out += 16;

// TODO /24
    //For Mask
    if (inet_ntop(AF_INET, &mask, out, 16)) maskip = out;
    else maskip = "?";
    out += 16;

    //Get flag Values
    flag_val = out;
    *out++ = 'U';
    for (dest = 0; dest < ARRAY_LEN(flagarray); dest++)
      if (flags&flagarray[dest]) *out++ = flagchars[dest];
    *out = 0;
    if (flags & RTF_REJECT) *flag_val = '!';

    printf("%-15.15s %-15.15s %-16s%-6s", destip, gateip, maskip, flag_val);
    if (!FLAG(e)) printf("%5d %-5d %6d %s\n", mss, win, irtt, iface);
    else printf("%-6d %-2d %7d %s\n", metric, ref, use, iface);
  }
  fclose(fp);
}

void netstat_main(void)
{
  int tuwx = FLAG_t|FLAG_u|FLAG_w|FLAG_x;
  char *type = "w/o servers";

  TT.wpad = FLAG(W) ? 51 : 23;
  if (!(toys.optflags&(FLAG_r|tuwx))) toys.optflags |= tuwx;
  if (FLAG(r)) display_routes();
  if (!(toys.optflags&tuwx)) return;

  if (FLAG(a)) type = "servers and established";
  else if (FLAG(l)) type = "only servers";

  if (FLAG(p)) dirtree_read("/proc", scan_pids);

  if (toys.optflags&(FLAG_t|FLAG_u|FLAG_w)) {
    printf("Active Internet connections (%s)\n", type);
    printf("Proto Recv-Q Send-Q %*s %*s State      ", -TT.wpad, "Local Address",
      -TT.wpad, "Foreign Address");
    if (FLAG(e)) printf(" User       Inode      ");
    if (FLAG(p)) printf(" PID/Program Name");
    xputc('\n');

    if (FLAG(t)) {
      show_net("tcp", show_ip);
      show_net("tcp6", show_ip);
    }
    if (FLAG(u)) {
      show_net("udp", show_ip);
      show_net("udp6", show_ip);
    }
    if (FLAG(w)) {
      show_net("raw", show_ip);
      show_net("raw6", show_ip);
    }
  }

  if (FLAG(x)) {
    printf("Active UNIX domain sockets (%s)\n", type);
    printf("Proto RefCnt Flags       Type       State         I-Node%sPath\n",
           FLAG(p) ? "   PID/Program Name     " : "   ");
    show_net("unix", show_unix);
  }

  if (FLAG(p) && CFG_TOYBOX_FREE) llist_traverse(TT.inodes, free);
  toys.exitval = 0;
}
