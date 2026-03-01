/**
 * main.cpp
 *
 * Entry point of the ReportServer application.
 *
 * This server provides an HTTP API for generating PDF reports from
 * Word (.docx) templates. Clients send a template name and data (as JSON),
 * and receive a ready-to-download PDF in response.
 *
 * Flow:
 *   1. Load configuration from config.json
 *   2. Create and start the HTTP server
 *   3. Run the Qt event loop (keeps the server alive)
 */

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDir>
#include <QDebug>

#include "HttpServer.h"

int main(int argc, char *argv[])
{
    // QCoreApplication sets up the Qt framework without a GUI.
    // It also provides the event loop that keeps the server running.
    QCoreApplication app(argc, argv);
    app.setApplicationName("ReportServer");
    app.setApplicationVersion("1.0.0");

    // --- Load config.json ---
    // The config file must be in the same directory as the executable.
    QFile configFile("config.json");
    if (!configFile.open(QIODevice::ReadOnly)) {
        qCritical() << "[ERROR] Cannot open config.json.";
        qCritical() << "        Make sure config.json is in the same folder as the executable.";
        return 1;
    }

    QJsonParseError parseError;
    QJsonDocument configDoc = QJsonDocument::fromJson(configFile.readAll(), &parseError);
    configFile.close();

    if (parseError.error != QJsonParseError::NoError) {
        qCritical() << "[ERROR] config.json is not valid JSON:" << parseError.errorString();
        return 1;
    }

    QJsonObject config = configDoc.object();

    // --- Ensure the temp directory exists ---
    // Temporary files (processed DOCX, generated PDFs) are stored here.
    QString tempDir = config["paths"].toObject()["temp"].toString("./temp");
    if (!QDir().mkpath(tempDir)) {
        qCritical() << "[ERROR] Could not create temp directory:" << tempDir;
        return 1;
    }

    // --- Start the HTTP server ---
    HttpServer server(config);
    if (!server.start()) {
        qCritical() << "[ERROR] Failed to start HTTP server. Is the port already in use?";
        return 1;
    }

    int port = config["server"].toObject()["port"].toInt(8080);
    qInfo() << "========================================";
    qInfo() << "  ReportServer running on port" << port;
    qInfo() << "  Templates dir:" << config["paths"].toObject()["templates"].toString();
    qInfo() << "========================================";

    // Start the Qt event loop. This call blocks until the application exits.
    // The server handles incoming HTTP requests asynchronously within this loop.
    return app.exec();
}
