"""Reusable test double for the serial board: a pty pair (not a test file)."""
import os
import pty
import select
import threading
import time


class FakeBoard:
    """Context manager. `path` is the slave tty to pass as --port.

    Every byte the helper writes is recorded; `received()` returns them all.
    `send(text)` writes text towards the helper.
    """

    def __init__(self):
        self.master = None
        self.slave = None
        self.path = None
        self._data = b""

    def __enter__(self):
        self.master, self.slave = pty.openpty()
        self.path = os.ttyname(self.slave)
        return self

    def __exit__(self, *exc):
        for fd in (self.master, self.slave):
            try:
                os.close(fd)
            except (OSError, TypeError):
                pass
        self.master = self.slave = None
        return False

    def _drain(self, wait=0.0):
        end = time.monotonic() + wait
        while True:
            left = max(0.0, end - time.monotonic())
            ready, _, _ = select.select([self.master], [], [], left)
            if not ready:
                break
            try:
                chunk = os.read(self.master, 4096)
            except OSError:
                break
            if not chunk:
                break
            self._data += chunk
            left = 0.0
            end = time.monotonic()
        return self._data

    def received(self, wait=0.0):
        """All bytes written by the helper so far."""
        return self._drain(wait)

    def send(self, text):
        os.write(self.master, text.encode() if isinstance(text, str) else text)


CLOSE_PTY = object()   # run_lines item: close the pty master (serial loss)

TEMP_REPLY = "TEMP load=25.0 C count=700.0 n=400 ambient=25.0 C count=700.0 n=400"


class ScriptedBoard(FakeBoard):
    """FakeBoard that drains the pty in a background thread and plays the
    board side of the PRBS load protocol (plan pinned observables).

    temp_skip      number of `temp` lines left unanswered first
    stale_temp     send two extra TEMP lines right before the READY reply
    ready          "ok" | "silent" | any text sent verbatim (a refusal)
    ready_delay    seconds between `prbs load N` and the READY reply
    levels         None (computed from the received values) or (low, high, changes)
    loaded_n       None (= N) or the count to report
    confirm        "ok" | "silent" | any text sent verbatim instead of both lines
    run            "ok" | "silent" | any text sent verbatim
    run_end        send `# RUN END ok n=<N>` after `# RUN START`
    run_lines      None, or a list played after `prbs run` instead of `run`/`run_end`:
                   a str is sent as one line, a number is a pause in seconds,
                   a threading.Event is waited for (at most 10 s),
                   CLOSE_PTY closes the pty master (the helper sees a serial error)
    """

    def __init__(self, temp_skip=0, stale_temp=False, ready="ok", ready_delay=0.0,
                 levels=None, loaded_n=None, confirm="ok", run="ok", run_end=True,
                 run_lines=None):
        super().__init__()
        self.run_lines = run_lines
        self.temp_skip = temp_skip
        self.stale_temp = stale_temp
        self.ready = ready
        self.ready_delay = ready_delay
        self.levels = levels
        self.loaded_n = loaded_n
        self.confirm = confirm
        self.run = run
        self.run_end = run_end
        self.early_values = 0      # value lines that arrived before READY was sent
        self.ready_sent = False
        self.values = []
        self._temps = 0
        self._state = "cmd"        # cmd | awaiting_ready | values
        self._expect = 0
        self._buf = b""
        self._stop = threading.Event()
        self._lock = threading.Lock()
        self._timers = []
        self._thread = None

    def __enter__(self):
        super().__enter__()
        self._thread = threading.Thread(target=self._loop, daemon=True)
        self._thread.start()
        return self

    def __exit__(self, *exc):
        self._stop.set()
        for t in self._timers:
            t.cancel()
        if self._thread is not None:
            self._thread.join(2.0)
        return super().__exit__(*exc)

    def received(self, wait=0.0):
        time.sleep(wait)
        with self._lock:
            return self._data

    def _write(self, text):
        try:
            self.send(text)
        except (OSError, TypeError):
            pass

    def _loop(self):
        while not self._stop.is_set():
            try:
                ready, _, _ = select.select([self.master], [], [], 0.02)
                if not ready:
                    continue
                chunk = os.read(self.master, 65536)
            except (OSError, ValueError, TypeError):
                return
            if not chunk:
                return
            with self._lock:
                self._data += chunk
            self._buf += chunk
            while b"\n" in self._buf:
                line, self._buf = self._buf.split(b"\n", 1)
                self._on_line(line.decode("ascii", "replace").strip("\r"))

    def _on_line(self, line):
        if self._state == "awaiting_ready":
            self.early_values += 1
            return
        if self._state == "values":
            self.values.append(line)
            if len(self.values) == self._expect:
                self._state = "cmd"
                self._confirm()
            return
        if line == "temp":
            self._temps += 1
            if self._temps > self.temp_skip:
                self._write(TEMP_REPLY + "\n")
        elif line.startswith("prbs load "):
            self._on_load(int(line.split()[2]))
        elif line == "prbs run":
            self._on_run()

    def _on_load(self, n):
        self._expect = n
        self.values = []
        if self.ready == "silent":
            return
        if self.ready != "ok":
            self._write(self.ready + "\n")
            return
        self._state = "awaiting_ready"

        def send_ready():
            if self.stale_temp:
                self._write(TEMP_REPLY + "\n" + TEMP_REPLY + "\n")
            self.ready_sent = True
            self._state = "values"
            self._write("PRBS LOAD READY n=%d\n" % n)

        t = threading.Timer(self.ready_delay, send_ready)
        self._timers.append(t)
        t.start()

    def _confirm(self):
        if self.confirm == "silent":
            return
        if self.confirm != "ok":
            self._write(self.confirm + "\n")
            return
        nums = [int(v) for v in self.values]
        if self.levels is None:
            low, high = min(nums), max(nums)
            changes = sum(1 for a, b in zip(nums, nums[1:]) if a != b)
        else:
            low, high, changes = self.levels
        n = len(nums) if self.loaded_n is None else self.loaded_n
        self._write("PRBS LEVELS low=%d high=%d changes=%d\nPRBS LOADED n=%d\n"
                    % (low, high, changes, n))

    def _play(self, items):
        for item in items:
            if self._stop.is_set():
                return
            if item is CLOSE_PTY:
                fd, self.master = self.master, None
                try:
                    os.close(fd)
                except (OSError, TypeError):
                    pass
            elif isinstance(item, threading.Event):
                item.wait(10.0)
            elif isinstance(item, (int, float)):
                time.sleep(item)
            else:
                self._write(item + "\n")

    def _on_run(self):
        if self.run_lines is not None:
            t = threading.Thread(target=self._play, args=(list(self.run_lines),),
                                 daemon=True)
            t.start()
            return
        if self.run == "silent":
            return
        if self.run != "ok":
            self._write(self.run + "\n")
            return
        n = len(self.values)
        out = "# RUN START n=%d cap=2047 limit=75.0\n" % n
        if self.run_end:
            out += "# RUN END ok n=%d\n" % n
        self._write(out)
