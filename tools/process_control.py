"""Native process-tree ownership and single-instance locks on Windows/Unix."""

import os
import time
import threading


def lock_file(stream):
    stream.seek(0)
    if os.name == "nt":
        import msvcrt

        try:
            msvcrt.locking(stream.fileno(), msvcrt.LK_NBLCK, 1)
        except OSError as exc:
            raise BlockingIOError("Another local helper owns this bundle.") from exc
    else:
        import fcntl

        fcntl.flock(stream, fcntl.LOCK_EX | fcntl.LOCK_NB)


class WindowsJob:
    def __init__(self, process):
        import ctypes as c
        from ctypes import wintypes as w

        self.c = c
        self.lock = threading.RLock()

        class IO(c.Structure):
            _fields_ = [
                (n, c.c_uint64)
                for n in (
                    "ReadOperationCount",
                    "WriteOperationCount",
                    "OtherOperationCount",
                    "ReadTransferCount",
                    "WriteTransferCount",
                    "OtherTransferCount",
                )
            ]

        class BASIC(c.Structure):
            _fields_ = [
                ("PerProcessUserTimeLimit", c.c_int64),
                ("PerJobUserTimeLimit", c.c_int64),
                ("LimitFlags", w.DWORD),
                ("MinimumWorkingSetSize", c.c_size_t),
                ("MaximumWorkingSetSize", c.c_size_t),
                ("ActiveProcessLimit", w.DWORD),
                ("Affinity", c.c_size_t),
                ("PriorityClass", w.DWORD),
                ("SchedulingClass", w.DWORD),
            ]

        class EXTENDED(c.Structure):
            _fields_ = [
                ("BasicLimitInformation", BASIC),
                ("IoInfo", IO),
                ("ProcessMemoryLimit", c.c_size_t),
                ("JobMemoryLimit", c.c_size_t),
                ("PeakProcessMemoryUsed", c.c_size_t),
                ("PeakJobMemoryUsed", c.c_size_t),
            ]

        class ACCOUNTING(c.Structure):
            _fields_ = [
                (n, c.c_int64)
                for n in (
                    "TotalUserTime",
                    "TotalKernelTime",
                    "ThisPeriodTotalUserTime",
                    "ThisPeriodTotalKernelTime",
                )
            ] + [
                (n, w.DWORD)
                for n in (
                    "TotalPageFaultCount",
                    "TotalProcesses",
                    "ActiveProcesses",
                    "TotalTerminatedProcesses",
                )
            ]

        self.ACCOUNTING = ACCOUNTING
        k = c.WinDLL("kernel32", use_last_error=True)
        k.CreateJobObjectW.argtypes = [c.c_void_p, w.LPCWSTR]
        k.CreateJobObjectW.restype = w.HANDLE
        k.SetInformationJobObject.argtypes = [w.HANDLE, c.c_int, c.c_void_p, w.DWORD]
        k.SetInformationJobObject.restype = w.BOOL
        k.AssignProcessToJobObject.argtypes = [w.HANDLE, w.HANDLE]
        k.AssignProcessToJobObject.restype = w.BOOL
        k.TerminateJobObject.argtypes = [w.HANDLE, w.UINT]
        k.TerminateJobObject.restype = w.BOOL
        k.QueryInformationJobObject.argtypes = [
            w.HANDLE,
            c.c_int,
            c.c_void_p,
            w.DWORD,
            c.c_void_p,
        ]
        k.QueryInformationJobObject.restype = w.BOOL
        k.CloseHandle.argtypes = [w.HANDLE]
        k.CloseHandle.restype = w.BOOL
        self.k = k
        self.handle = k.CreateJobObjectW(None, None)
        if not self.handle:
            raise c.WinError(c.get_last_error())
        limits = EXTENDED()
        limits.BasicLimitInformation.LimitFlags = 0x2000  # KILL_ON_JOB_CLOSE
        if not k.SetInformationJobObject(
            self.handle, 9, c.byref(limits), c.sizeof(limits)
        ) or not k.AssignProcessToJobObject(self.handle, int(process._handle)):
            error = c.WinError(c.get_last_error())
            self.close()
            raise error

    def alive(self):
        with self.lock:
            if not self.handle:
                return False
            value = self.ACCOUNTING()
            if not self.k.QueryInformationJobObject(
                self.handle, 1, self.c.byref(value), self.c.sizeof(value), None
            ):
                raise self.c.WinError(self.c.get_last_error())
            return bool(value.ActiveProcesses)

    def stop(self):
        with self.lock:
            if self.handle and not self.k.TerminateJobObject(self.handle, 130):
                raise self.c.WinError(self.c.get_last_error())
            until = time.monotonic() + 5
            while self.alive() and time.monotonic() < until:
                time.sleep(0.03)
            if self.alive():
                raise RuntimeError(
                    "A converter is still stopping. Do not start another job yet."
                )

    def close(self):
        with self.lock:
            if getattr(self, "handle", None):
                self.k.CloseHandle(self.handle)
                self.handle = None
