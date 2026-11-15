import io

# Side section panels, shared part (owner 2026-11-15: "sidebar utilities' menus need to be much better laid out, it
# needs way too much horizontal space"): the asset browsers (Materials, Models, Effects, Images, Sounds) put the search,
# In this map, All / Favourites / Recent, the kit, Buildable only and Apply / Place in one row (about 700 px). Now: the
# search and Apply / Place on the first row, the options in a row that wraps (FlowLayout), and the model / image
# preview under the list instead of beside it (it took 256 px of width).

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    if b in s and (a not in s or a in b):
        return
    assert s.count(a) == count, (p, s.count(a), a[:80])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


A = 'src/qtapp/asset_browser.cpp'
patch(A, '''    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    auto* top = new QHBoxLayout();
    search_ = new QLineEdit(this);''', '''    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    lay->setSpacing(4);
    // Row 1: the search and the Apply / Place button; row 2: the options, wrapping to more lines in a narrow column.
    auto* top = new QHBoxLayout();
    auto* optionsBox = new QWidget(this);
    optionsBox->setObjectName("assetOptions");
    auto* options = new FlowLayout(optionsBox);
    search_ = new QLineEdit(this);''')
patch(A, '''    search_->setPlaceholderText(kind == Kind::Images ? "Search 50k stock images (colour _c, normal _n, sky_, ...)"
                                : kind == Kind::Effects ? "Search 1020 stock effects that load in any map (e.g. fire, smoke, spark)"
                                : kind == Kind::Sounds ? "Search 11k stock sound aliases that load in any map (amb_, emt_ = ambient loops)"
                                : kind == Kind::Materials ? "Search 46k stock IW8 materials (e.g. concrete, brick, plaster)"
                                                        : "Search stock IW8 models (e.g. crate, barrel, car)");''',
      '''    // Short placeholders (the column is narrow); the longer hint is the tooltip.
    search_->setPlaceholderText(kind == Kind::Images ? "Search images" : kind == Kind::Effects ? "Search effects"
                                : kind == Kind::Sounds ? "Search sounds" : kind == Kind::Materials ? "Search materials"
                                                                                                  : "Search models");
    search_->setToolTip(kind == Kind::Images ? "50k stock images (colour _c, normal _n, sky_, ...)"
                        : kind == Kind::Effects ? "1020 stock effects that load in any map (e.g. fire, smoke, spark)"
                        : kind == Kind::Sounds ? "11k stock sound aliases that load in any map (amb_, emt_ = ambient loops)"
                        : kind == Kind::Materials ? "46k stock IW8 materials (e.g. concrete, brick, plaster)"
                                                  : "Stock IW8 models (e.g. crate, barrel, car)");''')
patch(A, '''    usedOnly_->setVisible(kind == Kind::Materials || kind == Kind::Models);
    top->addWidget(usedOnly_);''', '''    usedOnly_->setVisible(kind == Kind::Materials || kind == Kind::Models);
    options->addWidget(usedOnly_);''')
patch(A, '''                          "#tag: assets you tagged (right-click > Tags...).");
    top->addWidget(listMode_);''', '''                          "#tag: assets you tagged (right-click > Tags...).");
    options->addWidget(listMode_);''')
patch(A, '''            kit_->setToolTip("Stock families (containers, foliage, furniture, ...) from data/asset_catalog/iw8_kits.tsv");
            top->addWidget(kit_);''', '''            kit_->setToolTip("Stock families (containers, foliage, furniture, ...) from data/asset_catalog/iw8_kits.tsv");
            kit_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
            kit_->setMinimumContentsLength(12);  // long kit names are elided, not widening the panel
            options->addWidget(kit_);''')
patch(A, '''    buildableOnly_->setToolTip("Hide stock assets the availability audit could not export from Replay");
    top->addWidget(buildableOnly_);''', '''    buildableOnly_->setToolTip("Hide stock assets the availability audit could not export from Replay");
    options->addWidget(buildableOnly_);''')
patch(A, '''    top->addWidget(use);
    use->setVisible(kind != Kind::Images);  // images are previewed only
    lay->addLayout(top);''', '''    top->addWidget(use);
    use->setVisible(kind != Kind::Images);  // images are previewed only
    lay->addLayout(top);
    lay->addWidget(optionsBox);''')
patch(A, '''    info_ = new QLabel(this);
    info_->setStyleSheet("color: gray");
    lay->addWidget(info_);''', '''    info_ = new QLabel(this);
    info_->setStyleSheet("color: gray");
    info_->setWordWrap(true);  // the summary wraps instead of widening the panel
    lay->addWidget(info_);''')
patch(A, '''    if (kind == Kind::Models || kind == Kind::Images) {
        // List and preview side by side.
        auto* row = new QHBoxLayout();
        row->addWidget(view_, 1);
        auto* side = new QVBoxLayout();
        preview_ = new QLabel(this);
        preview_->setObjectName("modelPreview");
        preview_->setFixedSize(256, 256);
        preview_->setAlignment(Qt::AlignCenter);
        preview_->setStyleSheet("background: #202226; border: 1px solid #3a3d44");
        previewCaption_ = new QLabel("Select a model to preview it", this);
        previewCaption_->setWordWrap(true);
        previewCaption_->setFixedWidth(256);
        previewCaption_->setStyleSheet("color: gray");''', '''    if (kind == Kind::Models || kind == Kind::Images) {
        // The list above, the preview under it (side by side took 256 px of the narrow column); a splitter between them.
        auto* split = new QSplitter(Qt::Vertical, this);
        split->setObjectName("assetPreviewSplit");
        split->setChildrenCollapsible(false);
        split->addWidget(view_);
        auto* sideBox = new QWidget(split);
        auto* side = new QVBoxLayout(sideBox);
        side->setContentsMargins(0, 0, 0, 0);
        preview_ = new QLabel(this);
        preview_->setObjectName("modelPreview");
        preview_->setMinimumSize(128, 128);
        preview_->setMaximumHeight(256);
        preview_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        preview_->setAlignment(Qt::AlignCenter);
        preview_->setStyleSheet("background: #202226; border: 1px solid #3a3d44");
        previewCaption_ = new QLabel("Select a model to preview it", this);
        previewCaption_->setWordWrap(true);
        previewCaption_->setStyleSheet("color: gray");''')
patch(A, '''        side->addStretch(1);
        row->addLayout(side);
        lay->addLayout(row, 1);
    } else {''', '''        split->addWidget(sideBox);
        split->setStretchFactor(0, 3);
        split->setStretchFactor(1, 2);
        lay->addWidget(split, 1);
    } else {''')
patch(A, '''#include "asset_browser.h"
''', '''#include "asset_browser.h"

#include <QSplitter>

#include "flow_layout.h"
''')
print('ok')
