#include "Localizer.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QDir>

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
    try {
        QDir pluginDir(qApp->applicationDirPath());
        for (int depth = 0; depth < 6 && !pluginDir.exists("manifest.json"); ++depth) {
            if (!pluginDir.cdUp()) break;
        }
        QString pluginPath = pluginDir.absolutePath() + "/";
        if (!inLanguageCode.isEmpty() && !pluginPath.isEmpty()) {
            QString localizationFilePath = pluginPath + inLanguageCode + ".json";
            QFile file(localizationFilePath);
            if (file.open(QFile::ReadOnly)) {
                QJsonDocument languageJsonDocument = QJsonDocument::fromJson(file.readAll());
                QJsonObject languageJsonObject = languageJsonDocument.object();
                if (languageJsonObject.contains("Localization")) {
                    mLocalizationData = languageJsonObject.value("Localization").toObject();
                } else {
                    mLocalizationData = QJsonObject();
                }
            }
            file.close();
        }
    } catch (...) {
    }
}

QString Localizer::GetLocalizedStringIntern(const QString &inDefaultString)
{
    return mLocalizationData.value(inDefaultString).toString(inDefaultString);
}
