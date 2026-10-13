#include "Logger.h"
#include <ConnectionManager.h>
#include <QCoreApplication>
#include <QDir>
#include <QDebug>
#include <spdlog/logger.h>
#include <spdlog/sinks/daily_file_sink.h>
#include <memory>
#include <stdexcept>

namespace {
    ConnectionManager *mConnectionManager = nullptr;

    std::shared_ptr<spdlog::logger> fileLogger()
    {
        // Function-local initialization and the _mt sink protect concurrent RPC logs.
        // If initialization throws, the next call retries instead of caching a failure.
        static const auto logger = [] {
            const QDir executableDir(QCoreApplication::applicationDirPath());
            QDir pluginDir(executableDir);
            for (int depth = 0; depth < 6 && !pluginDir.exists("manifest.json"); ++depth) {
                if (!pluginDir.cdUp()) break;
            }
            if (!pluginDir.exists("manifest.json")) pluginDir = executableDir;
            const QString logDirectory = pluginDir.filePath("logs");
            if (!QDir().mkpath(logDirectory))
                throw std::runtime_error("Cannot create log directory: " + logDirectory.toUtf8().toStdString());
            const QString logPath = QDir(logDirectory).filePath("photoshop.log");
#if defined(_WIN32) && defined(SPDLOG_WCHAR_FILENAMES)
            const auto filename = logPath.toStdWString();
#else
            const auto filename = logPath.toUtf8().toStdString();
#endif
            // Local midnight, append on restart, no automatic deletion of older days.
            auto sink = std::make_shared<spdlog::sinks::daily_file_sink_mt>(filename, 0, 0, false, 0);
            auto result = std::make_shared<spdlog::logger>("photoshop", sink);
            result->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [thread %t] %v");
            result->flush_on(spdlog::level::info);
            result->set_error_handler([](const std::string &error) {
                // Do not recurse into LogToFile if the disk cannot be written.
                qWarning().noquote() << "[Photoshop Logger]" << QString::fromUtf8(error.c_str());
            });
            return result;
        }();
        return logger;
    }
}// namespace

void Logger::SetConnectionManager(ConnectionManager *connection)
{
    mConnectionManager = connection;
}

void Logger::LogToServer(const QString &message)
{
    if (mConnectionManager) {
        mConnectionManager->LogMessage(message);
    }
}

void Logger::LogToFile(const QString &message)
{
    try {
        fileLogger()->info("{}", message.toUtf8().toStdString());
    } catch (const std::exception &error) {
        qWarning().noquote() << "[Photoshop Logger]" << QString::fromUtf8(error.what());
    }
}
