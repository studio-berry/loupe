// MIT License
//
// Copyright (c) 2018-2025 Jakub Melka and Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#ifndef PDFTOOLABSTRACTAPPLICATION_H
#define PDFTOOLABSTRACTAPPLICATION_H

#include "pdfglobal.h"
#include "pdfoutputformatter.h"
#include "pdftoolresult.h"
#include "pdfdocument.h"
#include "pdfdocumenttextflow.h"
#include "pdfrenderer.h"
#include "pdfcms.h"
#include "pdfoptimizer.h"
#include "pdfimageoptimizer.h"
#include "pdfredact.h"
#include "pdfbleedfixup.h"
#include "pdftransparencyflattener.h"
#include "pdfrgbtocmykfixup.h"
#include "pdfrepairdiff.h"
#include "pdfrepairoperation.h"
#include "pdfactionlist.h"

#include <QtGlobal>
#include <QList>
#include <QString>
#include <QStringList>
#include <QDateTime>
#include <QCoreApplication>
#include <QStringConverter>

#include <vector>
#include <memory>

class QCommandLineParser;

namespace pdftool
{

struct PDFToolTranslationContext
{
    Q_DECLARE_TR_FUNCTIONS(PDFToolTranslationContext)
};

enum class PDFToolValueType
{
    Boolean,
    Integer,
    Number,
    String,
    Path,
    Enum,
    Csv
};

struct PDFToolOptionDescriptor
{
    QString id;
    QStringList names;
    QString valueName;
    PDFToolValueType valueType = PDFToolValueType::String;
    QStringList allowedValues;
    QString defaultValue;
    bool required = false;
    bool repeatable = false;
    bool sensitive = false;
};

struct PDFToolPositionalDescriptor
{
    QString id;
    PDFToolValueType valueType = PDFToolValueType::String;
    bool required = true;
    bool repeatable = false;
};

struct PDFToolCommandDescriptor
{
    QString id;
    QString name;
    QString description;
    QStringList capabilities;
    QStringList outputFormats;
    QList<PDFToolOptionDescriptor> options;
    QList<PDFToolPositionalDescriptor> positionals;
};

struct PDFToolOptions
{
    enum DateFormat
    {
        LocaleShortDate,
        LocaleLongDate,
        ISODate,
        RFC2822Date
    };

    // For option 'ConsoleFormat'
    PDFOutputFormatter::Style outputStyle = PDFOutputFormatter::Style::Text;
    QStringConverter::Encoding outputCodec = QStringConverter::Utf8;

    // For option 'DateFormat'
    DateFormat outputDateFormat = LocaleShortDate;

    // For option 'OpenDocument'
    QString document;
    QString password;
    bool permissiveReading = true;

    // For option 'SignatureVerification'
    bool verificationUseUserCertificates = true;
    bool verificationUseSystemCertificates = true;
    bool verificationOmitCertificateCheck = false;
    bool verificationPrintCertificateDetails = false;
    bool verificationIgnoreExpirationDate = false;

    // For option 'XMLExport'
    bool xmlExportStreams = false;
    bool xmlExportStreamsAsText = false;
    bool xmlUseIndent = false;
    bool xmlAlwaysBinaryStrings = false;

    // For option 'Attachments'
    QString attachmentsSaveNumber;
    QString attachmentsSaveFileName;
    QString attachmentsOutputDirectory;
    QString attachmentsTargetFile;
    bool attachmentsSaveAll = false;

    // For option 'ComputeHashes'
    bool computeHashes = false;

    // For option 'PageSelector'
    QString pageSelectorFirstPage;
    QString pageSelectorLastPage;
    QString pageSelectorSelection;

    // For option 'TextAnalysis'
    pdf::PDFDocumentTextFlowFactory::Algorithm textAnalysisAlgorithm = pdf::PDFDocumentTextFlowFactory::Algorithm::Auto;

    // For option 'TextShow'
    bool textShowPageNumbers = false;
    bool textShowStructTitles = false;
    bool textShowStructLanguage = false;
    bool textShowStructAlternativeDescription = false;
    bool textShowStructExpandedForm = false;
    bool textShowStructActualText = false;
    bool textShowStructPhoneme = false;

    // For option 'VoiceSelector'
    QString textVoiceName;
    QString textVoiceGender;
    QString textVoiceAge;
    QString textVoiceLangCode;

    // For option 'TextSpeech'
    bool textSpeechMarkPageNumbers = false;
    bool textSpeechSayPageNumbers = false;
    bool textSpeechSayStructTitles = false;
    bool textSpeechSayStructAlternativeDescription = false;
    bool textSpeechSayStructExpandedForm = false;
    bool textSpeechSayStructActualText = false;
    QString textSpeechAudioFormat = "mp3";

    // For option 'CharacterMaps'
    bool showCharacterMapsForEmbeddedFonts = false;

    // For option 'ImageWriterSettings'
    pdf::PDFImageWriterSettings imageWriterSettings;

    // For option 'ImageExportSettings'
    pdf::PDFPageImageExportSettings imageExportSettings;

    // For option 'ColorManagementSystem'
    pdf::PDFCMSSettings cmsSettings;

    // For option 'RenderFlags'
    pdf::PDFRenderer::Features renderFeatures = pdf::PDFRenderer::getDefaultFeatures();
    bool renderUseSoftwareRendering = true;
    bool renderShowPageStatistics = false;
    int renderMSAAsamples = 4;
    int renderRasterizerCount = pdf::PDFRasterizerPool::getDefaultRasterizerCount();

    // For the versioned STCH render-page contract
    int renderPageIndex = -1;
    int renderPageDpi = 300;
    qint64 renderPageMaxRasterPixels = 250000000;
    QString renderPageOutput;

    // For option 'Separate'
    QString separatePagePattern;

    // For option 'Unite'
    QStringList uniteFiles;

    // For option 'Diff'
    QStringList diffFiles;

    // For option 'Optimize'
    pdf::PDFOptimizer::OptimizationFlags optimizeFlags = pdf::PDFOptimizer::None;
    pdf::PDFImageOptimizer::Settings imageOptimizationSettings = pdf::PDFImageOptimizer::Settings::createDefault();

    // For option 'CertStore'
    bool certStoreEnumerateSystemCertificates = false;
    bool certStoreEnumerateUserCertificates = true;

    // For option 'CertStoreInstall'
    QString certificateStoreInstallCertificateFile;

    // For option 'Redact'
    pdf::PDFRedact::Options redactOptions = {};
    QString redactedDocument;

    // For option 'AddBleed'
    pdf::PDFBleedFixupSettings addBleedSettings;
    QString addBleedOutputDocument;

    // For option 'FlattenTransparency'
    pdf::PDFTransparencyFlattenSettings flattenTransparencySettings;
    QString flattenTransparencyOutputDocument;

    // For option 'RgbToCmyk'
    pdf::PDFRgbToCmykSettings rgbToCmykSettings;
    QString rgbToCmykOutputDocument;

    // Shared destructive-write guard (unite, separate, redact, encrypt, decrypt,
    // optimize, remove-external-links, attachments, render, fetch-images, add-bleed).
    // --overwrite is canonical; --force is kept as a silent alias on the commands
    // that historically accepted it (add-bleed --force keeps its heuristic meaning).
    bool destructiveDryRun = false;
    bool destructiveReport = false;
    bool destructiveOverwrite = false;

    // Shared empty-result policy (fetch-images, fetch-text, attachments --save).
    // Extraction commands are informational by default - "this document has no
    // figures" is a legitimate answer - so the fail-closed reading is opt-in.
    bool failIfEmpty = false;

    // For option 'PreflightProfile'
    QString preflightProfilePath;
    QString preflightJobContextPath;
    QString preflightProfileStorePath;
    QString preflightDecisionsPath;
    QString preflightDecisionsExportPath;
    bool preflightRequireSignoff = false;
    QString preflightClientId;
    QString preflightProductId;
    QString preflightJobType;
    QString preflightPressId;
    QString preflightStockId;
    QString preflightFinishingId;
    QStringList preflightParameterAssignments;
    QStringList preflightCheckFilter;

    // For option 'CapabilityDiscovery'
    QString capabilitiesCommand;

    // For option 'OcrOptions'
    QString ocrSidecarPath;
    int ocrDpi = 300;
    QString ocrLanguages = QStringLiteral("en");
    int ocrMinTextChars = 20;

    // For option 'Diagnostics'
    QString diagnosticsOutputDirectory;
    bool diagnosticsIncludeLogs = true;

    // For option 'RepairDiff'
    QStringList repairDiffFiles;
    pdf::PDFRepairDiffOptions repairDiffOptions;

    // For option 'Repair'
    QStringList repairFiles;
    QString repairOperationId;
    QStringList repairParameterAssignments;
    QString repairOutputDocument;
    QString repairReportFile;
    QString repairRenderDirectory;
    bool repairListOperations = false;
    bool repairAllowIncomplete = false;

    // For Action List recipes
    QString actionListSubcommand;
    QString actionListRecipe;
    QStringList actionListFiles;
    QString actionListOutputDocument;
    QString actionListOutputDirectory;
    QStringList actionListParameterAssignments;

    // Structured result contract context owned by main.cpp. Commands populate
    // diagnostics, outputs, and data through it instead of writing the envelope
    // themselves. Null when not running under the contract.
    PDFToolExecutionContext* executionContext = nullptr;

    // For option 'VerifyRedaction'
    QStringList verifyRedactionFiles;
    pdf::PDFRedact::Options verifyRedactionOptions = {};
    bool verifyRedactionCheckIncremental = true;

    // For option 'Encrypt'
    pdf::PDFSecurityHandlerFactory::Algorithm encryptionAlgorithm = pdf::PDFSecurityHandlerFactory::Algorithm::AES_256;
    pdf::PDFSecurityHandlerFactory::EncryptContents encryptionContents = pdf::PDFSecurityHandlerFactory::EncryptContents::All;
    QString encryptionUserPassword;
    QString encryptionOwnerPassword;
    uint32_t encryptionPermissions = 0;

    /// Returns page range. If page range is invalid, then \p errorMessage is empty.
    /// \param pageCount Page count
    /// \param[out] errorMessage Error message
    /// \param zeroBased Convert to zero based page range?
    std::vector<pdf::PDFInteger> getPageRange(pdf::PDFInteger pageCount, QString& errorMessage, bool zeroBased) const;

    struct RenderFeatureInfo
    {
        QString option;
        QString description;
        pdf::PDFRenderer::Feature feature;
    };

    /// Returns a list of available renderer features
    static std::vector<RenderFeatureInfo> getRenderFeatures();

    struct OptimizeFeatureInfo
    {
        QString option;
        QString description;
        pdf::PDFOptimizer::OptimizationFlag flag;
    };

    /// Returns a list of available optimize features
    static std::vector<OptimizeFeatureInfo> getOptimizeFlagInfos();
};

/// Base class for all applications
class PDFToolAbstractApplication
{
public:
    explicit PDFToolAbstractApplication(bool isDefault = false);
    virtual ~PDFToolAbstractApplication() = default;

    enum StandardString
    {
        Command,   ///< Command, by which is this application invoked
        Name,   ///< Name of application
        Description   ///< Description (what this application does)
    };

    enum Option : quint64
    {
        ConsoleFormat = 0x00000001,   ///< Set format of console output (text, xml or html)
        OpenDocument = 0x00000002,   ///< Flags for document reading
        SignatureVerification = 0x00000004,   ///< Flags for signature verification,
        XmlExport = 0x00000008,   ///< Flags for xml export
        Attachments = 0x00000010,   ///< Flags for attachments manipulating
        DateFormat = 0x00000020,   ///< Date format
        ComputeHashes = 0x00000040,   ///< Compute hashes
        PageSelector = 0x00000080,   ///< Select page range (or all pages)
        TextAnalysis = 0x00000100,   ///< Text analysis options
        TextShow = 0x00000200,   ///< Text extract and show options
        VoiceSelector = 0x00000400,   ///< Select voice from SAPI
        TextSpeech = 0x00000800,   ///< Text speech options
        CharacterMaps = 0x00001000,   ///< Character maps for embedded fonts
        ImageWriterSettings = 0x00002000,   ///< Settings for writing images (for example, format, etc.)
        ImageExportSettingsFiles = 0x00004000,   ///< Settings for exporting page images to files
        ImageExportSettingsResolution = 0x00008000,   ///< Settings for resolution of exported images
        ColorManagementSystem = 0x00010000,   ///< Color management system settings
        RenderFlags = 0x00020000,   ///< Render flags for page image rasterizer
        Separate = 0x00040000,   ///< Settings for Separate tool
        Unite = 0x00080000,   ///< Settings for Unite tool
        Optimize = 0x00100000,   ///< Settings for Optimize tool
        CertStore = 0x00200000,   ///< Settings for certificate store tool
        CertStoreInstall = 0x00400000,   ///< Settings for certificate store install certificate tool
        Encrypt = 0x00800000,   ///< Encryption settings
        Diff = 0x01000000,   ///< Diff settings (compare documents)
        Redact = 0x02000000,   ///< Settings for Redact tool
        AddBleed = 0x04000000,   ///< Settings for add-bleed tool
        FlattenTransparency = 0x2000000000ULL,   ///< Settings for flatten-transparency tool
        PreflightProfile = 0x08000000,   ///< Loop preflight profile path
        VerifyRedaction = 0x10000000,   ///< Settings for verify-redaction tool
        DestructiveWrite = 0x20000000,   ///< Shared --dry-run/--report/--force for overwrite commands
        OcrOptions = 0x40000000,   ///< Loop OCR sidecar settings
        Diagnostics = 0x80000000,   ///< Loop diagnostics bundle collection
        RgbToCmyk = 0x100000000ULL,   ///< ICC-managed RGB-to-CMYK fixup
        CapabilityDiscovery = 0x200000000ULL,   ///< Machine-readable command discovery
        RepairDiff = 0x400000000ULL,   ///< Deterministic before/after repair comparison
        Repair = 0x800000000ULL,   ///< Transactional prepress-safe repair operation
        ActionList = 0x1000000000ULL,   ///< Reusable declarative Action List execution
        RenderPage = 0x4000000000ULL,   ///< Settings for render-page STCH contract
        EmptyResultPolicy = 0x8000000000ULL,   ///< Shared --fail-if-empty for extraction commands
    };
    Q_DECLARE_FLAGS(Options, Option)

    virtual QString getStandardString(StandardString standardString) const = 0;
    virtual PDFToolExitCode execute(const PDFToolOptions& options) = 0;
    virtual Options getOptionsFlags() const = 0;

    /// Returns stable, machine-readable metadata for this command.
    virtual PDFToolCommandDescriptor describe() const;

    static QList<PDFToolOptionDescriptor> describeOptions(Options optionFlags);
    static QList<PDFToolPositionalDescriptor> describePositionals(Options optionFlags);
    static QStringList describeCapabilities(Options optionFlags);

    void initializeCommandLineParser(QCommandLineParser* parser) const;
    PDFToolOptions getOptions(QCommandLineParser* parser, PDFToolExecutionContext* executionContext) const;

    static QString convertDateTimeToString(const QDateTime& dateTime, PDFToolOptions::DateFormat dateFormat);

protected:
    /// Reports a structured diagnostic to the execution context (JSON mode) and,
    /// in human modes, keeps the existing stderr behavior. JSON mode does not
    /// duplicate handled diagnostics on stderr.
    /// \param options Options (carries execution context and output style)
    /// \param severity Diagnostic severity
    /// \param code Stable diagnostic identifier (e.g. "pdf.document-unreadable")
    /// \param message Human-oriented message
    /// \param context Optional structured context
    void reportDiagnostic(const PDFToolOptions& options,
                          PDFToolDiagnosticSeverity severity,
                          const QString& code,
                          const QString& message,
                          QJsonObject context = QJsonObject()) const;

    /// Reports that an extraction command completed without producing anything and
    /// returns the exit code the command should use. Extraction is informational by
    /// default: a document with no figures is not an error, so without
    /// --fail-if-empty this records an `output.empty-result` note and returns
    /// \p successCode. With --fail-if-empty it raises the same code to an error and
    /// returns PDFToolExitCode::Findings, so a pipeline that gates on "figures were
    /// produced" cannot be green-lit by an empty output directory.
    /// \param options Options (carries execution context, output style, and the flag)
    /// \param subject What was not produced, for the message (e.g. "images")
    /// \param successCode Exit code to return when the flag was not requested
    PDFToolExitCode reportEmptyResult(const PDFToolOptions& options,
                                      const QString& subject,
                                      PDFToolExitCode successCode = PDFToolExitCode::Success) const;

    /// Tries to read the document. If document is successfully read, true is returned,
    /// if error occurs, then false is returned. Optionally, original document content
    /// can also be retrieved.
    /// \param options Options
    /// \param document Document
    /// \param[out] sourceData Pointer, to which source data are stored
    /// \param authorizeOwnerOnly Require to authorize as owner
    bool readDocument(const PDFToolOptions& options, pdf::PDFDocument& document, QByteArray* sourceData, bool authorizeOwnerOnly);

    /// Like readDocument, but keeps the parsed PDFDocument on the LoopLibCore heap.
    bool readDocumentOnHeap(const PDFToolOptions& options,
                            std::unique_ptr<pdf::PDFDocument, void (*)(pdf::PDFDocument*)>& document,
                            QByteArray* sourceData,
                            bool authorizeOwnerOnly);

    /// Returns a list of available encodings
    static QList<QByteArray> getAvailableEncodings();

    /// Returns default encoding
    static QByteArray getDefaultEncoding();

    /// Converts string to encoding
    static QStringConverter::Encoding getEncoding(const QString& encodingName);

    /// Registers shared --dry-run, --report, and --overwrite options (and, unless
    /// \p registerForceAlias is false, the legacy --overwrite alias --force).
    static void registerDestructiveWriteOptions(QCommandLineParser* parser, bool registerForceAlias);

    /// Returns PDFToolExitCode::Success when the write may proceed; otherwise an
    /// error value.
    PDFToolExitCode validateDestructiveOutput(const PDFToolOptions& options, const QString& outputPath) const;

    /// Returns PDFToolExitCode::Success when every write may proceed; otherwise an
    /// error value.
    PDFToolExitCode validateDestructiveOutputs(const PDFToolOptions& options, const QStringList& outputPaths) const;
};

/// This class stores information about all applications available. Application
/// can be selected by command string. Also, default application is available.
class PDFToolApplicationStorage
{
public:
    /// Returns application by command. If application for given command is not found,
    /// then nullptr is returned.
    /// \param command Command
    /// \returns Application for given command or nullptr
    static PDFToolAbstractApplication* getApplicationByCommand(const QString& command);

    /// Registers application to the application storage. If \p isDefault
    /// is set to true, application is registered as default application.
    /// \param application Apllication
    /// \param isDefault Is default application?
    static void registerApplication(PDFToolAbstractApplication* application, bool isDefault = false);

    /// Returns default application
    /// \returns Default application
    static PDFToolAbstractApplication* getDefaultApplication();

    /// Returns a list of available applications
    static const std::vector<PDFToolAbstractApplication*>& getApplications();

private:
    PDFToolApplicationStorage() = default;
    static PDFToolApplicationStorage* getInstance();

    std::vector<PDFToolAbstractApplication*> m_applications;
    PDFToolAbstractApplication* m_defaultApplication = nullptr;
};

}   // namespace pdftool

Q_DECLARE_OPERATORS_FOR_FLAGS(pdftool::PDFToolAbstractApplication::Options)

#endif   // PDFTOOLABSTRACTAPPLICATION_H
