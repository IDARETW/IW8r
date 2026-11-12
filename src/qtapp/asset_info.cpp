#include "asset_info.h"
#include "iw8_contents.h"
#include "streamed_images.h"
#include "techset_info.h"
#include "material_blend.h"
#include <QDir>
#include <QCoreApplication>

#include <QClipboard>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QGuiApplication>
#include <QPushButton>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <set>

#include "material_slots.h"

namespace iw8r::qt {

namespace {

QString row(const QString& k, const QString& v) {
    return "<tr><td style='padding-right:12px;color:#9aa'>" + k.toHtmlEscaped() + "</td><td>" + v + "</td></tr>";
}
QString esc(const QString& s) { return s.toHtmlEscaped(); }
QString fileLine(const QString& path) {
    if (path.isEmpty()) return "<i>none</i>";
    const QString d = MaterialCache::describeImage(path);
    // "<path> (WxH)": show the file name and size, the folder is the cache.
    const int paren = d.lastIndexOf(" (");
    return esc(QFileInfo(path).fileName()) + (paren > 0 ? esc(d.mid(paren)) : QString()) +
           QString(" , %1 KB").arg((QFileInfo(path).size() + 1023) / 1024);
}

}  // namespace

const StreamedImageCatalog& streamedImages() {
    static const StreamedImageCatalog cat = [] {
        StreamedImageCatalog c;
        QDir d(QCoreApplication::applicationDirPath());
        for (int up = 0; up < 4; ++up, d.cdUp())
            if (QFileInfo::exists(d.filePath("data/asset_catalog/iw8_streamed_images.tsv"))) {
                std::string err;
                c.load(d.filePath("data/asset_catalog/iw8_streamed_images.tsv").toStdString(), &err);
                break;
            }
        return c;
    }();
    return cat;
}

QString materialInfoHtml(const MaterialInfoInput& in) {
    QString h = "<h3>" + esc(in.name) + "</h3><table>";
    if (in.toolMaterial) h += row("Kind", "tool material (not drawn in game; sets collision or visibility behaviour)");
    else if (!in.customSource.isEmpty()) h += row("Kind", "custom material, defined in " + esc(in.customSource));
    else h += row("Kind", "stock IW8 material");
    h += row("Used in this map", in.faces ? QString("%1 face(s) on %2 brush(es) / patch(es)").arg(in.faces).arg(in.primitives) : QString("not used"));
    {
        // 6.16: what the surface type does in game (impact effects, scrape steps).
        int st = 0;
        for (int t = 0; t <= 28; ++t)
            if (in.surfaceType == QString::fromLatin1(iw3SurfaceTypeName(t))) st = t;
        const SurfaceFeedback fb = surfaceFeedback(st);
        h += row("Surface type", (in.surfaceType.isEmpty() ? QString("default") : esc(in.surfaceType)) +
                                     QString(" &mdash; impacts vfx/iw8/weap/_impact/%1/").arg(fb.impactFamily) +
                                     (*fb.scrapeAlias ? QString(", scrape step %1").arg(fb.scrapeAlias) : QString()));
    }
    if (!in.toolMaterial && in.customSource.isEmpty()) {
        if (!in.entry) {
            h += row("Cache", "not fetched yet (it is requested now; open this again in a moment)");
        } else if (in.entry->failed) {
            h += row("Cache", "<b>not available in Replay</b>: " + esc(in.entry->error.isEmpty() ? QString("not found in any zone") : in.entry->error));
            h += row("Build", "builds grey (no image to carry)");
        } else {
            const auto& e = *in.entry;
            h += row("Zone", esc(e.zone));
            h += row("Technique set", e.techset.isEmpty() ? QString("<i>unknown</i>") : esc(e.techset));
            {
                // Research 66: what the set name and the body flags say about how it draws.
                const TechsetInfo ts = parseTechset(e.techset.toStdString());
                QString how = QString("%1, %2").arg(techsetFamilyName(ts.family), materialBlendName(ts.blend));
                if (ts.glass) how += " (glass)";
                if (!ts.tableToken.empty()) how += ", table " + QString::fromStdString(ts.tableToken);
                h += row("Draws as", esc(how) + " &mdash; " +
                                         materialDrawName(materialDrawFor(in.name.toStdString(), e.techset.toStdString(), e.surfaceFlags)));
                if (e.surfaceFlags >= 0)
                    h += row("Surface flags", QString("0x%1%2").arg(quint64(e.surfaceFlags), 0, 16)
                                                  .arg((quint64(e.surfaceFlags) & kIw8SurfNoDraw) ? " (nodraw: shadow only)" : ""));
                if (!e.materialType.isEmpty()) h += row("Material type", esc(e.materialType) + QString(", sort %1").arg(e.sortKey));
            }
            h += row("Colour image", fileLine(e.colorDds));
            h += row("Normal + gloss", fileLine(e.normalDds));
            h += row("Opacity mask (slot 27)", fileLine(e.maskDds));
            QString build = e.colorDds.isEmpty() ? QString("builds grey (no colour image)") : QString("colour");
            if (!e.colorDds.isEmpty() && !e.normalDds.isEmpty() && in.buildsNormal) build += ", normal, specular + gloss";
            if (!e.colorDds.isEmpty() && !e.maskDds.isEmpty()) build += ", alpha-tested and two-sided";
            h += row("Build carries", build);
            if (!e.error.isEmpty()) h += row("Notes", esc(e.error));
        }
    }
    h += "</table>";
    if (in.entry && !in.entry->failed && !in.entry->textures.isEmpty()) {
        h += "<h4>Texture table</h4><table><tr><th align='left'>Slot</th><th align='left'>Holds</th><th align='left'>Image</th>"
             "<th align='left'>Streaming</th></tr>";
        for (const QString& t : in.entry->textures) {
            const int slot = t.section('=', 0, 0).toInt();
            const QString image = t.section('=', 1);
            // 4.25 (research 52): a streamed image keeps its big mips in the XPak files. The editor's cache reads every
            // XPak, so it gets the full size; in game the smallest part is what shows until streaming catches up.
            QString stream = "<span style='color:#9aa'>not in the sample</span>";
            if (const StreamedImage* si = streamedImages().find(image.toStdString()))
                stream = si->streamed ? QString("streamed: %1x%2 full, smallest part %3x%4").arg(si->fullW).arg(si->fullH)
                                            .arg(si->residentW).arg(si->residentH)
                                      : QString("resident (%1x%2, in the zone)").arg(si->fullW).arg(si->fullH);
            h += QString("<tr><td style='padding-right:12px'>0x%1</td><td style='padding-right:12px'>%2%3</td><td style='padding-right:12px'>%4</td><td>%5</td></tr>")
                     .arg(slot, 2, 16, QChar('0'))
                     .arg(esc(QString::fromStdString(iw8TextureSlotName(slot))),
                          iw8TextureSlotIsBuilt(slot) ? QString() : QString(" <span style='color:#9aa'>(not built)</span>"), esc(image), stream);
        }
        h += "</table><p style='color:#9aa'>Streaming states come from a sample catalogue (data/asset_catalog/iw8_streamed_images.tsv, one zone).</p>";
    } else if (in.entry && !in.entry->failed && in.customSource.isEmpty() && !in.toolMaterial) {
        h += "<p style='color:#9aa'>The texture table was not kept for this entry (cached by an older version). Clear the "
             "material cache entry to fetch it again.</p>";
    }
    return h;
}

QString modelInfoHtml(const ModelInfoInput& in) {
    QString h = "<h3>" + esc(in.name) + "</h3><table>";
    h += row("Placed in this map", in.placed ? QString("%1 time(s)").arg(in.placed) : QString("not placed"));
    if (!in.catalogZones.isEmpty()) h += row("Catalog zones", esc(in.catalogZones.join(", ")));
    if (!in.entry) {
        h += row("Cache", "not fetched yet (it is requested now; open this again in a moment)");
    } else if (in.entry->failed) {
        h += row("Cache", "<b>not available</b>: " + esc(in.entry->error.isEmpty() ? QString("ACTS could not export the model") : in.entry->error));
    } else {
        const auto& e = *in.entry;
        h += row("Zone", esc(e.zone));
        h += row("LOD0 surfaces asset", esc(e.lod0Surfs));
        const auto& b = e.bounds;
        h += row("Size (units)", QString("%1 x %2 x %3").arg(b[3] - b[0], 0, 'f', 1).arg(b[4] - b[1], 0, 'f', 1).arg(b[5] - b[2], 0, 'f', 1));
        h += row("Bounds", QString("(%1 %2 %3) to (%4 %5 %6)").arg(b[0], 0, 'f', 1).arg(b[1], 0, 'f', 1).arg(b[2], 0, 'f', 1)
                               .arg(b[3], 0, 'f', 1).arg(b[4], 0, 'f', 1).arg(b[5], 0, 'f', 1));
        h += row("Cached mesh", e.glbPath.isEmpty() ? QString("<i>none</i>")
                                                    : esc(QFileInfo(e.glbPath).fileName()) + QString(" , %1 KB").arg((QFileInfo(e.glbPath).size() + 1023) / 1024));
        h += row("Build", "copied into the map as iw8r_stock_" + esc(in.name) + " with a solid collision box; LOD0 only");
    }
    h += "</table>";
    if (in.mesh) {
        size_t tris = 0, verts = 0;
        for (const auto& m : in.mesh->meshes) tris += m.idx.size() / 3, verts += m.pos.size();
        h += QString("<h4>LOD0: %1 surface(s), %2 triangles, %3 vertices</h4>").arg(in.mesh->meshes.size()).arg(tris).arg(verts);
        h += "<table><tr><th align='left'>#</th><th align='left'>Material</th><th align='left'>Triangles</th></tr>";
        for (size_t i = 0; i < in.mesh->meshes.size(); ++i) {
            const auto& m = in.mesh->meshes[i];
            QString mat = QString::fromStdString(m.material);
            if (in.entry && int(i) < in.entry->materials.size() && !in.entry->materials[int(i)].isEmpty()) mat = in.entry->materials[int(i)];
            h += QString("<tr><td style='padding-right:12px'>%1</td><td style='padding-right:12px'>%2</td><td>%3</td></tr>")
                     .arg(i).arg(esc(mat.isEmpty() ? QString("(none)") : mat)).arg(m.idx.size() / 3);
        }
        h += "</table>";
    } else if (in.entry && !in.entry->failed && !in.entry->materials.isEmpty()) {
        h += "<h4>LOD0 materials</h4><p>" + esc(in.entry->materials.join(", ")) + "</p>";
    }
    if (in.detail) {
        // 3.18: LOD switch distances (the game draws the last LOD whose distance the camera has passed), bones, tags.
        const XModelInfo& d = *in.detail;
        const int now = in.cameraDistance >= 0 ? lodForDistance(d, in.cameraDistance) : -1;
        h += QString("<h4>LODs (%1)</h4><table><tr><th align='left'>LOD</th><th align='left'>Drawn up to</th>"
                     "<th align='left'>Surfaces</th><th align='left'>Asset</th></tr>").arg(d.lods.size());
        for (size_t i = 0; i < d.lods.size(); ++i) {
            const auto& l = d.lods[i];
            const QString mark = int(i) == now ? " <b>(camera now)</b>" : QString();
            const QString coll = int(i) == d.collLod ? " (collision)" : QString();
            h += QString("<tr><td style='padding-right:12px'>%1%2%3</td><td style='padding-right:12px'>%4 units (%5 m)</td>"
                         "<td style='padding-right:12px'>%6</td><td>%7</td></tr>")
                     .arg(i).arg(coll, mark).arg(l.distance, 0, 'f', 0).arg(l.distance * 0.0254, 0, 'f', 1).arg(l.surfCount)
                     .arg(esc(QString::fromStdString(l.surfs)));
        }
        h += "</table>";
        if (in.cameraDistance >= 0)
            h += QString("<p>Editor camera to the nearest placed copy: %1 units%2.</p>").arg(in.cameraDistance, 0, 'f', 0).arg(now < 0 ? QString(" (past the last LOD: not drawn there)") : QString());
        QStringList tags;
        for (const XModelBone& b : d.bones)
            if (isTag(b))
                tags << QString("%1 (%2 %3 %4)").arg(esc(QString::fromStdString(b.name))).arg(b.pos[0], 0, 'f', 1)
                            .arg(b.pos[1], 0, 'f', 1).arg(b.pos[2], 0, 'f', 1);
        h += QString("<h4>Bones: %1, tags: %2</h4>").arg(d.bones.size()).arg(tags.size());
        if (!tags.isEmpty()) h += "<p>" + tags.join("<br>") + "</p>";
    } else if (in.entry && !in.entry->failed) {
        h += "<p style='color:#9aa'>LODs, bones and tags: cached before they were kept; they are fetched again now.</p>";
    }
    return h;
}

void showAssetInfo(QWidget* parent, const QString& objectName, const QString& title, const QString& html) {
    QDialog* dlg = parent ? parent->findChild<QDialog*>(objectName) : nullptr;
    QTextBrowser* text = dlg ? dlg->findChild<QTextBrowser*>() : nullptr;
    if (!dlg || !text) {
        dlg = new QDialog(parent);
        dlg->setObjectName(objectName);
        dlg->resize(640, 520);
        auto* lay = new QVBoxLayout(dlg);
        text = new QTextBrowser(dlg);
        text->setOpenLinks(false);
        lay->addWidget(text);
        auto* box = new QDialogButtonBox(QDialogButtonBox::Close, dlg);
        QPushButton* copy = box->addButton("Copy", QDialogButtonBox::ActionRole);
        QObject::connect(copy, &QPushButton::clicked, dlg, [text] { QGuiApplication::clipboard()->setText(text->toPlainText()); });
        QObject::connect(box, &QDialogButtonBox::rejected, dlg, &QDialog::close);
        lay->addWidget(box);
    }
    dlg->setWindowTitle(title);
    text->setHtml(html);
    dlg->show();
    dlg->raise();
}

}  // namespace iw8r::qt
