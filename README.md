# remote-job-runner

Remote job execution server in C — TCP, fork/exec, select()-based multiplexing

TCP로 명령 실행 요청을 받아 `fork`/`execvp`로 실행하고, stdout/stderr와 종료 상태를 돌려주는 서버. 단일 프로세스 `select` 루프로 여러 클라이언트를 동시에 처리하며, 실행 중인 job이 있어도 다른 요청이 막히지 않는다.

## 보안 주의

이 서버는 **인증된 클라이언트에게 임의 명령 실행을 허용하는 서비스**다.

- `127.0.0.1`에만 바인드한다. 외부 네트워크에 노출하지 말 것. 원격에서 쓰려면 SSH 터널(`ssh -L 8080:127.0.0.1:8080 host`)을 사용.
- 모든 연결은 첫 줄에 `AUTH <token>`을 보내야 한다. 틀리거나 다른 명령을 먼저 보내면 연결이 끊긴다.
- 토큰은 환경변수 `JOB_RUNNER_TOKEN`(16~127자)으로 지정하고, 없으면 시작 시 랜덤 토큰을 생성해 출력한다. 명령행 인자로는 받지 않는다(`ps`에 노출).
- 통신은 평문이다. TLS는 없다.
- 실행은 셸을 거치지 않고 `execvp`로 직접 한다(파이프, 리다이렉트, 와일드카드 미지원). 서버를 실행한 사용자 권한으로 돈다.
- 자식 프로세스는 소켓 fd를 상속하지 않고, stdin은 `/dev/null`이며, job당 stdout/stderr는 각각 최대 1MiB까지만 저장한다.

## 빌드 / 실행

```
make
JOB_RUNNER_TOKEN=change-me-0123456789 ./server      # 127.0.0.1:8080
JOB_RUNNER_TOKEN=change-me-0123456789 ./client      # 다른 터미널에서
```

`client`는 stdin의 줄을 서버로 보내고 응답을 그대로 출력한다. `nc 127.0.0.1 8080`으로도 접속할 수 있다(`AUTH <token>`을 직접 보내야 함).

```
$ JOB_RUNNER_TOKEN=change-me-0123456789 ./client
RUN ls -la
OK 0
RESULT 0
DONE 0 412 0
(stdout 412바이트)
```

## 프로토콜

텍스트 기반, 한 줄(`\n` 종료)이 메시지 하나. 단 `RESULT` 응답의 stdout/stderr는 길이 prefix 뒤에 raw 바이트로 온다.

| 요청 | 응답 |
| --- | --- |
| `AUTH <token>` | `OK` / `ERR auth failed`(연결 종료) |
| `RUN <command>` | `OK <job_id>` |
| `LIST` | `JOB <id> <status> <cmd>` 여러 줄 + `END` |
| `RESULT <job_id>` | 아래 참고 |
| `KILL <job_id>` | `OK` / `ERR job already finished` |

`RESULT` 응답
- 실행 중: `RUNNING`
- 종료됨: `DONE <exit_code> <stdout_len> <stderr_len>\n` + stdout 바이트 + stderr 바이트
- exec 실패(명령 없음 등): `ERROR <exit_code> <stdout_len> <stderr_len>\n` + 같은 형식 (stderr에 사유)
- `KILL`로 끝남: `KILLED`
- 없는 id: `ERR no such job`

기타 에러는 `ERR <reason>`. job 슬롯은 32개이며, 가득 차면 가장 오래된 종료 job의 결과부터 회수된다.

## 구조

- `server.c` — 소켓, `select` 이벤트 루프, 줄 단위 입력 버퍼링, 인증
- `job.c` / `job.h` — job 테이블, `fork`/`execvp`, stdout/stderr 파이프 수집, `SIGCHLD` 처리, 명령 파싱

설계 포인트
- exec 실패는 `O_CLOEXEC` errno 파이프로 감지한다(exec 성공 시 자동으로 닫혀 EOF, 실패 시 errno가 전달됨).
- `SIGCHLD` 핸들러는 `waitpid`로 상태만 기록하고 `malloc`/`printf`를 쓰지 않는다. 출력 수집은 `select` 루프에서 처리한다.
- fork 직후 슬롯 등록이 끝나기 전에 `SIGCHLD`가 끼어드는 레이스는 `sigprocmask`로 막는다.
- job은 종료 + 출력 파이프 EOF까지 끝나야 최종 상태(DONE/KILLED/ERROR)로 보인다.

## 테스트

```
make
python3 tests/run_tests.py
```

서버를 직접 띄워 인증, 프로토콜 응답, exec 실패, KILL, 동시 접속 중 논블로킹, 큰 출력, 슬롯 회수, fd 상속, 좀비, 바인드 주소를 검증한다.
