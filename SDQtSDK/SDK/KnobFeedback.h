#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QVector>

// Platform-independent drawing commands. All geometry uses top-left layout coordinates.
struct FeedbackCommand {
    enum Type { Fill, Triangle, Text, Image } type = Fill;
    QRectF rect;
    quint32 color = 0xffffffff; // ARGB
    QString text, alignment = "center", overflow = "clip";
    int fontSize = 16, fontWeight = 400;
    QByteArray image;
    double opacity = 1;
};

struct FeedbackScene {
    QSize logicalSize = QSize(200, 100), outputSize = QSize(200, 100);
    quint32 background = 0xff000000;
    QVector<FeedbackCommand> commands;
};

// One instance belongs to one action context; updates never affect another dial.
class FeedbackLayout {
public:
    bool Load(const QString &path, const QSize &output, QString &error);
    bool Load(const QJsonObject &layout, const QSize &output, const QString &assetRoot, QString &error);
    bool LoadFromManifest(const QString &action, QString &error);
    bool IsLoaded() const { return !mLayout.isEmpty(); }
    bool Update(const QJsonObject &feedback, QString &error);
    bool Contains(const QString &key) const;
    QString Render(QString &error) const;
private:
    QJsonObject mLayout;
    QString mAssetRoot;
    QSize mOutput;
    mutable QString mCachedImage;
};

QByteArray RenderNativeFeedback(const FeedbackScene &scene);
