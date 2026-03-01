cmake_minimum_required(VERSION 3.16)
project(ReportServer VERSION 1.0.0 LANGUAGES CXX)

# Require C++17 (needed for structured bindings and modern Qt)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable Qt's automatic MOC (Meta-Object Compiler) processing.
# This is needed for QObject-derived classes with Q_OBJECT macros.
set(CMAKE_AUTOMOC ON)

# --- Find Qt6 ---
# Qt6::Core       = base classes (QObject, QFile, QProcess, etc.)
# Qt6::Network    = networking base (needed by HttpServer)
# Qt6::HttpServer = the built-in REST API server (requires Qt 6.4+)
# Qt6::Xml        = QDomDocument for parsing/editing the DOCX XML content
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Network HttpServer Xml)

# --- Find QuaZip ---
# QuaZip is a Qt-friendly wrapper around zlib for reading/writing ZIP files.
# DOCX files are ZIP archives, so we need this to extract and repack them.
# Install via vcpkg: vcpkg install quazip:x64-windows
# Or on macOS for development: brew install quazip
find_package(QuaZip-Qt6 REQUIRED)

# --- Source Files ---
# All application source files live under src/
set(SOURCES
    src/main.cpp
    src/HttpServer.cpp
    src/TemplateEngine.cpp
    src/PdfConverter.cpp
)

set(HEADERS
    src/HttpServer.h
    src/TemplateEngine.h
    src/PdfConverter.h
)

# Create the executable
add_executable(ReportServer ${SOURCES} ${HEADERS})

# Add src/ to the include search path so headers can be included
# without a path prefix (e.g. #include "HttpServer.h" works from any source file)
target_include_directories(ReportServer PRIVATE src)

# Link against all required libraries
target_link_libraries(ReportServer
    Qt6::Core
    Qt6::Network
    Qt6::HttpServer
    Qt6::Xml
    QuaZip::QuaZip  # ZIP read/write support
)

# --- Copy runtime assets to build directory ---
# config.json must sit next to the executable at runtime
configure_file(config.json ${CMAKE_BINARY_DIR}/config.json COPYONLY)

# Create the templates and temp directories in the build folder
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/templates)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/temp)
