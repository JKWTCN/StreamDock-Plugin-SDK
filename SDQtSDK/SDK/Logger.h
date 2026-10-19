#ifndef LOGGER_H
#define LOGGER_H

#include <QString>

class ConnectionManager;

class Logger
{
public:
    Logger() = delete;
    using FileLogHandler = void (*)(const QString &message);
    static void SetConnectionManager(ConnectionManager *connection);
    // Configure once at startup. A null handler restores Qt's default log output.
    static void SetFileLogHandler(FileLogHandler handler);

    static void LogToServer(const QString &message);
    // The application decides the logging backend, file location and rotation policy.
    static void LogToFile(const QString &message);
};

#endif // LOGGER_H
