/**
 * PdfConverter.h
 *
 * Declares the PdfConverter class, which converts DOCX files to PDF
 * using LibreOffice in headless (no GUI) mode.
 *
 * LibreOffice is a free, open-source office suite available for Windows,
 * macOS, and Linux. When run with the --headless flag, it can be used as
 * a command-line document converter without opening any windows.
 *
 * The LibreOffice executable path must be configured in config.json under:
 *   paths.libreOffice
 *
 * Example (Windows default install path):
 *   "C:\\Program Files\\LibreOffice\\program\\soffice.exe"
 */

#pragma once

#include <QObject>
#include <QJsonObject>

class PdfConverter : public QObject
{
    Q_OBJECT

public:
    /**
     * Constructor.
     * @param config  Application configuration from config.json.
     * @param parent  Qt parent for memory management.
     */
    explicit PdfConverter(const QJsonObject &config, QObject *parent = nullptr);

    /**
     * Converts a DOCX file to PDF using LibreOffice headless.
     *
     * Runs the following command:
     *   soffice.exe --headless --convert-to pdf --outdir <tempDir> <docxPath>
     *
     * @param docxPath  Absolute path to the input DOCX file.
     * @return          Absolute path to the generated PDF file,
     *                  or an empty string if conversion failed.
     */
    QString convert(const QString &docxPath);

private:
    QString m_libreOfficePath;  // Full path to soffice.exe (from config.json)
    QString m_tempDir;          // Output directory for the generated PDF
};
