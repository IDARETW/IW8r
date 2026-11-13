"""Resolves the module+offset lines of a crash trace (abort-<tick>.txt, written by src/qtapp/crash_handler.cpp) to
function names and source lines, using DbgHelp and the PDB next to the exe.

Usage: py -3 -I tools/symbolize_trace.py <trace.txt> <IW8Radiant.exe>
Only frames of that exe are resolved; other modules are printed as they are.
"""
import ctypes
import ctypes.wintypes as wt
import os
import re
import sys


class SYMBOL_INFO(ctypes.Structure):
    _fields_ = [('SizeOfStruct', wt.ULONG), ('TypeIndex', wt.ULONG), ('Reserved', ctypes.c_ulonglong * 2),
                ('Index', wt.ULONG), ('Size', wt.ULONG), ('ModBase', ctypes.c_ulonglong), ('Flags', wt.ULONG),
                ('Value', ctypes.c_ulonglong), ('Address', ctypes.c_ulonglong), ('Register', wt.ULONG),
                ('Scope', wt.ULONG), ('Tag', wt.ULONG), ('NameLen', wt.ULONG), ('MaxNameLen', wt.ULONG),
                ('Name', ctypes.c_char * 1024)]


class IMAGEHLP_LINE64(ctypes.Structure):
    _fields_ = [('SizeOfStruct', wt.DWORD), ('Key', ctypes.c_void_p), ('LineNumber', wt.DWORD),
                ('FileName', ctypes.c_char_p), ('Address', ctypes.c_ulonglong)]


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    trace, exe = sys.argv[1], os.path.abspath(sys.argv[2])
    dbg = ctypes.WinDLL('dbghelp.dll')
    proc = wt.HANDLE(0x1234)  # any unique value: no real process is needed
    dbg.SymSetOptions(0x2 | 0x10)  # SYMOPT_UNDNAME | SYMOPT_LOAD_LINES
    if not dbg.SymInitialize(proc, ctypes.c_char_p(os.path.dirname(exe).encode()), False):
        print('SymInitialize failed')
        return 1
    dbg.SymLoadModuleEx.restype = ctypes.c_ulonglong
    dbg.SymLoadModuleEx.argtypes = [wt.HANDLE, wt.HANDLE, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_ulonglong, wt.DWORD,
                                    ctypes.c_void_p, wt.DWORD]
    base = 0x140000000
    if not dbg.SymLoadModuleEx(proc, None, exe.encode(), None, base, os.path.getsize(exe), None, 0):
        print('SymLoadModuleEx failed')
        return 1
    name = os.path.basename(exe).lower()
    for line in open(trace, encoding='utf-8', errors='replace'):
        line = line.rstrip('\n')
        m = re.match(r'#(\d+) (\S+)\+0x([0-9a-fA-F]+)', line)
        if not m or m.group(2).lower() != name:
            print(line)
            continue
        addr = base + int(m.group(3), 16)
        sym = SYMBOL_INFO()
        sym.SizeOfStruct = ctypes.sizeof(SYMBOL_INFO) - 1024
        sym.MaxNameLen = 1024
        disp = ctypes.c_ulonglong(0)
        text = line
        if dbg.SymFromAddr(proc, ctypes.c_ulonglong(addr), ctypes.byref(disp), ctypes.byref(sym)):
            text = '#%s %s+0x%x' % (m.group(1), sym.Name.decode(errors='replace'), disp.value)
            ln = IMAGEHLP_LINE64()
            ln.SizeOfStruct = ctypes.sizeof(IMAGEHLP_LINE64)
            ld = wt.DWORD(0)
            if dbg.SymGetLineFromAddr64(proc, ctypes.c_ulonglong(addr), ctypes.byref(ld), ctypes.byref(ln)):
                text += '  %s:%d' % (ln.FileName.decode(errors='replace'), ln.LineNumber)
        print(text)
    dbg.SymCleanup(proc)
    return 0


if __name__ == '__main__':
    sys.exit(main())
