/**
 * HttpServer.h
 *
 * Declares the HttpServer class, which sets up the HTTP API and
 * routes incoming requests to the appropriate handler functions.
 *
 * Available endpoints:
 *   GET  /api/templates   — List all available report templates
 *   POST /api/generate    — Generate a PDF from a template and provided data
 */

#pragma once

#include <QObject>
#include <QHttpServer>
#include <QJsonObject>

#include "TemplateEngine.h"
#include "PdfConverter.h"

class HttpServer : public QObject
{
    Q_OBJECT

public:
    /**
     * Constructor.
     * @param config  The full configuration object loaded from config.json.
     * @param parent  Qt parent object (for automatic memory management).
     */
    explicit HttpServer(const QJsonObject &config, QObject *parent = nullptr);

    /**
     * Registers all API routes and starts listening for connections.
     * @return true if the server started successfully, false otherwise.
     */
    bool start();

private:
    QJsonObject     m_config;          // Full application configuration
    QHttpServer     m_server;          // Qt's built-in HTTP server
    TemplateEngine  m_templateEngine;  // Handles DOCX template processing
    PdfConverter    m_pdfConverter;    // Handles conversion to PDF via LibreOffice

    // --- Route handler methods ---

    /** Handles GET /api/templates — returns the list of available templates. */
    QHttpServerResponse handleGetTemplates(const QHttpServerRequest &request);

    /** Handles POST /api/generate — processes a template and returns a PDF. */
    QHttpServerResponse handleGenerateReport(const QHttpServerRequest &request);

    // --- Helper methods ---

    /** Creates a JSON error response with the given message and HTTP status code. */
    QHttpServerResponse jsonError(const QString &message,
                                  QHttpServerResponse::StatusCode status);
};
