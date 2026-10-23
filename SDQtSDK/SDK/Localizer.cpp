#include "Localizer.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QDir>
#include <QStringList>

static Localizer *sLocalizer = nullptr;

void Localizer::Initialize(const QString &inLanguageCode)
{
    if (!sLocalizer) {
        sLocalizer = new Localizer(inLanguageCode);
    }
}

QString Localizer::GetLocalizedString(const QString &inDefaultString)
{
    if (sLocalizer) {
      return sLocalizer->GetLocalizedStringIntern(inDefaultString);
    }

    return inDefaultString;
}

Localizer::Localizer(const QString &inLanguageCode)
{
    QDir pluginDir(qApp->applicationDirPath());
    for (int depth = 0; depth < 6 && !pluginDir.exists("manifest.json"); ++depth) {
        if (!pluginDir.cdUp()) break;
    }
    const QString primary = QString(inLanguageCode).trimmed().toLower().replace('-', '_').section('_', 0, 0);
    const QStringList supported{"en", "de", "es", "fr", "ja", "ko"};
    const QString language = primary == "zh" ? "zh_CN" : supported.contains(primary) ? primary : "en";
    auto load = [&pluginDir](const QString &code) {
        QFile file(pluginDir.filePath(code + ".json"));
        if (!file.open(QFile::ReadOnly)) return QJsonObject();
        return QJsonDocument::fromJson(file.readAll()).object().value("Localization").toObject();
    };
    mLocalizationData = load("en");
    if (language != "en") {
        const auto translated = load(language);
        for (auto it = translated.begin(); it != translated.end(); ++it)
            if (it.value().isString() && !it.value().toString().isEmpty()) mLocalizationData.insert(it.key(), it.value());
    }
}

QString Localizer::GetLocalizedStringIntern(const QString &inDefaultString)
{
    return mLocalizationData.value(inDefaultString).toString(inDefaultString);
}
