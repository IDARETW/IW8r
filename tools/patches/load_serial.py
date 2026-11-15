import io

# Document::loadSerial (bumped by every load): the Layers panel tells a new map from an edit exactly (two MapEnts
# dumps in a row have the same empty path). The entity inspector's key table also takes Backspace (the default
# Delete Selection key) so it removes the selected keys instead of the entity.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    if s.count(a) == 0 and b in s:
        return
    assert s.count(a) == count, (p, s.count(a), a[:80])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


patch('src/core/document.h', '''    uint64_t revision() const { return revision_; }  // bumps on every change (render caches)''',
      '''    uint64_t revision() const { return revision_; }  // bumps on every change (render caches)
    uint64_t loadSerial() const { return loadSerial_; }  // bumps on every load (another map, even at the same path)''')
patch('src/core/document.h', '''    uint64_t revision_ = 1;''', '''    uint64_t revision_ = 1;
    uint64_t loadSerial_ = 0;''')
patch('src/core/document.cpp', '''    savedPosLost_ = false;
    drag_.reset();
    touch();
    return true;
}''', '''    savedPosLost_ = false;
    drag_.reset();
    ++loadSerial_;
    touch();
    return true;
}''')
patch('src/qtapp/layers_panel.cpp', '''        // Another map: its layers start visible. A different path, or an emptied undo history (only opening a map
        // clears it), tells it from an edit of this one.
        const std::string& path = ctx_->doc.path();
        const size_t history = ctx_->doc.history().size();
        if (path != shownPath_ || (shownHistory_ > 0 && history == 0)) hiddenLayers_.clear();
        shownPath_ = path;
        shownHistory_ = history;''', '''        // Another map: its layers start visible (Document::loadSerial bumps on every load, also at the same path).
        if (ctx_->doc.loadSerial() != shownLoad_) hiddenLayers_.clear();
        shownLoad_ = ctx_->doc.loadSerial();''')
patch('src/qtapp/layers_panel.h', '''    std::string shownPath_;               // the document path and undo history size at the last refresh
    size_t shownHistory_ = 0;''', '''    uint64_t shownLoad_ = 0;  // Document::loadSerial at the last refresh''')
patch('src/qtapp/panels.cpp', '''        if (ke->key() == Qt::Key_Delete && (ke->modifiers() & ~Qt::KeypadModifier) == Qt::NoModifier && !editing) {''',
      '''        // Delete, and Backspace (the default Delete Selection key), remove the selected keys here, never the entity.
        if ((ke->key() == Qt::Key_Delete || ke->key() == Qt::Key_Backspace) && (ke->modifiers() & ~Qt::KeypadModifier) == Qt::NoModifier &&
            !editing) {''')
print('ok')
