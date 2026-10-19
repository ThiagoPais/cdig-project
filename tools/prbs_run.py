#!/usr/bin/env python3
"""Helper for the open-loop PRBS experiment (specs/002-prbs-experiment).

Sends a two-level duty sequence to the board, starts the run and appends the board's log lines to a file.
Standard library only, Linux only. See plan.md D10 and D11.

    python3 tools/prbs_run.py --port /dev/ttyACM0 --seq docs/sinal_prbs.csv --log run.log

Exit status: 0 run ended ok, 1 run aborted by the board, 2 refusal or setup failure before the run,
3 the log is incomplete (serial loss, board reset, interruption).
"""
from __future__ import annotations

import argparse
import os
import re
import select
import signal
import sys
import termios
import threading
import time
from dataclasses import dataclass, field
from typing import List, Optional

EXIT_OK = 0
EXIT_ABORTED = 1
EXIT_SETUP = 2
EXIT_INCOMPLETE = 3

MAX_VALUES = 32000
CSV_HEADER = "Tempo_s,PWM_counts"
HANDSHAKE_TRIES = 5


@dataclass
class Timeouts:
    """Timeouts in seconds; tests shrink them, the command line does not expose them."""
    sync: float = 1.0       # between handshake tries
    ready: float = 5.0      # wait for `PRBS LOAD READY`
    loaded: float = 10.0    # wait for the confirmation after the last value
    run_start: float = 5.0  # wait for `# RUN START` after `prbs run`
    silence: float = 10.0   # no line during a run


class SequenceError(Exception):
    """The sequence file is unusable."""


@dataclass
class Sequence:
    values: List[int] = field(default_factory=list)

    @property
    def low(self) -> int:
        return min(self.values)

    @property
    def high(self) -> int:
        return max(self.values)

    @property
    def changes(self) -> int:
        return sum(1 for a, b in zip(self.values, self.values[1:]) if a != b)


_INT = re.compile(r"[0-9]+")


def read_sequence(path: str) -> Sequence:
    """Read the duty values from either file form of spec A7 (plain list, or the Tempo_s,PWM_counts CSV)."""
    with open(path, "rb") as f:
        raw = f.read()
    try:
        lines = raw.decode("ascii").splitlines()
    except UnicodeDecodeError as exc:
        raise SequenceError("the sequence file is not plain ASCII text") from exc
    while lines and not lines[-1].strip():
        lines.pop()
    if not lines:
        raise SequenceError("the sequence file holds no value")

    values: List[int] = []
    if "," in lines[0]:
        if lines[0].strip() != CSV_HEADER:
            raise SequenceError("unexpected CSV header %r (expected %r)" % (lines[0].strip(), CSV_HEADER))
        for index, line in enumerate(lines[1:]):
            parts = [p.strip() for p in line.split(",")]
            if len(parts) != 2 or not _INT.fullmatch(parts[0]) or not _INT.fullmatch(parts[1]):
                raise SequenceError("row %d is not 'k,duty' with integers: %r" % (index + 1, line))
            if int(parts[0]) != index:
                raise SequenceError("row %d has k=%s, expected %d" % (index + 1, parts[0], index))
            values.append(int(parts[1]))
    else:
        for index, line in enumerate(lines):
            text = line.strip()
            if not _INT.fullmatch(text):
                raise SequenceError("line %d is not an integer: %r" % (index + 1, line))
            values.append(int(text))

    if not values:
        raise SequenceError("the sequence file holds no value")
    if len(values) > MAX_VALUES:
        raise SequenceError("%d values (at most %d are accepted)" % (len(values), MAX_VALUES))
    return Sequence(values)


def open_port(path: str) -> int:
    """Open the serial device `path` raw, 115200 8N1, no flow control, HUPCL cleared; return the descriptor.

    HUPCL is cleared so that closing the port (Ctrl+C, closed terminal) does not lower DTR/RTS, which
    resets the board through its auto-reset circuit. DTR/RTS are never toggled here.
    """
    fd = os.open(path, os.O_RDWR | os.O_NOCTTY)
    try:
        iflag, oflag, cflag, lflag, _ispeed, _ospeed, cc = termios.tcgetattr(fd)
        iflag &= ~(termios.IGNBRK | termios.BRKINT | termios.PARMRK | termios.ISTRIP | termios.INLCR |
                   termios.IGNCR | termios.ICRNL | termios.IXON | termios.IXOFF | termios.IXANY)
        oflag &= ~termios.OPOST
        lflag &= ~(termios.ECHO | termios.ECHONL | termios.ICANON | termios.ISIG | termios.IEXTEN)
        crtscts = getattr(termios, "CRTSCTS", 0x80000000)
        cflag &= ~(termios.CSIZE | termios.PARENB | termios.CSTOPB | termios.HUPCL | crtscts)
        cflag |= termios.CS8 | termios.CREAD | termios.CLOCAL
        cc[termios.VMIN] = 1
        cc[termios.VTIME] = 0
        termios.tcsetattr(fd, termios.TCSANOW,
                          [iflag, oflag, cflag, lflag, termios.B115200, termios.B115200, cc])
    except Exception:
        os.close(fd)
        raise
    return fd


class LineReader:
    """Reads text lines from a file descriptor with a timeout; '\\r' is stripped, bytes are decoded as ASCII."""

    def __init__(self, fd: int) -> None:
        self._fd = fd
        self._buffer = b""

    def readline(self, timeout: float) -> Optional[str]:
        """Return the next line, or None when no complete line arrived within `timeout` seconds."""
        deadline = time.monotonic() + timeout
        while True:
            newline = self._buffer.find(b"\n")
            if newline >= 0:
                line, self._buffer = self._buffer[:newline], self._buffer[newline + 1:]
                return line.decode("ascii", errors="replace").rstrip("\r")
            # Poll once even when the deadline has passed: bytes that queued up while this process was
            # suspended must be read, not mistaken for a silent board (spec AC11).
            remaining = max(deadline - time.monotonic(), 0.0)
            ready, _, _ = select.select([self._fd], [], [], remaining)
            if ready:
                chunk = os.read(self._fd, 4096)
                if not chunk:
                    raise OSError("serial port closed")
                self._buffer += chunk
            elif remaining <= 0:
                return None


def _handshake(fd: int, reader: LineReader, timeouts: Timeouts) -> bool:
    """Send `temp` until the board answers with a `TEMP` or `# TEMP` line (plan D10 step 3)."""
    for _ in range(HANDSHAKE_TRIES):
        os.write(fd, b"temp\n")
        deadline = time.monotonic() + timeouts.sync
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                break
            line = reader.readline(remaining)
            if line is None:
                break
            if line.startswith("TEMP") or line.startswith("# TEMP"):
                return True
    return False


_LEVELS = re.compile(r"PRBS LEVELS low=(\d+) high=(\d+) changes=(\d+)")
_LOADED = re.compile(r"PRBS LOADED n=(\d+)")
_CHUNK = 256


def _refusal(line: str) -> bool:
    """A `REFUSED:` reply, also when the board printed it as a `# ` comment (a run was still playing)."""
    return (line[2:] if line.startswith("# ") else line).startswith("REFUSED:")


def _write_all(fd: int, data: bytes) -> None:
    """Write `data` in chunks of at most 256 bytes (the board has no flow control)."""
    for start in range(0, len(data), _CHUNK):
        chunk = data[start:start + _CHUNK]
        while chunk:
            written = os.write(fd, chunk)
            chunk = chunk[written:]


def _await(reader: LineReader, timeout: float, accept) -> tuple:
    """Show the board's lines until `accept(line)`. Returns ('ok', line), ('refused', line) or ('timeout', None)."""
    deadline = time.monotonic() + timeout
    while True:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            return "timeout", None
        line = reader.readline(remaining)
        if line is None:
            return "timeout", None
        print(line)
        if _refusal(line):
            print("the board refused: %s" % line, file=sys.stderr)
            return "refused", line
        if accept(line):
            return "ok", line


def _load_and_start(fd: int, reader: LineReader, sequence: Sequence, timeouts: Timeouts) -> tuple:
    """Plan D10 steps 4 and 5: load the sequence, check what the board stored, start the run.

    Returns (None, <the `# RUN START` line>) once the run has started, otherwise (<exit status>, None)."""
    count = len(sequence.values)

    os.write(fd, ("prbs load %d\n" % count).encode("ascii"))
    kind, _ = _await(reader, timeouts.ready, lambda l: l == "PRBS LOAD READY n=%d" % count)
    if kind != "ok":
        if kind == "timeout":
            print("no 'PRBS LOAD READY n=%d' from the board within %.1f s" % (count, timeouts.ready), file=sys.stderr)
        return EXIT_SETUP, None

    _write_all(fd, "".join("%d\n" % v for v in sequence.values).encode("ascii"))

    levels = None
    loaded = None

    def both_seen(line: str) -> bool:
        nonlocal levels, loaded
        m = _LEVELS.fullmatch(line)
        if m:
            levels = tuple(int(g) for g in m.groups())
        m = _LOADED.fullmatch(line)
        if m:
            loaded = int(m.group(1))
        return levels is not None and loaded is not None

    kind, _ = _await(reader, timeouts.loaded, both_seen)
    if kind != "ok":
        if kind == "timeout":
            print("no confirmation from the board within %.1f s of the last value" % timeouts.loaded, file=sys.stderr)
        return EXIT_SETUP, None
    expected = (sequence.low, sequence.high, sequence.changes)
    if loaded != count or levels != expected:
        print("the board stored something else: n=%s levels=%s, the file has n=%d levels=%s"
              % (loaded, levels, count, expected), file=sys.stderr)
        return EXIT_SETUP, None

    os.write(fd, b"prbs run\n")
    kind, line = _await(reader, timeouts.run_start, lambda l: l.startswith("# RUN START"))
    if kind != "ok":
        if kind == "timeout":
            print("no '# RUN START' within %.1f s of the run command" % timeouts.run_start, file=sys.stderr)
        os.write(fd, b"stop\n")  # leave the heater idle
        return EXIT_SETUP, None
    return None, line


_DATA_LINE = re.compile(r"\d+,\d+,\d+,(?:-?\d+(?:\.\d+)?|nan),(?:-?\d+(?:\.\d+)?|nan)")


def _log_line(log, text: str) -> None:
    """Append one line and make it durable before the next read (the log must survive a crash of the helper)."""
    log.write(text + "\n")
    log.flush()
    os.fsync(log.fileno())


class _Interrupted(Exception):
    """A termination signal arrived while the run was being logged."""

    def __init__(self, name: str) -> None:
        super().__init__(name)
        self.name = name


def _raise_interrupted(signum, _frame) -> None:
    raise _Interrupted(signal.Signals(signum).name)


def _follow_run(reader: LineReader, log, timeouts: Timeouts, start_line: str) -> int:
    """Plan D10 steps 6-7: append every line from `# RUN START` on to the log until the run-end line.

    Any other ending (serial error, silence, a board reset, a signal) leaves the lines already written and
    appends exactly one `# LOG INCOMPLETE <reason>` line; the status is then 3."""
    corrupt = 0
    previous_handlers = []
    if threading.current_thread() is threading.main_thread():
        for sig in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP):
            previous_handlers.append((sig, signal.signal(sig, _raise_interrupted)))

    def incomplete(reason: str) -> int:
        _log_line(log, "# LOG INCOMPLETE " + reason)
        return EXIT_INCOMPLETE

    try:
        try:
            _log_line(log, start_line)
            while True:
                line = reader.readline(timeouts.silence)
                if line is None:
                    return incomplete("no line received for %g s" % timeouts.silence)
                if line == "READY":
                    return incomplete("board reset (READY seen)")
                print(line)
                if line.startswith("#") or _DATA_LINE.fullmatch(line):
                    _log_line(log, line)
                else:
                    # Bytes dropped or joined while the reader was paused: keep the log readable by MATLAB.
                    _log_line(log, "# CORRUPT " + line)
                    corrupt += 1
                if line.startswith("# RUN END ok"):
                    return EXIT_OK
                if line.startswith("# RUN END aborted"):
                    return EXIT_ABORTED
        except _Interrupted as exc:
            return incomplete("interrupted by %s" % exc.name)
        except OSError as exc:
            return incomplete("serial error: %s" % exc)
    finally:
        for sig, handler in previous_handlers:
            signal.signal(sig, handler)
        if corrupt:
            print("%d corrupt line(s) were written to the log as '# CORRUPT ...'" % corrupt)


def _parse_args(argv: List[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(prog="prbs_run.py", description="Play a PRBS sequence on the board and log the run.")
    parser.add_argument("--port", required=True, help="serial device, for example /dev/ttyACM0 (115200 baud)")
    parser.add_argument("--seq", required=True, help="sequence file: one integer per line, or the Tempo_s,PWM_counts CSV")
    parser.add_argument("--log", required=True, help="log file (appended to)")
    return parser.parse_args(argv)


def main(argv: List[str], timeouts: Optional[Timeouts] = None) -> int:
    """Run the experiment; returns the exit status."""
    timeouts = timeouts if timeouts is not None else Timeouts()
    try:
        args = _parse_args(argv)
    except SystemExit as exc:
        return EXIT_SETUP if exc.code else EXIT_OK

    # 1. the log must be writable before anything is sent
    try:
        log = open(args.log, "a", encoding="ascii", errors="replace", newline="\n")
    except OSError as exc:
        print("cannot write the log file: %s" % exc, file=sys.stderr)
        return EXIT_SETUP
    try:
        # 2. the sequence file
        try:
            sequence = read_sequence(args.seq)
        except (SequenceError, OSError) as exc:
            print("bad sequence file: %s" % exc, file=sys.stderr)
            return EXIT_SETUP

        # 3. the port and the handshake
        try:
            fd = open_port(args.port)
        except OSError as exc:
            print("cannot open the serial port: %s" % exc, file=sys.stderr)
            return EXIT_SETUP
        try:
            reader = LineReader(fd)
            try:
                synced = _handshake(fd, reader, timeouts)
            except OSError as exc:
                print("serial error: %s" % exc, file=sys.stderr)
                return EXIT_SETUP
            if not synced:
                print("the board does not answer (no TEMP line)", file=sys.stderr)
                return EXIT_SETUP
            try:
                failure, start_line = _load_and_start(fd, reader, sequence, timeouts)
            except OSError as exc:
                print("serial error: %s" % exc, file=sys.stderr)
                return EXIT_SETUP
            if failure is not None:
                return failure
            return _follow_run(reader, log, timeouts, start_line)
        finally:
            os.close(fd)
    finally:
        log.close()


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
