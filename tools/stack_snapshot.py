"""Samples the stacks of a running IW8Radiant.exe (or any process) without a debugger: each busy thread is suspended
for a moment, walked with DbgHelp's StackWalk64 and resumed. For "the editor hangs / spins, where?" questions.

Usage: py -3 -I tools/stack_snapshot.py <pid> [samples=3] [top_threads=2]
Prints, for the threads that used the most CPU time, `samples` stacks with symbols and source lines (the PDB next to
the exe is found through the module path).
"""
import ctypes
import ctypes.wintypes as wt
import sys
import time

k32 = ctypes.WinDLL('kernel32', use_last_error=True)
dbg = ctypes.WinDLL('dbghelp.dll')
ntdll = ctypes.WinDLL('ntdll')

PROCESS_ALL = 0x1F0FFF
THREAD_ALL = 0x1F03FF
TH32CS_SNAPTHREAD = 0x4


class THREADENTRY32(ctypes.Structure):
    _fields_ = [('dwSize', wt.DWORD), ('cntUsage', wt.DWORD), ('th32ThreadID', wt.DWORD), ('th32OwnerProcessID', wt.DWORD),
                ('tpBasePri', wt.LONG), ('tpDeltaPri', wt.LONG), ('dwFlags', wt.DWORD)]


class M128A(ctypes.Structure):
    _fields_ = [('Low', ctypes.c_ulonglong), ('High', ctypes.c_longlong)]


class CONTEXT(ctypes.Structure):
    _pack_ = 16
    _fields_ = [('P1Home', ctypes.c_ulonglong), ('P2Home', ctypes.c_ulonglong), ('P3Home', ctypes.c_ulonglong),
                ('P4Home', ctypes.c_ulonglong), ('P5Home', ctypes.c_ulonglong), ('P6Home', ctypes.c_ulonglong),
                ('ContextFlags', wt.DWORD), ('MxCsr', wt.DWORD), ('SegCs', wt.WORD), ('SegDs', wt.WORD), ('SegEs', wt.WORD),
                ('SegFs', wt.WORD), ('SegGs', wt.WORD), ('SegSs', wt.WORD), ('EFlags', wt.DWORD),
                ('Dr0', ctypes.c_ulonglong), ('Dr1', ctypes.c_ulonglong), ('Dr2', ctypes.c_ulonglong), ('Dr3', ctypes.c_ulonglong),
                ('Dr6', ctypes.c_ulonglong), ('Dr7', ctypes.c_ulonglong),
                ('Rax', ctypes.c_ulonglong), ('Rcx', ctypes.c_ulonglong), ('Rdx', ctypes.c_ulonglong), ('Rbx', ctypes.c_ulonglong),
                ('Rsp', ctypes.c_ulonglong), ('Rbp', ctypes.c_ulonglong), ('Rsi', ctypes.c_ulonglong), ('Rdi', ctypes.c_ulonglong),
                ('R8', ctypes.c_ulonglong), ('R9', ctypes.c_ulonglong), ('R10', ctypes.c_ulonglong), ('R11', ctypes.c_ulonglong),
                ('R12', ctypes.c_ulonglong), ('R13', ctypes.c_ulonglong), ('R14', ctypes.c_ulonglong), ('R15', ctypes.c_ulonglong),
                ('Rip', ctypes.c_ulonglong), ('FltSave', ctypes.c_byte * 512), ('VectorRegister', M128A * 26),
                ('VectorControl', ctypes.c_ulonglong), ('DebugControl', ctypes.c_ulonglong),
                ('LastBranchToRip', ctypes.c_ulonglong), ('LastBranchFromRip', ctypes.c_ulonglong),
                ('LastExceptionToRip', ctypes.c_ulonglong), ('LastExceptionFromRip', ctypes.c_ulonglong)]


class ADDRESS64(ctypes.Structure):
    _fields_ = [('Offset', ctypes.c_ulonglong), ('Segment', wt.WORD), ('Mode', wt.DWORD)]


class KDHELP64(ctypes.Structure):
    _fields_ = [('Thread', ctypes.c_ulonglong), ('ThCallbackStack', wt.DWORD), ('ThCallbackBStore', wt.DWORD),
                ('NextCallback', wt.DWORD), ('FramePointer', wt.DWORD), ('KiCallUserMode', ctypes.c_ulonglong),
                ('KeUserCallbackDispatcher', ctypes.c_ulonglong), ('SystemRangeStart', ctypes.c_ulonglong),
                ('KiUserExceptionDispatcher', ctypes.c_ulonglong), ('StackBase', ctypes.c_ulonglong),
                ('StackLimit', ctypes.c_ulonglong), ('BuildVersion', wt.DWORD), ('RetpolineStubFunctionTableSize', wt.DWORD),
                ('RetpolineStubFunctionTable', ctypes.c_ulonglong), ('RetpolineStubOffset', wt.DWORD),
                ('RetpolineStubSize', wt.DWORD), ('Reserved0', ctypes.c_ulonglong * 2)]


class STACKFRAME64(ctypes.Structure):
    _fields_ = [('AddrPC', ADDRESS64), ('AddrReturn', ADDRESS64), ('AddrFrame', ADDRESS64), ('AddrStack', ADDRESS64),
                ('AddrBStore', ADDRESS64), ('FuncTableEntry', ctypes.c_void_p), ('Params', ctypes.c_ulonglong * 4),
                ('Far', wt.BOOL), ('Virtual', wt.BOOL), ('Reserved', ctypes.c_ulonglong * 3), ('KdHelp', KDHELP64)]


class SYMBOL_INFO(ctypes.Structure):
    _fields_ = [('SizeOfStruct', wt.ULONG), ('TypeIndex', wt.ULONG), ('Reserved', ctypes.c_ulonglong * 2),
                ('Index', wt.ULONG), ('Size', wt.ULONG), ('ModBase', ctypes.c_ulonglong), ('Flags', wt.ULONG),
                ('Value', ctypes.c_ulonglong), ('Address', ctypes.c_ulonglong), ('Register', wt.ULONG),
                ('Scope', wt.ULONG), ('Tag', wt.ULONG), ('NameLen', wt.ULONG), ('MaxNameLen', wt.ULONG),
                ('Name', ctypes.c_char * 1024)]


class IMAGEHLP_LINE64(ctypes.Structure):
    _fields_ = [('SizeOfStruct', wt.DWORD), ('Key', ctypes.c_void_p), ('LineNumber', wt.DWORD),
                ('FileName', ctypes.c_char_p), ('Address', ctypes.c_ulonglong)]


class FILETIME(ctypes.Structure):
    _fields_ = [('lo', wt.DWORD), ('hi', wt.DWORD)]


def thread_ids(pid):
    snap = k32.CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0)
    te = THREADENTRY32()
    te.dwSize = ctypes.sizeof(te)
    out = []
    ok = k32.Thread32First(snap, ctypes.byref(te))
    while ok:
        if te.th32OwnerProcessID == pid:
            out.append(te.th32ThreadID)
        ok = k32.Thread32Next(snap, ctypes.byref(te))
    k32.CloseHandle(snap)
    return out


def cpu_time(h):
    a, b, c, d = FILETIME(), FILETIME(), FILETIME(), FILETIME()
    if not k32.GetThreadTimes(h, ctypes.byref(a), ctypes.byref(b), ctypes.byref(c), ctypes.byref(d)):
        return 0
    return ((c.hi << 32) | c.lo) + ((d.hi << 32) | d.lo)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    pid = int(sys.argv[1])
    samples = int(sys.argv[2]) if len(sys.argv) > 2 else 3
    top = int(sys.argv[3]) if len(sys.argv) > 3 else 2
    k32.OpenProcess.restype = wt.HANDLE
    k32.OpenThread.restype = wt.HANDLE
    proc = k32.OpenProcess(PROCESS_ALL, False, pid)
    if not proc:
        print('OpenProcess failed', ctypes.get_last_error())
        return 1
    dbg.SymSetOptions(0x2 | 0x10 | 0x4)  # UNDNAME | LOAD_LINES | DEFERRED_LOADS
    if not dbg.SymInitialize(wt.HANDLE(proc), None, True):
        print('SymInitialize failed')
        return 1
    dbg.SymFunctionTableAccess64.restype = ctypes.c_void_p
    dbg.SymFunctionTableAccess64.argtypes = [wt.HANDLE, ctypes.c_ulonglong]
    dbg.SymGetModuleBase64.restype = ctypes.c_ulonglong
    dbg.SymGetModuleBase64.argtypes = [wt.HANDLE, ctypes.c_ulonglong]
    FTA = ctypes.WINFUNCTYPE(ctypes.c_void_p, wt.HANDLE, ctypes.c_ulonglong)(lambda h, a: dbg.SymFunctionTableAccess64(h, a))
    GMB = ctypes.WINFUNCTYPE(ctypes.c_ulonglong, wt.HANDLE, ctypes.c_ulonglong)(lambda h, a: dbg.SymGetModuleBase64(h, a))
    dbg.StackWalk64.argtypes = [wt.DWORD, wt.HANDLE, wt.HANDLE, ctypes.POINTER(STACKFRAME64), ctypes.c_void_p, ctypes.c_void_p,
                                ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p]
    # The busiest threads over half a second.
    handles = {}
    for tid in thread_ids(pid):
        h = k32.OpenThread(THREAD_ALL, False, tid)
        if h:
            handles[tid] = h
    t0 = {tid: cpu_time(h) for tid, h in handles.items()}
    time.sleep(0.5)
    busy = sorted(handles, key=lambda t: cpu_time(handles[t]) - t0[t], reverse=True)[:top]
    for tid in busy:
        h = handles[tid]
        print('== thread %d (cpu %.1f s total, %.2f s in the last 0.5 s)' % (tid, cpu_time(h) / 1e7, (cpu_time(h) - t0[tid]) / 1e7))
        for s in range(samples):
            k32.SuspendThread(wt.HANDLE(h))
            ctx = CONTEXT()
            ctx.ContextFlags = 0x10000B  # CONTEXT_FULL (AMD64)
            frames = []
            if k32.GetThreadContext(wt.HANDLE(h), ctypes.byref(ctx)):
                sf = STACKFRAME64()
                sf.AddrPC.Offset, sf.AddrPC.Mode = ctx.Rip, 3
                sf.AddrFrame.Offset, sf.AddrFrame.Mode = ctx.Rbp, 3
                sf.AddrStack.Offset, sf.AddrStack.Mode = ctx.Rsp, 3
                for _ in range(48):
                    if not dbg.StackWalk64(0x8664, wt.HANDLE(proc), wt.HANDLE(h), ctypes.byref(sf), ctypes.byref(ctx), None,
                                           FTA, GMB, None):
                        break
                    if sf.AddrPC.Offset == 0:
                        break
                    frames.append(sf.AddrPC.Offset)
            k32.ResumeThread(wt.HANDLE(h))
            print('-- sample %d' % (s + 1))
            for a in frames:
                sym = SYMBOL_INFO()
                sym.SizeOfStruct = ctypes.sizeof(SYMBOL_INFO) - 1024
                sym.MaxNameLen = 1024
                disp = ctypes.c_ulonglong(0)
                text = '0x%x' % a
                if dbg.SymFromAddr(wt.HANDLE(proc), ctypes.c_ulonglong(a), ctypes.byref(disp), ctypes.byref(sym)):
                    text = '%s+0x%x' % (sym.Name.decode(errors='replace'), disp.value)
                    ln = IMAGEHLP_LINE64()
                    ln.SizeOfStruct = ctypes.sizeof(IMAGEHLP_LINE64)
                    ld = wt.DWORD(0)
                    if dbg.SymGetLineFromAddr64(wt.HANDLE(proc), ctypes.c_ulonglong(a), ctypes.byref(ld), ctypes.byref(ln)):
                        text += '  %s:%d' % (ln.FileName.decode(errors='replace').split('\\\\')[-1], ln.LineNumber)
                print('   ' + text[:220])
            time.sleep(0.3)
    for h in handles.values():
        k32.CloseHandle(wt.HANDLE(h))
    dbg.SymCleanup(wt.HANDLE(proc))
    k32.CloseHandle(wt.HANDLE(proc))
    return 0


if __name__ == '__main__':
    sys.exit(main())
