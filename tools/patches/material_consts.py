import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    if b and s.count(b) >= 1:
        return
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


H = 'src/qtapp/material_cache.h'
C = 'src/qtapp/material_cache.cpp'

# ---------------- the entry and the work item
patch(H, '''        QString emissiveDds;  // emissive map (texture slot 18); empty when none''', '''        QString emissiveDds;  // emissive map (texture slot 18); empty when none
        // Research 107: the transmission map (texture slot 68, sRGB; foliage, tarps, cloth, flags), and the lit
        // technique's material constants by index: 18 transmission tint, 36 emissive scale, 103 wrap diffuse, 104 the
        // wrap normal blend (rgba floats). Missing = not in the material.
        QString transmissionDds;
        std::map<int, std::array<float, 4>> consts;''')
patch(H, '''        QString emissive;       // slot 18 image name, if any''', '''        QString emissive;       // slot 18 image name, if any
        QString transmission;   // slot 68 image name, if any (research 107)
        std::map<int, std::array<float, 4>> consts;  // Entry::consts''')
patch(H, '''        QByteArray psData;  // the body's first pixel constant buffer (every material since tintRules 1)''',
      '''        // The lit technique's pixel constant buffer (constantBufferIndex[34], research 107 section 6; before constRules
        // 1 the first buffer with pixel data). Single-layer materials keep it in the canonical row order the decoders
        // read: row 0 constant 72, row 1 constant 0, row 2 constant 9, row 3 constant 54 (the lit technique puts 18 / 36
        // before 54); shorter than 64 bytes when the material has no constant 54.
        QByteArray psData;''')

# ---------------- resolve: the lit buffer, constants by index
patch(C, '''    // The first buffer with pixel data (every material: a decal's flags and ramp, research 98): displacement layers
    // (`..._129dn_...`, slot 96) have a vertex-only buffer 0 and the pixel constants in buffer 1.
    for (const QJsonValue& b : fields.value("constantBufferTable").toObject().value("values").toArray()) {
        work.psData = QByteArray::fromHex(b.toObject().value("psData").toObject().value("bytes").toString().toLatin1());
        if (!work.psData.isEmpty()) break;
    }''', '''    // The lit technique's buffer (research 107 section 6: constantBufferIndex[34], TECHNIQUE_LIT_FORWARDPLUS_BITMASK);
    // else the first buffer with pixel data (a decal's flags and ramp, research 98; displacement layers, `..._129dn_...`
    // slot 96, have a vertex-only buffer 0).
    const QJsonArray cbt = fields.value("constantBufferTable").toObject().value("values").toArray();
    {
        const QByteArray cbi = QByteArray::fromHex(fields.value("constantBufferIndex").toObject().value("bytes").toString().toLatin1());
        const int lit = cbi.size() > 34 ? int(uint8_t(cbi[34])) : 255;
        if (lit < cbt.size())
            work.psData = QByteArray::fromHex(cbt[lit].toObject().value("psData").toObject().value("bytes").toString().toLatin1());
    }
    if (work.psData.isEmpty())
        for (const QJsonValue& b : cbt) {
            work.psData = QByteArray::fromHex(b.toObject().value("psData").toObject().value("bytes").toString().toLatin1());
            if (!work.psData.isEmpty()) break;
        }
    // The constants by index (research 107): literal floats, a non-finite word as {"bits": "<hex>"}.
    work.consts.clear();
    std::map<int, QByteArray> constBytes;
    for (const QJsonValue& cv : fields.value("constantTable").toObject().value("values").toArray()) {
        const QJsonObject c = cv.toObject();
        const int index = c.value("index").toInt(-1);
        const QJsonArray v = c.value("literal").toObject().value("v").toArray();
        if (index < 0 || v.size() != 4) continue;
        QByteArray bytes(16, '\\0');
        std::array<float, 4> f{0, 0, 0, 0};
        bool finite = true;
        for (int k = 0; k < 4; ++k) {
            uint32_t u = 0;
            if (v[k].isDouble()) {
                f[size_t(k)] = float(v[k].toDouble());
                std::memcpy(&u, &f[size_t(k)], 4);
            } else {
                u = v[k].toObject().value("bits").toString().toUInt(nullptr, 16);
                finite = false;
            }
            std::memcpy(bytes.data() + 4 * k, &u, 4);
        }
        constBytes[index] = bytes;
        if (finite && (index == 18 || index == 36 || index == 103 || index == 104)) work.consts[index] = f;
    }
    // Single-layer materials: the rows in the order the decoders read (row 0 c72, 1 c0, 2 c9, 3 c54). The lit
    // technique puts transmission (18) and emissive (36) constants before 54, which then sits on row 4 to 6.
    if (std::clamp(work.layerCount, 0, 8) < 2 && !constBytes.empty()) {
        QByteArray canon = work.psData.left(64);
        canon.resize(64, '\\0');
        const int order[4] = {72, 0, 9, 54};
        for (int r = 0; r < 4; ++r)
            if (const auto it = constBytes.find(order[r]); it != constBytes.end()) canon.replace(16 * r, 16, it->second);
        if (!constBytes.count(54)) canon.truncate(48);  // no spec / gloss remap: the decoder reports none
        work.psData = canon;
    }''')
patch(C, '''        if (slot == 18 && work.emissive.isEmpty()) work.emissive = name;  // emissive slot''',
      '''        if (slot == 18 && work.emissive.isEmpty()) work.emissive = name;  // emissive slot
        if (slot == 68 && work.transmission.isEmpty()) work.transmission = name;  // transmission slot (research 107)''')
patch(C, '''            QStringList names{w.color, w.normal, w.mask, w.emissive};''', '''            QStringList names{w.color, w.normal, w.mask, w.emissive, w.transmission};''')
patch(C, '''            if (!w.emissive.isEmpty() && images_.count(w.emissive)) {
                const Image& im = images_.at(w.emissive);
                if (im.done) e.emissiveDds = im.file;  // optional: a failed emissive export is not an error
            }''', '''            if (!w.emissive.isEmpty() && images_.count(w.emissive)) {
                const Image& im = images_.at(w.emissive);
                if (im.done) e.emissiveDds = im.file;  // optional: a failed emissive export is not an error
            }
            if (!w.transmission.isEmpty() && images_.count(w.transmission)) {
                const Image& im = images_.at(w.transmission);
                if (im.done) e.transmissionDds = im.file;  // optional, as the emissive map
            }
            e.consts = w.consts;''')
# ---------------- the index file
patch(C, '''        if (!e.failed && !e.emissiveDds.isEmpty()) o.insert("emissive", root.relativeFilePath(e.emissiveDds));''',
      '''        if (!e.failed && !e.emissiveDds.isEmpty()) o.insert("emissive", root.relativeFilePath(e.emissiveDds));
        if (!e.failed && !e.transmissionDds.isEmpty()) o.insert("transmission", root.relativeFilePath(e.transmissionDds));
        if (!e.failed) {
            o.insert("constRules", 1);  // 1: the lit buffer, canonical rows, constants 18 / 36 / 103 / 104, slot 68
            QJsonObject cs;
            for (const auto& [k, f] : e.consts) cs.insert(QString::number(k), QJsonArray{f[0], f[1], f[2], f[3]});
            if (!cs.isEmpty()) o.insert("consts", cs);
        }''')
patch(C, '''        const QString emissive = o.value("emissive").toString();
        e.emissiveDds = emissive.isEmpty() ? QString() : QDir(root_).filePath(emissive);''', '''        const QString emissive = o.value("emissive").toString();
        e.emissiveDds = emissive.isEmpty() ? QString() : QDir(root_).filePath(emissive);
        const QString transmission = o.value("transmission").toString();
        e.transmissionDds = transmission.isEmpty() ? QString() : QDir(root_).filePath(transmission);
        const QJsonObject cs = o.value("consts").toObject();
        for (auto it = cs.begin(); it != cs.end(); ++it) {
            const QJsonArray a = it.value().toArray();
            if (a.size() == 4) e.consts[it.key().toInt()] = {float(a[0].toDouble()), float(a[1].toDouble()), float(a[2].toDouble()), float(a[3].toDouble())};
        }''')
patch(C, '''        if (!e.failed && o.value("maskRules").toInt() < 4) continue;  // 4: + emissive (slot 18)''',
      '''        if (!e.failed && o.value("maskRules").toInt() < 4) continue;  // 4: + emissive (slot 18)
        // Resolved before the lit buffer and the constants by index were kept (research 107): resolve again once.
        if (!e.failed && o.value("constRules").toInt() < 1) continue;''')
print('ok')
