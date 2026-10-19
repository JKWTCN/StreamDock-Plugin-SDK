#include "Logger.h"
#include "ConnectionManager.h"
#include <QDebug>

namespace {
    ConnectionManager *mConnectionManager = nullptr;
    Logger::FileLogHandler fileLogHandler = nullptr;
}

void Logger::SetConnectionManager(ConnectionManager *connection)
{
    mConnectionManager = connection;
}

void Logger::SetFileLogHandler(FileLogHandler handler)
{
    fileLogHandler = handler;
}

void Logger::LogToServer(const QString &message)
{
    if (mConnectionManager) {
        mConnectionManager->LogMessage(message);
    }
}

void Logger::LogToFile(const QString &message)
{
    if (fileLogHandler) {
        fileLogHandler(message);
    } else {
        qInfo().noquote() << message;
    }
}
