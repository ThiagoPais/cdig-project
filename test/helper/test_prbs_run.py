"""Tests for tools/prbs_run.py (spec 002, plan D10/D11). Standard library only."""
import ast
import os
import pty
import sys
import termios
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
TOOLS = os.path.join(ROOT, "tools")
sys.path.insert(0, TOOLS)

import prbs_run  # noqa: E402


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


if __name__ == "__main__":
    unittest.main()
