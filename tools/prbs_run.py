#!/usr/bin/env python3
"""Helper for the open-loop PRBS experiment (specs/002-prbs-experiment).

Standard library only, Linux only. This module is built up task by task: it currently holds only
open_port(), the single place that touches termios (plan D11).
"""
import os
import termios


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
