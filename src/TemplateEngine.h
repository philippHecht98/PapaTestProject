/**
 * TemplateEngine.h
 *
 * Declares the TemplateEngine class, which is responsible for:
 *
 *  1. Listing available templates (.docx files in the templates directory)
 *  2. Loading a template, filling it with data, and saving a new DOCX file
 *
 * Template marker syntax (used inside the Word document):
 *
 *   {{variable_name}}           — Simple text substitution
 *   {{#if conditionName}}       — Start of a conditional block (own paragraph)
 *   {{/if}}                     — End of a conditional block (own paragraph)
 *   {{#each arrayName}}         — Marks a table row as a repeating row (one per array item)
 *   {{item.fieldName}}          — Field reference inside a repeating row
 *
 * Example template text in Word:
 *   "Dear {{client_name}}, your invoice total is {{total}}."
 *
 * Example table row (one row in the Word table):
 *   | {{#each items}}{{item.description}} | {{item.price}} |
 *   This row will be duplicated once per item in the "items" array.
 *
 * Example conditional block (each marker on its own paragraph in Word):
 *   {{#if show_discount}}
 *   Discount applied: {{discount}}
 *   {{/if}}
 */

#pragma once

#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QDomDocument>

class TemplateEngine : public QObject
{
    Q_OBJECT

public:
    /**
     * Constructor.
     * @param config  Application configuration from config.json.
     * @param parent  Qt parent for memory management.
     */
    explicit TemplateEngine(const QJsonObject &config, QObject *parent = nullptr);

    /**
     * Returns a list of available template names (without the .docx extension).
     * These correspond to files found in the configured templates directory.
     */
    QStringList listTemplates() const;

    /**
     * Processes a template with the provided data and writes a new DOCX file.
     *
     * @param templateName  Name of the template (without .docx extension).
     * @param data          JSON object containing the values for all markers.
     * @return              Path to the generated (filled) DOCX file,
     *                      or an empty string if something went wrong.
     */
    QString generateDocument(const QString &templateName, const QJsonObject &data);

private:
    QString m_templatesDir;  // Directory where .docx template files are stored
    QString m_tempDir;       // Directory for temporary extraction/output files

    // --- DOCX ZIP helpers ---

    /**
     * Extracts a DOCX (ZIP) file into a directory.
     * DOCX files are ZIP archives containing XML files.
     */
    bool extractDocx(const QString &docxPath, const QString &destDir);

    /**
     * Repacks a directory back into a DOCX (ZIP) file.
     */
    bool repackDocx(const QString &sourceDir, const QString &outputPath);

    // --- XML processing pipeline ---

    /**
     * Runs the full template processing pipeline on word/document.xml content.
     * Calls all sub-steps below in order.
     */
    QString processXml(const QString &xmlContent, const QJsonObject &data);

    /**
     * Merges split text runs within paragraphs that contain markers.
     *
     * Problem: Word sometimes splits a single word across multiple XML <w:r>
     * (run) elements. For example, typing "{{name}}" might produce:
     *   <w:r><w:t>{{na</w:t></w:r><w:r><w:t>me}}</w:t></w:r>
     * This makes marker detection impossible. This function detects paragraphs
     * that contain "{{" and merges all their runs into a single run so that
     * markers are intact.
     */
    void normalizeTextRuns(QDomDocument &doc);

    /**
     * Processes {{#if condition}} ... {{/if}} blocks.
     *
     * Rules:
     *  - {{#if conditionName}} must be the only text in its paragraph.
     *  - {{/if}} must be the only text in its paragraph.
     *  - If data["conditionName"] is true, the marker paragraphs are removed
     *    and the content paragraphs between them are kept.
     *  - If data["conditionName"] is false (or missing), all paragraphs
     *    including the content are removed.
     */
    void processConditionals(QDomDocument &doc, const QJsonObject &data);

    /**
     * Processes {{#each arrayName}} table row repetition.
     *
     * Rules:
     *  - Place {{#each arrayName}} inside the first cell of the template row.
     *    You can combine it with a field marker: {{#each items}}{{item.name}}
     *  - Other cells in the same row use {{item.fieldName}} markers.
     *  - The engine clones the row once for each element in data["arrayName"],
     *    replacing {{item.xxx}} with each element's fields.
     *  - The original template row is removed.
     */
    void processTableLoops(QDomDocument &doc, const QJsonObject &data);

    /**
     * Replaces all remaining {{variableName}} markers with their values from data.
     * This is a simple string replacement done after DOM processing.
     */
    QString replaceSimpleMarkers(const QString &xmlText, const QJsonObject &data);
};
