import io


def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:70])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


patch('src/core/stb_image_impl.cpp', [
 ('''    rgba.assign(pixels, pixels + static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
    stbi_image_free(pixels);
    return true;
}
''', '''    rgba.assign(pixels, pixels + static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
    stbi_image_free(pixels);
    return true;
}

// Raw deflate (zip method 8) of a known size, with stb_image's inflater (map_package.cpp extractZip).
bool inflateRaw(const char* data, size_t compressedSize, size_t size, std::string& out) {
    if (compressedSize > 0x7FFFFFFF || size > 0x7FFFFFFF) return false;
    out.assign(size, '\\0');
    if (size == 0) return true;
    const int got = stbi_zlib_decode_noheader_buffer(out.data(), int(size), data, int(compressedSize));
    return got == int(size);
}
'''),
])

patch('src/core/map_package.cpp', [
 ('''struct ZipEntry {
    std::string name;  // cleaned, forward slashes, no trailing slash
    bool isDir = false;
    size_t dataAt = 0;
    uint32_t size = 0;
};
''', '''struct ZipEntry {
    std::string name;  // cleaned, forward slashes, no trailing slash
    bool isDir = false;
    size_t dataAt = 0;
    uint32_t size = 0;
    std::string inflated;  // a deflated entry, decompressed (its bytes; dataAt is not used)
    bool deflated = false;
};
'''),
 ('''        if (method != 0)
            return fail("entry '" + rawName + "' is compressed (method " + std::to_string(method) +
                        "); only stored entries are supported");
        if (csize != usize) return fail("corrupt zip: size mismatch for " + rawName);
''', '''        if (method != 0 && method != 8)
            return fail("entry '" + rawName + "' is compressed with method " + std::to_string(method) +
                        "; only stored and deflated entries are supported");
        if (method == 0 && csize != usize) return fail("corrupt zip: size mismatch for " + rawName);
        // A zip bomb guard: one entry at most 512 MB unpacked.
        if (usize > 512u * 1024u * 1024u) return fail("entry '" + rawName + "' is larger than 512 MB unpacked");
'''),
 ('''        const size_t dataAt = static_cast<size_t>(localOff) + 30 + get16(buf, localOff + 26) + get16(buf, localOff + 28);
        if (dataAt + usize > cdOff) return fail("corrupt zip: data out of range for " + rawName);
        if (crc32Of(buf.data() + dataAt, usize) != crc) return fail("CRC mismatch for " + rawName);

        entries.push_back({joinParts(parts), isDir, dataAt, usize});''', '''        const size_t dataAt = static_cast<size_t>(localOff) + 30 + get16(buf, localOff + 26) + get16(buf, localOff + 28);
        if (dataAt + csize > cdOff) return fail("corrupt zip: data out of range for " + rawName);
        ZipEntry entry{joinParts(parts), isDir, dataAt, usize};
        if (method == 8) {
            if (!detail::inflateRaw(buf.data() + dataAt, csize, usize, entry.inflated))
                return fail("corrupt zip: cannot inflate " + rawName);
            entry.deflated = true;
            if (crc32Of(entry.inflated.data(), usize) != crc) return fail("CRC mismatch for " + rawName);
        } else if (crc32Of(buf.data() + dataAt, usize) != crc) {
            return fail("CRC mismatch for " + rawName);
        }

        entries.push_back(std::move(entry));'''),
 ('''        f.write(buf.data() + e.dataAt, static_cast<std::streamsize>(e.size));''',
  '''        f.write(e.deflated ? e.inflated.data() : buf.data() + e.dataAt, static_cast<std::streamsize>(e.size));'''),
])
s = io.open('src/core/map_package.cpp', encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
anchor = 'struct ZipEntry {'
decl = 'namespace detail {' + nl + 'bool inflateRaw(const char* data, size_t compressedSize, size_t size, std::string& out);  // stb_image_impl.cpp' + nl + '}  // namespace detail' + nl + nl
i = s.index(anchor)
# The struct lives in an anonymous namespace inside iw8r: put the declaration before that namespace opens.
ns = s.rfind('namespace {', 0, i)
assert ns != -1
s = s[:ns] + decl + s[ns:]
io.open('src/core/map_package.cpp', 'w', encoding='utf-8', newline='').write(s)

patch('src/core/map_package.h', [
 ('''// Extracts a zip written by writeZip (stored entries only). Compressed or encrypted entries are rejected with a
// clear message,''', '''// Extracts a zip: stored and deflated entries (zips from writeZip, Windows "Send to > Compressed folder" and most
// tools). Other compression methods and encrypted entries are rejected with a clear message,'''),
])
print('ok')
