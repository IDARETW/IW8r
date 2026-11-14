import io
import sys

# Repairs C++ sources in which a shell heredoc turned the escapes \0 and \x02 of a patch script into raw bytes.
for p in sys.argv[1:]:
    b = io.open(p, 'rb').read()
    n0, n2 = b.count(b'\x00'), b.count(b'\x02')
    b = b.replace(b'\x00', b'\\0').replace(b'\x02', b'\\x02')
    io.open(p, 'wb').write(b)
    print(p, n0, n2)
