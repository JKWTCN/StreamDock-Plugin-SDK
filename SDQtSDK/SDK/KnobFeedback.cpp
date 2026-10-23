#include "KnobFeedback.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QMap>
#include <QSet>
#include <algorithm>
#include <cmath>

namespace {
QString pluginRoot() {
    QDir root(QCoreApplication::applicationDirPath());
    for (int depth = 0; depth < 6 && !root.exists("manifest.json"); ++depth)
        if (!root.cdUp()) break;
    return root.absolutePath();
}
QString resolve(const QString &root, const QString &path) {
    return path.startsWith(":/") || QFileInfo(path).isAbsolute() ? path :
        root.startsWith(":/") ? root + (root.endsWith('/') ? QString() : "/") + path : QDir(root).filePath(path);
}
bool color(QString value, quint32 &result) {
    static const QMap<QString, QString> names = {
        {"white", "#ffffff"}, {"black", "#000000"}, {"red", "#ff0000"}, {"green", "#008000"},
        {"blue", "#0000ff"}, {"yellow", "#ffff00"}, {"cyan", "#00ffff"}, {"magenta", "#ff00ff"},
        {"gray", "#808080"}, {"grey", "#808080"}, {"darkgray", "#a9a9a9"}, {"darkgrey", "#a9a9a9"},
        {"pink", "#ffc0cb"}, {"orange", "#ffa500"}, {"transparent", "#00000000"}
    };
    value = value.trimmed().toLower();
    value = names.value(value, value);
    if (!value.startsWith('#')) return false;
    value.remove(0, 1);
    if (value.size() == 3) {
        QString expanded;
        for (const QChar character : value) { expanded += character; expanded += character; }
        value = expanded;
    }
    if (value.size() != 6 && value.size() != 8) return false;
    bool ok = false;
    result = value.toUInt(&ok, 16);
    if (value.size() == 6) result |= 0xff000000;
    return ok;
}
using Stop = QPair<double, quint32>;
bool brush(const QString &value, QVector<Stop> &stops) {
    quint32 solid;
    if (color(value, solid)) { stops = {{0, solid}, {1, solid}}; return true; }
    for (const auto &entry : value.split(',')) {
        const int colon = entry.indexOf(':');
        bool ok = false;
        const double offset = entry.left(colon).trimmed().toDouble(&ok);
        quint32 shade;
        if (colon < 0 || !ok || !std::isfinite(offset) || offset < 0 || offset > 1 ||
            !color(entry.mid(colon + 1), shade)) return false;
        stops.append({offset, shade});
    }
    std::stable_sort(stops.begin(), stops.end(), [](const Stop &a, const Stop &b) { return a.first < b.first; });
    if (stops.size() < 2) return false;
    if (stops.first().first > 0) stops.prepend({0, stops.first().second});
    if (stops.last().first < 1) stops.append({1, stops.last().second});
    return true;
}
quint32 interpolate(const QVector<Stop> &stops, double offset) {
    int index = 1;
    while (index < stops.size() - 1 && offset > stops[index].first) ++index;
    const auto &a = stops[index - 1], &b = stops[index];
    const double amount = b.first > a.first ? qBound(0.0, (offset - a.first) / (b.first - a.first), 1.0) : 1;
    quint32 result = 0;
    for (int shift = 0; shift <= 24; shift += 8) {
        const double first = (a.second >> shift) & 255, last = (b.second >> shift) & 255;
        result |= static_cast<quint32>(std::lround(first + (last - first) * amount)) << shift;
    }
    return result;
}
QJsonObject merge(QJsonObject base, const QJsonObject &patch) {
    for (auto it = patch.begin(); it != patch.end(); ++it)
        base.insert(it.key(), it.value().isObject() && base.value(it.key()).isObject() ?
            QJsonValue(merge(base.value(it.key()).toObject(), it.value().toObject())) : it.value());
    return base;
}
bool validate(const QJsonObject &layout, QString &error) {
    auto fail = [&error](const QString &message) { error = message; return false; };
    if (layout.value("id").toString().isEmpty() || !layout.value("items").isArray())
        return fail("Layout requires id and items");
    const auto items = layout.value("items").toArray();
    if (items.isEmpty() || items.size() > 128) return fail("Layout must contain 1..128 items");
    auto size = layout.value("size").toArray();
    if (layout.contains("size") && !layout.value("size").isArray()) return fail("Invalid logical canvas size");
    if (!size.isEmpty() && (size.size() != 2 || !size[0].isDouble() || !size[1].isDouble() ||
        size[0].toDouble() < 1 || size[1].toDouble() < 1 || size[0].toDouble() > 1024 || size[1].toDouble() > 1024 ||
        std::floor(size[0].toDouble()) != size[0].toDouble() || std::floor(size[1].toDouble()) != size[1].toDouble()))
        return fail("Invalid logical canvas size");
    const double width = size.isEmpty() ? 200 : size[0].toDouble(), height = size.isEmpty() ? 100 : size[1].toDouble();
    quint32 shade;
    if (layout.contains("background") && !color(layout.value("background").toString(), shade))
        return fail("Invalid canvas background color");
    QSet<QString> keys;
    for (const auto &entry : items) {
        const auto item = entry.toObject();
        const auto key = item.value("key").toString(), type = item.value("type").toString();
        if (key.isEmpty() || keys.contains(key)) return fail("Empty or duplicate item key: " + key);
        keys.insert(key);
        if (type != "text" && type != "pixmap" && type != "bar" && type != "gbar")
            return fail("Unsupported item type: " + type);
        const auto rect = item.value("rect").toArray();
        if (rect.size() != 4) return fail("Invalid rect for " + key);
        for (const auto &coordinate : rect)
            if (!coordinate.isDouble() || !std::isfinite(coordinate.toDouble())) return fail("Invalid rect for " + key);
        if (rect[0].toDouble() < 0 || rect[1].toDouble() < 0 || rect[2].toDouble() <= 0 || rect[3].toDouble() <= 0 ||
            rect[0].toDouble() + rect[2].toDouble() > width || rect[1].toDouble() + rect[3].toDouble() > height)
            return fail("Item is outside logical canvas: " + key);
        if (item.contains("opacity") && (!item.value("opacity").isDouble() || item.value("opacity").toDouble() < 0 ||
            item.value("opacity").toDouble() > 1)) return fail("Invalid opacity: " + key);
        if (item.contains("enabled") && !item.value("enabled").isBool()) return fail("Invalid enabled: " + key);
        if (item.contains("zOrder") && (!item.value("zOrder").isDouble() || item.value("zOrder").toDouble() < 0 ||
            item.value("zOrder").toDouble() > 700)) return fail("Invalid zOrder: " + key);
        for (const auto &name : {"background", "bar_bg_c", "bar_fill_c"}) {
            QVector<Stop> stops;
            if (item.contains(name) && !brush(item.value(name).toString(), stops)) return fail("Invalid color/gradient: " + key);
        }
        for (const auto &name : {"color", "bar_border_c"})
            if (item.contains(name) && !color(item.value(name).toString(), shade)) return fail("Invalid color: " + key);
        if (type == "text" || type == "pixmap") {
            if (item.contains("value") && !item.value("value").isString()) return fail("Expected string value: " + key);
        } else {
            if (!item.value("value").isDouble() || !std::isfinite(item.value("value").toDouble()))
                return fail("Expected finite numeric value: " + key);
            const auto range = item.value("range").toObject();
            const double low = range.value("min").toDouble(0), high = range.value("max").toDouble(100);
            if (item.contains("range") && (!item.value("range").isObject() ||
                (range.contains("min") && !range.value("min").isDouble()) ||
                (range.contains("max") && !range.value("max").isDouble()))) return fail("Invalid range: " + key);
            if (!std::isfinite(high - low) || high <= low) return fail("Invalid range: " + key);
            if (item.contains("subtype") && (!item.value("subtype").isDouble() ||
                (item.value("subtype").toDouble() != 0 && item.value("subtype").toDouble() != 4)))
                return fail("Only rectangle/groove bar subtypes (0/4) are supported: " + key);
            for (const auto &name : {"border_w", "bar_h"})
                if (item.contains(name) && (!item.value(name).isDouble() || item.value(name).toDouble() < 0 ||
                    item.value(name).toDouble() > rect[3].toDouble())) return fail("Invalid bar geometry: " + key);
        }
        if (type == "text") {
            const auto font = item.value("font").toObject();
            if (font.contains("size") && (!font.value("size").isDouble() || font.value("size").toDouble() < 1 ||
                font.value("size").toDouble() > 256)) return fail("Invalid font size: " + key);
            const auto weight = font.value("weight");
            if (!weight.isUndefined() && !(weight.isDouble() && weight.toDouble() >= 100 && weight.toDouble() <= 1000) &&
                weight.toString() != "bold" && weight.toString() != "normal") return fail("Invalid font weight: " + key);
            const auto alignment = item.value("alignment").toString("center");
            if (alignment != "left" && alignment != "right" && alignment != "center") return fail("Invalid alignment: " + key);
            const auto overflow = item.value("text-overflow").toString("clip");
            if (overflow != "clip" && overflow != "ellipsis" && overflow != "fit") return fail("Only clip/ellipsis/fit text overflow is supported: " + key);
        }
    }
    return true;
}
void fill(FeedbackScene &scene, const QRectF &rect, const QString &paint, double opacity,
          const QRectF &clip = QRectF()) {
    QVector<Stop> stops;
    if (!brush(paint, stops)) return;
    const int bands = stops.size() == 2 && stops.first().second == stops.last().second ? 1 : 256;
    for (int index = 0; index < bands; ++index) {
        FeedbackCommand command;
        command.rect = QRectF(rect.x() + rect.width() * index / bands, rect.y(), rect.width() / bands, rect.height());
        if (!clip.isNull()) command.rect = command.rect.intersected(clip);
        if (command.rect.isEmpty()) continue;
        command.color = interpolate(stops, (index + 0.5) / bands);
        command.opacity = opacity;
        scene.commands.append(command);
    }
}
bool readImage(const QString &value, const QString &root, QByteArray &bytes, QString &error) {
    if (value.isEmpty()) return true;
    if (value.startsWith("data:image/")) {
        const int marker = value.indexOf(";base64,");
        if (marker < 0 || value.startsWith("data:image/svg")) { error = "Only base64 bitmap data URLs are supported"; return false; }
        bytes = QByteArray::fromBase64(value.mid(marker + 8).toLatin1());
        if (bytes.isEmpty()) { error = "Invalid bitmap data URL"; return false; }
        return true;
    }
    QString path = resolve(root, value);
    if (path.endsWith(".svg", Qt::CaseInsensitive)) path = path.left(path.size() - 4) + ".png";
    if (!QFile::exists(path) && QFileInfo(path).suffix().isEmpty()) {
        for (const auto &extension : {".png", ".jpg", ".jpeg"})
            if (QFile::exists(path + extension)) { path += extension; break; }
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { error = "Cannot read bitmap: " + path; return false; }
    bytes = file.readAll();
    if (bytes.isEmpty()) error = "Empty bitmap: " + path;
    return !bytes.isEmpty();
}
}

bool FeedbackLayout::Load(const QString &path, const QSize &output, QString &error) {
    const QString root = pluginRoot();
    QFile file(resolve(root, path));
    if (!file.open(QIODevice::ReadOnly)) { error = "Cannot read layout: " + path; return false; }
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject()) { error = "Invalid layout JSON: " + parse.errorString(); return false; }
    return Load(document.object(), output, path.startsWith(":/") ? ":/" : root, error);
}
bool FeedbackLayout::Load(const QJsonObject &layout, const QSize &output, const QString &assetRoot, QString &error) {
    if (!validate(layout, error)) return false;
    mLayout = layout;
    mCachedImage.clear();
    mAssetRoot = assetRoot.isEmpty() ? pluginRoot() : assetRoot;
    mOutput = QSize(qBound(72, output.width(), 1024), qBound(72, output.height(), 1024));
    return true;
}
bool FeedbackLayout::LoadFromManifest(const QString &action, QString &error) {
    QFile manifest(QDir(pluginRoot()).filePath("manifest.json"));
    if (!manifest.open(QIODevice::ReadOnly)) { error = "Cannot read manifest.json"; return false; }
    const auto document = QJsonDocument::fromJson(manifest.readAll());
    for (const auto &entry : document.object().value("Actions").toArray()) {
        const auto definition = entry.toObject();
        if (definition.value("UUID").toString() != action) continue;
        const auto path = definition.value("Encoder").toObject().value("layout").toString();
        if (path.isEmpty()) { error = "Action has no Encoder.layout; call SetFeedbackLayout first"; return false; }
        return Load(path, QSize(200, 100), error);
    }
    error = "Action is absent from manifest.json: " + action;
    return false;
}
bool FeedbackLayout::Update(const QJsonObject &feedback, QString &error) {
    if (mLayout.isEmpty()) { error = "SetFeedbackLayout must be called first"; return false; }
    auto items = mLayout.value("items").toArray();
    for (auto patch = feedback.begin(); patch != feedback.end(); ++patch) {
        int index = 0;
        while (index < items.size() && items[index].toObject().value("key").toString() != patch.key()) ++index;
        if (index == items.size()) { error = "Unknown feedback key: " + patch.key(); return false; }
        auto item = items[index].toObject();
        if (patch.value().isObject()) {
            const auto properties = patch.value().toObject();
            for (const auto &fixed : {"key", "type", "rect"})
                if (properties.contains(fixed) && properties.value(fixed) != item.value(fixed)) {
                    error = "Cannot change layout key/type/rect at runtime: " + patch.key(); return false;
                }
            item = merge(item, properties);
        } else item.insert("value", patch.value());
        items[index] = item;
    }
    auto updated = mLayout;
    updated.insert("items", items);
    if (!validate(updated, error)) return false;
    if (updated != mLayout) { mLayout = updated; mCachedImage.clear(); }
    return true;
}
bool FeedbackLayout::Contains(const QString &key) const {
    for (const auto &item : mLayout.value("items").toArray())
        if (item.toObject().value("key").toString() == key) return true;
    return false;
}
QString FeedbackLayout::Render(QString &error) const {
    if (!mCachedImage.isEmpty()) return mCachedImage;
    FeedbackScene scene;
    scene.outputSize = mOutput;
    const auto size = mLayout.value("size").toArray();
    if (size.size() == 2) scene.logicalSize = QSize(size[0].toInt(), size[1].toInt());
    color(mLayout.value("background").toString("black"), scene.background);
    QVector<QJsonObject> items;
    for (const auto &item : mLayout.value("items").toArray()) items.append(item.toObject());
    std::stable_sort(items.begin(), items.end(), [](const QJsonObject &a, const QJsonObject &b) {
        return a.value("zOrder").toInt() < b.value("zOrder").toInt();
    });
    for (const auto &item : items) {
        const double opacity = item.value("opacity").toDouble(1);
        if (!item.value("enabled").toBool(true) || opacity == 0) continue;
        const auto geometry = item.value("rect").toArray();
        const QRectF rect(geometry[0].toDouble(), geometry[1].toDouble(), geometry[2].toDouble(), geometry[3].toDouble());
        const QString type = item.value("type").toString();
        if (item.contains("background")) fill(scene, rect, item.value("background").toString(), opacity);
        if (type == "text" || type == "pixmap") {
            FeedbackCommand command;
            command.type = type == "text" ? FeedbackCommand::Text : FeedbackCommand::Image;
            command.rect = rect; command.opacity = opacity;
            if (type == "text") {
                command.text = item.value("value").toString();
                command.alignment = item.value("alignment").toString("center");
                command.overflow = item.value("text-overflow").toString("clip");
                color(item.value("color").toString("white"), command.color);
                const auto font = item.value("font").toObject();
                command.fontSize = font.value("size").toInt(16);
                command.fontWeight = font.value("weight").toString() == "bold" ? 700 : font.value("weight").toInt(400);
            } else if (!readImage(item.value("value").toString(), mAssetRoot, command.image, error)) return {};
            scene.commands.append(command);
            continue;
        }
        const auto range = item.value("range").toObject();
        const double low = range.value("min").toDouble(0), high = range.value("max").toDouble(100);
        const double fraction = qBound(0.0, (item.value("value").toDouble() - low) / (high - low), 1.0);
        QRectF bar = rect;
        if (type == "gbar") bar.setHeight(qBound(0.0, item.value("bar_h").toDouble(10), rect.height()));
        const double border = qMin(item.value("border_w").toDouble(2), qMin(bar.width(), bar.height()) / 2);
        if (border > 0) fill(scene, bar, item.value("bar_border_c").toString("white"), opacity);
        const QRectF inside = bar.adjusted(border, border, -border, -border);
        if (!inside.isEmpty()) {
            fill(scene, inside, item.value("bar_bg_c").toString("darkGray"), opacity);
            if (type == "bar" && fraction > 0)
                fill(scene, inside, item.value("bar_fill_c").toString("white"), opacity,
                     QRectF(inside.x(), inside.y(), inside.width() * fraction, inside.height()));
        }
        if (type == "gbar" && rect.height() > bar.height()) {
            FeedbackCommand triangle;
            triangle.type = FeedbackCommand::Triangle;
            const double height = rect.height() - bar.height(), width = qMin(12.0, rect.width());
            const double center = qBound(rect.x() + width / 2, rect.x() + rect.width() * fraction, rect.right() - width / 2);
            triangle.rect = QRectF(center - width / 2, bar.bottom(), width, height);
            QVector<Stop> stops;
            brush(item.value("bar_fill_c").toString("white"), stops);
            triangle.color = interpolate(stops, fraction); triangle.opacity = opacity;
            scene.commands.append(triangle);
        }
    }
    const QByteArray png = RenderNativeFeedback(scene);
    if (png.isEmpty()) { error = "Native rendering/PNG encoding failed"; return {}; }
    mCachedImage = "data:image/png;base64," + QString::fromLatin1(png.toBase64());
    return mCachedImage;
}

#if !defined(Q_OS_WIN) && !defined(Q_OS_MAC)
QByteArray RenderNativeFeedback(const FeedbackScene &) { return {}; }
#endif
