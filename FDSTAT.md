# fdstat 프로젝트 안내

[toybox](https://landley.net/toybox) 포크에서 **FD(File Descriptor) 사용량을 보여주는 `fdstat` 명령**을 새로 만들고,
관련된 toybox 명령들의 버그 수정·기능 추가·테스트 작성을 진행하는 팀 프로젝트입니다.

- 팀원: 김건우, 나주한
- 저장소: https://github.com/Geon-NI/toybox (원본: https://github.com/landley/toybox)
- 최종 갱신: 2026-11-19

> 이 파일은 팀 포크 전용 문서입니다. 원본(upstream)에 PR 을 보낼 때는 포함하지 않습니다.

---

## 1. fdstat 이란

```
usage: fdstat [-tlLU] [-u USER] [-c NAME] [-w PERCENT] [-d SECONDS] [-n COUNT] [PID...]
```

| 사용 예 | 동작 |
|---|---|
| `fdstat` | 시스템 전체 FD 사용량 (할당 / 미사용 / 최대) |
| `fdstat 1234 5678` | 지정한 프로세스별 열린 FD 수와 한도 대비 사용률 |
| `fdstat -t 1234` | FD 종류별 분류 (file/pipe/device/tcp/udp/unix/netlink/eventfd/epoll/...) |
| `fdstat -n 10` | FD 를 가장 많이 연 프로세스 상위 10개 |
| `fdstat -w 80` | 한도의 80% 를 넘으면 경고 |
| `fdstat -d 2 -L 1234` | 2초마다 갱신하며 FD 가 계속 늘어나는(누수 의심) 프로세스 표시 |
| `fdstat -l 1234` | 프로세스의 FD 를 하나씩 (번호 / 종류 / 대상) 출력 |
| `fdstat -U` | 사용자별 FD 합계 |
| `fdstat -n 5 -u www-data` | `www-data` 소유 프로세스 중 상위 5개 (`-c NAME` 은 명령 이름으로 필터) |

데이터 출처: `/proc/sys/fs/file-nr`, `/proc/sys/fs/nr_open`, `/proc/PID/fd`, `/proc/PID/limits`, `/proc/net/*`

## 2. 파일 위치

| 경로 | 내용 |
|---|---|
| `toys/pending/fdstat.c` | fdstat 본체 (현재 뼈대 + TODO) |
| `tests/fdstat.test` | fdstat 테스트 |
| `FDSTAT.md` | 이 문서 |

toybox 전체 구조는 [`www/code.html`](www/code.html), 코드 정리 기준은 [`www/cleanup.html`](www/cleanup.html) 참고.

## 3. 빌드와 테스트

```sh
make fdstat              # fdstat 하나만 단독 빌드 (./fdstat 생성, 커밋하지 말 것)
./fdstat --help
make test_fdstat         # tests/fdstat.test 실행
make test_NAME           # 다른 명령 테스트 (예: make test_vmstat)
```

전체 toybox 에 넣어 빌드하려면 `make menuconfig` 에서 `Pending → fdstat` 를 켭니다.

## 4. 브랜치 전략

```
upstream/master ──> master ──> dev <── feature/<작업이름>   (PR → CI ✅ 확인 후 본인이 merge)
```

| 브랜치 | 역할 |
|---|---|
| `master` | 원본 toybox 를 그대로 따라가는 기준 브랜치. 직접 커밋하지 않음 |
| `dev` | 팀 통합 브랜치. 모든 작업은 여기로 PR |
| `feature/*` | 작업 단위 브랜치. `dev` 에서 따서 작업 (예: `feature/fdstat-pid`, `feature/column`) |

작업 하나 = 이슈 하나 = 브랜치 하나 = PR 하나를 원칙으로 합니다.
PR 설명에 `Closes #이슈번호` 를 적으면 merge 될 때 이슈가 자동으로 닫힙니다.
이슈 목록: https://github.com/Geon-NI/toybox/issues (라벨 `area-A` = 나주한, `area-B` = 김건우)

## 5. toybox 코딩 규칙 요약

- 명령 하나 = 파일 하나. 파일 맨 위 주석의 `NEWTOY(...)` 와 `config` 블록이 옵션·도움말이 된다.
- `GLOBALS()` 필드는 **인자를 받는 옵션을 옵션 문자열의 역순**으로 나열한다. 접근은 `TT.x`.
- 옵션 확인은 `FLAG(x)`, 위치 인자는 `toys.optargs` / `toys.optc`.
- 공용 함수는 `lib/` 에 있다 (`xopen`, `readfile`, `dirtree_read`, `error_exit` 등). 직접 짜기 전에 먼저 찾아본다.
- 들여쓰기 2칸, 짧고 단순하게. 기능을 추가하면 도움말과 테스트도 같이 고친다.

---

## 6. 역할 분담

### 6.1 fdstat 내부 분담

| 영역 | 담당 | 함수 / 옵션 |
|---|---|---|
| A. 시스템 통계·한도·모니터링 | 나주한 | `show_system`, `fd_limit`, `check_warn` (`-w`), `-d` 반복문, `check_leak` (`-L`) |
| B. 프로세스별 통계 | 김건우 | `count_fds`, `fd_type`, `sock_type` (`-t`), `show_pid`, `show_top` (`-n`), `list_fds` (`-l`), `match_proc` (`-u`/`-c`), `show_users` (`-U`) |
| 공통 | 김건우 | `fdstat_main` 옵션 검증·모드 분기 |

영역 간 연결점: `fd_limit()`(A) 는 `show_pid`/`show_top`(B) 에서 사용률 계산에 쓰고,
`check_leak()`(A) 은 `count_fds()`(B) 결과를 받아 판정한다. 함수 원형을 바꿀 때는 상대방에게 먼저 알린다.

### 6.2 전체 작업 분담 요약

| 담당 | 작업 | 예상 줄 수 (코드 + 테스트) |
|---|---|---|
| 김건우 | fdstat 영역 B + 확장, `/proc` 공용 lib, lsof 정리, `column`, vmstat·`/proc` 명령 테스트, `swapon -a` | 약 1,225 ~ 1,880 |
| 나주한 | fdstat 영역 A, `sysctl --system`, `envsubst`, fuzzing 오버플로 수정, `ipcs`/`ipcrm` 정리, `ulimit` 테스트, `w` 테스트 | 약 810 ~ 1,350 (제안, 협의 필요) |

---

## 7. 작업 목록 (로드맵)

상태: ⬜ 할 일 · 🟨 진행 중 · ✅ 완료. 줄 수는 추정치입니다.

### 7.1 김건우

#### B1. fdstat 영역 B 기본 구현 — 코드 170~230 / 테스트 60~90 · 이슈 [#9](https://github.com/Geon-NI/toybox/issues/9)
| 상태 | 작업 | 내용 |
|---|---|---|
| ✅ | 뼈대 | 옵션 문자열, 도움말, 함수 원형, 옵션 검증 테스트 4개 |
| ✅ | `count_fds`, `show_pid` | `/proc/PID/fd` 순회, 개수 + 한도 대비 사용률 출력, 없는 PID·권한 에러 처리 |
| ✅ | `fd_type` | `readlink` 결과 접두사로 file/pipe/device/socket/anon_inode 판정, anon_inode 세분화 |
| ✅ | `sock_type` | `/proc/net/{tcp,tcp6,udp,udp6,unix,netlink}` 를 한 번 읽어 캐시, inode 로 종류 판정 |
| ✅ | `show_top` (`-n`) | `/proc` 전체 순회, FD 수 기준 정렬, 상위 N개 출력, 권한 없어 건너뛴 프로세스 수 표시 |
| ✅ | 테스트 | `sh -c 'exec 3</dev/null 4</dev/null; ...'` 로 FD 수를 고정해 비교, `-t`·`-n` 출력 확인 |

#### B2. fdstat 영역 B 확장 — 코드 80~115 / 테스트 40~60 · 이슈 [#10](https://github.com/Geon-NI/toybox/issues/10)
| 상태 | 작업 | 내용 |
|---|---|---|
| ✅ | 뼈대 | 옵션 문자열·도움말·함수 원형 (`list_fds`, `match_proc`, `show_users`), 옵션 충돌 테스트 5개 |
| ⬜ | `-l` | FD 번호 / 종류 / 대상을 한 줄씩 상세 출력 |
| ⬜ | `-u USER`, `-c NAME` | 사용자·명령 이름으로 집계 대상 필터링 |
| ⬜ | `-U` | 프로세스 대신 사용자(UID)별 합계 |

#### B3. `/proc` 공용 라이브러리 — 코드 180~270 · 이슈 [#11](https://github.com/Geon-NI/toybox/issues/11)
`lsof.c` 와 `netstat.c` 가 `/proc/net/*` 파싱을 각자 중복 구현하고 있고, fdstat 도 같은 기능이 필요하다.
| 상태 | 작업 | 내용 |
|---|---|---|
| ⬜ | `lib/` 함수 추가 | PID 별 FD 순회, 소켓 inode → 종류/주소 조회 |
| ⬜ | 적용 | fdstat · lsof · netstat 이 공용 함수를 쓰도록 수정 (동작 변화 없음을 테스트로 확인) |

#### B4. `lsof` 정리 (`toys/pending/lsof.c`) — 코드 150~250 / 테스트 80~120 · 이슈 [#12](https://github.com/Geon-NI/toybox/issues/12)
| 상태 | 작업 | 내용 |
|---|---|---|
| ⬜ | 정리 | `www/cleanup.html` 기준 정리, B3 공용 함수 적용, 옵션·출력 버그 수정 |
| ⬜ | 테스트 신규 | 현재 `tests/lsof.test` 없음. `-p`, `-t` 등 FD 를 고정한 셸로 확인 |

#### B5. `column` 신규 명령 ([#591](https://github.com/landley/toybox/issues/591)) — 코드 200~300 / 테스트 60~100 · 이슈 [#13](https://github.com/Geon-NI/toybox/issues/13)
| 상태 | 작업 | 내용 |
|---|---|---|
| ⬜ | 구현 | `toys/pending/column.c`. `-t`, `-s`, `-o`, `-c`, `-x`, `-N`, `-R` |
| ⬜ | 테스트 | util-linux `column` 출력과 비교 |

#### B6. `/proc` 명령 버그 수정·테스트 — 코드 5~15 / 테스트 120~190 · 이슈 [#14](https://github.com/Geon-NI/toybox/issues/14)
| 상태 | 작업 | 내용 |
|---|---|---|
| ⬜ | `vmstat` bi/bo ([#525](https://github.com/landley/toybox/issues/525)) | `pgpgin`/`pgpgout` 은 KB 단위인데 페이지 크기를 곱하는 버그 수정 + 테스트 신규 |
| ⬜ | 테스트 신규 | `netstat`, `pwdx`, `free`, `pmap` (현재 모두 테스트 없음) |

#### B7. `swapon -a` / `swapoff -a` ([#594](https://github.com/landley/toybox/issues/594)) — 코드 60~100 / 테스트 20~40 · 이슈 [#15](https://github.com/Geon-NI/toybox/issues/15)
| 상태 | 작업 | 내용 |
|---|---|---|
| ⬜ | 구현 | `/etc/fstab` 의 swap 항목 처리, `noauto`·이미 활성(`/proc/swaps`) 건너뛰기, `pri=` 지원 |
| ⬜ | 테스트 | root 필요 → root 가 아니면 건너뛰도록 작성 |

### 7.2 나주한 (제안, 협의 필요)

#### A1. fdstat 영역 A — 코드 100~150 / 테스트 40~70 · 이슈 [#2](https://github.com/Geon-NI/toybox/issues/2)
| 상태 | 작업 | 내용 |
|---|---|---|
| ⬜ | `show_system` | `file-nr`, `file-max`, `nr_open` 파싱·출력 |
| ⬜ | `fd_limit`, `check_warn` (`-w`) | `/proc/PID/limits` 의 `Max open files` 파싱(unlimited 처리), 사용률 경고 |
| ⬜ | `-d` | `fdstat_main` 분기를 반복문으로 감싸고 직전 값 대비 변화량 출력 |
| ⬜ | `check_leak` (`-L`) | PID 별 이력 저장(사라진 PID 정리), 연속 증가 판정 |

#### A2. `sysctl --system` ([#575](https://github.com/landley/toybox/issues/575)) — 코드 40~80 / 테스트 30~50 · 이슈 [#3](https://github.com/Geon-NI/toybox/issues/3)
| 상태 | 작업 | 내용 |
|---|---|---|
| ⬜ | 구현 | `/etc/sysctl.d`, `/run/sysctl.d`, `/usr/lib/sysctl.d`, `/etc/sysctl.conf` 순서 처리, 같은 파일 이름은 우선순위 높은 쪽만. 기존 `-p` 코드 재사용 |
| ⬜ | 테스트 신규 | 현재 `tests/sysctl.test` 없음 |

#### A3. `envsubst` 신규 명령 ([#507](https://github.com/landley/toybox/issues/507)) — 120~200 · 이슈 [#4](https://github.com/Geon-NI/toybox/issues/4)
| 상태 | 작업 | 내용 |
|---|---|---|
| ⬜ | 구현 + 테스트 | `$VAR`, `${VAR}` 치환, SHELL-FORMAT 인자로 대상 변수 제한, `-v`. 명령 치환 `$(...)` 은 실행하지 않음 |

#### A4. fuzzing 으로 발견된 메모리 오류 수정 — 건당 10~30 · 이슈 [#5](https://github.com/Geon-NI/toybox/issues/5)
| 상태 | 이슈 | 대상 |
|---|---|---|
| ⬜ | [#571](https://github.com/landley/toybox/issues/571) | getopt heap-buffer-overflow |
| ⬜ | [#572](https://github.com/landley/toybox/issues/572) | patch heap-buffer-overflow |
| ⬜ | [#573](https://github.com/landley/toybox/issues/573) | mkpasswd global-buffer-overflow |
| ⬜ | [#574](https://github.com/landley/toybox/issues/574) | printf out-of-bounds read |
| ⬜ | [#578](https://github.com/landley/toybox/issues/578) | find stack-buffer-overflow |
| ⬜ | [#579](https://github.com/landley/toybox/issues/579) | sed heap-buffer-overflow |

ASAN 으로 재현 → 수정 → 같은 입력으로 회귀 테스트 추가.

#### A5. `w` 테스트 신규 — 테스트 20~40 · 이슈 [#6](https://github.com/Geon-NI/toybox/issues/6)

#### A6. `ipcs` / `ipcrm` 정리 (`toys/pending/`) — 코드 120~200 / 테스트 50~80 · 이슈 [#7](https://github.com/Geon-NI/toybox/issues/7)
`/proc/sysvipc/{shm,sem,msg}` 를 읽어 시스템 자원 사용량을 보여주는 명령으로, 영역 A 의 `show_system` 과 구조가 비슷하다.
| 상태 | 작업 | 내용 |
|---|---|---|
| ⬜ | 정리 | `www/cleanup.html` 기준 정리 (`ipcs.c` 447줄, `ipcrm.c` 88줄), 중복 출력 코드 통합, 옵션·에러 처리 수정 |
| ⬜ | 테스트 신규 | 현재 `tests/ipcs.test` 없음. `ipcs -l` 한도 출력, `ipcrm` 잘못된 ID 에러 등 |

#### A7. `ulimit` / `prlimit` 테스트 신규 (`toys/posix/ulimit.c`) — 테스트 40~70 · 이슈 [#8](https://github.com/Geon-NI/toybox/issues/8)
fdstat 의 `fd_limit()` 이 읽는 RLIMIT_NOFILE 을 설정하는 명령이라 영역 A 와 직접 연결된다.
| 상태 | 작업 | 내용 |
|---|---|---|
| ⬜ | 테스트 신규 | `ulimit -n` 조회·변경, `prlimit` 로 다른 프로세스 한도 조회, 설정 후 `/proc/PID/limits` 및 `fdstat PID` 결과와 일치하는지 확인 |
| ⬜ | 발견 시 수정 | 테스트 중 발견한 버그는 별도 PR 로 수정 |

---

## 8. 손대지 않기로 한 이슈

| 이슈 | 이유 |
|---|---|
| [#489](https://github.com/landley/toybox/issues/489) `diff -I` | 관리자·구글 개발자 모두 구조상 어렵다고 보류한 상태 |
| [#406](https://github.com/landley/toybox/issues/406) `install -T` | 관리자가 자기 쪽에서 `cp.c` 를 수정 중이라 충돌 |
| [#563](https://github.com/landley/toybox/issues/563) `cp` 쓰기 에러 | 수정 범위가 몇 줄이고 다른 개발자가 이미 진행 중 |
| `sh` 관련 이슈 전반 | toysh 는 규모가 크고 관리자가 직접 작업 중 |

## 9. 처음 기여하는 방법

1. 이 문서 7장에서 ⬜ 상태인 작업을 고르고, 연결된 이슈에 자신을 담당자(assignee)로 지정한다. 새 작업이면 이슈부터 만든다.
2. `dev` 에서 `feature/<작업이름>` 브랜치를 딴다.
3. 코드 + 도움말 + 테스트를 함께 작성하고 `make test_NAME` 이 통과하는지 확인한다.
4. `dev` 로 PR 을 올리고 (설명에 `Closes #번호`), CI ✅ 를 확인한 뒤 본인이 merge 한다. merge 후 이 문서의 상태를 ✅ 로 바꾼다.
5. 기여 줄 수 확인: `git log --author=<이름> --numstat dev`
