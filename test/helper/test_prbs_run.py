"""Tests for tools/prbs_run.py (spec 002, plan D10/D11). Standard library only."""
import ast
import contextlib
import io
import os
import pty
import sys
import tempfile
import random
import re
import signal
import subprocess
import termios
import threading
import time
import unittest
from unittest import mock

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
TOOLS = os.path.join(ROOT, "tools")
sys.path.insert(0, TOOLS)

import prbs_run  # noqa: E402
from fake_board import CLOSE_PTY, FakeBoard, ScriptedBoard  # noqa: E402


class OpenPortTest(unittest.TestCase):
    def setUp(self):
        self.master, self.slave = pty.openpty()
        self.addCleanup(os.close, self.master)
        self.addCleanup(os.close, self.slave)
        self.path = os.ttyname(self.slave)
        # Start from a cooked, 9600 baud, HUPCL-set, RTS/CTS-on state so the
        # assertions prove that open_port changed it.
        attrs = termios.tcgetattr(self.slave)
        attrs[2] |= termios.HUPCL | getattr(termios, "CRTSCTS", 0)
        attrs[3] |= termios.ICANON | termios.ECHO
        attrs[4] = attrs[5] = termios.B9600
        termios.tcsetattr(self.slave, termios.TCSANOW, attrs)
        self.fd = prbs_run.open_port(self.path)
        self.addCleanup(self._close_fd)

    def _close_fd(self):
        try:
            os.close(self.fd)
        except OSError:
            pass

    def attrs(self):
        return termios.tcgetattr(self.slave)

    def test_AC15_returns_open_file_descriptor(self):
        self.assertIsInstance(self.fd, int)
        os.fstat(self.fd)
        self.assertTrue(os.isatty(self.fd))

    def test_AC15_descriptor_is_read_write(self):
        os.write(self.fd, b"x")
        self.assertEqual(os.read(self.master, 1), b"x")
        os.write(self.master, b"y")
        self.assertEqual(os.read(self.fd, 1), b"y")

    def test_AC15_hupcl_cleared(self):
        self.assertEqual(self.attrs()[2] & termios.HUPCL, 0)

    def test_AC15_baud_115200(self):
        a = self.attrs()
        self.assertEqual(a[4], termios.B115200)
        self.assertEqual(a[5], termios.B115200)

    def test_AC15_hardware_flow_control_off(self):
        self.assertEqual(self.attrs()[2] & getattr(termios, "CRTSCTS", 0), 0)

    def test_AC15_raw_mode_8N1(self):
        a = self.attrs()
        iflag, oflag, cflag, lflag = a[0], a[1], a[2], a[3]
        self.assertEqual(lflag & (termios.ICANON | termios.ECHO | termios.ISIG | termios.IEXTEN), 0)
        self.assertEqual(oflag & termios.OPOST, 0)
        self.assertEqual(iflag & (termios.ICRNL | termios.INLCR | termios.IGNCR
                                  | termios.IXON | termios.IXOFF | termios.ISTRIP), 0)
        self.assertEqual(cflag & termios.CSIZE, termios.CS8)
        self.assertEqual(cflag & termios.PARENB, 0)
        self.assertEqual(cflag & termios.CSTOPB, 0)
        self.assertNotEqual(cflag & termios.CREAD, 0)

    def test_AC15_bytes_pass_unmodified(self):
        data = b"\r\n\x03\x11\x13abc\n"
        os.write(self.master, data)
        got = b""
        while len(got) < len(data):
            got += os.read(self.fd, 64)
        self.assertEqual(got, data)


class ModuleStructureTest(unittest.TestCase):
    def test_AC15_open_port_is_only_function_using_termios(self):
        with open(os.path.join(TOOLS, "prbs_run.py"), encoding="utf-8") as f:
            tree = ast.parse(f.read())
        users = []
        for node in ast.walk(tree):
            if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)):
                if any(isinstance(n, ast.Name) and n.id == "termios"
                       for n in ast.walk(node)):
                    users.append(node.name)
        # nested functions are walked inside their parent too; only the
        # top-level function open_port may mention termios.
        top = [n for n in tree.body if isinstance(n, (ast.FunctionDef, ast.ClassDef))]
        offenders = []
        for n in top:
            if n.name == "open_port":
                continue
            if any(isinstance(m, ast.Name) and m.id == "termios" for m in ast.walk(n)):
                offenders.append(n.name)
        self.assertEqual(offenders, [])
        self.assertIn("open_port", users)

    def test_AC15_no_test_prefixed_directory_under_test_helper(self):
        for _, dirs, _ in os.walk(HERE):
            bad = [d for d in dirs if d.startswith("test_")]
            self.assertEqual(bad, [])

HEADER = "Tempo_s,PWM_counts"
FAST = dict(sync=0.01, ready=0.2, loaded=0.2, run_start=0.2, silence=0.2)


class MainSequenceAndLogTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.dir = self.tmp.name
        self.log = os.path.join(self.dir, "run.log")

    def write(self, name, text):
        path = os.path.join(self.dir, name)
        with open(path, "wb") as f:
            f.write(text.encode() if isinstance(text, str) else text)
        return path

    def run_main(self, seq, log=None):
        """Returns (status, bytes the board received)."""
        with FakeBoard() as board:
            argv = ["--port", board.path, "--seq", seq,
                    "--log", self.log if log is None else log]
            status = prbs_run.main(argv, prbs_run.Timeouts(**FAST))
            return status, board.received(0.05)

    def assert_accepted(self, seq):
        status, got = self.run_main(seq)
        self.assertEqual(got, b"temp\n" * 5)  # handshake started, never answered
        self.assertEqual(status, 2)

    def assert_rejected(self, seq, log=None):
        status, got = self.run_main(seq, log)
        self.assertEqual(status, 2)
        self.assertEqual(got, b"")

    # ---- valid forms (A7) ----
    def test_AC12_plain_list_accepted(self):
        self.assert_accepted(self.write("s.txt", "1266\n1490\n1266\n"))

    def test_AC12_csv_lf_accepted(self):
        rows = "".join("%d,%d\n" % (k, 1266 if k < 3 else 1490) for k in range(6))
        self.assert_accepted(self.write("s.csv", HEADER + "\n" + rows))

    def test_AC12_csv_crlf_accepted(self):
        rows = "".join("%d,%d\r\n" % (k, 1266 if k < 3 else 1490) for k in range(6))
        self.assert_accepted(self.write("s.csv", HEADER + "\r\n" + rows))

    def test_AC12_trailing_blank_lines_ignored(self):
        self.assert_accepted(self.write("a.txt", "1266\n1490\n\n\n"))
        rows = "".join("%d,%d\r\n" % (k, 1266) for k in range(4))
        self.assert_accepted(self.write("b.csv", HEADER + "\r\n" + rows + "\r\n\r\n"))

    def test_AC12_csv_shaped_like_docs_sinal_prbs(self):
        rows = []
        level, run = 1266, 0
        for k in range(31500):
            rows.append("%d,%d" % (k, level))
            run += 1
            if run == 200:
                level = 1490 if level == 1266 else 1266
                run = 0
        text = HEADER + "\r\n" + "\r\n".join(rows) + "\r\n"
        self.assert_accepted(self.write("sinal.csv", text))

    def test_AC12_maximum_32000_values_accepted(self):
        self.assert_accepted(self.write("max.txt", "1266\n" * 32000))

    # ---- invalid files (E12): status 2, no byte sent ----
    def test_E12_empty_file(self):
        self.assert_rejected(self.write("e.txt", ""))

    def test_E12_csv_header_only_zero_values(self):
        self.assert_rejected(self.write("h.csv", HEADER + "\n"))

    def test_E12_more_than_32000_values(self):
        self.assert_rejected(self.write("big.txt", "1266\n" * 32001))

    def test_E12_non_integer_text(self):
        self.assert_rejected(self.write("t.txt", "1266\nabc\n1490\n"))

    def test_E12_non_integer_decimal(self):
        self.assert_rejected(self.write("d.txt", "1266\n12.5\n"))

    def test_E12_csv_non_integer_duty(self):
        self.assert_rejected(self.write("d.csv", HEADER + "\n0,1266\n1,12.5\n"))

    def test_E12_csv_wrong_header(self):
        self.assert_rejected(self.write("w.csv", "Time,PWM\n0,1266\n1,1490\n"))

    def test_E12_csv_gap_in_k(self):
        self.assert_rejected(self.write("g.csv", HEADER + "\n0,1266\n1,1266\n3,1490\n"))

    def test_E12_csv_k_out_of_order(self):
        self.assert_rejected(self.write("o.csv", HEADER + "\n0,1266\n2,1266\n1,1490\n"))

    def test_E12_missing_file(self):
        self.assert_rejected(os.path.join(self.dir, "does_not_exist.txt"))

    # ---- log (E9) ----
    def test_E9_log_in_missing_directory(self):
        seq = self.write("s.txt", "1266\n1490\n")
        self.assert_rejected(seq, os.path.join(self.dir, "nodir", "run.log"))

    def test_E9_log_path_is_a_directory(self):
        seq = self.write("s.txt", "1266\n1490\n")
        self.assert_rejected(seq, self.dir)

    def test_E9_log_file_created(self):
        seq = self.write("s.txt", "1266\n1490\n")
        self.assertFalse(os.path.exists(self.log))
        self.run_main(seq)
        self.assertTrue(os.path.isfile(self.log))

    def test_E9_existing_log_content_kept(self):
        with open(self.log, "w") as f:
            f.write("previous line\n")
        seq = self.write("s.txt", "1266\n1490\n")
        self.run_main(seq)
        with open(self.log) as f:
            self.assertTrue(f.read().startswith("previous line\n"))

SMALL = [1266, 1266, 1266, 1490, 1490, 1490, 1490, 1266, 1266]   # N=9, changes=2
SMALL_REPLY = "PRBS LEVELS low=1266 high=1490 changes=2"


def prbs_values_like_docs():
    """31 500 values, two levels, 158 changes, runs of 100 to 900."""
    rnd = random.Random(7)
    runs = [100] * 159
    left = 31500 - sum(runs)
    while left > 0:
        i = rnd.randrange(len(runs))
        add = min(rnd.randint(1, 50), left, 900 - runs[i])
        runs[i] += add
        left -= add
    out, level = [], 1266
    for r in runs:
        out += [level] * r
        level = 1490 if level == 1266 else 1266
    return out


class HandshakeLoadRunTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.log = os.path.join(self.tmp.name, "run.log")

    def seq_file(self, values, csv=False):
        path = os.path.join(self.tmp.name, "seq.csv" if csv else "seq.txt")
        if csv:
            text = HEADER + "\r\n" + "".join("%d,%d\r\n" % kv for kv in enumerate(values))
        else:
            text = "".join("%d\n" % v for v in values)
        with open(path, "w", newline="") as f:
            f.write(text)
        return path

    def go(self, values=SMALL, csv=False, **board_kw):
        """Returns (status, received bytes, stdout, stderr, board)."""
        out, err = io.StringIO(), io.StringIO()
        with ScriptedBoard(**board_kw) as board:
            argv = ["--port", board.path, "--seq", self.seq_file(values, csv),
                    "--log", self.log]
            with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
                status = prbs_run.main(argv, prbs_run.Timeouts(
                    sync=0.02, ready=0.2, loaded=0.2, run_start=0.2, silence=0.2))
            got = board.received(0.1)
            return status, got, out.getvalue(), err.getvalue(), board

    def assert_expected_stream(self, got, values, with_run=True):
        m = re.match(rb"(temp\n)+", got)
        self.assertIsNotNone(m, got[:40])
        rest = got[m.end():]
        expect = b"prbs load %d\n" % len(values)
        expect += b"".join(b"%d\n" % v for v in values)
        if with_run:
            expect += b"prbs run\n"
        self.assertEqual(rest, expect)
        self.assertNotIn(b"stop", got)

    def assert_refused_before_run(self, status, got):
        self.assertEqual(status, 2)
        self.assertNotIn(b"prbs run", got)

    # ---- happy path to the run start ----
    def test_AC12_plain_list_load_then_run(self):
        status, got, out, err, board = self.go(SMALL, ready_delay=0.1)
        self.assert_expected_stream(got, SMALL)
        self.assertEqual(board.early_values, 0)

    def test_AC12_csv_load_then_run(self):
        status, got, out, err, board = self.go(SMALL, csv=True, ready_delay=0.1)
        self.assert_expected_stream(got, SMALL)
        self.assertEqual(board.early_values, 0)

    def test_AC12_values_only_after_ready_reply(self):
        status, got, out, err, board = self.go(SMALL, ready_delay=0.15)
        self.assertEqual(board.early_values, 0)
        self.assertTrue(board.ready_sent)
        self.assertEqual(len(board.values), len(SMALL))

    def test_AC13_levels_and_loaded_lines_shown_on_console(self):
        status, got, out, err, board = self.go(SMALL)
        self.assertIn(SMALL_REPLY, out)
        self.assertIn("PRBS LOADED n=9", out)

    def test_AC12_docs_sized_sequence_31500_values(self):
        values = prbs_values_like_docs()
        changes = sum(1 for a, b in zip(values, values[1:]) if a != b)
        self.assertEqual(len(values), 31500)
        self.assertEqual(changes, 158)
        status, got, out, err, board = self.go(values, csv=True)
        self.assert_expected_stream(got, values)
        self.assertIn("PRBS LEVELS low=1266 high=1490 changes=158", out)
        self.assertIn("PRBS LOADED n=31500", out)

    # ---- handshake ----
    def test_AC12_first_temp_unanswered_second_answered(self):
        status, got, out, err, board = self.go(SMALL, temp_skip=1)
        self.assertTrue(got.startswith(b"temp\ntemp\nprbs load 9\n"), got[:40])
        self.assert_expected_stream(got, SMALL)

    def test_AC12_stale_temp_lines_do_not_disturb(self):
        status, got, out, err, board = self.go(SMALL, stale_temp=True, ready_delay=0.05)
        self.assert_expected_stream(got, SMALL)
        self.assertIn(SMALL_REPLY, out)

    # ---- refusals and mismatches: status 2, no prbs run ----
    def reason_shown(self, out, err, text):
        self.assertIn(text, out + err)

    def test_AC13_refused_instead_of_ready(self):
        text = "REFUSED: a duty sweep is in progress"
        status, got, out, err, board = self.go(ready=text)
        self.assert_refused_before_run(status, got)
        self.reason_shown(out, err, text)

    def test_AC13_refused_instead_of_ready_with_hash_prefix(self):
        text = "REFUSED: a PRBS run is in progress"
        status, got, out, err, board = self.go(ready="# " + text)
        self.assert_refused_before_run(status, got)
        self.reason_shown(out, err, text)

    def test_AC13_count_mismatch(self):
        status, got, out, err, board = self.go(loaded_n=len(SMALL) - 1)
        self.assert_refused_before_run(status, got)

    def test_AC13_levels_low_mismatch(self):
        status, got, out, err, board = self.go(levels=(1267, 1490, 2))
        self.assert_refused_before_run(status, got)

    def test_AC13_levels_high_mismatch(self):
        status, got, out, err, board = self.go(levels=(1266, 1491, 2))
        self.assert_refused_before_run(status, got)

    def test_AC13_levels_changes_mismatch(self):
        status, got, out, err, board = self.go(levels=(1266, 1490, 3))
        self.assert_refused_before_run(status, got)

    def test_AC13_refused_instead_of_confirmation(self):
        text = "REFUSED: value 3 is not a non-negative integer"
        status, got, out, err, board = self.go(confirm=text)
        self.assert_refused_before_run(status, got)
        self.reason_shown(out, err, text)

    def test_AC13_refused_instead_of_confirmation_with_hash_prefix(self):
        text = "REFUSED: no data for 5 s (got 3 of 9 values)"
        status, got, out, err, board = self.go(confirm="# " + text)
        self.assert_refused_before_run(status, got)
        self.reason_shown(out, err, text)

    def test_AC13_no_ready_within_timeout(self):
        status, got, out, err, board = self.go(ready="silent")
        self.assert_refused_before_run(status, got)

    def test_AC13_no_confirmation_within_timeout(self):
        status, got, out, err, board = self.go(confirm="silent")
        self.assert_refused_before_run(status, got)
        self.assertEqual(len(board.values), len(SMALL))

    # ---- E12: refused announcement ----
    def test_E12_refused_announcement_receives_no_value_line(self):
        text = "REFUSED: bad length 9 (must be 1..32000)"
        status, got, out, err, board = self.go(SMALL, ready=text)
        self.assertEqual(status, 2)
        m = re.match(rb"(temp\n)+prbs load 9\n$", got)
        self.assertIsNotNone(m, got[:60])
        self.assertEqual(board.values, [])
        self.assertNotIn(b"prbs run", got)
        self.reason_shown(out, err, text)

    # ---- after prbs run ----
    def test_AC13_no_run_start_sends_stop(self):
        status, got, out, err, board = self.go(run="silent")
        self.assertEqual(status, 2)
        self.assertTrue(got.endswith(b"prbs run\nstop\n"), got[-30:])

    def test_AC13_run_refused_sends_stop(self):
        text = "REFUSED: no sequence loaded"
        status, got, out, err, board = self.go(run=text)
        self.assertEqual(status, 2)
        self.assertTrue(got.endswith(b"prbs run\nstop\n"), got[-30:])
        self.reason_shown(out, err, text)

    def test_AC13_run_refused_with_hash_prefix_sends_stop(self):
        status, got, out, err, board = self.go(run="# REFUSED: a PRBS run is in progress")
        self.assertEqual(status, 2)
        self.assertTrue(got.endswith(b"prbs run\nstop\n"), got[-30:])


START = "# RUN START n=3 cap=2047 limit=75.0"
COLS = "# columns: k,time_s,u_counts,load_C,ambient_C"
DATA3 = ["0,1,1266,25.3,25.0", "5,7,1490,nan,nan", "12,13,1266,60.1,24.9"]
END_OK = "# RUN END ok n=3"
PRE_TEXT = ["TEMP load=25.0", "PRBS LOAD READY", "PRBS LEVELS", "PRBS LOADED"]


class RunLogTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.log = os.path.join(self.tmp.name, "run.log")
        self.seq = os.path.join(self.tmp.name, "seq.txt")
        with open(self.seq, "w") as f:
            f.write("1266\n1490\n1266\n")

    def timeouts(self, silence=0.5):
        return prbs_run.Timeouts(sync=0.02, ready=0.5, loaded=0.5,
                                 run_start=0.5, silence=silence)

    def play(self, lines, previous=None):
        """Returns (status, log text, console text)."""
        if previous is not None:
            with open(self.log, "w") as f:
                f.write(previous)
        out, err = io.StringIO(), io.StringIO()
        with ScriptedBoard(run_lines=lines) as board:
            argv = ["--port", board.path, "--seq", self.seq, "--log", self.log]
            with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
                status = prbs_run.main(argv, self.timeouts())
        with open(self.log) as f:
            return status, f.read(), out.getvalue() + err.getvalue()

    def complete(self):
        return [START, COLS] + DATA3 + [END_OK]

    def test_AC12_complete_run_logged_exactly_status_0(self):
        status, log, _ = self.play(self.complete())
        self.assertEqual(log, "".join(l + "\n" for l in self.complete()))
        self.assertEqual(status, 0)

    def test_AC12_log_appended_after_previous_content(self):
        status, log, _ = self.play(self.complete(), previous="old 1\nold 2\n")
        self.assertEqual(log, "old 1\nold 2\n"
                         + "".join(l + "\n" for l in self.complete()))
        self.assertEqual(status, 0)

    def test_AC12_crlf_stripped_in_log(self):
        status, log, _ = self.play([l + "\r" for l in self.complete()])
        self.assertEqual(log, "".join(l + "\n" for l in self.complete()))
        self.assertEqual(status, 0)

    def test_AC12_lines_before_run_start_not_in_log(self):
        status, log, console = self.play(self.complete())
        for text in PRE_TEXT:
            self.assertNotIn(text, log)
        self.assertIn("PRBS LOADED", console)

    def test_AC12_lines_after_run_end_not_logged(self):
        status, log, _ = self.play(self.complete() + ["# late line", "9,9,1266,1.0,2.0"])
        self.assertEqual(log, "".join(l + "\n" for l in self.complete()))
        self.assertEqual(status, 0)

    def test_AC12_aborted_run_status_1_and_logged(self):
        end = "# RUN END aborted k=3 reason=over-temperature"
        lines = [START, COLS, DATA3[0], end]
        status, log, _ = self.play(lines)
        self.assertEqual(log, "".join(l + "\n" for l in lines))
        self.assertEqual(status, 1)

    def test_AC12_comment_lines_kept_verbatim(self):
        lines = [START, COLS, "# some comment, with: punctuation", DATA3[0], END_OK]
        status, log, _ = self.play(lines)
        self.assertEqual(log, "".join(l + "\n" for l in lines))

    def test_AC12_negative_decimal_data_line_valid(self):
        lines = [START, COLS, "1,2,1266,-0.5,24.9", END_OK]
        status, log, _ = self.play(lines)
        self.assertEqual(log, "".join(l + "\n" for l in lines))
        self.assertNotIn("CORRUPT", log)

    def test_AC12_corrupt_lines_marked_and_counted(self):
        bad = ["12,13,1266,2", "1,2,1266,25.0,25.01,3,1266,25.1,25.0", "xyz"]
        lines = [START, COLS, DATA3[0], bad[0], DATA3[1], bad[1], bad[2],
                 DATA3[2], END_OK]
        status, log, console = self.play(lines)
        expect = [START, COLS, DATA3[0], "# CORRUPT " + bad[0], DATA3[1],
                  "# CORRUPT " + bad[1], "# CORRUPT " + bad[2], DATA3[2], END_OK]
        self.assertEqual(log, "".join(l + "\n" for l in expect))
        self.assertEqual(status, 0)
        self.assertIn("corrupt", console.lower())
        self.assertIn("3", console)

    def test_AC12_corrupt_count_shown_is_one(self):
        lines = [START, COLS, "12,13,1266,2", DATA3[0], END_OK]
        status, log, console = self.play(lines)
        self.assertIn("# CORRUPT 12,13,1266,2\n", log)
        self.assertIn("corrupt", console.lower())
        self.assertRegex(console.lower(), r"corrupt[^\n]*\b1\b|\b1\b[^\n]*corrupt")

    def test_AC12_each_line_on_disk_before_run_ends(self):
        release = threading.Event()
        self.addCleanup(release.set)
        result = {}
        out, err = io.StringIO(), io.StringIO()
        with ScriptedBoard(run_lines=[START, COLS, DATA3[0], release, END_OK]) as board:
            argv = ["--port", board.path, "--seq", self.seq, "--log", self.log]

            def target():
                result["status"] = prbs_run.main(argv, self.timeouts(silence=8.0))

            with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
                th = threading.Thread(target=target, daemon=True)
                th.start()
                want = "".join(l + "\n" for l in (START, COLS, DATA3[0]))
                content = ""
                deadline = time.monotonic() + 6.0
                seen_at = None
                while time.monotonic() < deadline:
                    try:
                        with open(self.log) as f:
                            content = f.read()
                    except OSError:
                        content = ""
                    if content == want:
                        seen_at = time.monotonic()
                        break
                    time.sleep(0.02)
                alive = th.is_alive()
                release.set()
                th.join(5.0)
        self.assertEqual(content, want)
        self.assertTrue(alive, "main returned before the run ended")
        self.assertEqual(result.get("status"), 0)
        with open(self.log) as f:
            self.assertEqual(f.read(), want + END_OK + "\n")

    def test_AC12_every_logged_line_fsynced(self):
        calls = []
        real = os.fsync

        def counting(fd):
            calls.append(fd)
            return real(fd)

        with mock.patch("os.fsync", counting):
            status, log, _ = self.play(self.complete())
        self.assertEqual(len(log.splitlines()), 6)
        self.assertGreaterEqual(len(calls), 6)


class IncompleteLogTest(unittest.TestCase):
    """Plan D10 step 7: AC14, AC15 (helper side), E4 (helper side)."""

    SILENCE = 0.5

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.log = os.path.join(self.tmp.name, "run.log")
        self.seq = os.path.join(self.tmp.name, "seq.txt")
        with open(self.seq, "w") as f:
            f.write("1266\n1490\n1266\n")

    def play(self, lines, previous=None):
        """In-process run. Returns (status, log text)."""
        if previous is not None:
            with open(self.log, "w") as f:
                f.write(previous)
        out, err = io.StringIO(), io.StringIO()
        with ScriptedBoard(run_lines=lines) as board:
            argv = ["--port", board.path, "--seq", self.seq, "--log", self.log]
            with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
                status = prbs_run.main(argv, prbs_run.Timeouts(
                    sync=0.02, ready=0.5, loaded=0.5, run_start=0.5,
                    silence=self.SILENCE))
        with open(self.log) as f:
            return status, f.read()

    def assert_incomplete(self, log, kept, marker):
        lines = log.split("\n")
        self.assertEqual(lines[-1], "", "log must end with a newline")
        lines = lines[:-1]
        self.assertEqual(lines[:-1], kept)
        self.assertEqual(lines[-1], marker)
        self.assertEqual([l for l in lines if l.startswith("# LOG INCOMPLETE ")],
                         [marker])
        self.assertFalse([l for l in lines if l.startswith("# RUN END")])

    def test_AC14_serial_error_when_pty_closed(self):
        status, log = self.play([START, COLS, DATA3[0], 0.3, CLOSE_PTY],
                                previous="old 1\n")
        self.assertEqual(status, 3)
        self.assertTrue(log.startswith("old 1\n"))
        lines = log.splitlines()
        self.assertEqual(lines[1:4], [START, COLS, DATA3[0]])
        self.assertEqual(len(lines), 5, lines)
        self.assertRegex(lines[-1], r"^# LOG INCOMPLETE serial error: \S.*$")
        self.assertNotIn("# RUN END", log)

    def test_AC14_silent_board(self):
        status, log = self.play([START, COLS, DATA3[0], 3.0])
        self.assertEqual(status, 3)
        self.assert_incomplete(log, [START, COLS, DATA3[0]],
                               "# LOG INCOMPLETE no line received for %g s"
                               % self.SILENCE)

    def test_AC14_board_reset_ready_seen(self):
        status, log = self.play([START, COLS, DATA3[0], "READY",
                                 "# boot line", DATA3[1], 1.0])
        self.assertEqual(status, 3)
        self.assert_incomplete(log, [START, COLS, DATA3[0]],
                               "# LOG INCOMPLETE board reset (READY seen)")

    def test_AC14_no_traceback_on_incomplete_end(self):
        err = io.StringIO()
        with ScriptedBoard(run_lines=[START, COLS, 0.2, CLOSE_PTY]) as board:
            argv = ["--port", board.path, "--seq", self.seq, "--log", self.log]
            with contextlib.redirect_stdout(io.StringIO()), \
                    contextlib.redirect_stderr(err):
                status = prbs_run.main(argv, prbs_run.Timeouts(
                    sync=0.02, ready=0.5, loaded=0.5, run_start=0.5, silence=0.5))
        self.assertEqual(status, 3)
        self.assertNotIn("Traceback", err.getvalue())

    # ---- subprocess tests -------------------------------------------------

    def spawn(self, board):
        self.errfile = os.path.join(self.tmp.name, "stderr.txt")
        ef = open(self.errfile, "w")
        self.addCleanup(ef.close)
        proc = subprocess.Popen(
            [sys.executable, os.path.join(TOOLS, "prbs_run.py"),
             "--port", board.path, "--seq", self.seq, "--log", self.log],
            cwd=ROOT, stdout=subprocess.DEVNULL, stderr=ef)

        def reap():
            if proc.poll() is None:
                proc.kill()
            proc.wait()
        self.addCleanup(reap)
        return proc

    def read_log(self):
        try:
            with open(self.log) as f:
                return f.read()
        except OSError:
            return ""

    def wait_log_has(self, text, timeout=15.0):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if text in self.read_log():
                return True
            time.sleep(0.05)
        return False

    def signal_case(self, sig):
        release = threading.Event()
        self.addCleanup(release.set)
        with ScriptedBoard(run_lines=[START, COLS, DATA3[0], release]) as board:
            proc = self.spawn(board)
            self.assertTrue(self.wait_log_has(DATA3[0] + "\n"), self.read_log())
            proc.send_signal(sig)
            try:
                status = proc.wait(timeout=3.0)
            except subprocess.TimeoutExpired:
                self.fail("helper did not exit within 3 s of %s" % sig.name)
        self.assertEqual(status, 3)
        self.assert_incomplete(self.read_log(), [START, COLS, DATA3[0]],
                              "# LOG INCOMPLETE interrupted by %s" % sig.name)
        with open(self.errfile) as f:
            self.assertNotIn("Traceback", f.read())

    def test_AC14_sigint_interrupted(self):
        self.signal_case(signal.SIGINT)

    def test_AC14_sigterm_interrupted(self):
        self.signal_case(signal.SIGTERM)

    def test_AC14_sighup_interrupted(self):
        self.signal_case(signal.SIGHUP)

    def test_AC14_pause_longer_than_silence_is_not_silence(self):
        gate = threading.Event()
        self.addCleanup(gate.set)
        data = ["%d,%d,1266,25.0,25.0" % (k, k + 1) for k in range(12)]
        script = [START, COLS, gate]
        for d in data:
            script += [d, 1.0]
        script.append(END_OK)
        with ScriptedBoard(run_lines=script) as board:
            proc = self.spawn(board)
            self.assertTrue(self.wait_log_has(COLS + "\n"), self.read_log())
            proc.send_signal(signal.SIGSTOP)
            try:
                gate.set()
                time.sleep(12.5)   # longer than the default silence of 10 s
            finally:
                proc.send_signal(signal.SIGCONT)
            try:
                status = proc.wait(timeout=10.0)
            except subprocess.TimeoutExpired:
                self.fail("helper did not finish after SIGCONT")
        log = self.read_log()
        self.assertNotIn("LOG INCOMPLETE", log)
        self.assertEqual(status, 0)
        self.assertEqual(log.splitlines(), [START, COLS] + data + [END_OK])


if __name__ == "__main__":
    unittest.main()
