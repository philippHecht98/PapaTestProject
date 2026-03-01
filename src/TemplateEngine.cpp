/**
 * TemplateEngine.cpp
 *
 * Implementation of the DOCX template processing engine.
 *
 * Processing pipeline for each report generation request:
 *
 *  1. extractDocx()         — Unzip the .docx template into a temp directory
 *  2. processXml()          — Transform word/document.xml:
 *       a. normalizeTextRuns()   — Fix Word's run-splitting of markers
 *       b. processConditionals() — Handle {{#if}} / {{/if}} blocks
 *       c. processTableLoops()   — Handle {{#each}} repeating table rows
 *       d. replaceSimpleMarkers()— Replace {{variableName}} with values
 *  3. repackDocx()          — Zip the modified files back into a .docx
 */

#include "TemplateEngine.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QDomDocument>
#include <QRegularExpression>
#include <QJsonValue>
#include <QDebug>

// QuaZip provides Qt-friendly ZIP file read/write support.
// JlCompress is a high-level helper that wraps common operations.
#include <JlCompress.h>

TemplateEngine::TemplateEngine(const QJsonObject &config, QObject *parent)
    : QObject(parent)
{
    QJsonObject paths = config["paths"].toObject();
    m_templatesDir = paths["templates"].toString("./templates");
    m_tempDir      = paths["temp"].toString("./temp");
}

// ---------------------------------------------------------------------------
// listTemplates()
// Scans the templates directory and returns all .docx filenames (without
// the extension) as a list of template names.
// ---------------------------------------------------------------------------
QStringList TemplateEngine::listTemplates() const
{
    QDir dir(m_templatesDir);
    if (!dir.exists()) {
        qWarning() << "[TemplateEngine] Templates directory not found:" << m_templatesDir;
        return {};
    }

    // Filter for .docx files only
    QStringList filters = { "*.docx" };
    QStringList files   = dir.entryList(filters, QDir::Files | QDir::Readable);

    // Strip the .docx extension to return clean template names
    QStringList templateNames;
    for (const QString &file : files) {
        templateNames.append(QFileInfo(file).completeBaseName());
    }

    return templateNames;
}

// ---------------------------------------------------------------------------
// generateDocument()
// Main entry point for producing a filled DOCX from a template + data.
// Returns the path to the resulting .docx file, or empty string on failure.
// ---------------------------------------------------------------------------
QString TemplateEngine::generateDocument(const QString &templateName, const QJsonObject &data)
{
    // Build the path to the template file
    QString templatePath = m_templatesDir + "/" + templateName + ".docx";
    if (!QFile::exists(templatePath)) {
        qWarning() << "[TemplateEngine] Template not found:" << templatePath;
        return {};
    }

    // Create a unique temporary directory for extracting this DOCX.
    // We use a timestamp to ensure no two simultaneous requests collide.
    QString uniqueId     = QString::number(QDateTime::currentMSecsSinceEpoch());
    QString extractDir   = m_tempDir + "/extract_" + templateName + "_" + uniqueId;
    QDir().mkpath(extractDir);

    // --- Step 1: Extract the DOCX (it's a ZIP archive) ---
    if (!extractDocx(templatePath, extractDir)) {
        qWarning() << "[TemplateEngine] Failed to extract:" << templatePath;
        QDir(extractDir).removeRecursively();
        return {};
    }

    // --- Step 2: Process word/document.xml ---
    // This is the main XML file that contains the document body content.
    QString documentXmlPath = extractDir + "/word/document.xml";
    QFile documentXmlFile(documentXmlPath);

    if (!documentXmlFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "[TemplateEngine] Cannot open word/document.xml in extracted DOCX";
        QDir(extractDir).removeRecursively();
        return {};
    }

    QString originalXml = QString::fromUtf8(documentXmlFile.readAll());
    documentXmlFile.close();

    // Run the full template processing pipeline
    QString processedXml = processXml(originalXml, data);

    // Write the processed XML back to the extracted directory
    if (!documentXmlFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        qWarning() << "[TemplateEngine] Cannot write processed word/document.xml";
        QDir(extractDir).removeRecursively();
        return {};
    }
    documentXmlFile.write(processedXml.toUtf8());
    documentXmlFile.close();

    // --- Step 3: Repack the directory back into a DOCX ---
    QString outputDocxPath = m_tempDir + "/output_" + templateName + "_" + uniqueId + ".docx";
    if (!repackDocx(extractDir, outputDocxPath)) {
        qWarning() << "[TemplateEngine] Failed to repack DOCX";
        QDir(extractDir).removeRecursively();
        return {};
    }

    // Clean up the temporary extraction directory
    QDir(extractDir).removeRecursively();

    return outputDocxPath;
}

// ---------------------------------------------------------------------------
// extractDocx()
// Uses QuaZip's JlCompress helper to extract a ZIP/DOCX file.
// Returns true on success.
// ---------------------------------------------------------------------------
bool TemplateEngine::extractDocx(const QString &docxPath, const QString &destDir)
{
    // JlCompress::extractDir() extracts all files from the ZIP into destDir.
    // It returns a list of extracted file paths, or an empty list on failure.
    QStringList extracted = JlCompress::extractDir(docxPath, destDir);
    if (extracted.isEmpty()) {
        qWarning() << "[TemplateEngine] JlCompress::extractDir failed for:" << docxPath;
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// repackDocx()
// Uses QuaZip's JlCompress helper to compress a directory into a ZIP/DOCX.
// Returns true on success.
// ---------------------------------------------------------------------------
bool TemplateEngine::repackDocx(const QString &sourceDir, const QString &outputPath)
{
    // JlCompress::compressDir() zips the entire sourceDir into outputPath.
    // The second argument (true) means include subdirectories recursively.
    bool ok = JlCompress::compressDir(outputPath, sourceDir, true);
    if (!ok) {
        qWarning() << "[TemplateEngine] JlCompress::compressDir failed for:" << sourceDir;
    }
    return ok;
}

// ---------------------------------------------------------------------------
// processXml()
// Runs the full template processing pipeline on the document XML string.
// Returns the fully processed XML string ready to be written back to disk.
// ---------------------------------------------------------------------------
QString TemplateEngine::processXml(const QString &xmlContent, const QJsonObject &data)
{
    // Parse the XML string into a DOM tree for structured manipulation
    QDomDocument doc;
    QString errorMsg;
    int errorLine = 0, errorCol = 0;

    if (!doc.setContent(xmlContent, &errorMsg, &errorLine, &errorCol)) {
        qWarning() << "[TemplateEngine] XML parse error at line" << errorLine
                   << "col" << errorCol << ":" << errorMsg;
        // Return the original content unchanged if parsing fails
        return xmlContent;
    }

    // Step a: Fix run splitting (see normalizeTextRuns() header comment)
    normalizeTextRuns(doc);

    // Step b: Handle {{#if condition}} ... {{/if}} conditional sections
    processConditionals(doc, data);

    // Step c: Handle {{#each array}} repeating table rows
    processTableLoops(doc, data);

    // Step d: Serialize back to string, then do simple marker substitution
    QString processedXml = doc.toString(/* indent = */ -1);  // -1 = compact, no added whitespace
    processedXml = replaceSimpleMarkers(processedXml, data);

    return processedXml;
}

// ---------------------------------------------------------------------------
// normalizeTextRuns()
//
// Word often splits text across multiple <w:r> XML runs. For example, when
// you type "{{name}}" and make corrections, it may become:
//   <w:r><w:t>{{na</w:t></w:r><w:r><w:t>me}}</w:t></w:r>
//
// This function finds every paragraph (<w:p>) whose combined text contains
// "{{" and merges all its runs into a single run. This ensures markers are
// intact and can be detected by the later processing steps.
//
// Note: This only affects paragraphs that contain markers. Formatting within
// those paragraphs may be simplified (inheriting the first run's style).
// ---------------------------------------------------------------------------
void TemplateEngine::normalizeTextRuns(QDomDocument &doc)
{
    // Get all paragraph elements in the document
    QDomNodeList paragraphs = doc.elementsByTagName("w:p");

    for (int i = 0; i < paragraphs.count(); i++) {
        QDomElement para = paragraphs.at(i).toElement();

        // Collect all text runs (<w:r>) in this paragraph
        QDomNodeList runs = para.elementsByTagName("w:r");
        if (runs.count() < 2) continue;  // Nothing to merge if only 0 or 1 runs

        // Concatenate the text from all runs in this paragraph
        QString fullText;
        for (int j = 0; j < runs.count(); j++) {
            QDomNodeList texts = runs.at(j).toElement().elementsByTagName("w:t");
            for (int k = 0; k < texts.count(); k++) {
                fullText += texts.at(k).toElement().text();
            }
        }

        // Only normalize paragraphs that contain at least the start of a marker
        if (!fullText.contains("{{")) continue;

        // Save the formatting properties (<w:rPr>) of the first run so we can
        // apply them to the merged run (keeps bold/italic/font of the first run)
        QDomElement firstRun      = runs.at(0).toElement();
        QDomElement firstRunProps = firstRun.firstChildElement("w:rPr");

        // Collect all run nodes into a separate list before removing them,
        // because elementsByTagName() returns a live list that changes on removal.
        QList<QDomNode> runsToRemove;
        for (int j = 0; j < runs.count(); j++) {
            runsToRemove.append(runs.at(j));
        }
        for (QDomNode &node : runsToRemove) {
            para.removeChild(node);
        }

        // Build a single new run with the combined text
        QDomElement newRun = doc.createElement("w:r");

        // Attach formatting from the first run (if any)
        if (!firstRunProps.isNull()) {
            newRun.appendChild(firstRunProps.cloneNode(true));
        }

        // Create the text element; xml:space="preserve" keeps leading/trailing spaces
        QDomElement newText = doc.createElement("w:t");
        newText.setAttribute("xml:space", "preserve");
        newText.appendChild(doc.createTextNode(fullText));
        newRun.appendChild(newText);

        // Append the merged run to the paragraph
        para.appendChild(newRun);
    }
}

// ---------------------------------------------------------------------------
// processConditionals()
//
// Handles {{#if condition}} ... {{/if}} blocks.
//
// Expected template structure (each marker on its own paragraph in Word):
//
//   Paragraph: "{{#if show_discount}}"
//   Paragraph: "Discount: {{discount}}"
//   Paragraph: "{{/if}}"
//
// If data["show_discount"] == true:
//   → The two marker paragraphs are removed; the content paragraph is kept.
//
// If data["show_discount"] == false (or missing):
//   → All three paragraphs (markers + content) are removed.
// ---------------------------------------------------------------------------
void TemplateEngine::processConditionals(QDomDocument &doc, const QJsonObject &data)
{
    // We need a snapshot of all paragraphs because we'll be removing some.
    // elementsByTagName() is a live NodeList, so we copy it to a vector first.
    QDomNodeList rawParagraphs = doc.elementsByTagName("w:p");
    QVector<QDomElement> paragraphs;
    for (int i = 0; i < rawParagraphs.count(); i++) {
        paragraphs.append(rawParagraphs.at(i).toElement());
    }

    // Pattern to match the opening marker: {{#if conditionName}}
    static const QRegularExpression ifPattern(R"(\{\{#if\s+(\w+)\}\})");
    // Pattern to match the closing marker: {{/if}}
    static const QRegularExpression endIfPattern(R"(\{\{/if\}\})");

    bool    inConditional = false;
    bool    conditionMet  = false;
    QList<QDomNode> markerNodes;   // The {{#if}} and {{/if}} paragraphs — always removed
    QList<QDomNode> contentNodes;  // The paragraphs between them — kept only if condition is true

    for (QDomElement &para : paragraphs) {
        // Get the full text content of this paragraph (stripping all XML tags)
        QString paraText;
        QDomNodeList texts = para.elementsByTagName("w:t");
        for (int i = 0; i < texts.count(); i++) {
            paraText += texts.at(i).toElement().text();
        }
        paraText = paraText.trimmed();

        if (!inConditional) {
            // Look for an opening {{#if condition}} marker
            QRegularExpressionMatch match = ifPattern.match(paraText);
            if (match.hasMatch()) {
                QString conditionName = match.captured(1);
                // Evaluate the condition: look up the name in the data object
                conditionMet  = data.value(conditionName).toBool(false);
                inConditional = true;
                markerNodes.append(para);  // The {{#if}} paragraph itself is always removed
            }
        } else {
            // We're inside a conditional block — look for the closing marker
            if (endIfPattern.match(paraText).hasMatch()) {
                markerNodes.append(para);  // The {{/if}} paragraph is always removed

                // If the condition was false, remove the content paragraphs too
                if (!conditionMet) {
                    for (QDomNode &node : contentNodes) {
                        node.parentNode().removeChild(node);
                    }
                }

                // Always remove the marker paragraphs ({{#if}} and {{/if}})
                for (QDomNode &node : markerNodes) {
                    node.parentNode().removeChild(node);
                }

                // Reset state for the next conditional block
                inConditional = false;
                conditionMet  = false;
                markerNodes.clear();
                contentNodes.clear();
            } else {
                // This paragraph is content inside the conditional block
                contentNodes.append(para);
            }
        }
    }

    // If a {{#if}} was never closed, emit a warning
    if (inConditional) {
        qWarning() << "[TemplateEngine] Unclosed {{#if}} block found in template — "
                      "the block and its content were left unchanged.";
    }
}

// ---------------------------------------------------------------------------
// processTableLoops()
//
// Handles {{#each arrayName}} repeating table rows.
//
// Expected template structure (a single row in a Word table):
//
//   | {{#each items}}{{item.description}} | {{item.quantity}} | {{item.price}} |
//
// For each element in data["items"], this row is cloned and {{item.xxx}}
// markers are replaced with that element's field values.
// The original template row is then removed.
// ---------------------------------------------------------------------------
void TemplateEngine::processTableLoops(QDomDocument &doc, const QJsonObject &data)
{
    // Pattern to detect a loop marker in a table row
    static const QRegularExpression eachPattern(R"(\{\{#each\s+(\w+)\}\})");
    // Pattern to match item field references: {{item.fieldName}}
    static const QRegularExpression itemFieldPattern(R"(\{\{item\.(\w+)\}\})");

    // Collect template rows in a snapshot (can't modify while iterating a live NodeList)
    QDomNodeList allRows = doc.elementsByTagName("w:tr");
    QVector<QDomElement> templateRows;
    QVector<QString>     arrayNames;

    for (int i = 0; i < allRows.count(); i++) {
        QDomElement row = allRows.at(i).toElement();

        // Concatenate all text in this row to check for a loop marker
        QString rowText;
        QDomNodeList texts = row.elementsByTagName("w:t");
        for (int j = 0; j < texts.count(); j++) {
            rowText += texts.at(j).toElement().text();
        }

        QRegularExpressionMatch match = eachPattern.match(rowText);
        if (match.hasMatch()) {
            templateRows.append(row);
            arrayNames.append(match.captured(1));  // e.g., "items"
        }
    }

    // Process each template row
    for (int i = 0; i < templateRows.count(); i++) {
        QDomElement templateRow = templateRows[i];
        QString     arrayName   = arrayNames[i];

        // Get the parent node (the <w:tbl> table element)
        QDomNode parentNode = templateRow.parentNode();

        // Get the array from the data (e.g., data["items"])
        QJsonArray items = data.value(arrayName).toArray();

        if (items.isEmpty()) {
            qInfo() << "[TemplateEngine] Array '" << arrayName
                    << "' is empty — repeating row will be removed.";
        }

        // For each item in the array, clone the template row and fill in values
        for (const QJsonValue &itemVal : items) {
            QJsonObject item = itemVal.toObject();

            // Deep-clone the template row so we get an independent copy
            QDomNode clonedRow = templateRow.cloneNode(/* deep = */ true);

            // Get all text elements in the cloned row and replace markers
            QDomNodeList clonedTexts = clonedRow.toElement().elementsByTagName("w:t");
            for (int j = 0; j < clonedTexts.count(); j++) {
                QDomElement textElem = clonedTexts.at(j).toElement();
                QString     text     = textElem.text();

                // Remove the {{#each arrayName}} marker (it's a control marker, not content)
                text.remove(eachPattern);

                // Replace all {{item.fieldName}} markers with actual values
                QRegularExpressionMatchIterator it = itemFieldPattern.globalMatch(text);
                // Collect replacements first to avoid match position shifting
                QList<QPair<QString, QString>> replacements;
                while (it.hasNext()) {
                    QRegularExpressionMatch m = it.next();
                    QString fullMarker = m.captured(0);   // e.g., "{{item.price}}"
                    QString fieldName  = m.captured(1);   // e.g., "price"
                    QString value      = item.value(fieldName).toVariant().toString();
                    replacements.append({ fullMarker, value });
                }
                for (const auto &[marker, value] : replacements) {
                    text.replace(marker, value);
                }

                // Update the text node value in the DOM
                // (w:t has exactly one child text node)
                if (textElem.firstChild().isNull()) {
                    textElem.appendChild(doc.createTextNode(text));
                } else {
                    textElem.firstChild().setNodeValue(text);
                }
            }

            // Insert the filled clone before the template row in the table
            parentNode.insertBefore(clonedRow, templateRow);
        }

        // Remove the original template row
        parentNode.removeChild(templateRow);
    }
}

// ---------------------------------------------------------------------------
// replaceSimpleMarkers()
//
// Performs a plain-text search-and-replace of all {{variableName}} markers
// in the XML string using the values from the data object.
//
// This runs after DOM processing, so by this point:
//   - Conditional blocks have already been resolved
//   - Table loop rows have already been generated
// Only simple scalar substitutions remain.
// ---------------------------------------------------------------------------
QString TemplateEngine::replaceSimpleMarkers(const QString &xmlText, const QJsonObject &data)
{
    QString result = xmlText;

    // Pattern: {{word}} — matches any simple variable marker
    static const QRegularExpression markerPattern(R"(\{\{(\w+)\}\})");

    // Find all matches in one pass, then apply replacements
    QRegularExpressionMatchIterator it = markerPattern.globalMatch(result);

    // Collect all unique markers and their replacement values
    QList<QPair<QString, QString>> replacements;
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        QString marker = match.captured(0);  // e.g., "{{client_name}}"
        QString key    = match.captured(1);  // e.g., "client_name"

        if (data.contains(key)) {
            QString value = data.value(key).toVariant().toString();
            replacements.append({ marker, value });
        } else {
            qWarning() << "[TemplateEngine] No value found for marker:" << marker
                       << "— it will be left as-is in the output.";
        }
    }

    // Apply all replacements (using replaceAll semantics)
    for (const auto &[marker, value] : replacements) {
        result.replace(marker, value);
    }

    return result;
}
