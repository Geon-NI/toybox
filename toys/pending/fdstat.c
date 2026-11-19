/* fdstat.c - Show file descriptor usage.
 *
 * Copyright 2026 Kim Geonwoo, Na Juhan
 *
 * No standard. Reads /proc/sys/fs/file-nr, /proc/$PID/fd, /proc/$PID/limits
 * and /proc/net/{tcp,tcp6,udp,udp6,unix,netlink}.

USE_FDSTAT(NEWTOY(fdstat, "LtlUu:c:w#<1>100d#<1n#<1[!nU]", TOYFLAG_USR|TOYFLAG_BIN))

config FDSTAT
  bool "fdstat"
  default n
  help
    usage: fdstat [-tlLU] [-u USER] [-c NAME] [-w PERCENT] [-d SECONDS] [-n COUNT] [PID...]

    Show file descriptor (FD) usage.

    With no arguments, show system-wide FD usage (allocated/unused/max).
    With PIDs, show the number of open FDs of each process and its
    usage of the per-process limit (RLIMIT_NOFILE).

    -t	Break down FDs by type (file/pipe/device/socket/anon_inode).
    	Sockets are split into tcp/udp/unix/netlink, anon_inodes into
    	eventfd/epoll/inotify/timerfd/signalfd.
    -w	Warn if usage exceeds PERCENT of the limit (system or per-process)
    -d	Repeat every SECONDS and show the change since last time
    -L	With -d, flag processes whose FD count keeps growing (likely leak)
    -n	Show the COUNT processes with the most open FDs
    -U	Show FD totals per user instead of per process
    -l	List each FD of PID (number, type, target)
    -u	Only count processes owned by USER (with -n or -U)
    -c	Only count processes whose command name is NAME (with -n or -U)
*/

#define FOR_fdstat
#include "toys.h"

// GLOBALS 필드는 옵션 문자열의 인자 옵션(u, c, w, d, n) 순서의 "역순"으로 나열한다.
// 인자 없는 플래그(L, t, l, U)는 GLOBALS 에 영향을 주지 않는다.
GLOBALS(
  long n, d, w;
  char *c, *u;

  // 여기부터는 옵션과 무관한 내부 상태
  int hdr;        // 프로세스 표 머리글을 이미 출력했으면 1
)

/* ------------------------------------------------------------------ *
 * 영역 A (나주한): 시스템 전체 FD 통계, 한도/경고, 주기 모니터링
 *   show_system, -w 사용률 경고, 프로세스별 한도, -d 주기 모니터링,
 *   -L 누수 감지
 * ------------------------------------------------------------------ */

// 시스템 전체 FD 통계 출력: /proc/sys/fs/file-nr, file-max, nr_open
static void show_system(void)
{
  // TODO(나주한): 파싱 및 출력, -w 이면 file-max 대비 사용률 경고
}

// /proc/PID/limits 의 "Max open files" soft 한도를 반환.
// 실패 시 -1, "unlimited" 면 0. (show_pid 는 -1 → "?", 0 → "-" 로 출력)
// 영역 B 의 show_pid/show_top 에서도 사용률 계산에 쓴다.
static long fd_limit(char *pid)
{
  // TODO(나주한): "Max open files" 줄 파싱, "unlimited" 처리
  return -1;
}

// count 가 limit 의 TT.w% 를 넘으면 경고 출력 (-w)
static void check_warn(char *name, long count, long limit)
{
  // TODO(나주한): 사용률 계산 및 경고 형식 결정
}

// -L: 직전 값들과 비교해 FD 수가 연속 증가 중인 PID 를 표시
// -d 반복 사이에 PID 별 이전 개수와 증가 횟수를 기억해야 한다.
static void check_leak(long pid, long count)
{
  // TODO(나주한): PID 별 이력 저장(사라진 PID 정리 포함), 연속 증가 판정
}

/* ------------------------------------------------------------------ *
 * 영역 B (김건우): 프로세스별 FD 통계
 *   show_pid, -t 타입 분류(소켓/anon_inode 세분화), -n Top-N 정렬
 * ------------------------------------------------------------------ */

// FD 종류. -t 출력 순서와 같게 유지한다.
enum {
  FD_FILE, FD_PIPE, FD_DEV, FD_TCP, FD_UDP, FD_UNIX, FD_NETLINK, FD_SOCK,
  FD_EVENTFD, FD_EPOLL, FD_INOTIFY, FD_TIMERFD, FD_SIGNALFD, FD_ANON,
  FD_TYPES
};

// readlink(/proc/PID/fd/N) 결과로 FD 종류를 판정
// "socket:[inode]" 는 sock_type() 으로, "anon_inode:[name]" 은 이름으로 세분화
static int fd_type(char *link)
{
  // TODO(김건우): 접두사 비교 (socket:, pipe:, anon_inode:, /dev/)
  return FD_FILE;
}

// 소켓 inode 가 /proc/net/{tcp,tcp6,udp,udp6,unix,netlink} 중 어디 있는지 확인
static int sock_type(long inode)
{
  // TODO(김건우): /proc/net 테이블을 한 번 읽어 캐시해 두고 조회
  return FD_SOCK;
}

// /proc/PID/fd 를 순회해 FD 개수를 세고, types 가 있으면 종류별로 누적.
// 열 수 없으면 (없는 PID, 권한 없음) errno 를 남기고 -1 을 반환한다.
// show_pid 와 show_top 이 함께 쓴다.
static long count_fds(char *pid, long *types)
{
  char buf[64];
  struct dirent *de;
  DIR *dir;
  long count = 0;

  sprintf(buf, "/proc/%s/fd", pid);
  if (!(dir = opendir(buf))) return -1;
  while ((de = readdir(dir))) {
    if (*de->d_name == '.') continue;
    count++;
    if (types) {
      readlinkat0(dirfd(dir), de->d_name, toybuf, sizeof(toybuf));
      types[fd_type(toybuf)]++;
    }
  }
  closedir(dir);

  return count;
}

// -l: PID 의 FD 를 하나씩 출력 (번호, fd_type 결과, readlink 대상)
static void list_fds(char *pid)
{
  // TODO(김건우): /proc/PID/fd 순회하며 한 줄씩 출력
}

// 지정한 PID 하나의 FD 정보를 출력 (개수, 한도 대비 사용률, -t 분류)
//   PID     COMMAND            FDS    LIMIT  USE%
//   1234    nginx               42     1024    4%
static void show_pid(char *pid)
{
  char buf[64], *s;
  long count, limit;

  for (s = pid; isdigit(*s); s++);
  if (*s || s == pid) return error_msg("bad PID '%s'", pid);
  if ((count = count_fds(pid, 0)) < 0) {
    if (errno == ENOENT) return error_msg("%s: no such process", pid);
    return perror_msg("%s", pid);
  }

  // 프로세스 이름. 읽는 사이 프로세스가 끝났으면 "?"
  sprintf(buf, "/proc/%s/comm", pid);
  if (!readfile(buf, s = libbuf, sizeof(libbuf))) s = "?";
  chomp(s);

  if (!TT.hdr++)
    printf("%-7s %-15s %6s %8s %5s\n", "PID", "COMMAND", "FDS", "LIMIT", "USE%");
  printf("%-7s %-15s %6ld ", pid, s, count);
  if ((limit = fd_limit(pid)) > 0) {
    printf("%8ld %4ld%%\n", limit, count*100/limit);
    if (FLAG(w)) check_warn(pid, count, limit);
  } else printf("%8s %5s\n", limit ? "?" : "-", limit ? "?" : "-");

  // TODO(김건우): -t 이면 count_fds(pid, types) 로 종류별 개수 출력 (PR ②)
  if (FLAG(l)) list_fds(pid);
}

// -u/-c 필터: 집계 대상 프로세스면 1. 필터가 없으면 항상 1.
static int match_proc(char *pid)
{
  // TODO(김건우): -u 는 /proc/PID 소유 uid 와 비교 (USER 이름은 main 에서
  //               한 번만 uid 로 변환), -c 는 /proc/PID/comm 과 비교
  return 1;
}

// 전체 프로세스 중 FD 사용량 상위 TT.n 개 출력 (match_proc 통과한 것만)
static void show_top(void)
{
  // TODO(김건우): /proc 순회, 정렬, 출력. 권한 없는 프로세스 수는 따로 표시
}

// -U: 사용자(uid)별 FD 합계 출력 (match_proc 통과한 것만)
static void show_users(void)
{
  // TODO(김건우): /proc 순회하며 uid 별 누적, 많은 순 정렬, 이름으로 출력
}

/* ------------------------------------------------------------------ *
 * 공통 (김건우): 옵션 검증과 모드 분기
 * ------------------------------------------------------------------ */

void fdstat_main(void)
{
  char **arg;

  // 모드 충돌 검사 (-n 과 -U 동시 사용은 옵션 문자열의 [!nU] 가 막는다)
  if ((FLAG(n) || FLAG(U)) && toys.optc) error_exit("-n/-U take no PID");
  if (FLAG(L) && !FLAG(d)) error_exit("-L needs -d");
  if (FLAG(l) && !toys.optc) error_exit("-l needs PID");
  if ((FLAG(u) || FLAG(c)) && !FLAG(n) && !FLAG(U))
    error_exit("-u/-c need -n or -U");

  // TODO(나주한): -d 이면 아래 분기를 TT.d 초 간격 반복문으로 감싼다
  if (FLAG(n)) show_top();
  else if (FLAG(U)) show_users();
  else if (toys.optc) for (arg = toys.optargs; *arg; arg++) show_pid(*arg);
  else show_system();
}
