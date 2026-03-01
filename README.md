# ReportServer

A self-hosted HTTP server that generates PDF reports from Word (`.docx`) templates.

Clients send a template name and data (as JSON) to the server, which fills in the template and returns a PDF file.

---

## Table of Contents

1. [How It Works](#how-it-works)
2. [Project Structure](#project-structure)
3. [Prerequisites](#prerequisites)
4. [Running with Docker (Recommended for Testing)](#running-with-docker-recommended-for-testing)
5. [Building Natively (Windows Server Deployment)](#building-natively-windows-server-deployment)
6. [Configuration](#configuration)
7. [Creating Templates](#creating-templates)
8. [API Reference](#api-reference)
9. [Running the Server](#running-the-server)
10. [Troubleshooting](#troubleshooting)

---

## How It Works

```
Client App
   │
   │  POST /api/generate
   │  { "template": "invoice", "data": { "client_name": "John", ... } }
   │
   ▼
ReportServer (this application)
   ├── Loads the matching invoice.docx from the templates folder
   ├── Replaces all {{markers}} in the Word document with the provided data
   ├── Calls LibreOffice (in the background) to convert the filled DOCX to PDF
   └── Returns the PDF as a file download
```

---

## Project Structure

```
ReportServer/
├── CMakeLists.txt           — CMake build configuration
├── config.json              — Server config for native Windows deployment
├── README.md
│
├── src/                     — All C++ source code
│   ├── main.cpp             — Entry point: load config, start server
│   ├── HttpServer.h/cpp     — HTTP API routes and request handling
│   ├── TemplateEngine.h/cpp — DOCX template processing engine
│   └── PdfConverter.h/cpp   — LibreOffice PDF conversion
│
├── docker/                  — Container setup for testing
│   ├── Dockerfile           — Multi-stage image build (Ubuntu 24.04 + Qt6 + LibreOffice)
│   ├── docker-compose.yml   — Service definition for easy test startup
│   └── config.json          — Linux-specific config (used inside the container)
│
└── templates/               — Place your .docx template files here
```

---

## Running with Docker (Recommended for Testing)

Docker is the easiest way to test ReportServer without installing Qt or LibreOffice locally. Everything runs inside the container.

### Prerequisites for Docker

- [Docker Desktop](https://www.docker.com/products/docker-desktop/) installed and running.
  That's it — Qt, LibreOffice, and all dependencies are handled inside the image.

### Start the server

From the **repository root** folder, run:

```bash
docker compose -f docker/docker-compose.yml up --build
```

- `--build` compiles the application inside Docker on first run (takes a few minutes).
- On subsequent starts you can omit `--build` if you haven't changed any code.

The server is ready when you see:
```
reportserver  | ========================================
reportserver  |   ReportServer running on port 8080
reportserver  | ========================================
```

### Test it

```bash
# List available templates
curl http://localhost:8080/api/templates

# Generate a PDF (saves to output.pdf on your machine)
curl -X POST http://localhost:8080/api/generate \
  -H "Content-Type: application/json" \
  -d '{"template":"invoice","data":{"client_name":"Test Client"}}' \
  --output output.pdf
```

### Add templates without rebuilding

The `templates/` folder on your host is mounted directly into the container.
Drop a `.docx` file into `templates/` and the server picks it up immediately.

### Stop / view logs

```bash
docker compose -f docker/docker-compose.yml down        # stop
docker compose -f docker/docker-compose.yml logs -f     # follow logs
```

---

## Building Natively (Windows Server Deployment)

For the production Windows on-premise server, follow these steps to build a native `.exe`.

### 1. Qt 6.4 or newer

Qt is the C++ framework this application is built with.

- Download the free **Qt Online Installer** from: https://www.qt.io/download-qt-installer
- During installation, select **Qt 6.4** (or newer) and make sure to include these components:
  - `Qt HTTP Server`
  - `Qt Network`
  - `Qt XML`
  - `MSVC 2022 64-bit` (or `MinGW 64-bit` — pick one)
- Also install **CMake** and **Ninja** from the Tools section of the installer.

### 2. LibreOffice

LibreOffice converts the filled Word document to PDF. It is free and open source.

- Download from: https://www.libreoffice.org/download/download/
- Install with the default settings.
- After installation, find the path to `soffice.exe`. It is usually:
  ```
  C:\Program Files\LibreOffice\program\soffice.exe
  ```
- You will need this path for `config.json` (see [Configuration](#configuration)).

### 3. QuaZip (ZIP library for Qt)

QuaZip allows the application to read and write `.docx` files, which are ZIP archives internally.

The easiest way to install it is via **vcpkg** (a free package manager for C++ libraries).

**Install vcpkg** (only needed once per machine):
```cmd
git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
cd C:\vcpkg
.\bootstrap-vcpkg.bat
```

**Install QuaZip:**
```cmd
C:\vcpkg\vcpkg.exe install quazip:x64-windows
```

---

## Building the Application (Native Windows)

### Step 1 – Clone or copy the project

Place the project folder (e.g., `ReportServer`) somewhere on your machine, for example:
```
C:\Projects\ReportServer
```

### Step 2 – Open Qt Creator

Qt Creator is the development environment that comes with Qt.

1. Open Qt Creator.
2. Click **File → Open File or Project…**
3. Navigate to the project folder and open `CMakeLists.txt`.

### Step 3 – Configure the project

Qt Creator will ask you to configure the project. Select the Qt kit you installed (e.g., `Qt 6.4.x MSVC 2022 64bit`).

If Qt Creator cannot find QuaZip, you need to tell CMake where vcpkg is. In Qt Creator:

1. Go to **Projects** (left sidebar) → **Build Settings**.
2. Under **CMake**, add the following CMake argument:
   ```
   -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
   ```

### Step 4 – Build

Click the **Build** button (hammer icon) or press `Ctrl+B`.

The compiled `ReportServer.exe` will appear in the build output folder (e.g., `build/Debug/` or `build/Release/`).

---

## Configuration

Edit `config.json` (located next to `ReportServer.exe`) to configure the server:

```json
{
    "server": {
        "port": 8080,
        "host": "0.0.0.0"
    },
    "paths": {
        "templates": "./templates",
        "temp":      "./temp",
        "libreOffice": "C:\\Program Files\\LibreOffice\\program\\soffice.exe"
    }
}
```

| Setting | Description |
|---|---|
| `server.port` | The network port the server listens on (default: `8080`). |
| `server.host` | Use `0.0.0.0` to accept connections from all devices on your network. |
| `paths.templates` | Folder where your `.docx` template files are stored. |
| `paths.temp` | Folder for temporary files during PDF generation (auto-cleaned). |
| `paths.libreOffice` | Full path to the LibreOffice `soffice.exe` executable. |

> **Note on Windows paths:** In JSON, backslashes must be doubled. Write `C:\\Program Files\\...` not `C:\Program Files\...`.

---

## Creating Templates

Templates are standard **Microsoft Word `.docx` files** stored in the `templates` folder.

You can create and edit them in Microsoft Word just like any other document.
To make them dynamic, you add **marker placeholders** directly in the text.

### Simple Variable Markers

Use double curly braces to mark where data should be inserted:

```
Dear {{client_name}},

Please find attached your invoice for {{total_amount}}.

Invoice number: {{invoice_number}}
Date:           {{date}}
```

When the server receives the data `{ "client_name": "John Doe", "total_amount": "€500", ... }`,
it replaces each marker with the corresponding value.

> **Important tip:** When typing markers in Word, type the entire `{{marker_name}}` in one go
> without using backspace or undo. Word sometimes splits text internally when you correct typos,
> which can break marker detection. If a marker is not being replaced, delete it and retype it
> from scratch in one continuous input.

---

### Repeating Table Rows

To generate a table with a variable number of rows (e.g., a list of items on an invoice),
mark one row in your Word table as the **template row**:

| Description | Quantity | Price |
|---|---|---|
| `{{#each items}}{{item.description}}` | `{{item.quantity}}` | `{{item.price}}` |

- Put `{{#each items}}` at the beginning of the **first cell** of the template row.
  It can be directly followed by the first field: `{{#each items}}{{item.description}}`
- Use `{{item.fieldName}}` in each cell to reference a field from each array element.
- The server will duplicate this row once for every element in the `items` array.
- The `{{#each items}}` marker itself is removed from the output.

**Example data:**
```json
{
    "items": [
        { "description": "Web design",  "quantity": "1", "price": "€ 800" },
        { "description": "Hosting",     "quantity": "1", "price": "€ 120" },
        { "description": "Maintenance", "quantity": "2", "price": "€ 200" }
    ]
}
```

**Result in PDF:** The table will have three rows, one for each item.

---

### Conditional Sections

To show or hide a section of the document depending on the data,
use `{{#if conditionName}}` and `{{/if}}` — **each on its own separate paragraph** in Word:

```
{{#if show_discount}}
A discount of {{discount_percent}} has been applied to your order.
{{/if}}
```

- If `data["show_discount"]` is `true`, the content is shown (and the marker lines are removed).
- If `data["show_discount"]` is `false` or missing, the entire section (including content) is removed.

**Example data:**
```json
{
    "show_discount": true,
    "discount_percent": "10%"
}
```

> **Important:** The `{{#if ...}}` line and the `{{/if}}` line must each be on their own
> paragraph in Word (press Enter after each one). Do not put them inline with other text.

---

## API Reference

### GET `/api/templates`

Returns a list of all available template names.

**Example request:**
```
GET http://your-server-ip:8080/api/templates
```

**Example response:**
```json
{
    "templates": ["invoice", "report", "letter"]
}
```

---

### POST `/api/generate`

Generates a PDF from the specified template and data.

**Request headers:**
```
Content-Type: application/json
```

**Request body:**
```json
{
    "template": "invoice",
    "filename": "invoice_for_john.pdf",
    "data": {
        "client_name": "John Doe",
        "invoice_number": "INV-2026-001",
        "date": "28.02.2026",
        "total_amount": "€ 1.120",
        "show_discount": true,
        "discount_percent": "10%",
        "items": [
            { "description": "Web design",  "quantity": "1", "price": "€ 800" },
            { "description": "Hosting",     "quantity": "1", "price": "€ 120" },
            { "description": "Maintenance", "quantity": "2", "price": "€ 200" }
        ]
    }
}
```

| Field | Required | Description |
|---|---|---|
| `template` | ✅ Yes | Name of the template file (without `.docx`). |
| `data` | ✅ Yes | Object containing all values to fill into the template. |
| `filename` | ❌ No | Desired filename for the downloaded PDF (default: `report.pdf`). |

**Success response:**
- HTTP `200 OK`
- `Content-Type: application/pdf`
- Body: the PDF file as binary data

**Error response:**
- HTTP `400`, `404`, or `500`
- `Content-Type: application/json`
- Body: `{ "error": "Description of what went wrong" }`

---

## Running the Server

### As a regular application

1. Navigate to the folder containing `ReportServer.exe`.
2. Double-click it, or run it from the command line:
   ```cmd
   cd C:\Projects\ReportServer\build\Release
   ReportServer.exe
   ```
3. You should see:
   ```
   ========================================
     ReportServer running on port 8080
     Templates dir: ./templates
   ========================================
   ```

### As a Windows Service (always-on, starts with Windows)

To have the server start automatically with Windows, you can use **NSSM** (Non-Sucking Service Manager):

1. Download NSSM from: https://nssm.cc/download
2. Open a command prompt **as Administrator** and run:
   ```cmd
   nssm install ReportServer
   ```
3. In the dialog that appears:
   - **Path:** `C:\Projects\ReportServer\build\Release\ReportServer.exe`
   - **Startup directory:** `C:\Projects\ReportServer\build\Release`
4. Click **Install service**.
5. Start the service:
   ```cmd
   nssm start ReportServer
   ```

### Firewall

If clients on other computers cannot reach the server, you may need to allow port `8080`
through the Windows Firewall:

```cmd
netsh advfirewall firewall add rule name="ReportServer" dir=in action=allow protocol=TCP localport=8080
```

---

## Troubleshooting

### "Failed to open config.json"
Make sure `config.json` is in the same folder as `ReportServer.exe`.
If you ran the executable from a different directory, copy `config.json` there.

### Template not found
- Check that the `.docx` file is in the `templates` folder (as configured in `config.json`).
- The `template` field in your request must match the filename without `.docx`.
  For `templates/invoice.docx`, use `"template": "invoice"`.

### Markers are not being replaced
- Make sure you typed the marker as one continuous piece of text (no backspace/undo mid-marker).
- Check that the field name in the marker matches **exactly** the key in your `data` object.
  `{{ClientName}}` ≠ `{{client_name}}` — they are case-sensitive.
- The server logs will print a warning for any marker that has no matching data key.

### PDF conversion fails
- Verify the `paths.libreOffice` path in `config.json` is correct.
- Try running the conversion manually in a command prompt to see any error:
  ```cmd
  "C:\Program Files\LibreOffice\program\soffice.exe" --headless --convert-to pdf --outdir C:\Temp C:\Temp\test.docx
  ```
- Make sure no LibreOffice windows are open at the same time (LibreOffice can only run one instance).

### Port already in use
Change the `server.port` in `config.json` to a different number (e.g., `8090`) and restart the server.
# PapaTestProject
