/**
 * PdfConverter.cpp
 *
 * Implementation of the LibreOffice-based DOCX-to-PDF converter.
 *
 * LibreOffice is invoked as a subprocess with the following arguments:
 *   --headless          Run without any graphical user interface
 *   --convert-to pdf    Output format
 *   --outdir <dir>      Directory where the PDF will be written
 *   <inputFile>         The DOCX file to convert
 *
 * LibreOffice names the output file after the input file, replacing
 * the extension with .pdf. For example:
 *   output_invoice_1234567890.docx → output_invoice_1234567890.pdf
 */

#include "PdfConverter.h"

#include <QProcess>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDebug>

PdfConverter::PdfConverter(const QJsonObject &config, QObject *parent)
    : QObject(parent)
{
    QJsonObject paths = config["paths"].toObject();

    // Read LibreOffice path from config, with a sensible Windows default
    m_libreOfficePath = paths["libreOffice"].toString(
        "C:\\Program Files\\LibreOffice\\program\\soffice.exe");

    m_tempDir = paths["temp"].toString("./temp");
}

// ---------------------------------------------------------------------------
// convert()
// Converts a DOCX file to PDF using LibreOffice headless mode.
// Returns the path to the generated PDF, or empty string on failure.
// ---------------------------------------------------------------------------
QString PdfConverter::convert(const QString &docxPath)
{
    // Verify the input file actually exists before attempting conversion
    if (!QFile::exists(docxPath)) {
        qWarning() << "[PdfConverter] Input file does not exist:" << docxPath;
        return {};
    }

    // Verify LibreOffice is installed at the configured path
    if (!QFile::exists(m_libreOfficePath)) {
        qWarning() << "[PdfConverter] LibreOffice not found at:" << m_libreOfficePath;
        qWarning() << "              Check the 'paths.libreOffice' setting in config.json.";
        return {};
    }

    // Build the LibreOffice command-line arguments
    QStringList args;
    args << "--headless"               // No GUI — required for server-side use
         << "--convert-to" << "pdf"    // Output format
         << "--outdir" << m_tempDir    // Where to write the output PDF
         << docxPath;                  // Input DOCX file

    qInfo() << "[PdfConverter] Running LibreOffice conversion for:" << docxPath;

    // Start LibreOffice as a subprocess and wait for it to finish.
    // We allow up to 60 seconds — large documents may take a moment.
    QProcess process;
    process.start(m_libreOfficePath, args);

    if (!process.waitForStarted(5000)) {
        qWarning() << "[PdfConverter] LibreOffice process failed to start.";
        qWarning() << "              Error:" << process.errorString();
        return {};
    }

    if (!process.waitForFinished(60000)) {
        qWarning() << "[PdfConverter] LibreOffice conversion timed out after 60 seconds.";
        process.kill();
        return {};
    }

    // Check if LibreOffice reported an error
    if (process.exitCode() != 0) {
        qWarning() << "[PdfConverter] LibreOffice exited with code" << process.exitCode();
        qWarning() << "              stderr:" << process.readAllStandardError();
        return {};
    }

    // LibreOffice writes the PDF to m_tempDir with the same base name as the input.
    // For example: output_invoice_123.docx → <tempDir>/output_invoice_123.pdf
    QFileInfo docxInfo(docxPath);
    QString pdfPath = m_tempDir + "/" + docxInfo.completeBaseName() + ".pdf";

    if (!QFile::exists(pdfPath)) {
        qWarning() << "[PdfConverter] Expected PDF not found at:" << pdfPath;
        qWarning() << "              LibreOffice stdout:" << process.readAllStandardOutput();
        return {};
    }

    qInfo() << "[PdfConverter] PDF created successfully:" << pdfPath;
    return pdfPath;
}
