/**
 * HttpServer.cpp
 *
 * Implementation of the HTTP API server.
 *
 * This file wires up the REST endpoints and delegates the actual work to
 * TemplateEngine (DOCX processing) and PdfConverter (PDF generation).
 *
 * API summary:
 *
 *   GET /api/templates
 *     Returns: { "templates": ["invoice", "letter", ...] }
 *
 *   POST /api/generate
 *     Body:    { "template": "invoice", "filename": "out.pdf", "data": { ... } }
 *     Returns: PDF binary (Content-Type: application/pdf)
 *              or JSON error with appropriate HTTP status code
 */

#include "HttpServer.h"

#include <QHttpServerRequest>
#include <QHttpServerResponse>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDebug>

HttpServer::HttpServer(const QJsonObject &config, QObject *parent)
    : QObject(parent)
    , m_config(config)
    , m_templateEngine(config)
    , m_pdfConverter(config)
{}

bool HttpServer::start()
{
    // -----------------------------------------------------------------------
    // Route: GET /api/templates
    // Returns a JSON list of all .docx files found in the templates directory.
    // Clients call this on startup to populate their template selection UI.
    // -----------------------------------------------------------------------
    m_server.route("/api/templates", QHttpServerRequest::Method::Get,
        [this](const QHttpServerRequest &request) {
            return handleGetTemplates(request);
        });

    // -----------------------------------------------------------------------
    // Route: POST /api/generate
    // Accepts a JSON body with template name + data, returns a PDF binary.
    // -----------------------------------------------------------------------
    m_server.route("/api/generate", QHttpServerRequest::Method::Post,
        [this](const QHttpServerRequest &request) {
            return handleGenerateReport(request);
        });

    // Read the desired port from config (defaults to 8080 if not set)
    int port = m_config["server"].toObject()["port"].toInt(8080);

    // Start listening on all network interfaces (0.0.0.0) so LAN clients
    // on the same network can reach the server, not just localhost.
    quint16 boundPort = m_server.listen(QHostAddress::Any, static_cast<quint16>(port));
    if (boundPort == 0) {
        qCritical() << "[HttpServer] Failed to bind to port" << port
                    << "— is another process already using it?";
        return false;
    }

    qInfo() << "[HttpServer] Listening on port" << boundPort;
    return true;
}

// ---------------------------------------------------------------------------
// GET /api/templates
// ---------------------------------------------------------------------------
QHttpServerResponse HttpServer::handleGetTemplates(const QHttpServerRequest &request)
{
    Q_UNUSED(request)

    QStringList templates = m_templateEngine.listTemplates();

    // Build a JSON array from the template name list
    QJsonArray templateArray;
    for (const QString &name : templates) {
        templateArray.append(name);
    }

    QJsonObject responseObj;
    responseObj["templates"] = templateArray;

    qInfo() << "[HttpServer] GET /api/templates — returning" << templates.size() << "template(s)";

    return QHttpServerResponse(QJsonDocument(responseObj).toJson(QJsonDocument::Compact),
                               "application/json");
}

// ---------------------------------------------------------------------------
// POST /api/generate
//
// Expected request body (JSON):
// {
//   "template": "invoice",           // Required: name of the .docx template (no extension)
//   "filename": "my_invoice.pdf",    // Optional: desired PDF filename in the response headers
//   "data": {                        // Required: key-value data to fill into the template
//     "client_name": "John Doe",
//     "total": "€ 500",
//     "items": [                     // Arrays generate repeated table rows
//       { "description": "Web design", "price": "€ 300" },
//       { "description": "Hosting",    "price": "€ 200" }
//     ],
//     "show_vat": true               // Booleans control conditional sections
//   }
// }
// ---------------------------------------------------------------------------
QHttpServerResponse HttpServer::handleGenerateReport(const QHttpServerRequest &request)
{
    // --- Parse JSON body ---
    QJsonParseError parseError;
    QJsonDocument requestDoc = QJsonDocument::fromJson(request.body(), &parseError);

    if (parseError.error != QJsonParseError::NoError) {
        return jsonError("Invalid JSON in request body: " + parseError.errorString(),
                         QHttpServerResponse::StatusCode::BadRequest);
    }

    if (!requestDoc.isObject()) {
        return jsonError("Request body must be a JSON object.",
                         QHttpServerResponse::StatusCode::BadRequest);
    }

    QJsonObject body = requestDoc.object();

    // --- Validate required fields ---
    if (!body.contains("template") || body["template"].toString().isEmpty()) {
        return jsonError("Missing required field: 'template'",
                         QHttpServerResponse::StatusCode::BadRequest);
    }

    if (!body.contains("data") || !body["data"].isObject()) {
        return jsonError("Missing required field: 'data' (must be a JSON object)",
                         QHttpServerResponse::StatusCode::BadRequest);
    }

    QString templateName = body["template"].toString();
    QJsonObject data     = body["data"].toObject();
    QString filename     = body.value("filename").toString("report.pdf");

    qInfo() << "[HttpServer] POST /api/generate — template:" << templateName;

    // --- Step 1: Fill the DOCX template with the provided data ---
    // The TemplateEngine extracts the DOCX, replaces all markers in the XML,
    // and repacks it into a new temporary DOCX file.
    QString processedDocxPath = m_templateEngine.generateDocument(templateName, data);
    if (processedDocxPath.isEmpty()) {
        return jsonError("Failed to process template '" + templateName + "'. "
                         "Check that the template file exists in the templates folder.",
                         QHttpServerResponse::StatusCode::InternalServerError);
    }

    // --- Step 2: Convert the filled DOCX to PDF via LibreOffice ---
    QString pdfPath = m_pdfConverter.convert(processedDocxPath);

    // Clean up the temporary DOCX regardless of PDF conversion result
    QFile::remove(processedDocxPath);

    if (pdfPath.isEmpty()) {
        return jsonError("PDF conversion failed. "
                         "Make sure LibreOffice is installed and the path in config.json is correct.",
                         QHttpServerResponse::StatusCode::InternalServerError);
    }

    // --- Step 3: Read the PDF and return it as a binary HTTP response ---
    QFile pdfFile(pdfPath);
    if (!pdfFile.open(QIODevice::ReadOnly)) {
        QFile::remove(pdfPath);
        return jsonError("Could not read the generated PDF file.",
                         QHttpServerResponse::StatusCode::InternalServerError);
    }

    QByteArray pdfData = pdfFile.readAll();
    pdfFile.close();
    QFile::remove(pdfPath);  // Clean up the temporary PDF

    qInfo() << "[HttpServer] PDF generated successfully —" << pdfData.size() << "bytes";

    // Return the PDF binary with appropriate headers so browsers/clients
    // know to treat it as a downloadable file.
    QHttpServerResponse response(pdfData, "application/pdf");
    response.setHeader("Content-Disposition",
                       QString("attachment; filename=\"%1\"").arg(filename).toUtf8());
    return response;
}

// ---------------------------------------------------------------------------
// Helper: Build a JSON error response
// ---------------------------------------------------------------------------
QHttpServerResponse HttpServer::jsonError(const QString &message,
                                          QHttpServerResponse::StatusCode status)
{
    qWarning() << "[HttpServer] Error:" << message;

    QJsonObject errorObj;
    errorObj["error"] = message;

    QHttpServerResponse response(QJsonDocument(errorObj).toJson(QJsonDocument::Compact),
                                 "application/json");
    response.setStatusCode(status);
    return response;
}
