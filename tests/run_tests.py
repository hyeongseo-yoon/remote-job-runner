#!/usr/bin/env python3
"""서버를 직접 띄워서 프로토콜/동시성/인증/좀비를 검증한다. 사용: python3 tests/run_tests.py"""
import os, socket, subprocess, sys, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOKEN = "test-token-0123456789abcdef"
PORT = 8080
fails = 0


def check(name, cond, detail=""):
    global fails
    print(("PASS " if cond else "FAIL ") + name + ("" if cond else f"  -> {detail}"))
    if not cond:
        fails += 1


class Client:
    def __init__(self, auth=True):
        self.s = socket.create_connection(("127.0.0.1", PORT), timeout=3)
        self.buf = b""
        if auth:
            self.send("AUTH " + TOKEN)
            assert self.line() == "OK"

    def send(self, line):
        self.s.sendall(line.encode() + b"\n")

    def fill(self):
        d = self.s.recv(65536)
        if not d:
            raise EOFError
        self.buf += d

    def line(self):
        while b"\n" not in self.buf:
            self.fill()
        l, self.buf = self.buf.split(b"\n", 1)
        return l.decode()

    def exact(self, n):
        while len(self.buf) < n:
            self.fill()
        d, self.buf = self.buf[:n], self.buf[n:]
        return d

    def run(self, cmd):
        self.send("RUN " + cmd)
        r = self.line()
        assert r.startswith("OK "), r
        return int(r.split()[1])

    def result(self, jid, timeout=5):
        end = time.time() + timeout
        while time.time() < end:
            self.send(f"RESULT {jid}")
            h = self.line()
            if h == "RUNNING":
                time.sleep(0.05)
                continue
            p = h.split()
            if p[0] in ("DONE", "ERROR"):
                code, ol, el = int(p[1]), int(p[2]), int(p[3])
                return p[0], code, self.exact(ol), self.exact(el)
            return (h,)
        return ("TIMEOUT",)


def zombies(pid):
    out = subprocess.run(["ps", "--ppid", str(pid), "-o", "stat="], capture_output=True, text=True).stdout
    return [l for l in out.split() if l.startswith("Z")]


env = dict(os.environ, JOB_RUNNER_TOKEN=TOKEN)
srv = subprocess.Popen([os.path.join(ROOT, "server")], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
time.sleep(0.5)
try:
    # 인증
    c = Client(auth=False); c.send("LIST")
    check("미인증 명령은 거부 + 연결 종료", c.line() == "ERR auth failed")
    c = Client(auth=False); c.send("AUTH wrong-token-wrong-token")
    check("틀린 토큰 거부", c.line() == "ERR auth failed")
    try:
        c.s.settimeout(1); check("틀린 토큰 후 연결 닫힘", c.s.recv(10) == b"")
    except Exception as e:
        check("틀린 토큰 후 연결 닫힘", False, e)

    c = Client()
    # 기본 실행
    j = c.run("echo hello")
    check("RUN echo → DONE 0 stdout", c.result(j) == ("DONE", 0, b"hello\n", b""))
    j = c.run("sh -c exit\t3") if False else c.run("false")
    r = c.result(j); check("비정상 종료 exit code", r[:2] == ("DONE", 1), r)
    j = c.run("ls /nonexistent-path-xyz")
    r = c.result(j); check("실제 stderr 캡처", r[0] == "DONE" and r[1] != 0 and b"nonexistent" in r[3] and r[2] == b"", r)
    j = c.run("no-such-command-xyz")
    r = c.result(j); check("exec 실패 → ERROR", r[0] == "ERROR" and b"exec failed" in r[3], r)

    # 잘못된 입력
    for cmd in ["RESULT 999", "RESULT abc", "KILL 999", "KILL -1", "RESULT"]:
        c.send(cmd); check(f"'{cmd}' → ERR no such job", c.line() == "ERR no such job")
    c.send("FOO"); check("알 수 없는 명령", c.line() == "ERR unknown command")
    c.send("RUN"); check("RUN 인자 없음", c.line().startswith("ERR usage"))

    # KILL
    j = c.run("sleep 30")
    c.send(f"RESULT {j}"); check("실행 중 RESULT=RUNNING", c.line() == "RUNNING")
    c.send(f"KILL {j}"); check("KILL → OK", c.line() == "OK")
    check("KILL 후 KILLED", c.result(j) == ("KILLED",))
    c.send(f"KILL {j}"); check("끝난 job KILL → ERR", c.line() == "ERR job already finished")

    # 동시성: 한 클라이언트의 긴 job이 다른 클라이언트를 막지 않음
    slow = c.run("sleep 3")
    c2, c3 = Client(), Client()
    t = time.time()
    jb = c2.run("echo fast")
    check("긴 job 실행 중에도 다른 클라이언트 즉시 응답", c2.result(jb)[2] == b"fast\n" and time.time() - t < 1.5)
    c3.send("LIST")
    lines = []
    while True:
        l = c3.line()
        if l == "END": break
        lines.append(l)
    st = {l.split()[2] for l in lines}
    check("LIST에 서로 다른 상태 표시", {"RUNNING", "DONE", "KILLED", "ERROR"} <= st, st)
    check("LIST에 명령줄 전체 표시", any(l.endswith("sleep 3") for l in lines), lines)
    c.send(f"KILL {slow}"); c.line()

    # 큰 출력
    j = c.run("seq 1 150000")
    r = c.result(j, 10); check("큰 stdout(1MB 미만) 전체 캡처", r[0] == "DONE" and r[2].endswith(b"150000\n") and r[2].startswith(b"1\n"), r[:2])
    j = c.run("head -c 3000000 /dev/zero")
    r = c.result(j, 10); check("출력 상한(1MiB) 적용", r[0] == "DONE" and len(r[2]) == 1 << 20, (r[0], len(r[2]) if len(r) > 2 else None))

    # 슬롯 회수: 32개 넘게 실행
    ok = True
    for _ in range(40):
        j = c.run("true"); r = c.result(j)
        ok = ok and r[:2] == ("DONE", 0)
    check("job 40개 연속 실행(슬롯 회수)", ok)

    # 동시 다수
    ids = [c.run(f"echo n{i}") for i in range(8)]
    check("동시 8개 job 각각 정확한 출력", [c.result(i)[2] for i in ids] == [f"n{i}\n".encode() for i in range(8)])

    # 소켓 fd 누수: 자식이 서버 소켓을 물려받지 않는지
    j = c.run("sleep 2")
    time.sleep(0.2)
    out = subprocess.run(["ls", "-l", f"/proc/{srv.pid}/task"], capture_output=True, text=True)
    kids = subprocess.run(["pgrep", "-P", str(srv.pid), "sleep"], capture_output=True, text=True).stdout.split()
    leaked = subprocess.run(["ls", f"/proc/{kids[0]}/fd"], capture_output=True, text=True).stdout.split() if kids else ["?"]
    check("자식이 소켓 fd를 상속하지 않음(0,1,2만)", sorted(leaked) == ["0", "1", "2"], leaked)
    c.send(f"KILL {j}"); c.line(); c.result(j)

    time.sleep(0.3)
    check("좀비 없음", zombies(srv.pid) == [], zombies(srv.pid))
    check("서버 생존", srv.poll() is None)

    # 바인드 주소
    ss = subprocess.run(["ss", "-ltnH"], capture_output=True, text=True).stdout
    check("127.0.0.1에만 바인드", any("127.0.0.1:8080" in l for l in ss.splitlines()) and not any(l.split()[3] in ("0.0.0.0:8080", "*:8080") for l in ss.splitlines()), ss)
finally:
    srv.terminate(); srv.wait()
print(f"\n{'ALL PASS' if fails == 0 else str(fails) + ' FAILED'}")
sys.exit(1 if fails else 0)
