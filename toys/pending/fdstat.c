/* fdstat.c - Show file descriptor usage.
 *
 * Copyright 2026 Kim Geonwoo, Na Juhan
 *
 * No standard. Reads /proc/sys/fs/file-nr and /proc/$PID/fd.

USE_FDSTAT(NEWTOY(fdstat, "tw#<1>100d#<1n#<1", TOYFLAG_USR|TOYFLAG_BIN))

config FDSTAT
  bool "fdstat"
  default n
  help
    usage: fdstat [-t] [-w PERCENT] [-d SECONDS] [-n COUNT] [PID...]

    Show file descriptor (FD) usage.

    With no arguments, show system-wide FD usage (allocated/unused/max).
    With PIDs, show the number of open FDs of each process.

    -t	Break down FDs by type (file/socket/pipe/anon_inode/device)
    -w	Warn if usage exceeds PERCENT of the limit
    -d	Repeat every SECONDS and show the change since last time
    -n	Show the COUNT processes with the most open FDs
*/

#define FOR_fdstat
#include "toys.h"

// GLOBALS 필드는 옵션 문자열의 인자 옵션(w, d, n) 순서의 "역순"으로 나열한다.
GLOBALS(
  long n, d, w;
)

/* ------------------------------------------------------------------ *
 * 영역 A (나주한): 시스템 전체 FD 통계
 *   -w 사용률 경고, -d 주기 모니터링
 * ------------------------------------------------------------------ */

// 시스템 전체 FD 통계 출력: /proc/sys/fs/file-nr, file-max, nr_open
static void show_system(void)
{
  // TODO(나주한): 파싱 및 출력
}

/* ------------------------------------------------------------------ *
 * 영역 B (김건우): 프로세스별 FD 통계
 *   -t 타입 분류, -n Top-N 정렬
 * ------------------------------------------------------------------ */

// 지정한 PID 하나의 FD 정보를 출력: /proc/PID/fd 순회
static void show_pid(char *pid)
{
  // TODO(김건우): 순회 및 출력
}

// 전체 프로세스 중 FD 사용량 상위 TT.n 개 출력
static void show_top(void)
{
  // TODO(김건우): /proc 순회, 정렬, 출력
}

/* ------------------------------------------------------------------ *
 * 공통 (김건우): 옵션 검증과 모드 분기
 * ------------------------------------------------------------------ */

void fdstat_main(void)
{
  char **arg;

  // 모드 충돌 검사
  if (FLAG(n) && toys.optc) error_exit("-n takes no PID");

  if (FLAG(n)) show_top();
  else if (toys.optc) for (arg = toys.optargs; *arg; arg++) show_pid(*arg);
  else show_system();
}
