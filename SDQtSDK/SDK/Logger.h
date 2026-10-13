#ifndef LOGGER_H
#define LOGGER_H

#include <QString>

class ConnectionManager;

class Logger
{
public:
    Logger() = delete;
    static void SetConnectionManager(ConnectionManager *connection);

    static void LogToServer(const QString &message);
    // UTF-8 logs under the plugin's logs/photoshop_YYYY-MM-DD.log; rotates at local midnight.
    static void LogToFile(const QString &message);
};

#endif // LOGGER_H
