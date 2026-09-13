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

#include "pdftoolabstractapplication.h"
#include "pdfdocumentreader.h"
#include "pdfsafefilewriter.h"
#include "pdfutils.h"
#include "ocrsidecarprotocol.h"

#include <QFileInfo>
#include <QCommandLineParser>
#include <QFile>
#include <QDir>
#include <algorithm>
#include <memory>
#include <utility>

#include <algorithm>

namespace pdftool
{

class PDFToolHelpApplication : public PDFToolAbstractApplication
{
public:
    PDFToolHelpApplication() :
        PDFToolAbstractApplication(true)
    {
    }

    virtual QString getStandardString(StandardString standardString) const override;
    virtual PDFToolExitCode execute(const PDFToolOptions& options) override;
    virtual Options getOptionsFlags() const override;
};

static PDFToolHelpApplication s_helpApplication;

QString PDFToolHelpApplication::getStandardString(StandardString standardString) const
{
    switch (standardString)
    {
        case Command:
            return "help";

        case Name:
            return PDFToolTranslationContext::tr("Help");

        case Description:
            return PDFToolTranslationContext::tr("Show list of all available commands.");

        default:
            Q_ASSERT(false);
            break;
    }

    return QString();
}

PDFToolExitCode PDFToolHelpApplication::execute(const PDFToolOptions& options)
{
    PDFOutputFormatter formatter(options.outputStyle);
    formatter.beginDocument("help", PDFToolTranslationContext::tr("PDFTool help"));
    formatter.endl();

    formatter.beginTable("commands", PDFToolTranslationContext::tr("List of available commands"));

    // Table header
    formatter.beginTableHeaderRow("header");
    formatter.writeTableHeaderColumn("command", PDFToolTranslationContext::tr("Command"));
    formatter.writeTableHeaderColumn("tool", PDFToolTranslationContext::tr("Tool"));
    formatter.writeTableHeaderColumn("description", PDFToolTranslationContext::tr("Description"));
    formatter.endTableHeaderRow();

    struct Info
    {
        bool operator<(const Info& other) const
        {
            return command < other.command;
        }

        QString command;
        QString name;
        QString description;
    };

    std::vector<Info> infos;
    for (PDFToolAbstractApplication* application : PDFToolApplicationStorage::getApplications())
    {
        Info info;

        info.command = application->getStandardString(Command);
        info.name = application->getStandardString(Name);
        info.description = application->getStandardString(Description);

        infos.emplace_back(qMove(info));
    }
    std::sort(infos.begin(), infos.end());

    for (const Info& info : infos)
    {
        formatter.beginTableRow("command");
        formatter.writeTableColumn("command", info.command);
        formatter.writeTableColumn("name", info.name);
        formatter.writeTableColumn("description", info.description);
        formatter.endTableRow();
    }

    formatter.endTable();

    formatter.endl();
    formatter.beginHeader("text-output", PDFToolTranslationContext::tr("Text Encoding"));

    formatter.writeText("header", PDFToolTranslationContext::tr("When you redirect console to a file, then specific codec is used to transform output text to target encoding. UTF-8 encoding is used by default. For XML output, you should use only UTF-8 codec. Available codecs:"));
    formatter.endl();

    QList<QByteArray> codecs = getAvailableEncodings();
    QStringList codecNames;
    for (const QByteArray& codecName : codecs)
    {
        codecNames << QString::fromLatin1(codecName);
    }
    formatter.writeText("codecs", codecNames.join(", "));
    formatter.endl();
    formatter.writeText("default-codec", PDFToolTranslationContext::tr("Suggested codec: UTF-8 or %1").arg(QString::fromLatin1(getDefaultEncoding())));

    formatter.endHeader();

    formatter.endDocument();

    if (options.outputStyle == PDFOutputFormatter::Style::Json)
    {
        if (options.executionContext)
        {
            options.executionContext->setData(formatter.getJsonObject());
        }
    }
    else
    {
        PDFConsole::writeText(formatter.getString(), options.outputCodec);
    }

    return PDFToolExitCode::Success;
}

PDFToolAbstractApplication::Options PDFToolHelpApplication::getOptionsFlags() const
{
    return ConsoleFormat;
}

PDFToolAbstractApplication::PDFToolAbstractApplication(bool isDefault)
{
    PDFToolApplicationStorage::registerApplication(this, isDefault);
}

namespace
{

PDFToolOptionDescriptor makeOption(const QString& id,
                                   const QStringList& names,
                                   const QString& valueName = QString(),
                                   PDFToolValueType valueType = PDFToolValueType::String,
                                   const QStringList& allowedValues = {},
                                   const QString& defaultValue = QString(),
                                   bool required = false,
                                   bool repeatable = false,
                                   bool sensitive = false)
{
    return { id, names, valueName, valueType, allowedValues, defaultValue, required, repeatable, sensitive };
}

void appendOption(QList<PDFToolOptionDescriptor>& options, PDFToolOptionDescriptor option)
{
    for (const PDFToolOptionDescriptor& existing : options)
    {
        if (existing.id == option.id)
        {
            return;
        }
    }
    options.append(std::move(option));
}

void appendPositional(QList<PDFToolPositionalDescriptor>& positionals, PDFToolPositionalDescriptor positional)
{
    for (const PDFToolPositionalDescriptor& existing : positionals)
    {
        if (existing.id == positional.id)
        {
            return;
        }
    }
    positionals.append(std::move(positional));
}

void addDescribedOption(QCommandLineParser* parser,
                        const QList<PDFToolOptionDescriptor>& descriptors,
                        const QString& id,
                        const QString& description)
{
    const auto found = std::find_if(descriptors.cbegin(), descriptors.cend(), [&](const auto& descriptor)
                                    { return descriptor.id == id; });
    if (found == descriptors.cend())
    {
        return;
    }

    QStringList names;
    for (const QString& name : found->names)
    {
        names.append(name.startsWith(QStringLiteral("--")) ? name.mid(2) : name.mid(1));
    }
    parser->addOption(QCommandLineOption(names, description, found->valueName, found->defaultValue));
}

}   // namespace

PDFToolCommandDescriptor PDFToolAbstractApplication::describe() const
{
    const QString command = getStandardString(Command);
    PDFToolCommandDescriptor descriptor;
    descriptor.id = command;
    descriptor.name = getStandardString(Name);
    descriptor.description = getStandardString(Description);
    descriptor.options = describeOptions(getOptionsFlags());
    descriptor.positionals = describePositionals(getOptionsFlags());
    descriptor.capabilities = describeCapabilities(getOptionsFlags());

    if (command == QStringLiteral("preflight") || command == QStringLiteral("ocr") || command == QStringLiteral("capabilities"))
    {
        descriptor.outputFormats = { QStringLiteral("json") };
    }
    else if (getOptionsFlags().testFlag(ConsoleFormat))
    {
        descriptor.outputFormats = { QStringLiteral("html"), QStringLiteral("json"), QStringLiteral("text"), QStringLiteral("xml") };
    }
    descriptor.outputFormats.sort();
    return descriptor;
}

QList<PDFToolOptionDescriptor> PDFToolAbstractApplication::describeOptions(Options optionFlags)
{
    QList<PDFToolOptionDescriptor> options;
    auto add = [&](const QString& id,
                   const QStringList& names,
                   const QString& valueName = QString(),
                   PDFToolValueType valueType = PDFToolValueType::String,
                   const QStringList& allowedValues = {},
                   const QString& defaultValue = QString(),
                   bool required = false,
                   bool repeatable = false,
                   bool sensitive = false)
    {
        appendOption(options, makeOption(id, names, valueName, valueType, allowedValues, defaultValue, required, repeatable, sensitive));
    };

    if (optionFlags.testFlag(ConsoleFormat))
    {
        add(QStringLiteral("console-format"), { QStringLiteral("--console-format") }, QStringLiteral("format"), PDFToolValueType::Enum,
            { QStringLiteral("html"), QStringLiteral("json"), QStringLiteral("text"), QStringLiteral("xml") }, QStringLiteral("text"));
        add(QStringLiteral("text-codec"), { QStringLiteral("--text-codec") }, QStringLiteral("text-codec"), PDFToolValueType::String, {}, QStringLiteral("UTF-8"));
    }
    if (optionFlags.testFlag(DateFormat))
    {
        add(QStringLiteral("date-format"), { QStringLiteral("--date-format") }, QStringLiteral("date-format"), PDFToolValueType::Enum,
            { QStringLiteral("iso"), QStringLiteral("long"), QStringLiteral("rfc2822"), QStringLiteral("short") }, QStringLiteral("short"));
    }
    if (optionFlags.testFlag(OpenDocument))
    {
        add(QStringLiteral("pswd"), { QStringLiteral("--pswd") }, QStringLiteral("password"), PDFToolValueType::String, {}, {}, false, false, true);
        add(QStringLiteral("no-permissive-reading"), { QStringLiteral("--no-permissive-reading") }, {}, PDFToolValueType::Boolean);
    }
    if (optionFlags.testFlag(RenderPage))
    {
        add(QStringLiteral("page-index"), { QStringLiteral("--page-index") }, QStringLiteral("index"), PDFToolValueType::Integer, {}, {}, true);
        add(QStringLiteral("dpi"), { QStringLiteral("--dpi") }, QStringLiteral("dpi"), PDFToolValueType::Integer, {}, QStringLiteral("300"));
        add(QStringLiteral("max-raster-pixels"), { QStringLiteral("--max-raster-pixels") }, QStringLiteral("pixels"), PDFToolValueType::Integer, {}, QStringLiteral("250000000"));
        add(QStringLiteral("output"), { QStringLiteral("--output") }, QStringLiteral("file"), PDFToolValueType::Path, {}, {}, true);
    }
    if (optionFlags.testFlag(Redact))
    {
        add(QStringLiteral("redact-copy-title"), { QStringLiteral("--redact-copy-title") }, {}, PDFToolValueType::Boolean);
        add(QStringLiteral("redact-copy-metadata"), { QStringLiteral("--redact-copy-metadata") }, {}, PDFToolValueType::Boolean);
        add(QStringLiteral("redact-copy-outline"), { QStringLiteral("--redact-copy-outline") }, {}, PDFToolValueType::Boolean);
    }
    if (optionFlags.testFlag(AddBleed))
    {
        add(QStringLiteral("output"), { QStringLiteral("-o"), QStringLiteral("--output") }, QStringLiteral("file"), PDFToolValueType::Path);
        add(QStringLiteral("mode"), { QStringLiteral("--mode") }, QStringLiteral("mode"), PDFToolValueType::Enum,
            { QStringLiteral("mirror"), QStringLiteral("pixel-repeat"), QStringLiteral("stretch") }, QStringLiteral("mirror"));
        add(QStringLiteral("bleed-mm"), { QStringLiteral("--bleed-mm") }, QStringLiteral("mm"), PDFToolValueType::Number, {}, QStringLiteral("3"));
        add(QStringLiteral("bleed-mm-ltrb"), { QStringLiteral("--bleed-mm-ltrb") }, QStringLiteral("ltrb"), PDFToolValueType::Csv);
        add(QStringLiteral("reference-box"), { QStringLiteral("--reference-box") }, QStringLiteral("box"), PDFToolValueType::Enum,
            { QStringLiteral("crop"), QStringLiteral("media"), QStringLiteral("trim") }, QStringLiteral("trim"));
        add(QStringLiteral("dpi"), { QStringLiteral("--dpi") }, QStringLiteral("dpi"), PDFToolValueType::Integer, {}, QStringLiteral("300"));
        add(QStringLiteral("sample-pixels"), { QStringLiteral("--sample-pixels") }, QStringLiteral("n"), PDFToolValueType::Integer, {}, QStringLiteral("1"));
        add(QStringLiteral("force"), { QStringLiteral("--force") }, {}, PDFToolValueType::Boolean);
    }
    if (optionFlags.testFlag(FlattenTransparency))
    {
        add(QStringLiteral("output"), { QStringLiteral("-o"), QStringLiteral("--output") }, QStringLiteral("file"), PDFToolValueType::Path);
        add(QStringLiteral("raster-dpi"), { QStringLiteral("--raster-dpi") }, QStringLiteral("dpi"), PDFToolValueType::Integer, {}, QStringLiteral("300"));
        add(QStringLiteral("line-art-dpi"), { QStringLiteral("--line-art-dpi") }, QStringLiteral("dpi"), PDFToolValueType::Integer, {}, QStringLiteral("600"));
        add(QStringLiteral("text-dpi"), { QStringLiteral("--text-dpi") }, QStringLiteral("dpi"), PDFToolValueType::Integer, {}, QStringLiteral("600"));
        add(QStringLiteral("max-raster-pixels"), { QStringLiteral("--max-raster-pixels") }, QStringLiteral("pixels"), PDFToolValueType::Integer, {}, QStringLiteral("250000000"));
        add(QStringLiteral("no-preserve-spots"), { QStringLiteral("--no-preserve-spots") }, {}, PDFToolValueType::Boolean);
        add(QStringLiteral("preserve-text-vector"), { QStringLiteral("--preserve-text-vector") }, {}, PDFToolValueType::Boolean);
    }
    if (optionFlags.testFlag(RgbToCmyk))
    {
        add(QStringLiteral("output"), { QStringLiteral("-o"), QStringLiteral("--output") }, QStringLiteral("file"), PDFToolValueType::Path);
        add(QStringLiteral("target-profile"), { QStringLiteral("--target-profile") }, QStringLiteral("file"), PDFToolValueType::Path, {}, {}, true);
        add(QStringLiteral("source-rgb-profile"), { QStringLiteral("--source-rgb-profile") }, QStringLiteral("file"), PDFToolValueType::Path);
        add(QStringLiteral("intent"), { QStringLiteral("--intent") }, QStringLiteral("intent"), PDFToolValueType::Enum,
            { QStringLiteral("absolute"), QStringLiteral("perceptual"), QStringLiteral("relative"), QStringLiteral("saturation") }, QStringLiteral("relative"));
        add(QStringLiteral("black-point-compensation"), { QStringLiteral("--black-point-compensation") }, {}, PDFToolValueType::Boolean);
        add(QStringLiteral("no-black-point-compensation"), { QStringLiteral("--no-black-point-compensation") }, {}, PDFToolValueType::Boolean);
        add(QStringLiteral("output-intent"), { QStringLiteral("--output-intent") }, QStringLiteral("policy"), PDFToolValueType::Enum,
            { QStringLiteral("preserve-matching"), QStringLiteral("replace") }, QStringLiteral("replace"));
    }
    if (optionFlags.testFlag(EmptyResultPolicy))
    {
        add(QStringLiteral("fail-if-empty"), { QStringLiteral("--fail-if-empty") }, {}, PDFToolValueType::Boolean);
    }
    if (optionFlags.testFlag(DestructiveWrite))
    {
        add(QStringLiteral("dry-run"), { QStringLiteral("--dry-run") }, {}, PDFToolValueType::Boolean);
        add(QStringLiteral("report"), { QStringLiteral("--report") }, {}, PDFToolValueType::Boolean);
        add(QStringLiteral("overwrite"), { QStringLiteral("--overwrite") }, {}, PDFToolValueType::Boolean);
        if (!optionFlags.testFlag(AddBleed))
        {
            add(QStringLiteral("force"), { QStringLiteral("--force") }, {}, PDFToolValueType::Boolean);
        }
    }
    if (optionFlags.testFlag(ActionList))
    {
        add(QStringLiteral("output"), { QStringLiteral("-o"), QStringLiteral("--output") }, QStringLiteral("file"), PDFToolValueType::Path);
        add(QStringLiteral("output-dir"), { QStringLiteral("--output-dir") }, QStringLiteral("directory"), PDFToolValueType::Path);
        add(QStringLiteral("param"), { QStringLiteral("--param") }, QStringLiteral("key=value"), PDFToolValueType::String, {}, {}, false, true);
    }
    if (optionFlags.testFlag(PreflightProfile))
    {
        add(QStringLiteral("profile"), { QStringLiteral("--profile") }, QStringLiteral("profile"), PDFToolValueType::Path);
        add(QStringLiteral("job-context"), { QStringLiteral("--job-context") }, QStringLiteral("file"), PDFToolValueType::Path);
        add(QStringLiteral("profile-store"), { QStringLiteral("--profile-store") }, QStringLiteral("directory"), PDFToolValueType::Path);
        add(QStringLiteral("decisions"), { QStringLiteral("--decisions") }, QStringLiteral("file"), PDFToolValueType::Path);
        add(QStringLiteral("export-decisions"), { QStringLiteral("--export-decisions") }, QStringLiteral("file"), PDFToolValueType::Path);
        add(QStringLiteral("require-signoff"), { QStringLiteral("--require-signoff") }, {}, PDFToolValueType::Boolean);
        add(QStringLiteral("client"), { QStringLiteral("--client") }, QStringLiteral("id"), PDFToolValueType::String);
        add(QStringLiteral("product"), { QStringLiteral("--product") }, QStringLiteral("id"), PDFToolValueType::String);
        add(QStringLiteral("job-type"), { QStringLiteral("--job-type") }, QStringLiteral("id"), PDFToolValueType::String);
        add(QStringLiteral("press"), { QStringLiteral("--press") }, QStringLiteral("id"), PDFToolValueType::String);
        add(QStringLiteral("stock"), { QStringLiteral("--stock") }, QStringLiteral("id"), PDFToolValueType::String);
        add(QStringLiteral("finishing"), { QStringLiteral("--finishing") }, QStringLiteral("id"), PDFToolValueType::String);
        add(QStringLiteral("param"), { QStringLiteral("--param") }, QStringLiteral("key=value"), PDFToolValueType::String, {}, {}, false, true);
        add(QStringLiteral("checks"), { QStringLiteral("--checks") }, QStringLiteral("ids"), PDFToolValueType::Csv);
    }
    if (optionFlags.testFlag(CapabilityDiscovery))
    {
        add(QStringLiteral("command"), { QStringLiteral("--command") }, QStringLiteral("id"), PDFToolValueType::String);
    }
    if (optionFlags.testFlag(OcrOptions))
    {
        add(QStringLiteral("sidecar"), { QStringLiteral("--sidecar") }, QStringLiteral("path"), PDFToolValueType::Path);
        add(QStringLiteral("dpi"), { QStringLiteral("--dpi") }, QStringLiteral("dpi"), PDFToolValueType::Integer, {}, QString(pdftool::ocr::DEFAULT_OCR_DPI));
        add(QStringLiteral("languages"), { QStringLiteral("--languages") }, QStringLiteral("codes"), PDFToolValueType::Csv, {}, QString(pdftool::ocr::DEFAULT_OCR_LANGUAGES));
        add(QStringLiteral("min-text-chars"), { QStringLiteral("--min-text-chars") }, QStringLiteral("n"), PDFToolValueType::Integer, {}, QString(pdftool::ocr::DEFAULT_OCR_MIN_TEXT_CHARS));
    }
    if (optionFlags.testFlag(VerifyRedaction))
    {
        add(QStringLiteral("verify-redact-copy-title"), { QStringLiteral("--verify-redact-copy-title") }, {}, PDFToolValueType::Boolean);
        add(QStringLiteral("verify-redact-copy-metadata"), { QStringLiteral("--verify-redact-copy-metadata") }, {}, PDFToolValueType::Boolean);
        add(QStringLiteral("verify-redact-copy-outline"), { QStringLiteral("--verify-redact-copy-outline") }, {}, PDFToolValueType::Boolean);
        add(QStringLiteral("verify-redact-allow-incremental"), { QStringLiteral("--verify-redact-allow-incremental") }, {}, PDFToolValueType::Boolean);
    }
    if (optionFlags.testFlag(Diagnostics))
    {
        add(QStringLiteral("output"), { QStringLiteral("--output") }, QStringLiteral("dir"), PDFToolValueType::Path);
        add(QStringLiteral("no-logs"), { QStringLiteral("--no-logs") }, {}, PDFToolValueType::Boolean);
        add(QStringLiteral("no-settings"), { QStringLiteral("--no-settings") }, {}, PDFToolValueType::Boolean);
    }
    if (optionFlags.testFlag(SignatureVerification))
    {
        add(QStringLiteral("ver-no-user-cert"), { QStringLiteral("--ver-no-user-cert") }, {}, PDFToolValueType::Boolean);
        add(QStringLiteral("ver-no-sys-cert"), { QStringLiteral("--ver-no-sys-cert") }, {}, PDFToolValueType::Boolean);
        add(QStringLiteral("ver-no-cert-check"), { QStringLiteral("--ver-no-cert-check") }, {}, PDFToolValueType::Boolean);
        add(QStringLiteral("ver-details"), { QStringLiteral("--ver-details") }, {}, PDFToolValueType::Boolean);
        add(QStringLiteral("ver-ignore-exp-date"), { QStringLiteral("--ver-ignore-exp-date") }, {}, PDFToolValueType::Boolean);
    }
    if (optionFlags.testFlag(XmlExport))
    {
        add(QStringLiteral("xml-export-streams"), { QStringLiteral("--xml-export-streams") }, {}, PDFToolValueType::Boolean);
        add(QStringLiteral("xml-export-streams-as-text"), { QStringLiteral("--xml-export-streams-as-text") }, {}, PDFToolValueType::Boolean);
        add(QStringLiteral("xml-use-indent"), { QStringLiteral("--xml-use-indent") }, {}, PDFToolValueType::Boolean);
        add(QStringLiteral("xml-always-binary"), { QStringLiteral("--xml-always-binary") }, {}, PDFToolValueType::Boolean);
    }
    if (optionFlags.testFlag(Attachments))
    {
        add(QStringLiteral("att-save-n"), { QStringLiteral("--att-save-n") }, QStringLiteral("number"), PDFToolValueType::Integer);
        add(QStringLiteral("att-save-file"), { QStringLiteral("--att-save-file") }, QStringLiteral("file"), PDFToolValueType::Path);
        add(QStringLiteral("att-save-all"), { QStringLiteral("--att-save-all") }, {}, PDFToolValueType::Boolean);
        add(QStringLiteral("att-target-dir"), { QStringLiteral("--att-target-dir") }, QStringLiteral("directory"), PDFToolValueType::Path);
        add(QStringLiteral("att-target-file"), { QStringLiteral("--att-target-file") }, QStringLiteral("target"), PDFToolValueType::Path);
    }
    if (optionFlags.testFlag(ComputeHashes))
    {
        add(QStringLiteral("compute-hashes"), { QStringLiteral("--compute-hashes") }, {}, PDFToolValueType::Boolean);
    }
    if (optionFlags.testFlag(PageSelector))
    {
        add(QStringLiteral("page-first"), { QStringLiteral("--page-first") }, QStringLiteral("number"), PDFToolValueType::Integer);
        add(QStringLiteral("page-last"), { QStringLiteral("--page-last") }, QStringLiteral("number"), PDFToolValueType::Integer);
        add(QStringLiteral("page-select"), { QStringLiteral("--page-select") }, QStringLiteral("number"), PDFToolValueType::Csv);
    }
    if (optionFlags.testFlag(TextAnalysis))
    {
        add(QStringLiteral("text-analysis-alg"), { QStringLiteral("--text-analysis-alg") }, QStringLiteral("algorithm"), PDFToolValueType::Enum,
            { QStringLiteral("auto"), QStringLiteral("content"), QStringLiteral("layout"), QStringLiteral("structure") }, QStringLiteral("auto"));
    }
    if (optionFlags.testFlag(TextShow))
    {
        for (const QString& id : { QStringLiteral("text-show-page-numbers"), QStringLiteral("text-show-struct-title"), QStringLiteral("text-show-struct-lang"),
                                   QStringLiteral("text-show-struct-alt-desc"), QStringLiteral("text-show-struct-expanded-form"), QStringLiteral("text-show-struct-act-text"),
                                   QStringLiteral("text-show-phoneme") })
        {
            add(id, { QStringLiteral("--") + id }, {}, PDFToolValueType::Boolean);
        }
    }
    if (optionFlags.testFlag(VoiceSelector))
    {
        for (const QString& id : { QStringLiteral("voice-name"), QStringLiteral("voice-gender"), QStringLiteral("voice-age"), QStringLiteral("voice-lang-code") })
        {
            add(id, { QStringLiteral("--") + id }, QStringLiteral("value"));
        }
    }
    if (optionFlags.testFlag(TextSpeech))
    {
        add(QStringLiteral("audio-format"), { QStringLiteral("--audio-format") }, QStringLiteral("audio-format"), PDFToolValueType::Enum,
            { QStringLiteral("mp3"), QStringLiteral("wav") }, QStringLiteral("mp3"));
        for (const QString& id : { QStringLiteral("mark-page-numbers"), QStringLiteral("say-page-numbers"), QStringLiteral("say-struct-titles"),
                                   QStringLiteral("say-struct-alt-desc"), QStringLiteral("say-struct-exp-form"), QStringLiteral("say-struct-act-text") })
        {
            add(id, { QStringLiteral("--") + id }, {}, PDFToolValueType::Boolean);
        }
    }
    if (optionFlags.testFlag(CharacterMaps))
    {
        add(QStringLiteral("character-maps"), { QStringLiteral("--character-maps") }, {}, PDFToolValueType::Boolean);
    }
    if (optionFlags.testFlag(ImageWriterSettings))
    {
        add(QStringLiteral("image-format"), { QStringLiteral("--image-format") }, QStringLiteral("format"), PDFToolValueType::String, {}, QStringLiteral("png"));
        add(QStringLiteral("image-subtype"), { QStringLiteral("--image-subtype") }, QStringLiteral("subtype"));
        add(QStringLiteral("image-compress-lvl"), { QStringLiteral("--image-compress-lvl") }, QStringLiteral("level"), PDFToolValueType::Integer, {}, QStringLiteral("9"));
        add(QStringLiteral("image-quality"), { QStringLiteral("--image-quality") }, QStringLiteral("quality"), PDFToolValueType::Integer, {}, QStringLiteral("100"));
        add(QStringLiteral("image-optimized-write"), { QStringLiteral("--image-optimized-write") }, {}, PDFToolValueType::Boolean);
        add(QStringLiteral("image-progressive-scan-write"), { QStringLiteral("--image-progressive-scan-write") }, {}, PDFToolValueType::Boolean);
    }
    if (optionFlags.testFlag(ImageExportSettingsFiles))
    {
        add(QStringLiteral("image-output-dir"), { QStringLiteral("--image-output-dir") }, QStringLiteral("dir"), PDFToolValueType::Path);
        add(QStringLiteral("image-template-fn"), { QStringLiteral("--image-template-fn") }, QStringLiteral("template-file-name"), PDFToolValueType::String, {}, QStringLiteral("Image_%"));
    }
    if (optionFlags.testFlag(ImageExportSettingsResolution))
    {
        add(QStringLiteral("image-res-mode"), { QStringLiteral("--image-res-mode") }, QStringLiteral("mode"), PDFToolValueType::Enum,
            { QStringLiteral("dpi"), QStringLiteral("pixel") }, QStringLiteral("dpi"));
        add(QStringLiteral("image-res-dpi"), { QStringLiteral("--image-res-dpi") }, QStringLiteral("dpi"), PDFToolValueType::Integer);
        add(QStringLiteral("image-res-pixel"), { QStringLiteral("--image-res-pixel") }, QStringLiteral("pixel"), PDFToolValueType::Integer);
    }
    if (optionFlags.testFlag(ColorManagementSystem))
    {
        add(QStringLiteral("cms"), { QStringLiteral("--cms") }, QStringLiteral("cms"), PDFToolValueType::Enum,
            { QStringLiteral("generic"), QStringLiteral("lcms") }, QStringLiteral("lcms"));
        add(QStringLiteral("cms-accuracy"), { QStringLiteral("--cms-accuracy") }, QStringLiteral("accuracy"), PDFToolValueType::Enum,
            { QStringLiteral("high"), QStringLiteral("low"), QStringLiteral("medium") }, QStringLiteral("medium"));
        add(QStringLiteral("cms-color-adaptation"), { QStringLiteral("--cms-color-adaptation") }, QStringLiteral("color-adaptation-method"), PDFToolValueType::Enum,
            { QStringLiteral("bradford"), QStringLiteral("cat02"), QStringLiteral("cat97"), QStringLiteral("none"), QStringLiteral("xyzscaling") }, QStringLiteral("bradford"));
        add(QStringLiteral("cms-intent"), { QStringLiteral("--cms-intent") }, QStringLiteral("intent"), PDFToolValueType::Enum,
            { QStringLiteral("abs"), QStringLiteral("auto"), QStringLiteral("perceptual"), QStringLiteral("rel"), QStringLiteral("saturation") }, QStringLiteral("auto"));
        add(QStringLiteral("cms-black-compensated"), { QStringLiteral("--cms-black-compensated") }, QStringLiteral("bool"), PDFToolValueType::Boolean, {}, QStringLiteral("1"));
        add(QStringLiteral("cms-white-paper-trans"), { QStringLiteral("--cms-white-paper-trans") }, QStringLiteral("bool"), PDFToolValueType::Boolean, {}, QStringLiteral("0"));
        add(QStringLiteral("cms-consider-output-intents"), { QStringLiteral("--cms-consider-output-intents") }, QStringLiteral("bool"), PDFToolValueType::Boolean, {}, QStringLiteral("1"));
        add(QStringLiteral("cms-profile-output"), { QStringLiteral("--cms-profile-output") }, QStringLiteral("profile"), PDFToolValueType::Path);
        add(QStringLiteral("cms-profile-gray"), { QStringLiteral("--cms-profile-gray") }, QStringLiteral("profile"), PDFToolValueType::Path);
        add(QStringLiteral("cms-profile-rgb"), { QStringLiteral("--cms-profile-rgb") }, QStringLiteral("profile"), PDFToolValueType::Path);
        add(QStringLiteral("cms-profile-cmyk"), { QStringLiteral("--cms-profile-cmyk") }, QStringLiteral("profile"), PDFToolValueType::Path);
        add(QStringLiteral("cms-profile-dir"), { QStringLiteral("--cms-profile-dir") }, QStringLiteral("directory"), PDFToolValueType::Path);
    }
    if (optionFlags.testFlag(RenderFlags))
    {
        for (const PDFToolOptions::RenderFeatureInfo& info : PDFToolOptions::getRenderFeatures())
        {
            add(info.option, { QStringLiteral("--") + info.option }, QStringLiteral("bool"), PDFToolValueType::Boolean);
        }
        add(QStringLiteral("render-hw-accel"), { QStringLiteral("--render-hw-accel") }, QStringLiteral("bool"), PDFToolValueType::Boolean, {}, QStringLiteral("1"));
        add(QStringLiteral("render-show-page-stat"), { QStringLiteral("--render-show-page-stat") }, {}, PDFToolValueType::Boolean);
        add(QStringLiteral("render-msaa-samples"), { QStringLiteral("--render-msaa-samples") }, QStringLiteral("samples"), PDFToolValueType::Integer, {}, QStringLiteral("4"));
        add(QStringLiteral("render-rasterizers"), { QStringLiteral("--render-rasterizers") }, QStringLiteral("rasterizers"), PDFToolValueType::Integer);
    }
    if (optionFlags.testFlag(Optimize))
    {
        for (const PDFToolOptions::OptimizeFeatureInfo& info : PDFToolOptions::getOptimizeFlagInfos())
        {
            add(info.option, { QStringLiteral("--") + info.option }, {}, PDFToolValueType::Boolean);
        }
        add(QStringLiteral("opt-images"), { QStringLiteral("--opt-images") }, {}, PDFToolValueType::Boolean);
        add(QStringLiteral("opt-images-mode"), { QStringLiteral("--opt-images-mode") }, QStringLiteral("mode"), PDFToolValueType::Enum,
            { QStringLiteral("auto"), QStringLiteral("custom") }, QStringLiteral("auto"));
        add(QStringLiteral("opt-images-color-mode"), { QStringLiteral("--opt-images-color-mode") }, QStringLiteral("mode"), PDFToolValueType::Enum,
            { QStringLiteral("auto"), QStringLiteral("bitonal"), QStringLiteral("color"), QStringLiteral("gray"), QStringLiteral("preserve") }, QStringLiteral("auto"));
        add(QStringLiteral("opt-images-goal"), { QStringLiteral("--opt-images-goal") }, QStringLiteral("goal"), PDFToolValueType::Enum,
            { QStringLiteral("quality"), QStringLiteral("size") }, QStringLiteral("quality"));
        add(QStringLiteral("opt-images-keep-original"), { QStringLiteral("--opt-images-keep-original") }, QStringLiteral("bool"), PDFToolValueType::Boolean, {}, QStringLiteral("1"));
        add(QStringLiteral("opt-images-preserve-alpha"), { QStringLiteral("--opt-images-preserve-alpha") }, QStringLiteral("bool"), PDFToolValueType::Boolean, {}, QStringLiteral("1"));
        add(QStringLiteral("opt-images-color-alg"), { QStringLiteral("--opt-images-color-alg") }, QStringLiteral("algorithm"), PDFToolValueType::Enum);
        add(QStringLiteral("opt-images-color-dpi"), { QStringLiteral("--opt-images-color-dpi") }, QStringLiteral("dpi"), PDFToolValueType::Integer, {}, QStringLiteral("150"));
        add(QStringLiteral("opt-images-color-jpeg-quality"), { QStringLiteral("--opt-images-color-jpeg-quality") }, QStringLiteral("quality"), PDFToolValueType::Integer, {}, QStringLiteral("85"));
        add(QStringLiteral("opt-images-color-jpx-rate"), { QStringLiteral("--opt-images-color-jpx-rate") }, QStringLiteral("rate"), PDFToolValueType::Number, {}, QStringLiteral("0"));
        add(QStringLiteral("opt-images-color-resample"), { QStringLiteral("--opt-images-color-resample") }, QStringLiteral("filter"), PDFToolValueType::Enum);
        add(QStringLiteral("opt-images-color-png-predictor"), { QStringLiteral("--opt-images-color-png-predictor") }, QStringLiteral("bool"), PDFToolValueType::Boolean, {}, QStringLiteral("1"));
        add(QStringLiteral("opt-images-gray-alg"), { QStringLiteral("--opt-images-gray-alg") }, QStringLiteral("algorithm"), PDFToolValueType::Enum);
        add(QStringLiteral("opt-images-gray-dpi"), { QStringLiteral("--opt-images-gray-dpi") }, QStringLiteral("dpi"), PDFToolValueType::Integer, {}, QStringLiteral("150"));
        add(QStringLiteral("opt-images-gray-jpeg-quality"), { QStringLiteral("--opt-images-gray-jpeg-quality") }, QStringLiteral("quality"), PDFToolValueType::Integer, {}, QStringLiteral("85"));
        add(QStringLiteral("opt-images-gray-jpx-rate"), { QStringLiteral("--opt-images-gray-jpx-rate") }, QStringLiteral("rate"), PDFToolValueType::Number, {}, QStringLiteral("0"));
        add(QStringLiteral("opt-images-gray-resample"), { QStringLiteral("--opt-images-gray-resample") }, QStringLiteral("filter"), PDFToolValueType::Enum);
        add(QStringLiteral("opt-images-gray-png-predictor"), { QStringLiteral("--opt-images-gray-png-predictor") }, QStringLiteral("bool"), PDFToolValueType::Boolean, {}, QStringLiteral("1"));
        add(QStringLiteral("opt-images-bitonal-alg"), { QStringLiteral("--opt-images-bitonal-alg") }, QStringLiteral("algorithm"), PDFToolValueType::Enum);
        add(QStringLiteral("opt-images-bitonal-dpi"), { QStringLiteral("--opt-images-bitonal-dpi") }, QStringLiteral("dpi"), PDFToolValueType::Integer, {}, QStringLiteral("300"));
        add(QStringLiteral("opt-images-bitonal-threshold"), { QStringLiteral("--opt-images-bitonal-threshold") }, QStringLiteral("threshold"), PDFToolValueType::Integer, {}, QStringLiteral("-1"));
        add(QStringLiteral("opt-images-bitonal-resample"), { QStringLiteral("--opt-images-bitonal-resample") }, QStringLiteral("filter"), PDFToolValueType::Enum);
        add(QStringLiteral("opt-images-bitonal-png-predictor"), { QStringLiteral("--opt-images-bitonal-png-predictor") }, QStringLiteral("bool"), PDFToolValueType::Boolean, {}, QStringLiteral("1"));
    }
    if (optionFlags.testFlag(CertStore))
    {
        add(QStringLiteral("list-user-certs"), { QStringLiteral("--list-user-certs") }, QStringLiteral("bool"), PDFToolValueType::Boolean, {}, QStringLiteral("1"));
        add(QStringLiteral("list-system-certs"), { QStringLiteral("--list-system-certs") }, QStringLiteral("bool"), PDFToolValueType::Boolean, {}, QStringLiteral("0"));
    }
    if (optionFlags.testFlag(Encrypt))
    {
        add(QStringLiteral("enc-algorithm"), { QStringLiteral("--enc-algorithm") }, QStringLiteral("encryption-algorithm"), PDFToolValueType::Enum,
            { QStringLiteral("aes-128"), QStringLiteral("aes-256"), QStringLiteral("rc4") }, QStringLiteral("aes-256"));
        add(QStringLiteral("enc-contents"), { QStringLiteral("--enc-contents") }, QStringLiteral("encryption-contents"), PDFToolValueType::Enum,
            { QStringLiteral("all"), QStringLiteral("all-except-metadata"), QStringLiteral("only-embedded-files") }, QStringLiteral("all"));
        add(QStringLiteral("enc-user-password"), { QStringLiteral("--enc-user-password") }, QStringLiteral("user-password"), PDFToolValueType::String, {}, {}, false, false, true);
        add(QStringLiteral("enc-owner-password"), { QStringLiteral("--enc-owner-password") }, QStringLiteral("owner-password"), PDFToolValueType::String, {}, {}, false, false, true);
        add(QStringLiteral("enc-permissions"), { QStringLiteral("--enc-permissions") }, QStringLiteral("permissions"), PDFToolValueType::Integer);
    }

    std::sort(options.begin(), options.end(), [](const auto& left, const auto& right)
              { return left.id < right.id; });
    return options;
}

QList<PDFToolPositionalDescriptor> PDFToolAbstractApplication::describePositionals(Options optionFlags)
{
    QList<PDFToolPositionalDescriptor> positionals;
    if (optionFlags.testFlag(OpenDocument))
    {
        appendPositional(positionals, { QStringLiteral("document"), PDFToolValueType::Path, true, false });
    }
    if (optionFlags.testFlag(Separate))
    {
        appendPositional(positionals, { QStringLiteral("pattern"), PDFToolValueType::String, true, false });
    }
    if (optionFlags.testFlag(Unite))
    {
        appendPositional(positionals, { QStringLiteral("source"), PDFToolValueType::Path, true, true });
        appendPositional(positionals, { QStringLiteral("target"), PDFToolValueType::Path, true, false });
    }
    if (optionFlags.testFlag(Diff))
    {
        appendPositional(positionals, { QStringLiteral("left"), PDFToolValueType::Path, true, false });
        appendPositional(positionals, { QStringLiteral("right"), PDFToolValueType::Path, true, false });
    }
    if (optionFlags.testFlag(ActionList))
    {
        appendPositional(positionals, { QStringLiteral("subcommand"), PDFToolValueType::String, true, false });
        appendPositional(positionals, { QStringLiteral("recipe"), PDFToolValueType::Path, true, false });
        appendPositional(positionals, { QStringLiteral("input"), PDFToolValueType::Path, false, true });
    }
    if (optionFlags.testFlag(Redact))
    {
        appendPositional(positionals, { QStringLiteral("redacteddocument"), PDFToolValueType::Path, true, false });
    }
    if (optionFlags.testFlag(VerifyRedaction))
    {
        appendPositional(positionals, { QStringLiteral("original"), PDFToolValueType::Path, true, false });
        appendPositional(positionals, { QStringLiteral("redacted"), PDFToolValueType::Path, true, false });
    }
    if (optionFlags.testFlag(CertStoreInstall))
    {
        appendPositional(positionals, { QStringLiteral("certificate"), PDFToolValueType::Path, true, false });
    }
    return positionals;
}

QStringList PDFToolAbstractApplication::describeCapabilities(Options optionFlags)
{
    QStringList capabilities;
    auto add = [&](Option option, const QString& id)
    {
        if (optionFlags.testFlag(option))
        {
            capabilities.append(id);
        }
    };
    add(ConsoleFormat, QStringLiteral("output.console"));
    add(OpenDocument, QStringLiteral("document.read"));
    add(SignatureVerification, QStringLiteral("document.signatures"));
    add(XmlExport, QStringLiteral("document.xml"));
    add(Attachments, QStringLiteral("document.attachments"));
    add(ComputeHashes, QStringLiteral("document.hashes"));
    add(PageSelector, QStringLiteral("page.selection"));
    add(TextAnalysis, QStringLiteral("text.analysis"));
    add(TextSpeech, QStringLiteral("text.speech"));
    add(VoiceSelector, QStringLiteral("text.voice"));
    add(CharacterMaps, QStringLiteral("font.character-maps"));
    add(ImageWriterSettings, QStringLiteral("image.write"));
    add(ImageExportSettingsFiles, QStringLiteral("image.export"));
    add(ImageExportSettingsResolution, QStringLiteral("image.resolution"));
    add(ColorManagementSystem, QStringLiteral("color.management"));
    add(RenderFlags, QStringLiteral("render"));
    add(Separate, QStringLiteral("document.split"));
    add(Unite, QStringLiteral("document.merge"));
    add(Diff, QStringLiteral("document.compare"));
    add(Optimize, QStringLiteral("document.optimize"));
    add(CertStore, QStringLiteral("certificates.store"));
    add(CertStoreInstall, QStringLiteral("certificates.install"));
    add(Encrypt, QStringLiteral("document.encrypt"));
    add(Redact, QStringLiteral("document.redact"));
    add(VerifyRedaction, QStringLiteral("document.redaction.verify"));
    add(DestructiveWrite, QStringLiteral("document.write.destructive"));
    add(EmptyResultPolicy, QStringLiteral("output.empty-result.policy"));
    add(AddBleed, QStringLiteral("fixup.add-bleed"));
    add(FlattenTransparency, QStringLiteral("fixup.flatten-transparency"));
    add(RgbToCmyk, QStringLiteral("fixup.rgb-to-cmyk"));
    add(PreflightProfile, QStringLiteral("preflight.run"));
    add(OcrOptions, QStringLiteral("ocr.client"));
    add(Diagnostics, QStringLiteral("diagnostics.bundle"));
    add(CapabilityDiscovery, QStringLiteral("pdftool.discovery.v1"));
    add(ActionList, QStringLiteral("action-list.execute"));
    capabilities.sort();
    return capabilities;
}

void PDFToolAbstractApplication::initializeCommandLineParser(QCommandLineParser* parser) const
{
    Options optionFlags = getOptionsFlags();
    const QList<PDFToolOptionDescriptor> optionDescriptors = describeOptions(optionFlags);

    if (optionFlags.testFlag(ConsoleFormat))
    {
        addDescribedOption(parser, optionDescriptors, QStringLiteral("console-format"), QStringLiteral("Console output text format (valid values: text|xml|html|json)."));
        addDescribedOption(parser, optionDescriptors, QStringLiteral("text-codec"), QStringLiteral("Text codec used when writing text output to redirected standard output. UTF-8 is default."));
    }

    if (optionFlags.testFlag(DateFormat))
    {
        addDescribedOption(parser, optionDescriptors, QStringLiteral("date-format"), QStringLiteral("Console output date/time format (valid values: short|long|iso|rfc2822)."));
    }

    if (optionFlags.testFlag(OpenDocument))
    {
        addDescribedOption(parser, optionDescriptors, QStringLiteral("pswd"), QStringLiteral("Password for encrypted document."));
        parser->addPositionalArgument("document", "Processed document.");
        addDescribedOption(parser, optionDescriptors, QStringLiteral("no-permissive-reading"), QStringLiteral("Do not attempt to fix damaged documents."));
    }

    if (optionFlags.testFlag(RenderPage))
    {
        addDescribedOption(parser, optionDescriptors, QStringLiteral("page-index"), QStringLiteral("Zero-based page index to render."));
        addDescribedOption(parser, optionDescriptors, QStringLiteral("dpi"), QStringLiteral("Rasterization resolution in DPI."));
        addDescribedOption(parser, optionDescriptors, QStringLiteral("max-raster-pixels"), QStringLiteral("Maximum pixels permitted for the render."));
        addDescribedOption(parser, optionDescriptors, QStringLiteral("output"), QStringLiteral("Output PNG file."));
    }

    if (optionFlags.testFlag(Separate))
    {
        parser->addPositionalArgument("pattern", "Page pattern, must contain '%' character if multiple pages are selected.");
    }

    if (optionFlags.testFlag(Unite))
    {
        parser->addPositionalArgument("source", "Documents to be merged into single document.", "file1.pdf [file2.pdf, ...]");
        parser->addPositionalArgument("target", "Merged document filename.");
    }

    if (optionFlags.testFlag(Diff))
    {
        parser->addPositionalArgument("left", "Left (old) document to be compared.");
        parser->addPositionalArgument("right", "Right (new) document to be compared.");
    }

    if (optionFlags.testFlag(RepairDiff))
    {
        parser->addPositionalArgument("before", "Source document used as the repair baseline.");
        parser->addPositionalArgument("after", "Serialized candidate document to compare.");
        parser->addOption(QCommandLineOption("pswd", "Password for encrypted documents.", "password"));
        parser->addOption(QCommandLineOption("no-permissive-reading", "Do not attempt to fix damaged documents."));
        parser->addOption(QCommandLineOption("render-dpi", "Deterministic comparison render resolution.", "dpi", "144"));
        parser->addOption(QCommandLineOption("no-visual", "Only produce the semantic structural report."));
        parser->addOption(QCommandLineOption("render-dir", "Directory for before/after/diff PNG artifacts.", "directory"));
        parser->addOption(QCommandLineOption("max-rendered-pages", "Maximum number of pages rendered.", "pages", "200"));
        parser->addOption(QCommandLineOption("max-render-pixels", "Maximum total pixels rendered.", "pixels", "250000000"));
        parser->addOption(QCommandLineOption("channel-tolerance", "Per-channel visual diff tolerance.", "delta", "2"));
        parser->addOption(QCommandLineOption("allow-page-boxes", "Classify page-box changes as expected."));
        parser->addOption(QCommandLineOption("allow-page-content", "Classify page-content changes as expected."));
        parser->addOption(QCommandLineOption("allow-images", "Classify image changes as expected."));
        parser->addOption(QCommandLineOption("allow-fonts", "Classify font changes as expected."));
        parser->addOption(QCommandLineOption("allow-color-spaces", "Classify color-space changes as expected."));
        parser->addOption(QCommandLineOption("allow-output-intent", "Classify output-intent changes as expected."));
        parser->addOption(QCommandLineOption("allow-metadata", "Classify metadata changes as expected."));
        parser->addOption(QCommandLineOption("allow-annotations", "Classify annotation changes as expected."));
        parser->addOption(QCommandLineOption("allow-signatures", "Classify signature changes as expected."));
    }

    if (optionFlags.testFlag(Repair))
    {
        parser->addPositionalArgument("document", "Source PDF for the repair transaction.");
        parser->addOption(QCommandLineOption("operation", "Registered repair operation id.", "id"));
        if (!optionFlags.testFlag(PreflightProfile))
        {
            parser->addOption(QCommandLineOption("param", "Typed operation parameter as key=value; may be repeated.", "key=value"));
        }
        parser->addOption(QCommandLineOption("output", "Final output PDF path.", "file"));
        parser->addOption(QCommandLineOption("report-file", "Portable operation report JSON path.", "file"));
        parser->addOption(QCommandLineOption("render-dir", "Directory for repair-diff artifacts.", "directory"));
        parser->addOption(QCommandLineOption("list-operations", "List registered repair operation descriptors."));
        parser->addOption(QCommandLineOption("allow-incomplete", "Allow an incomplete diff result to be returned for review; never auto-commit it."));
        parser->addOption(QCommandLineOption("pswd", "Password for an encrypted source PDF.", "password"));
        parser->addOption(QCommandLineOption("no-permissive-reading", "Do not attempt to fix damaged documents."));
    }

    if (optionFlags.testFlag(ActionList))
    {
        parser->addPositionalArgument("subcommand", "Action List operation: validate|plan|run|batch.");
        parser->addPositionalArgument("recipe", "Action List JSON recipe.");
        parser->addPositionalArgument("input", "Input PDF (or multiple PDFs for batch).", "input.pdf ...");
        parser->addOption(QCommandLineOption(QStringList{ QStringLiteral("o"), QStringLiteral("output") }, "Output PDF for action-list run.", "file"));
        parser->addOption(QCommandLineOption("output-dir", "Output directory for action-list batch.", "directory"));
        parser->addOption(QCommandLineOption("param", "Invocation binding as key=value; may be repeated.", "key=value"));
        parser->addOption(QCommandLineOption("pswd", "Password for encrypted input PDFs.", "password"));
        parser->addOption(QCommandLineOption("no-permissive-reading", "Do not attempt to fix damaged documents."));
    }

    if (optionFlags.testFlag(Redact))
    {
        parser->addPositionalArgument("redacteddocument", "Output redacted document filename.");
        parser->addOption(QCommandLineOption("redact-copy-title", "Copy source title into the redacted document."));
        parser->addOption(QCommandLineOption("redact-copy-metadata", "Copy source metadata into the redacted document."));
        parser->addOption(QCommandLineOption("redact-copy-outline", "Copy source outline into the redacted document."));
    }

    if (optionFlags.testFlag(AddBleed))
    {
        addDescribedOption(parser, optionDescriptors, QStringLiteral("output"), QStringLiteral("Output document filename."));
        addDescribedOption(parser, optionDescriptors, QStringLiteral("mode"), QStringLiteral("Bleed fill mode: mirror|pixel-repeat|stretch."));
        addDescribedOption(parser, optionDescriptors, QStringLiteral("bleed-mm"), QStringLiteral("Uniform bleed distance in millimeters."));
        addDescribedOption(parser, optionDescriptors, QStringLiteral("bleed-mm-ltrb"), QStringLiteral("Per-side bleed in millimeters as left,top,right,bottom."));
        addDescribedOption(parser, optionDescriptors, QStringLiteral("reference-box"), QStringLiteral("Reference content box: trim|crop|media."));
        addDescribedOption(parser, optionDescriptors, QStringLiteral("dpi"), QStringLiteral("Rasterization DPI for edge sampling."));
        addDescribedOption(parser, optionDescriptors, QStringLiteral("sample-pixels"), QStringLiteral("Edge sample depth in pixels for pixel-repeat/stretch."));
        addDescribedOption(parser, optionDescriptors, QStringLiteral("force"), QStringLiteral("Ignore skip-if-already-bleeding heuristic."));
    }
    if (optionFlags.testFlag(FlattenTransparency))
    {
        addDescribedOption(parser, optionDescriptors, QStringLiteral("output"), QStringLiteral("Output document filename."));
        addDescribedOption(parser, optionDescriptors, QStringLiteral("raster-dpi"), QStringLiteral("Resolution used to rasterize flattened pages."));
        addDescribedOption(parser, optionDescriptors, QStringLiteral("line-art-dpi"), QStringLiteral("Reserved vector/line-art resolution policy."));
        addDescribedOption(parser, optionDescriptors, QStringLiteral("text-dpi"), QStringLiteral("Reserved text resolution policy."));
        addDescribedOption(parser, optionDescriptors, QStringLiteral("max-raster-pixels"), QStringLiteral("Maximum pixels permitted for one flattened page."));
        addDescribedOption(parser, optionDescriptors, QStringLiteral("no-preserve-spots"), QStringLiteral("Allow process-color raster output when spot colors are present."));
        addDescribedOption(parser, optionDescriptors, QStringLiteral("preserve-text-vector"), QStringLiteral("Request vector/text preservation; currently rejected by the full-page production mode."));
    }

    if (optionFlags.testFlag(RgbToCmyk))
    {
        addDescribedOption(parser, optionDescriptors, QStringLiteral("output"), QStringLiteral("Output document filename."));
        addDescribedOption(parser, optionDescriptors, QStringLiteral("target-profile"), QStringLiteral("Target CMYK ICC profile."));
        addDescribedOption(parser, optionDescriptors, QStringLiteral("source-rgb-profile"), QStringLiteral("Optional source RGB ICC profile for untagged DeviceRGB."));
        addDescribedOption(parser, optionDescriptors, QStringLiteral("intent"), QStringLiteral("Rendering intent: perceptual|relative|absolute|saturation."));
        addDescribedOption(parser, optionDescriptors, QStringLiteral("black-point-compensation"), QStringLiteral("Enable black-point compensation."));
        addDescribedOption(parser, optionDescriptors, QStringLiteral("no-black-point-compensation"), QStringLiteral("Disable black-point compensation."));
        addDescribedOption(parser, optionDescriptors, QStringLiteral("output-intent"), QStringLiteral("OutputIntent policy: replace|preserve-matching."));
    }

    if (optionFlags.testFlag(EmptyResultPolicy))
    {
        addDescribedOption(parser, optionDescriptors, QStringLiteral("fail-if-empty"),
                           QStringLiteral("Exit with 1 (findings) when the command extracted nothing."));
    }

    if (optionFlags.testFlag(DestructiveWrite))
    {
        // add-bleed keeps --overwrite/--dry-run/--report shared with unite/separate via
        // registerDestructiveWriteOptions(); it does not register the legacy --force
        // alias because --force there already means "ignore skip-if-already-bleeding".
        const bool registerForceAlias = !optionFlags.testFlag(AddBleed);
        addDescribedOption(parser, optionDescriptors, QStringLiteral("dry-run"), QStringLiteral("Compute the result but do not write an output file."));
        addDescribedOption(parser, optionDescriptors, QStringLiteral("report"), QStringLiteral("Print a summary of the pending write operation."));
        addDescribedOption(parser, optionDescriptors, QStringLiteral("overwrite"), QStringLiteral("Overwrite an existing output file without confirmation."));
        if (registerForceAlias)
        {
            addDescribedOption(parser, optionDescriptors, QStringLiteral("force"), QStringLiteral("Overwrite an existing output file without confirmation (legacy alias of --overwrite)."));
        }
    }

    if (optionFlags.testFlag(PreflightProfile))
    {
        addDescribedOption(parser, optionDescriptors, QStringLiteral("profile"), QStringLiteral("Explicit Loop preflight profile (JSON); bypasses contextual selection."));
        addDescribedOption(parser, optionDescriptors, QStringLiteral("job-context"), QStringLiteral("Structured production context JSON."));
        addDescribedOption(parser, optionDescriptors, QStringLiteral("profile-store"), QStringLiteral("Directory containing versioned contextual profile JSON sources."));
        addDescribedOption(parser, optionDescriptors, QStringLiteral("decisions"), QStringLiteral("Standalone operator decision JSON to import."));
        addDescribedOption(parser, optionDescriptors, QStringLiteral("export-decisions"), QStringLiteral("Write the normalized operator decision JSON after the run."));
        addDescribedOption(parser, optionDescriptors, QStringLiteral("require-signoff"), QStringLiteral("Require an active accept, waive, or override decision for every error finding."));
        addDescribedOption(parser, optionDescriptors, QStringLiteral("client"), QStringLiteral("Stable client identifier."));
        addDescribedOption(parser, optionDescriptors, QStringLiteral("product"), QStringLiteral("Stable product identifier."));
        addDescribedOption(parser, optionDescriptors, QStringLiteral("job-type"), QStringLiteral("Stable job-type identifier."));
        addDescribedOption(parser, optionDescriptors, QStringLiteral("press"), QStringLiteral("Stable press/device identifier."));
        addDescribedOption(parser, optionDescriptors, QStringLiteral("stock"), QStringLiteral("Stable stock identifier."));
        addDescribedOption(parser, optionDescriptors, QStringLiteral("finishing"), QStringLiteral("Stable finishing identifier."));
        addDescribedOption(parser, optionDescriptors, QStringLiteral("param"), QStringLiteral("Profile variable binding as key=value; may be repeated. Overrides job-spec and profile defaults."));
        addDescribedOption(parser, optionDescriptors, QStringLiteral("checks"), QStringLiteral("Comma-separated preflight check ids for targeted revalidation; default runs all enabled checks."));
    }

    if (optionFlags.testFlag(CapabilityDiscovery))
    {
        addDescribedOption(parser, optionDescriptors, QStringLiteral("command"), QStringLiteral("Limit discovery to one stable command ID."));
    }

    if (optionFlags.testFlag(OcrOptions))
    {
        parser->addOption(QCommandLineOption("sidecar", "Path to LoopOcrService executable.", "path"));
        parser->addOption(QCommandLineOption("dpi", "Rasterization DPI for OCR pages.", "dpi", QString(pdftool::ocr::DEFAULT_OCR_DPI)));
        parser->addOption(QCommandLineOption("languages", "Comma-separated EasyOCR language codes (ISO 639-1).", "codes", QString(pdftool::ocr::DEFAULT_OCR_LANGUAGES)));
        parser->addOption(QCommandLineOption("min-text-chars", "Skip OCR when page has at least this many non-whitespace characters.", "n", QString(pdftool::ocr::DEFAULT_OCR_MIN_TEXT_CHARS)));
    }

    if (optionFlags.testFlag(VerifyRedaction))
    {
        parser->addPositionalArgument("original", "Original document containing redact annotations.");
        parser->addPositionalArgument("redacted", "Redacted output document to verify.");
        parser->addOption(QCommandLineOption("verify-redact-copy-title", "Original redaction copied the title."));
        parser->addOption(QCommandLineOption("verify-redact-copy-metadata", "Original redaction copied metadata."));
        parser->addOption(QCommandLineOption("verify-redact-copy-outline", "Original redaction copied outline."));
        parser->addOption(QCommandLineOption("verify-redact-allow-incremental", "Do not fail when the output trailer contains /Prev."));
    }

    if (optionFlags.testFlag(Diagnostics))
    {
        parser->addOption(QCommandLineOption("output", "Directory the diagnostics bundle directory is created under (default: current directory).", "dir"));
        parser->addOption(QCommandLineOption("no-logs", "Do not include the rotated log files in the bundle."));
    }

    if (optionFlags.testFlag(SignatureVerification))
    {
        parser->addOption(QCommandLineOption("ver-no-user-cert", "Disable user certificate store."));
        parser->addOption(QCommandLineOption("ver-no-sys-cert", "Disable system certificate store."));
        parser->addOption(QCommandLineOption("ver-no-cert-check", "Disable certificate validation."));
        parser->addOption(QCommandLineOption("ver-details", "Print details (including certificate chain, if found)."));
        parser->addOption(QCommandLineOption("ver-ignore-exp-date", "Ignore certificate expiration date."));
    }

    if (optionFlags.testFlag(XmlExport))
    {
        parser->addOption(QCommandLineOption("xml-export-streams", "Export streams as hexadecimally encoded data. By default, stream data are not exported."));
        parser->addOption(QCommandLineOption("xml-export-streams-as-text", "Export streams as text, if possible."));
        parser->addOption(QCommandLineOption("xml-use-indent", "Use automatic indent when writing output xml file."));
        parser->addOption(QCommandLineOption("xml-always-binary", "Do not try to attempt transform strings to text."));
    }

    if (optionFlags.testFlag(Attachments))
    {
        parser->addOption(QCommandLineOption("att-save-n", "Save the specified file attached in document. File name is, by default, same as attachment, it can be changed by a switch.", "number", QString()));
        parser->addOption(QCommandLineOption("att-save-file", "Save the specified file attached in document. File name is, by default, same as attachment, it can be changed by a switch.", "file", QString()));
        parser->addOption(QCommandLineOption("att-save-all", "Save all attachments to target directory."));
        parser->addOption(QCommandLineOption("att-target-dir", "Target directory to which is attachment saved.", "directory", QString()));
        parser->addOption(QCommandLineOption("att-target-file", "File, to which is attachment saved.", "target", QString()));
    }

    if (optionFlags.testFlag(ComputeHashes))
    {
        parser->addOption(QCommandLineOption("compute-hashes", "Compute hashes (MD5, SHA1, SHA256...) of document."));
    }

    if (optionFlags.testFlag(PageSelector))
    {
        parser->addOption(QCommandLineOption("page-first", "First page of page range.", "number"));
        parser->addOption(QCommandLineOption("page-last", "Last page of page range.", "number"));
        parser->addOption(QCommandLineOption("page-select", "Choose arbitrary pages, in form '1,5,3,7-11,-29,43-.'.", "number"));
    }

    if (optionFlags.testFlag(TextAnalysis))
    {
        parser->addOption(QCommandLineOption("text-analysis-alg", "Text analysis algorithm (auto - select automatically, layout - perform automatic layout algorithm, content - simple content stream reading order, structure - use tagged document structure).", "algorithm", "auto"));
    }

    if (optionFlags.testFlag(TextShow))
    {
        parser->addOption(QCommandLineOption("text-show-page-numbers", "Show page numbers in extracted text."));
        parser->addOption(QCommandLineOption("text-show-struct-title", "Show title extracted from structure tree."));
        parser->addOption(QCommandLineOption("text-show-struct-lang", "Show language extracted from structure tree."));
        parser->addOption(QCommandLineOption("text-show-struct-alt-desc", "Show alternative description extracted from structure tree."));
        parser->addOption(QCommandLineOption("text-show-struct-expanded-form", "Show expanded form extracted from structure tree."));
        parser->addOption(QCommandLineOption("text-show-struct-act-text", "Show actual text extracted from structure tree."));
        parser->addOption(QCommandLineOption("text-show-phoneme", "Show phoneme extracted from structure tree."));
    }

    if (optionFlags.testFlag(VoiceSelector))
    {
        parser->addOption(QCommandLineOption("voice-name", "Choose voice name for text-to-speech engine.", "name"));
        parser->addOption(QCommandLineOption("voice-gender", "Choose voice gender for text-to-speech engine.", "gender"));
        parser->addOption(QCommandLineOption("voice-age", "Choose voice age for text-to-speech engine.", "age"));
        parser->addOption(QCommandLineOption("voice-lang-code", "Choose voice language code for text-to-speech engine.", "code"));
    }

    if (optionFlags.testFlag(TextSpeech))
    {
        parser->addOption(QCommandLineOption("audio-format", "Audio fromat, valid values are wav/mp3.", "audio format", "mp3"));
        parser->addOption(QCommandLineOption("mark-page-numbers", "Mark page numbers in audio stream."));
        parser->addOption(QCommandLineOption("say-page-numbers", "Say page numbers."));
        parser->addOption(QCommandLineOption("say-struct-titles", "Say titles extracted from structure tree (only for tagged pdf)."));
        parser->addOption(QCommandLineOption("say-struct-alt-desc", "Say alternative descriptions extracted from structure tree (only for tagged pdf)."));
        parser->addOption(QCommandLineOption("say-struct-exp-form", "Say expanded form extracted from structure tree (only for tagged pdf)."));
        parser->addOption(QCommandLineOption("say-struct-act-text", "Say actual text extracted from structure tree (only for tagged pdf)."));
    }

    if (optionFlags.testFlag(CharacterMaps))
    {
        parser->addOption(QCommandLineOption("character-maps", "Show character maps for embedded fonts."));
    }

    if (optionFlags.testFlag(ImageWriterSettings))
    {
        parser->addOption(QCommandLineOption("image-format", "Image format. Common formats as png, jpeg, are supported.", "format", "png"));
        parser->addOption(QCommandLineOption("image-subtype", "Image format subtype. Some image formats can have this setting.", "subtype"));
        parser->addOption(QCommandLineOption("image-compress-lvl", "Image compression level. Different formats can have different meaning.", "level", "9"));
        parser->addOption(QCommandLineOption("image-quality", "Image quality. Different formats can have different meaning.", "quality", "100"));
        parser->addOption(QCommandLineOption("image-optimized-write", "Use optimized write mode."));
        parser->addOption(QCommandLineOption("image-progressive-scan-write", "Use image progressive scan mode."));
    }

    if (optionFlags.testFlag(ImageExportSettingsFiles))
    {
        parser->addOption(QCommandLineOption("image-output-dir", "Output directory, where images are saved.", "dir"));
        parser->addOption(QCommandLineOption("image-template-fn", "Template file name, must contain '%' character, must not contain suffix.", "template file name", "Image_%"));
    }

    if (optionFlags.testFlag(ImageExportSettingsResolution))
    {
        parser->addOption(QCommandLineOption("image-res-mode", "Image resolution mode (valid values are dpi|pixel). Dpi is default.", "mode", "dpi"));
        parser->addOption(QCommandLineOption("image-res-dpi", "DPI resolution of target image.", "dpi"));
        parser->addOption(QCommandLineOption("image-res-pixel", "Pixel resolution of target image.", "pixel"));
    }

    if (optionFlags.testFlag(ColorManagementSystem))
    {
        parser->addOption(QCommandLineOption("cms", "Color management system. Valid values are generic|lcms.", "cms", "lcms"));
        parser->addOption(QCommandLineOption("cms-accuracy", "Accuracy of cms system. Valid values are low|medium|high. Higher accuracy means higher memory consumption.", "accuracy", "medium"));
        parser->addOption(QCommandLineOption("cms-color-adaptation", "Color adaptation method for XYZ whitepoint scaling. Valid values are none|xyzscaling|cat97|cat02|bradford. Higher accuracy means higher memory consumption.", "color-adaptation-method", "bradford"));
        parser->addOption(QCommandLineOption("cms-intent", "Rendering intent. Valid values are auto|perceptual|abs|rel|saturation.", "intent", "auto"));
        parser->addOption(QCommandLineOption("cms-black-compensated", "Black point compensation.", "bool", "1"));
        parser->addOption(QCommandLineOption("cms-white-paper-trans", "Transform also color of paper using cms.", "bool", "0"));
        parser->addOption(QCommandLineOption("cms-consider-output-intents", "Consider output rendering intents in the document.", "bool", "1"));
        parser->addOption(QCommandLineOption("cms-profile-output", "Output color profile.", "profile"));
        parser->addOption(QCommandLineOption("cms-profile-gray", "Gray color profile for gray device.", "profile"));
        parser->addOption(QCommandLineOption("cms-profile-rgb", "RGB color profile for RGB device.", "profile"));
        parser->addOption(QCommandLineOption("cms-profile-cmyk", "CMYK color profile for CMYK device.", "profile"));
        parser->addOption(QCommandLineOption("cms-profile-dir", "External directory containing color profiles.", "directory"));
    }

    if (optionFlags.testFlag(RenderFlags))
    {
        const pdf::PDFRenderer::Features defaultFeatures = pdf::PDFRenderer::getDefaultFeatures();
        for (const PDFToolOptions::RenderFeatureInfo& info : PDFToolOptions::getRenderFeatures())
        {
            parser->addOption(QCommandLineOption(info.option, info.description, "bool", defaultFeatures.testFlag(info.feature) ? "1" : "0"));
        }

        parser->addOption(QCommandLineOption("render-hw-accel", "Use hardware acceleration (using GPU).", "bool", "1"));
        parser->addOption(QCommandLineOption("render-show-page-stat", "Show page rendering statistics."));
        parser->addOption(QCommandLineOption("render-msaa-samples", "MSAA sample count for GPU rendering.", "samples", "4"));
        parser->addOption(QCommandLineOption("render-rasterizers", "Number of rasterizer contexts.", "rasterizers", QString::number(pdf::PDFRasterizerPool::getDefaultRasterizerCount())));
    }

    if (optionFlags.testFlag(Optimize))
    {
        for (const PDFToolOptions::OptimizeFeatureInfo& info : PDFToolOptions::getOptimizeFlagInfos())
        {
            parser->addOption(QCommandLineOption(info.option, info.description));
        }

        parser->addOption(QCommandLineOption("opt-images", "Enable image optimization/compression."));
        parser->addOption(QCommandLineOption("opt-images-mode", "Image optimization mode (auto|custom).", "mode", "auto"));
        parser->addOption(QCommandLineOption("opt-images-color-mode", "Image color mode (auto|preserve|color|gray|bitonal).", "mode", "auto"));
        parser->addOption(QCommandLineOption("opt-images-goal", "Optimization goal (quality|size).", "goal", "quality"));
        parser->addOption(QCommandLineOption("opt-images-keep-original", "Keep original stream if compression is not smaller.", "bool", "1"));
        parser->addOption(QCommandLineOption("opt-images-preserve-alpha", "Preserve transparency using soft masks when needed.", "bool", "1"));

        parser->addOption(QCommandLineOption("opt-images-color-alg", "Color image compression algorithm (auto|flate|jpeg|jpx|runlength).", "algorithm", "auto"));
        parser->addOption(QCommandLineOption("opt-images-color-dpi", "Target DPI for color images (0 disables).", "dpi", "150"));
        parser->addOption(QCommandLineOption("opt-images-color-jpeg-quality", "JPEG quality for color images (0-100).", "quality", "85"));
        parser->addOption(QCommandLineOption("opt-images-color-jpx-rate", "JPEG2000 rate for color images (0=lossless).", "rate", "0"));
        parser->addOption(QCommandLineOption("opt-images-color-resample", "Resample filter for color images (nearest|bilinear|bicubic|lanczos).", "filter", "bicubic"));
        parser->addOption(QCommandLineOption("opt-images-color-png-predictor", "Enable PNG predictor for flate (color).", "bool", "1"));

        parser->addOption(QCommandLineOption("opt-images-gray-alg", "Grayscale image compression algorithm (auto|flate|jpeg|jpx|runlength).", "algorithm", "auto"));
        parser->addOption(QCommandLineOption("opt-images-gray-dpi", "Target DPI for grayscale images (0 disables).", "dpi", "150"));
        parser->addOption(QCommandLineOption("opt-images-gray-jpeg-quality", "JPEG quality for grayscale images (0-100).", "quality", "85"));
        parser->addOption(QCommandLineOption("opt-images-gray-jpx-rate", "JPEG2000 rate for grayscale images (0=lossless).", "rate", "0"));
        parser->addOption(QCommandLineOption("opt-images-gray-resample", "Resample filter for grayscale images (nearest|bilinear|bicubic|lanczos).", "filter", "bicubic"));
        parser->addOption(QCommandLineOption("opt-images-gray-png-predictor", "Enable PNG predictor for flate (grayscale).", "bool", "1"));

        parser->addOption(QCommandLineOption("opt-images-bitonal-alg", "Bitonal image compression algorithm (auto|flate|runlength|ccittg4|jbig2).", "algorithm", "auto"));
        parser->addOption(QCommandLineOption("opt-images-bitonal-dpi", "Target DPI for bitonal images (0 disables).", "dpi", "300"));
        parser->addOption(QCommandLineOption("opt-images-bitonal-threshold", "Bitonal threshold (0-255, -1=auto).", "threshold", "-1"));
        parser->addOption(QCommandLineOption("opt-images-bitonal-resample", "Resample filter for bitonal images (nearest|bilinear|bicubic|lanczos).", "filter", "bicubic"));
        parser->addOption(QCommandLineOption("opt-images-bitonal-png-predictor", "Enable PNG predictor for flate (bitonal).", "bool", "1"));
    }

    if (optionFlags.testFlag(CertStore))
    {
        parser->addOption(QCommandLineOption("list-user-certs", "Show list of user certificates.", "bool", "1"));
        parser->addOption(QCommandLineOption("list-system-certs", "Show list of system certificates.", "bool", "0"));
    }

    if (optionFlags.testFlag(CertStoreInstall))
    {
        parser->addPositionalArgument("certificate", "Certificate file");
    }

    if (optionFlags.testFlag(Encrypt))
    {
        parser->addOption(QCommandLineOption("enc-algorithm", "Encryption algorithm (valid values: rc4|aes-128|aes-256).", "encryption algorithm", "aes-256"));
        parser->addOption(QCommandLineOption("enc-contents", "Encryption scope (valid values: all|all-except-metadata|only-embedded-files).", "encryption contents", "all"));
        parser->addOption(QCommandLineOption("enc-user-password", "User password (for document reading).", "user password"));
        parser->addOption(QCommandLineOption("enc-owner-password", "Owner password.", "owner password"));
        parser->addOption(QCommandLineOption("enc-permissions", "Document permissions (flags represented as a number).", "permissions"));
    }
}

PDFToolOptions PDFToolAbstractApplication::getOptions(QCommandLineParser* parser, PDFToolExecutionContext* executionContext) const
{
    PDFToolOptions options;
    options.executionContext = executionContext;

    QStringList positionalArguments = parser->positionalArguments();

    Options optionFlags = getOptionsFlags();
    if (optionFlags.testFlag(ConsoleFormat))
    {
        QString consoleFormat = parser->value("console-format");
        if (consoleFormat == "text")
        {
            options.outputStyle = PDFOutputFormatter::Style::Text;
        }
        else if (consoleFormat == "xml")
        {
            options.outputStyle = PDFOutputFormatter::Style::Xml;
        }
        else if (consoleFormat == "html")
        {
            options.outputStyle = PDFOutputFormatter::Style::Html;
        }
        else if (consoleFormat == "json")
        {
            options.outputStyle = PDFOutputFormatter::Style::Json;
        }
        else
        {
            if (!consoleFormat.isEmpty())
            {
                PDFConsole::writeError(PDFToolTranslationContext::tr("Unknown console format '%1'. Defaulting to text console format.").arg(consoleFormat), options.outputCodec);
            }

            options.outputStyle = PDFOutputFormatter::Style::Text;
        }

        if (!parser->isSet("console-format"))
        {
            const QString command = getStandardString(Command);
            if (command == QStringLiteral("preflight") || command == QStringLiteral("ocr") || command == QStringLiteral("capabilities"))
            {
                options.outputStyle = PDFOutputFormatter::Style::Json;
            }
        }

        options.outputCodec = getEncoding(parser->value("text-codec"));
    }

    if (optionFlags.testFlag(DateFormat))
    {
        QString dateFormat = parser->value("date-format");
        if (dateFormat == "short")
        {
            options.outputDateFormat = PDFToolOptions::LocaleShortDate;
        }
        else if (dateFormat == "long")
        {
            options.outputDateFormat = PDFToolOptions::LocaleLongDate;
        }
        else if (dateFormat == "iso")
        {
            options.outputDateFormat = PDFToolOptions::ISODate;
        }
        else if (dateFormat == "rfc2822")
        {
            options.outputDateFormat = PDFToolOptions::RFC2822Date;
        }
        else if (!dateFormat.isEmpty())
        {
            PDFConsole::writeError(PDFToolTranslationContext::tr("Unknown console date/time format '%1'. Defaulting to short date/time format.").arg(dateFormat), options.outputCodec);
        }
    }

    if (optionFlags.testFlag(OpenDocument))
    {
        options.document = positionalArguments.isEmpty() ? QString() : positionalArguments.front();
        options.password = parser->isSet("pswd") ? parser->value("pswd") : QString();
        options.permissiveReading = !parser->isSet("no-permissive-reading");
    }

    if (optionFlags.testFlag(RenderPage))
    {
        bool ok = false;
        options.renderPageIndex = parser->value("page-index").toInt(&ok);
        if (!ok)
        {
            options.renderPageIndex = -1;
        }
        options.renderPageDpi = parser->value("dpi").toInt(&ok);
        if (!ok)
        {
            options.renderPageDpi = 300;
        }
        options.renderPageMaxRasterPixels = parser->value("max-raster-pixels").toLongLong(&ok);
        if (!ok)
        {
            options.renderPageMaxRasterPixels = 250000000;
        }
        options.renderPageOutput = parser->value("output");
    }

    if (optionFlags.testFlag(Redact))
    {
        options.redactedDocument = positionalArguments.size() >= 2 ? positionalArguments[1] : QString();

        options.redactOptions = pdf::PDFRedact::None;

        if (parser->isSet("redact-copy-title"))
        {
            options.redactOptions |= pdf::PDFRedact::CopyTitle;
        }

        if (parser->isSet("redact-copy-metadata"))
        {
            options.redactOptions |= pdf::PDFRedact::CopyMetadata;
        }

        if (parser->isSet("redact-copy-outline"))
        {
            options.redactOptions |= pdf::PDFRedact::CopyOutline;
        }
    }

    if (optionFlags.testFlag(AddBleed))
    {
        options.addBleedOutputDocument = parser->isSet("output") ? parser->value("output") : QString();
        options.addBleedSettings = pdf::PDFBleedFixupSettings();

        const QString mode = parser->value("mode").trimmed().toLower();
        if (mode == QStringLiteral("mirror") || mode.isEmpty())
        {
            options.addBleedSettings.mode = pdf::PDFBleedFixupMode::Mirror;
        }
        else if (mode == QStringLiteral("pixel-repeat") || mode == QStringLiteral("repeat"))
        {
            options.addBleedSettings.mode = pdf::PDFBleedFixupMode::PixelRepeat;
        }
        else if (mode == QStringLiteral("stretch"))
        {
            options.addBleedSettings.mode = pdf::PDFBleedFixupMode::Stretch;
        }
        else
        {
            PDFConsole::writeError(PDFToolTranslationContext::tr("Unknown bleed mode '%1'. Defaulting to mirror.").arg(mode), options.outputCodec);
            options.addBleedSettings.mode = pdf::PDFBleedFixupMode::Mirror;
        }

        const QString referenceBox = parser->value("reference-box").trimmed().toLower();
        if (referenceBox == QStringLiteral("crop"))
        {
            options.addBleedSettings.referenceBox = pdf::PDFBleedFixupSettings::ReferenceBox::CropBox;
        }
        else if (referenceBox == QStringLiteral("media"))
        {
            options.addBleedSettings.referenceBox = pdf::PDFBleedFixupSettings::ReferenceBox::MediaBox;
        }
        else
        {
            options.addBleedSettings.referenceBox = pdf::PDFBleedFixupSettings::ReferenceBox::TrimBox;
            if (!referenceBox.isEmpty() && referenceBox != QStringLiteral("trim"))
            {
                PDFConsole::writeError(PDFToolTranslationContext::tr("Unknown reference box '%1'. Defaulting to trim.").arg(referenceBox), options.outputCodec);
            }
        }

        bool ok = false;
        const qreal bleedMm = parser->value("bleed-mm").toDouble(&ok);
        if (ok)
        {
            if (bleedMm < 0.0)
            {
                PDFConsole::writeError(PDFToolTranslationContext::tr("Invalid --bleed-mm value '%1': margin must be non-negative.").arg(parser->value("bleed-mm")), options.outputCodec);
            }
            else
            {
                options.addBleedSettings.bleedMM = QMarginsF(bleedMm, bleedMm, bleedMm, bleedMm);
            }
        }

        if (parser->isSet("bleed-mm-ltrb"))
        {
            const QStringList parts = parser->value("bleed-mm-ltrb").split(QLatin1Char(','), Qt::KeepEmptyParts);
            if (parts.size() == 4)
            {
                bool okL = false;
                bool okT = false;
                bool okR = false;
                bool okB = false;
                const qreal left = parts[0].trimmed().toDouble(&okL);
                const qreal top = parts[1].trimmed().toDouble(&okT);
                const qreal right = parts[2].trimmed().toDouble(&okR);
                const qreal bottom = parts[3].trimmed().toDouble(&okB);
                if (okL && okT && okR && okB)
                {
                    if (left < 0.0 || top < 0.0 || right < 0.0 || bottom < 0.0)
                    {
                        PDFConsole::writeError(PDFToolTranslationContext::tr("Invalid --bleed-mm-ltrb value '%1': margins must be non-negative.").arg(parser->value("bleed-mm-ltrb")), options.outputCodec);
                    }
                    else
                    {
                        options.addBleedSettings.bleedMM = QMarginsF(left, top, right, bottom);
                    }
                }
                else
                {
                    PDFConsole::writeError(PDFToolTranslationContext::tr("Invalid --bleed-mm-ltrb value '%1'.").arg(parser->value("bleed-mm-ltrb")), options.outputCodec);
                }
            }
            else
            {
                PDFConsole::writeError(PDFToolTranslationContext::tr("Invalid --bleed-mm-ltrb value '%1'. Expected left,top,right,bottom.").arg(parser->value("bleed-mm-ltrb")), options.outputCodec);
            }
        }

        const int dpi = parser->value("dpi").toInt(&ok);
        if (ok && dpi > 0)
        {
            options.addBleedSettings.dpi = dpi;
        }

        const int samplePixels = parser->value("sample-pixels").toInt(&ok);
        if (ok && samplePixels > 0)
        {
            options.addBleedSettings.samplePixels = samplePixels;
        }

        options.addBleedSettings.force = parser->isSet("force");
        if (options.addBleedSettings.force)
        {
            options.addBleedSettings.skipIfAlreadyBleeding = false;
        }
    }

    if (optionFlags.testFlag(FlattenTransparency))
    {
        options.flattenTransparencyOutputDocument = parser->isSet("output") ? parser->value("output") : QString();
        options.flattenTransparencySettings = pdf::PDFTransparencyFlattenSettings();
        bool ok = false;
        const int rasterDpi = parser->value("raster-dpi").toInt(&ok);
        if (ok)
        {
            options.flattenTransparencySettings.rasterizationDpi = rasterDpi;
        }
        const int lineArtDpi = parser->value("line-art-dpi").toInt(&ok);
        if (ok)
        {
            options.flattenTransparencySettings.lineArtResolutionDpi = lineArtDpi;
        }
        const int textDpi = parser->value("text-dpi").toInt(&ok);
        if (ok)
        {
            options.flattenTransparencySettings.textResolutionDpi = textDpi;
        }
        const qint64 maxPixels = parser->value("max-raster-pixels").toLongLong(&ok);
        if (ok)
        {
            options.flattenTransparencySettings.maxRasterPixels = maxPixels;
        }
        options.flattenTransparencySettings.preserveSpotColors = !parser->isSet("no-preserve-spots");
        options.flattenTransparencySettings.preserveTextAndVector = parser->isSet("preserve-text-vector");
    }

    if (optionFlags.testFlag(RgbToCmyk))
    {
        options.rgbToCmykOutputDocument = parser->isSet("output") ? parser->value("output") : QString();
        options.rgbToCmykSettings = pdf::PDFRgbToCmykSettings();
        options.rgbToCmykSettings.targetProfileName = parser->value("target-profile");
        options.rgbToCmykSettings.fallbackRgbIccId = parser->value("source-rgb-profile").toUtf8();
        options.rgbToCmykSettings.blackPointCompensation = !parser->isSet("no-black-point-compensation");
        if (parser->isSet("black-point-compensation"))
        {
            options.rgbToCmykSettings.blackPointCompensation = true;
        }

        const QString intent = parser->value("intent").trimmed().toLower();
        if (intent == QStringLiteral("perceptual"))
        {
            options.rgbToCmykSettings.intent = pdf::RenderingIntent::Perceptual;
        }
        else if (intent == QStringLiteral("absolute"))
        {
            options.rgbToCmykSettings.intent = pdf::RenderingIntent::AbsoluteColorimetric;
        }
        else if (intent == QStringLiteral("saturation"))
        {
            options.rgbToCmykSettings.intent = pdf::RenderingIntent::Saturation;
        }
        else
        {
            options.rgbToCmykSettings.intent = pdf::RenderingIntent::RelativeColorimetric;
        }

        const QString outputIntentPolicy = parser->value("output-intent").trimmed().toLower();
        options.rgbToCmykSettings.outputIntentPolicy = outputIntentPolicy == QStringLiteral("preserve-matching")
                                                           ? pdf::PDFRgbToCmykOutputIntentPolicy::PreserveMatching
                                                           : pdf::PDFRgbToCmykOutputIntentPolicy::Replace;
    }

    if (optionFlags.testFlag(PreflightProfile))
    {
        options.preflightProfilePath = parser->value("profile");
        options.preflightJobContextPath = parser->value("job-context");
        options.preflightProfileStorePath = parser->value("profile-store");
        options.preflightDecisionsPath = parser->value("decisions");
        options.preflightDecisionsExportPath = parser->value("export-decisions");
        options.preflightRequireSignoff = parser->isSet("require-signoff");
        options.preflightClientId = parser->value("client");
        options.preflightProductId = parser->value("product");
        options.preflightJobType = parser->value("job-type");
        options.preflightPressId = parser->value("press");
        options.preflightStockId = parser->value("stock");
        options.preflightFinishingId = parser->value("finishing");
        options.preflightParameterAssignments = parser->values("param");
        const QString checksValue = parser->value("checks").trimmed();
        if (!checksValue.isEmpty())
        {
            options.preflightCheckFilter = checksValue.split(QLatin1Char(','), Qt::SkipEmptyParts);
            for (QString& checkId : options.preflightCheckFilter)
            {
                checkId = checkId.trimmed();
            }
        }
    }

    if (optionFlags.testFlag(CapabilityDiscovery))
    {
        options.capabilitiesCommand = parser->value("command").trimmed();
    }

    if (optionFlags.testFlag(OcrOptions))
    {
        options.ocrSidecarPath = parser->value("sidecar");
        bool ok = false;
        const int dpi = parser->value("dpi").toInt(&ok);
        if (ok && dpi > 0)
        {
            options.ocrDpi = dpi;
        }
        if (parser->isSet("languages"))
        {
            options.ocrLanguages = parser->value("languages");
        }
        // Reject 0: the gate tests 'characters >= threshold', so a threshold of 0
        // is always satisfied and would skip every page as "has text" -- the exact
        // opposite of what asking for a zero threshold means.
        const int minTextChars = parser->value("min-text-chars").toInt(&ok);
        if (ok && minTextChars >= 1)
        {
            options.ocrMinTextChars = minTextChars;
        }
    }

    if (optionFlags.testFlag(Diagnostics))
    {
        options.diagnosticsOutputDirectory = parser->isSet("output") ? parser->value("output") : QDir::currentPath();
        options.diagnosticsIncludeLogs = !parser->isSet("no-logs");
    }

    if (optionFlags.testFlag(VerifyRedaction))
    {
        options.verifyRedactionFiles = positionalArguments;
        options.verifyRedactionOptions = pdf::PDFRedact::None;
        if (parser->isSet("verify-redact-copy-title"))
        {
            options.verifyRedactionOptions |= pdf::PDFRedact::CopyTitle;
        }
        if (parser->isSet("verify-redact-copy-metadata"))
        {
            options.verifyRedactionOptions |= pdf::PDFRedact::CopyMetadata;
        }
        if (parser->isSet("verify-redact-copy-outline"))
        {
            options.verifyRedactionOptions |= pdf::PDFRedact::CopyOutline;
        }
        options.verifyRedactionCheckIncremental = !parser->isSet("verify-redact-allow-incremental");
    }

    if (optionFlags.testFlag(Separate))
    {
        options.separatePagePattern = positionalArguments.size() >= 2 ? positionalArguments[1] : QString();
    }

    if (optionFlags.testFlag(SignatureVerification))
    {
        options.verificationUseUserCertificates = !parser->isSet("ver-no-user-cert");
        options.verificationUseSystemCertificates = !parser->isSet("ver-no-sys-cert");
        options.verificationOmitCertificateCheck = parser->isSet("ver-no-cert-check");
        options.verificationPrintCertificateDetails = parser->isSet("ver-details");
        options.verificationIgnoreExpirationDate = parser->isSet("ver-ignore-exp-date");
    }

    if (optionFlags.testFlag(XmlExport))
    {
        options.xmlExportStreams = parser->isSet("xml-export-streams");
        options.xmlExportStreamsAsText = parser->isSet("xml-export-streams-as-text");
        options.xmlUseIndent = parser->isSet("xml-use-indent");
        options.xmlAlwaysBinaryStrings = parser->isSet("xml-always-binary");
    }

    if (optionFlags.testFlag(Attachments))
    {
        options.attachmentsSaveNumber = parser->isSet("att-save-n") ? parser->value("att-save-n") : QString();
        options.attachmentsSaveFileName = parser->isSet("att-save-file") ? parser->value("att-save-file") : QString();
        options.attachmentsSaveAll = parser->isSet("att-save-all");
        options.attachmentsOutputDirectory = parser->isSet("att-target-dir") ? parser->value("att-target-dir") : QString();
        options.attachmentsTargetFile = parser->isSet("att-target-file") ? parser->value("att-target-file") : QString();
    }

    if (optionFlags.testFlag(ComputeHashes))
    {
        options.computeHashes = parser->isSet("compute-hashes");
    }

    if (optionFlags.testFlag(PageSelector))
    {
        options.pageSelectorFirstPage = parser->isSet("page-first") ? parser->value("page-first") : QString();
        options.pageSelectorLastPage = parser->isSet("page-last") ? parser->value("page-last") : QString();
        options.pageSelectorSelection = parser->isSet("page-select") ? parser->value("page-select") : QString();
    }

    if (optionFlags.testFlag(TextAnalysis))
    {
        QString algoritm = parser->value("text-analysis-alg");
        if (algoritm == "auto")
        {
            options.textAnalysisAlgorithm = pdf::PDFDocumentTextFlowFactory::Algorithm::Auto;
        }
        else if (algoritm == "layout")
        {
            options.textAnalysisAlgorithm = pdf::PDFDocumentTextFlowFactory::Algorithm::Layout;
        }
        else if (algoritm == "content")
        {
            options.textAnalysisAlgorithm = pdf::PDFDocumentTextFlowFactory::Algorithm::Content;
        }
        else if (algoritm == "structure")
        {
            options.textAnalysisAlgorithm = pdf::PDFDocumentTextFlowFactory::Algorithm::Structure;
        }
        else if (!algoritm.isEmpty())
        {
            PDFConsole::writeError(PDFToolTranslationContext::tr("Unknown text layout analysis algorithm '%1'. Defaulting to automatic algorithm selection.").arg(algoritm), options.outputCodec);
        }
    }

    if (optionFlags.testFlag(TextShow))
    {
        options.textShowPageNumbers = parser->isSet("text-show-page-numbers");
        options.textShowStructTitles = parser->isSet("text-show-struct-title");
        options.textShowStructLanguage = parser->isSet("text-show-struct-lang");
        options.textShowStructAlternativeDescription = parser->isSet("text-show-struct-alt-desc");
        options.textShowStructExpandedForm = parser->isSet("text-show-struct-expanded-form");
        options.textShowStructActualText = parser->isSet("text-show-struct-act-text");
        options.textShowStructPhoneme = parser->isSet("text-show-phoneme");
    }

    if (optionFlags.testFlag(VoiceSelector))
    {
        options.textVoiceName = parser->isSet("voice-name") ? parser->value("voice-name") : QString();
        options.textVoiceGender = parser->isSet("voice-gender") ? parser->value("voice-gender") : QString();
        options.textVoiceAge = parser->isSet("voice-age") ? parser->value("voice-age") : QString();
        options.textVoiceLangCode = parser->isSet("voice-lang-code") ? parser->value("voice-lang-code") : QString();
    }

    if (optionFlags.testFlag(TextSpeech))
    {
        options.textSpeechAudioFormat = parser->value("audio-format");
        if (options.textSpeechAudioFormat != "wav" && options.textSpeechAudioFormat != "mp3")
        {
            PDFConsole::writeError(PDFToolTranslationContext::tr("Unknown audio format '%1'. Defaulting to mp3 audio format.").arg(options.textSpeechAudioFormat), options.outputCodec);
            options.textSpeechAudioFormat = "mp3";
        }

        options.textSpeechMarkPageNumbers = parser->isSet("mark-page-numbers");
        options.textSpeechSayPageNumbers = parser->isSet("say-page-numbers");
        options.textSpeechSayStructTitles = parser->isSet("say-struct-titles");
        options.textSpeechSayStructAlternativeDescription = parser->isSet("say-struct-alt-desc");
        options.textSpeechSayStructExpandedForm = parser->isSet("say-struct-exp-form");
        options.textSpeechSayStructActualText = parser->isSet("say-struct-act-text");
    }

    if (optionFlags.testFlag(CharacterMaps))
    {
        options.showCharacterMapsForEmbeddedFonts = parser->isSet("character-maps");
    }

    if (optionFlags.testFlag(ImageWriterSettings))
    {
        // Image format
        QByteArray imageWriterFormat = parser->value("image-format").toLatin1();
        if (!options.imageWriterSettings.getFormats().contains(imageWriterFormat))
        {
            PDFConsole::writeError(PDFToolTranslationContext::tr("Image format '%1' is not supported. Defaulting to png.").arg(QString::fromLatin1(imageWriterFormat)), options.outputCodec);
            imageWriterFormat = "png";
        }
        Q_ASSERT(options.imageWriterSettings.getFormats().contains(imageWriterFormat));

        options.imageWriterSettings.selectFormat(imageWriterFormat);

        // Image subtype
        if (parser->isSet("image-subtype"))
        {
            QByteArray imageWriterSubtype = parser->value("image-subtype").toLatin1();
            if (options.imageWriterSettings.getSubtypes().contains(imageWriterSubtype))
            {
                options.imageWriterSettings.setCurrentSubtype(imageWriterSubtype);
            }
            else
            {
                PDFConsole::writeError(PDFToolTranslationContext::tr("Image format subtype '%1' is not supported.").arg(QString::fromLatin1(imageWriterSubtype)), options.outputCodec);
            }
        }

        // Compression level
        if (parser->isSet("image-compress-lvl"))
        {
            QString valueText = parser->value("image-compress-lvl");

            bool ok = false;
            int value = valueText.toInt(&ok);
            if (ok)
            {
                if (options.imageWriterSettings.isOptionSupported(QImageIOHandler::CompressionRatio))
                {
                    options.imageWriterSettings.setCompression(value);
                }
                else
                {
                    PDFConsole::writeError(PDFToolTranslationContext::tr("Image compression for current format is not supported."), options.outputCodec);
                }
            }
            else
            {
                PDFConsole::writeError(PDFToolTranslationContext::tr("Invalid compression level '%1'.").arg(valueText), options.outputCodec);
            }
        }

        // Quality
        if (parser->isSet("image-quality"))
        {
            QString valueText = parser->value("image-quality");

            bool ok = false;
            int value = valueText.toInt(&ok);
            if (ok)
            {
                if (options.imageWriterSettings.isOptionSupported(QImageIOHandler::Quality))
                {
                    options.imageWriterSettings.setQuality(value);
                }
                else
                {
                    PDFConsole::writeError(PDFToolTranslationContext::tr("Image quality settings for current format is not supported."), options.outputCodec);
                }
            }
            else
            {
                PDFConsole::writeError(PDFToolTranslationContext::tr("Invalid image quality '%1'.").arg(valueText), options.outputCodec);
            }
        }

        options.imageWriterSettings.setOptimizedWrite(false);
        options.imageWriterSettings.setProgressiveScanWrite(false);

        if (parser->isSet("image-optimized-write"))
        {
            if (options.imageWriterSettings.isOptionSupported(QImageIOHandler::OptimizedWrite))
            {
                options.imageWriterSettings.setOptimizedWrite(true);
            }
            else
            {
                PDFConsole::writeError(PDFToolTranslationContext::tr("Optimized write is not supported."), options.outputCodec);
            }
        }

        if (parser->isSet("image-progressive-scan-write"))
        {
            if (options.imageWriterSettings.isOptionSupported(QImageIOHandler::ProgressiveScanWrite))
            {
                options.imageWriterSettings.setProgressiveScanWrite(true);
            }
            else
            {
                PDFConsole::writeError(PDFToolTranslationContext::tr("Progressive scan write is not supported."), options.outputCodec);
            }
        }
    }

    if (optionFlags.testFlag(ImageExportSettingsFiles))
    {
        QFileInfo documentFileInfo(options.document);
        QString outputDir = documentFileInfo.path();

        if (parser->isSet("image-output-dir"))
        {
            outputDir = parser->value("image-output-dir");
        }

        options.imageExportSettings.setDirectory(outputDir);
        options.imageExportSettings.setFileTemplate(parser->value("image-template-fn"));
    }

    if (optionFlags.testFlag(ImageExportSettingsResolution))
    {
        QString resMode = parser->value("image-res-mode").toLower();
        if (resMode == "dpi")
        {
            options.imageExportSettings.setResolutionMode(pdf::PDFPageImageExportSettings::ResolutionMode::DPI);
        }
        else if (resMode == "pixel")
        {
            options.imageExportSettings.setResolutionMode(pdf::PDFPageImageExportSettings::ResolutionMode::Pixels);
        }
        else
        {
            PDFConsole::writeError(PDFToolTranslationContext::tr("Invalid image resolution mode '%1'. Defaulting to dpi.").arg(resMode), options.outputCodec);
            options.imageExportSettings.setResolutionMode(pdf::PDFPageImageExportSettings::ResolutionMode::DPI);
        }

        if (parser->isSet("image-res-dpi"))
        {
            if (options.imageExportSettings.getResolutionMode() != pdf::PDFPageImageExportSettings::ResolutionMode::DPI)
            {
                PDFConsole::writeError(PDFToolTranslationContext::tr("Cannot set dpi value, resolution mode must be dpi."), options.outputCodec);
            }

            bool ok = false;
            int dpi = parser->value("image-res-dpi").toInt(&ok);
            if (ok)
            {
                int boundedDpi = qBound(pdf::PDFPageImageExportSettings::getMinDPIResolution(), dpi, pdf::PDFPageImageExportSettings::getMaxDPIResolution());

                if (boundedDpi != dpi)
                {
                    PDFConsole::writeError(PDFToolTranslationContext::tr("Dpi must be in range from %1 to %2. Defaulting to %3.").arg(pdf::PDFPageImageExportSettings::getMinDPIResolution()).arg(pdf::PDFPageImageExportSettings::getMaxDPIResolution()).arg(boundedDpi), options.outputCodec);
                }

                options.imageExportSettings.setDpiResolution(boundedDpi);
            }
            else
            {
                PDFConsole::writeError(PDFToolTranslationContext::tr("Invalid image dpi value '%1'.").arg(parser->value("image-res-dpi")), options.outputCodec);
            }
        }

        if (parser->isSet("image-res-pixel"))
        {
            if (options.imageExportSettings.getResolutionMode() != pdf::PDFPageImageExportSettings::ResolutionMode::Pixels)
            {
                PDFConsole::writeError(PDFToolTranslationContext::tr("Cannot set pixel value, resolution mode must be pixel."), options.outputCodec);
            }

            bool ok = false;
            int pixel = parser->value("image-res-pixel").toInt(&ok);
            if (ok)
            {
                int boundedPixel = qBound(pdf::PDFPageImageExportSettings::getMinPixelResolution(), pixel, pdf::PDFPageImageExportSettings::getMaxPixelResolution());

                if (boundedPixel != pixel)
                {
                    PDFConsole::writeError(PDFToolTranslationContext::tr("Pixel value must be in range from %1 to %2. Defaulting to %3.").arg(pdf::PDFPageImageExportSettings::getMinPixelResolution()).arg(pdf::PDFPageImageExportSettings::getMaxPixelResolution()).arg(boundedPixel), options.outputCodec);
                }

                options.imageExportSettings.setPixelResolution(boundedPixel);
            }
            else
            {
                PDFConsole::writeError(PDFToolTranslationContext::tr("Invalid image pixel value '%1'.").arg(parser->value("image-res-pixel")), options.outputCodec);
            }
        }
    }

    if (optionFlags.testFlag(ColorManagementSystem))
    {
        pdf::PDFCMSManager cmsManager(nullptr);
        options.cmsSettings = cmsManager.getDefaultSettings();

        QString cms = parser->value("cms");
        if (cms == "generic")
        {
            options.cmsSettings.system = pdf::PDFCMSSettings::System::Generic;
        }
        else if (cms == "lcms")
        {
            options.cmsSettings.system = pdf::PDFCMSSettings::System::LittleCMS2;
        }
        else
        {
            PDFConsole::writeError(PDFToolTranslationContext::tr("Unknown color management system '%1'. Defaulting to lcms.").arg(cms), options.outputCodec);
            options.cmsSettings.system = pdf::PDFCMSSettings::System::LittleCMS2;
        }

        QString accuracy = parser->value("cms-accuracy");
        if (accuracy == "medium")
        {
            options.cmsSettings.accuracy = pdf::PDFCMSSettings::Accuracy::Medium;
        }
        else if (accuracy == "low")
        {
            options.cmsSettings.accuracy = pdf::PDFCMSSettings::Accuracy::Low;
        }
        else if (accuracy == "high")
        {
            options.cmsSettings.accuracy = pdf::PDFCMSSettings::Accuracy::High;
        }
        else
        {
            PDFConsole::writeError(PDFToolTranslationContext::tr("Uknown color management system accuracy '%1'. Defaulting to medium.").arg(accuracy), options.outputCodec);
            options.cmsSettings.accuracy = pdf::PDFCMSSettings::Accuracy::Medium;
        }

        QString colorAdaptationMethod = parser->value("cms-color-adaptation");
        if (colorAdaptationMethod == "none")
        {
            options.cmsSettings.colorAdaptationXYZ = pdf::PDFCMSSettings::ColorAdaptationXYZ::None;
        }
        else if (colorAdaptationMethod == "xyzscaling")
        {
            options.cmsSettings.colorAdaptationXYZ = pdf::PDFCMSSettings::ColorAdaptationXYZ::XYZScaling;
        }
        else if (colorAdaptationMethod == "cat97")
        {
            options.cmsSettings.colorAdaptationXYZ = pdf::PDFCMSSettings::ColorAdaptationXYZ::CAT97;
        }
        else if (colorAdaptationMethod == "cat02")
        {
            options.cmsSettings.colorAdaptationXYZ = pdf::PDFCMSSettings::ColorAdaptationXYZ::CAT02;
        }
        else if (colorAdaptationMethod == "bradford")
        {
            options.cmsSettings.colorAdaptationXYZ = pdf::PDFCMSSettings::ColorAdaptationXYZ::Bradford;
        }
        else
        {
            PDFConsole::writeError(PDFToolTranslationContext::tr("Unknown color adaptation method '%1'. Defaulting to bradford.").arg(colorAdaptationMethod), options.outputCodec);
            options.cmsSettings.colorAdaptationXYZ = pdf::PDFCMSSettings::ColorAdaptationXYZ::Bradford;
        }

        QString intent = parser->value("cms-intent");
        if (intent == "auto")
        {
            options.cmsSettings.intent = pdf::RenderingIntent::Auto;
        }
        else if (intent == "perceptual")
        {
            options.cmsSettings.intent = pdf::RenderingIntent::Perceptual;
        }
        else if (intent == "abs")
        {
            options.cmsSettings.intent = pdf::RenderingIntent::AbsoluteColorimetric;
        }
        else if (intent == "rel")
        {
            options.cmsSettings.intent = pdf::RenderingIntent::RelativeColorimetric;
        }
        else if (intent == "saturation")
        {
            options.cmsSettings.intent = pdf::RenderingIntent::Saturation;
        }
        else
        {
            PDFConsole::writeError(PDFToolTranslationContext::tr("Uknown color management system rendering intent '%1'. Defaulting to auto.").arg(intent), options.outputCodec);
            options.cmsSettings.intent = pdf::RenderingIntent::Auto;
        }

        if (parser->isSet("cms-black-compensated"))
        {
            options.cmsSettings.isBlackPointCompensationActive = parser->value("cms-black-compensated").toInt();
        }

        if (parser->isSet("cms-white-paper-trans"))
        {
            options.cmsSettings.isWhitePaperColorTransformed = parser->value("cms-white-paper-trans").toInt();
        }

        if (parser->isSet("cms-consider-output-intents"))
        {
            options.cmsSettings.isConsiderOutputIntent = parser->value("cms-consider-output-intents").toInt();
        }

        auto setProfile = [&parser, &options](QString settings, QString& profile)
        {
            if (parser->isSet(settings))
            {
                profile = parser->value(settings);
            }
        };

        setProfile("cms-profile-output", options.cmsSettings.outputCS);
        setProfile("cms-profile-gray", options.cmsSettings.deviceGray);
        setProfile("cms-profile-rgb", options.cmsSettings.deviceRGB);
        setProfile("cms-profile-cmyk", options.cmsSettings.deviceCMYK);
        setProfile("cms-profile-dir", options.cmsSettings.profileDirectory);
    }

    if (optionFlags.testFlag(RenderFlags))
    {
        for (const PDFToolOptions::RenderFeatureInfo& info : PDFToolOptions::getRenderFeatures())
        {
            QString textValue = parser->value(info.option);

            bool ok = false;
            bool value = textValue.toInt(&ok);

            if (ok)
            {
                options.renderFeatures.setFlag(info.feature, value);
            }
            else
            {
                PDFConsole::writeError(PDFToolTranslationContext::tr("Uknown bool value '%1'. Default value is used.").arg(textValue), options.outputCodec);
            }
        }

        QString textValue = parser->value("render-hw-accel");
        bool ok = false;
        bool value = textValue.toInt(&ok);
        if (ok)
        {
            options.renderUseSoftwareRendering = !value;
        }
        else
        {
            PDFConsole::writeError(PDFToolTranslationContext::tr("Uknown bool value '%1'. GPU rendering is used as default.").arg(textValue), options.outputCodec);
        }

        textValue = parser->value("render-msaa-samples");
        options.renderMSAAsamples = textValue.toInt(&ok);
        if (!ok)
        {
            PDFConsole::writeError(PDFToolTranslationContext::tr("Uknown MSAA sample count '%1'. 4 samples are used as default.").arg(textValue), options.outputCodec);
            options.renderMSAAsamples = 4;
        }

        textValue = parser->value("render-rasterizers");
        options.renderRasterizerCount = textValue.toInt(&ok);
        if (!ok)
        {
            options.renderRasterizerCount = pdf::PDFRasterizerPool::getDefaultRasterizerCount();
            PDFConsole::writeError(PDFToolTranslationContext::tr("Uknown rasterizer count '%1'. %2 rasterizers are used as default.").arg(textValue).arg(options.renderRasterizerCount), options.outputCodec);
        }
        int correctedRasterizerCount = pdf::PDFRasterizerPool::getCorrectedRasterizerCount(options.renderRasterizerCount);
        if (correctedRasterizerCount != options.renderRasterizerCount)
        {
            PDFConsole::writeError(PDFToolTranslationContext::tr("Invalid raterizer count: %1. Correcting to use %2 rasterizers.").arg(options.renderRasterizerCount).arg(correctedRasterizerCount), options.outputCodec);
            options.renderRasterizerCount = correctedRasterizerCount;
        }

        options.renderShowPageStatistics = parser->isSet("render-show-page-stat");
    }

    if (optionFlags.testFlag(Unite))
    {
        options.uniteFiles = positionalArguments;
    }

    if (optionFlags.testFlag(Diff))
    {
        options.diffFiles = positionalArguments;
    }

    if (optionFlags.testFlag(RepairDiff))
    {
        options.repairDiffFiles = positionalArguments;
        options.password = parser->value("pswd");
        options.permissiveReading = !parser->isSet("no-permissive-reading");
        options.repairDiffOptions.renderDpi = std::max(1, parser->value("render-dpi").toInt());
        options.repairDiffOptions.renderVisualDiff = !parser->isSet("no-visual");
        options.repairDiffOptions.renderDirectory = parser->value("render-dir");
        options.repairDiffOptions.maxRenderedPages = std::max(0, parser->value("max-rendered-pages").toInt());
        options.repairDiffOptions.maxRenderPixels = std::max<qint64>(0, parser->value("max-render-pixels").toLongLong());
        options.repairDiffOptions.channelTolerance = std::max(0, parser->value("channel-tolerance").toInt());
        options.repairDiffOptions.expected.pageBoxes = parser->isSet("allow-page-boxes");
        options.repairDiffOptions.expected.pageContent = parser->isSet("allow-page-content");
        options.repairDiffOptions.expected.images = parser->isSet("allow-images");
        options.repairDiffOptions.expected.fonts = parser->isSet("allow-fonts");
        options.repairDiffOptions.expected.colorSpaces = parser->isSet("allow-color-spaces");
        options.repairDiffOptions.expected.outputIntent = parser->isSet("allow-output-intent");
        options.repairDiffOptions.expected.metadata = parser->isSet("allow-metadata");
        options.repairDiffOptions.expected.annotations = parser->isSet("allow-annotations");
        options.repairDiffOptions.expected.signatures = parser->isSet("allow-signatures");
    }

    if (optionFlags.testFlag(Repair))
    {
        options.repairFiles = positionalArguments;
        options.repairOperationId = parser->value("operation");
        options.repairParameterAssignments = parser->values("param");
        options.repairOutputDocument = parser->value("output");
        options.repairReportFile = parser->value("report-file");
        options.repairRenderDirectory = parser->value("render-dir");
        options.repairListOperations = parser->isSet("list-operations");
        options.repairAllowIncomplete = parser->isSet("allow-incomplete");
        options.preflightProfilePath = parser->value("profile");
        options.password = parser->value("pswd");
        options.permissiveReading = !parser->isSet("no-permissive-reading");
    }

    if (optionFlags.testFlag(ActionList))
    {
        options.actionListSubcommand = positionalArguments.value(0).trimmed().toLower();
        options.actionListRecipe = positionalArguments.value(1);
        options.actionListFiles = positionalArguments.mid(2);
        options.actionListOutputDocument = parser->value("output");
        options.actionListOutputDirectory = parser->value("output-dir");
        options.actionListParameterAssignments = parser->values("param");
        options.password = parser->value("pswd");
        options.permissiveReading = !parser->isSet("no-permissive-reading");
    }

    if (optionFlags.testFlag(Optimize))
    {
        options.optimizeFlags = pdf::PDFOptimizer::None;
        for (const PDFToolOptions::OptimizeFeatureInfo& info : PDFToolOptions::getOptimizeFlagInfos())
        {
            if (parser->isSet(info.option))
            {
                options.optimizeFlags |= info.flag;
            }
        }

        if (parser->isSet("opt-images"))
        {
            options.imageOptimizationSettings = pdf::PDFImageOptimizer::Settings::createDefault();
            options.imageOptimizationSettings.enabled = true;

            auto parseBool = [&parser](const char* option, bool defaultValue)
            {
                if (!parser->isSet(option))
                {
                    return defaultValue;
                }
                return parser->value(option).toInt() != 0;
            };

            auto parseMode = [&parser](const QString& value) -> pdf::PDFImageOptimizer::ColorMode
            {
                if (value == "auto")
                {
                    return pdf::PDFImageOptimizer::ColorMode::Auto;
                }
                if (value == "preserve")
                {
                    return pdf::PDFImageOptimizer::ColorMode::Preserve;
                }
                if (value == "color")
                {
                    return pdf::PDFImageOptimizer::ColorMode::Color;
                }
                if (value == "gray" || value == "grayscale")
                {
                    return pdf::PDFImageOptimizer::ColorMode::Grayscale;
                }
                if (value == "bitonal")
                {
                    return pdf::PDFImageOptimizer::ColorMode::Bitonal;
                }
                return pdf::PDFImageOptimizer::ColorMode::Auto;
            };

            auto parseAlgorithm = [](const QString& value) -> pdf::PDFImageOptimizer::CompressionAlgorithm
            {
                if (value == "auto")
                {
                    return pdf::PDFImageOptimizer::CompressionAlgorithm::Auto;
                }
                if (value == "flate")
                {
                    return pdf::PDFImageOptimizer::CompressionAlgorithm::Flate;
                }
                if (value == "jpeg")
                {
                    return pdf::PDFImageOptimizer::CompressionAlgorithm::JPEG;
                }
                if (value == "jpx" || value == "jpeg2000")
                {
                    return pdf::PDFImageOptimizer::CompressionAlgorithm::JPEG2000;
                }
                if (value == "runlength")
                {
                    return pdf::PDFImageOptimizer::CompressionAlgorithm::RunLength;
                }
                if (value == "ccittg4")
                {
                    return pdf::PDFImageOptimizer::CompressionAlgorithm::CCITTGroup4;
                }
                if (value == "jbig2")
                {
                    return pdf::PDFImageOptimizer::CompressionAlgorithm::JBIG2;
                }
                return pdf::PDFImageOptimizer::CompressionAlgorithm::Auto;
            };

            auto parseResample = [](const QString& value) -> pdf::PDFImage::ResampleFilter
            {
                if (value == "nearest")
                {
                    return pdf::PDFImage::ResampleFilter::Nearest;
                }
                if (value == "bilinear")
                {
                    return pdf::PDFImage::ResampleFilter::Bilinear;
                }
                if (value == "lanczos")
                {
                    return pdf::PDFImage::ResampleFilter::Lanczos;
                }
                return pdf::PDFImage::ResampleFilter::Bicubic;
            };

            QString mode = parser->value("opt-images-mode");
            options.imageOptimizationSettings.autoMode = (mode != "custom");
            options.imageOptimizationSettings.colorMode = parseMode(parser->value("opt-images-color-mode"));

            QString goal = parser->value("opt-images-goal");
            options.imageOptimizationSettings.goal = (goal == "size") ? pdf::PDFImageOptimizer::OptimizationGoal::MinimumSize
                                                                      : pdf::PDFImageOptimizer::OptimizationGoal::PreferQuality;

            options.imageOptimizationSettings.keepOriginalIfLarger = parseBool("opt-images-keep-original", true);
            options.imageOptimizationSettings.preserveTransparency = parseBool("opt-images-preserve-alpha", true);

            auto applyProfile = [&parser, &parseAlgorithm, &parseResample](pdf::PDFImageOptimizer::CompressionProfile& profile,
                                                                           const char* algOption,
                                                                           const char* dpiOption,
                                                                           const char* qualityOption,
                                                                           const char* rateOption,
                                                                           const char* resampleOption,
                                                                           const char* predictorOption)
            {
                if (parser->isSet(algOption))
                {
                    profile.algorithm = parseAlgorithm(parser->value(algOption));
                }
                if (parser->isSet(dpiOption))
                {
                    bool ok = false;
                    int dpi = parser->value(dpiOption).toInt(&ok);
                    if (ok)
                    {
                        profile.targetDpi = dpi;
                    }
                }
                if (qualityOption && parser->isSet(qualityOption))
                {
                    bool ok = false;
                    int quality = parser->value(qualityOption).toInt(&ok);
                    if (ok)
                    {
                        profile.jpegQuality = qBound(0, quality, 100);
                    }
                }
                if (rateOption && parser->isSet(rateOption))
                {
                    bool ok = false;
                    float rate = parser->value(rateOption).toFloat(&ok);
                    if (ok)
                    {
                        profile.jpeg2000Rate = rate;
                    }
                }
                if (resampleOption && parser->isSet(resampleOption))
                {
                    profile.resampleFilter = parseResample(parser->value(resampleOption));
                }
                if (predictorOption && parser->isSet(predictorOption))
                {
                    profile.enablePngPredictor = parser->value(predictorOption).toInt() != 0;
                }
            };

            applyProfile(options.imageOptimizationSettings.colorProfile,
                         "opt-images-color-alg",
                         "opt-images-color-dpi",
                         "opt-images-color-jpeg-quality",
                         "opt-images-color-jpx-rate",
                         "opt-images-color-resample",
                         "opt-images-color-png-predictor");

            applyProfile(options.imageOptimizationSettings.grayProfile,
                         "opt-images-gray-alg",
                         "opt-images-gray-dpi",
                         "opt-images-gray-jpeg-quality",
                         "opt-images-gray-jpx-rate",
                         "opt-images-gray-resample",
                         "opt-images-gray-png-predictor");

            applyProfile(options.imageOptimizationSettings.bitonalProfile,
                         "opt-images-bitonal-alg",
                         "opt-images-bitonal-dpi",
                         nullptr,
                         nullptr,
                         "opt-images-bitonal-resample",
                         "opt-images-bitonal-png-predictor");

            if (parser->isSet("opt-images-bitonal-threshold"))
            {
                bool ok = false;
                int threshold = parser->value("opt-images-bitonal-threshold").toInt(&ok);
                if (ok)
                {
                    options.imageOptimizationSettings.bitonalProfile.monochromeThreshold = threshold;
                }
            }
        }
    }

    if (optionFlags.testFlag(CertStore))
    {
        options.certStoreEnumerateSystemCertificates = parser->value("list-system-certs").toInt();
        options.certStoreEnumerateUserCertificates = parser->value("list-user-certs").toInt();
    }

    if (optionFlags.testFlag(CertStoreInstall))
    {
        options.certificateStoreInstallCertificateFile = positionalArguments.isEmpty() ? QString() : positionalArguments.front();
    }

    if (optionFlags.testFlag(Encrypt))
    {
        QString encryptionAlgorithm = parser->value("enc-algorithm");
        if (encryptionAlgorithm == "rc4")
        {
            options.encryptionAlgorithm = pdf::PDFSecurityHandlerFactory::Algorithm::RC4;
        }
        else if (encryptionAlgorithm == "aes-128")
        {
            options.encryptionAlgorithm = pdf::PDFSecurityHandlerFactory::Algorithm::AES_128;
        }
        else if (encryptionAlgorithm == "aes-256")
        {
            options.encryptionAlgorithm = pdf::PDFSecurityHandlerFactory::Algorithm::AES_256;
        }
        else
        {
            if (!encryptionAlgorithm.isEmpty())
            {
                PDFConsole::writeError(PDFToolTranslationContext::tr("Unknown encryption algorithm '%1'. Defaulting to AES-256 encryption.").arg(encryptionAlgorithm), options.outputCodec);
            }

            options.encryptionAlgorithm = pdf::PDFSecurityHandlerFactory::Algorithm::AES_256;
        }

        QString encryptionContents = parser->value("enc-contents");
        if (encryptionContents == "all")
        {
            options.encryptionContents = pdf::PDFSecurityHandlerFactory::EncryptContents::All;
        }
        else if (encryptionContents == "all-except-metadata")
        {
            options.encryptionContents = pdf::PDFSecurityHandlerFactory::EncryptContents::AllExceptMetadata;
        }
        else if (encryptionContents == "only-embedded-files")
        {
            options.encryptionContents = pdf::PDFSecurityHandlerFactory::EncryptContents::EmbeddedFiles;
        }
        else
        {
            if (!encryptionContents.isEmpty())
            {
                PDFConsole::writeError(PDFToolTranslationContext::tr("Unknown encryption contents mode '%1'. Defaulting to encrypt all contents.").arg(encryptionContents), options.outputCodec);
            }

            options.encryptionContents = pdf::PDFSecurityHandlerFactory::EncryptContents::All;
        }

        options.encryptionUserPassword = parser->value("enc-user-password");
        options.encryptionOwnerPassword = parser->value("enc-owner-password");
        options.encryptionPermissions = parser->value("enc-permissions").toUInt();
    }

    if (optionFlags.testFlag(EmptyResultPolicy))
    {
        options.failIfEmpty = parser->isSet("fail-if-empty");
    }

    if (optionFlags.testFlag(DestructiveWrite))
    {
        options.destructiveDryRun = parser->isSet("dry-run");
        options.destructiveReport = parser->isSet("report");
        // --overwrite is canonical everywhere. --force is the legacy alias only on
        // commands that register it; add-bleed's own --force means "ignore the
        // skip-if-already-bleeding heuristic", so it must not select overwrite.
        options.destructiveOverwrite = parser->isSet("overwrite");
        if (!optionFlags.testFlag(AddBleed) && parser->isSet("force"))
        {
            options.destructiveOverwrite = true;
        }
    }

    return options;
}

QString PDFToolAbstractApplication::convertDateTimeToString(const QDateTime& dateTime, PDFToolOptions::DateFormat dateFormat)
{
    switch (dateFormat)
    {
        case PDFToolOptions::LocaleShortDate:
            return QLocale::system().toString(dateTime, QLocale::ShortFormat);
        case PDFToolOptions::LocaleLongDate:
            return QLocale::system().toString(dateTime, QLocale::LongFormat);
        case PDFToolOptions::ISODate:
            return dateTime.toString(Qt::ISODate);
        case PDFToolOptions::RFC2822Date:
            return dateTime.toString(Qt::RFC2822Date);
        default:
            break;
    }

    Q_ASSERT(false);
    return QLocale::system().toString(dateTime, QLocale::ShortFormat);
}

bool PDFToolAbstractApplication::readDocument(const PDFToolOptions& options, pdf::PDFDocument& document, QByteArray* sourceData, bool authorizeOwnerOnly)
{
    bool isFirstPasswordAttempt = true;
    auto passwordCallback = [&options, &isFirstPasswordAttempt](bool* ok) -> QString
    {
        *ok = isFirstPasswordAttempt;
        isFirstPasswordAttempt = false;
        return options.password;
    };
    pdf::PDFDocumentReader reader(nullptr, passwordCallback, options.permissiveReading, authorizeOwnerOnly);
    document = reader.readFromFile(options.document);

    switch (reader.getReadingResult())
    {
        case pdf::PDFDocumentReader::Result::OK:
        {
            if (sourceData)
            {
                *sourceData = reader.getSource();
            }
            break;
        }

        case pdf::PDFDocumentReader::Result::Cancelled:
        {
            reportDiagnostic(options,
                             PDFToolDiagnosticSeverity::Error,
                             QStringLiteral("pdf.invalid-password"),
                             PDFToolTranslationContext::tr("Invalid password provided."));
            return false;
        }

        case pdf::PDFDocumentReader::Result::Failed:
        {
            reportDiagnostic(options,
                             PDFToolDiagnosticSeverity::Error,
                             QStringLiteral("pdf.document-unreadable"),
                             PDFToolTranslationContext::tr("Error occured during document reading. %1").arg(reader.getErrorMessage()));
            return false;
        }

        default:
        {
            Q_ASSERT(false);
            return false;
        }
    }

    for (const QString& warning : reader.getWarnings())
    {
        reportDiagnostic(options,
                         PDFToolDiagnosticSeverity::Warning,
                         QStringLiteral("pdf.reader-warning"),
                         PDFToolTranslationContext::tr("Warning: %1").arg(warning));
    }

    return true;
}

bool PDFToolAbstractApplication::readDocumentOnHeap(const PDFToolOptions& options,
                                                    std::unique_ptr<pdf::PDFDocument, void (*)(pdf::PDFDocument*)>& document,
                                                    QByteArray* sourceData,
                                                    bool authorizeOwnerOnly)
{
    bool isFirstPasswordAttempt = true;
    auto passwordCallback = [&options, &isFirstPasswordAttempt](bool* ok) -> QString
    {
        *ok = isFirstPasswordAttempt;
        isFirstPasswordAttempt = false;
        return options.password;
    };
    pdf::PDFDocumentReader reader(nullptr, passwordCallback, options.permissiveReading, authorizeOwnerOnly);
    document.reset(reader.readFromFileOnHeap(options.document));

    switch (reader.getReadingResult())
    {
        case pdf::PDFDocumentReader::Result::OK:
        {
            if (sourceData)
            {
                *sourceData = reader.getSource();
            }
            break;
        }

        case pdf::PDFDocumentReader::Result::Cancelled:
        {
            reportDiagnostic(options,
                             PDFToolDiagnosticSeverity::Error,
                             QStringLiteral("pdf.invalid-password"),
                             PDFToolTranslationContext::tr("Invalid password provided."));
            return false;
        }

        case pdf::PDFDocumentReader::Result::Failed:
        {
            reportDiagnostic(options,
                             PDFToolDiagnosticSeverity::Error,
                             QStringLiteral("pdf.document-unreadable"),
                             PDFToolTranslationContext::tr("Error occured during document reading. %1").arg(reader.getErrorMessage()));
            return false;
        }

        default:
        {
            Q_ASSERT(false);
            return false;
        }
    }

    for (const QString& warning : reader.getWarnings())
    {
        reportDiagnostic(options,
                         PDFToolDiagnosticSeverity::Warning,
                         QStringLiteral("pdf.reader-warning"),
                         PDFToolTranslationContext::tr("Warning: %1").arg(warning));
    }

    return true;
}

PDFToolExitCode PDFToolAbstractApplication::reportEmptyResult(const PDFToolOptions& options,
                                                              const QString& subject,
                                                              PDFToolExitCode successCode) const
{
    const bool fail = options.failIfEmpty;
    const QJsonObject context{ { QStringLiteral("subject"), subject },
                               { QStringLiteral("fail_if_empty"), fail } };

    if (fail)
    {
        reportDiagnostic(options,
                         PDFToolDiagnosticSeverity::Error,
                         QStringLiteral("output.empty-result"),
                         PDFToolTranslationContext::tr("No %1 were extracted from document '%2', and --fail-if-empty was requested.").arg(subject, options.document),
                         context);
        return PDFToolExitCode::Findings;
    }

    // Without the flag this stays a machine-readable note only: extraction
    // commands are informational by default and must not start writing to stderr
    // on documents that simply have nothing to extract.
    if (options.executionContext)
    {
        PDFToolDiagnostic diagnostic;
        diagnostic.severity = PDFToolDiagnosticSeverity::Info;
        diagnostic.code = QStringLiteral("output.empty-result");
        diagnostic.message = PDFToolTranslationContext::tr("No %1 were extracted from document '%2'.").arg(subject, options.document);
        diagnostic.context = context;
        options.executionContext->addDiagnostic(std::move(diagnostic));
    }

    return successCode;
}

void PDFToolAbstractApplication::reportDiagnostic(const PDFToolOptions& options,
                                                  PDFToolDiagnosticSeverity severity,
                                                  const QString& code,
                                                  const QString& message,
                                                  QJsonObject context) const
{
    if (options.executionContext)
    {
        PDFToolDiagnostic diagnostic;
        diagnostic.severity = severity;
        diagnostic.code = code;
        diagnostic.message = message;
        diagnostic.context = std::move(context);
        options.executionContext->addDiagnostic(std::move(diagnostic));
    }

    if (options.outputStyle != PDFOutputFormatter::Style::Json)
    {
        PDFConsole::writeError(message, options.outputCodec);
    }
}

QList<QByteArray> PDFToolAbstractApplication::getAvailableEncodings()
{
    QList<QByteArray> encodings;
    encodings << "utf8";
    encodings << "latin1";
    encodings << "system";
    return encodings;
}

QByteArray PDFToolAbstractApplication::getDefaultEncoding()
{
    return getAvailableEncodings().front();
}

QStringConverter::Encoding PDFToolAbstractApplication::getEncoding(const QString& encodingName)
{
    if (encodingName == "utf8")
    {
        return QStringConverter::Utf8;
    }

    if (encodingName == "latin1")
    {
        return QStringConverter::Latin1;
    }

    if (encodingName == "system")
    {
        return QStringConverter::System;
    }

    return QStringConverter::System;
}

PDFToolAbstractApplication* PDFToolApplicationStorage::getApplicationByCommand(const QString& command)
{
    for (PDFToolAbstractApplication* application : getInstance()->m_applications)
    {
        if (application->getStandardString(PDFToolAbstractApplication::Command) == command)
        {
            return application;
        }
    }

    return nullptr;
}

void PDFToolApplicationStorage::registerApplication(PDFToolAbstractApplication* application, bool isDefault)
{
    PDFToolApplicationStorage* storage = getInstance();
    storage->m_applications.push_back(application);

    if (isDefault)
    {
        storage->m_defaultApplication = application;
    }
}

PDFToolAbstractApplication* PDFToolApplicationStorage::getDefaultApplication()
{
    return getInstance()->m_defaultApplication;
}

const std::vector<PDFToolAbstractApplication*>& PDFToolApplicationStorage::getApplications()
{
    return getInstance()->m_applications;
}

PDFToolApplicationStorage* PDFToolApplicationStorage::getInstance()
{
    static PDFToolApplicationStorage storage;
    return &storage;
}

std::vector<pdf::PDFInteger> PDFToolOptions::getPageRange(pdf::PDFInteger pageCount, QString& errorMessage, bool zeroBased) const
{
    QStringList parts;

    const bool hasFirst = !pageSelectorFirstPage.isEmpty();
    const bool hasLast = !pageSelectorLastPage.isEmpty();
    const bool hasSelection = !pageSelectorSelection.isEmpty();

    if (hasFirst && hasLast)
    {
        parts << QString("%1-%2").arg(pageSelectorFirstPage, pageSelectorLastPage);
    }
    else if (hasFirst)
    {
        parts << QString("%1-").arg(pageSelectorFirstPage);
    }
    else if (hasLast)
    {
        parts << QString("-%1").arg(pageSelectorLastPage);
    }

    if (hasSelection)
    {
        parts << pageSelectorSelection;
    }

    if (parts.empty())
    {
        parts << "-";
    }

    QString partsString = parts.join(",");
    pdf::PDFClosedIntervalSet result = pdf::PDFClosedIntervalSet::parse(1, pageCount, partsString, &errorMessage);
    std::vector<pdf::PDFInteger> pageIndices = result.unfold();

    if (zeroBased)
    {
        std::for_each(pageIndices.begin(), pageIndices.end(), [](auto& index)
                      { --index; });
    }

    return pageIndices;
}

std::vector<PDFToolOptions::RenderFeatureInfo> PDFToolOptions::getRenderFeatures()
{
    return {
        RenderFeatureInfo{ "render-antialiasing", "Antialiasing for lines, shapes, etc.", pdf::PDFRenderer::Antialiasing },
        RenderFeatureInfo{ "render-text-antialiasing", "Antialiasing for text outlines.", pdf::PDFRenderer::TextAntialiasing },
        RenderFeatureInfo{ "render-smooth-img", "Smooth image transformation (slower, but better quality images).", pdf::PDFRenderer::SmoothImages },
        RenderFeatureInfo{ "render-ignore-opt-content", "Ignore optional content settings (draw everything).", pdf::PDFRenderer::IgnoreOptionalContent },
        RenderFeatureInfo{ "render-clip-to-crop-box", "Clip page graphics to crop box.", pdf::PDFRenderer::ClipToCropBox },
        RenderFeatureInfo{ "render-invert-colors", "Color conversion: invert all colors.", pdf::PDFRenderer::ColorAdjust_Invert },
        RenderFeatureInfo{ "render-grayscale", "Color conversion: convert to grayscale", pdf::PDFRenderer::ColorAdjust_Grayscale },
        RenderFeatureInfo{ "render-high-contrast", "Color conversion: high contrast colors", pdf::PDFRenderer::ColorAdjust_HighContrast },
        RenderFeatureInfo{ "render-bitonal", "Color conversion: bitonal page image", pdf::PDFRenderer::ColorAdjust_Bitonal },
        RenderFeatureInfo{ "render-custom-colors", "Color conversion: custom colors", pdf::PDFRenderer::ColorAdjust_CustomColors },
        RenderFeatureInfo{ "render-display-annot", "Display annotations.", pdf::PDFRenderer::DisplayAnnotations }
    };
}

std::vector<PDFToolOptions::OptimizeFeatureInfo> PDFToolOptions::getOptimizeFlagInfos()
{
    return {
        OptimizeFeatureInfo{ "opt-deref-simple", "Dereference referenced simple objects (integers, bools, ...).", pdf::PDFOptimizer::DereferenceSimpleObjects },
        OptimizeFeatureInfo{ "opt-remove-null", "Remove null objects from dictionary entries.", pdf::PDFOptimizer::RemoveNullObjects },
        OptimizeFeatureInfo{ "opt-remove-unused", "Remove not referenced objects.", pdf::PDFOptimizer::RemoveUnusedObjects },
        OptimizeFeatureInfo{ "opt-merge-identical", "Merge identical objects.", pdf::PDFOptimizer::MergeIdenticalObjects },
        OptimizeFeatureInfo{ "opt-shrink-storage", "Shrink object storage by renumbering objects.", pdf::PDFOptimizer::ShrinkObjectStorage },
        OptimizeFeatureInfo{ "opt-recompress-flate", "Recompress flate streams with maximal compression.", pdf::PDFOptimizer::RecompressFlateStreams },
        OptimizeFeatureInfo{ "opt-all", "Use all optimization algorithms.", pdf::PDFOptimizer::All }
    };
}

void PDFToolAbstractApplication::registerDestructiveWriteOptions(QCommandLineParser* parser, bool registerForceAlias)
{
    parser->addOption(QCommandLineOption("dry-run", "Compute the result but do not write an output file."));
    parser->addOption(QCommandLineOption("report", "Print a summary of the pending write operation."));
    parser->addOption(QCommandLineOption("overwrite", "Overwrite an existing output file without confirmation."));
    if (registerForceAlias)
    {
        parser->addOption(QCommandLineOption("force", "Overwrite an existing output file without confirmation (legacy alias of --overwrite)."));
    }
}

PDFToolExitCode PDFToolAbstractApplication::validateDestructiveOutput(const PDFToolOptions& options, const QString& outputPath) const
{
    if (outputPath.isEmpty())
    {
        reportDiagnostic(options,
                         PDFToolDiagnosticSeverity::Error,
                         QStringLiteral("cli.invalid-arguments"),
                         PDFToolTranslationContext::tr("Output document file name is not set."));
        return PDFToolExitCode::InvalidInvocation;
    }

    const QFileInfo outputInfo(outputPath);
    if (outputInfo.isDir())
    {
        reportDiagnostic(options,
                         PDFToolDiagnosticSeverity::Error,
                         QStringLiteral("output.destination-is-directory"),
                         PDFToolTranslationContext::tr("Output '%1' is a directory.").arg(outputPath),
                         QJsonObject{ { QStringLiteral("path"), outputPath } });
        return PDFToolExitCode::InvalidInvocation;
    }

    if (outputInfo.exists() && !options.destructiveOverwrite && !options.destructiveDryRun)
    {
        reportDiagnostic(options,
                         PDFToolDiagnosticSeverity::Error,
                         QStringLiteral("output.already-exists"),
                         PDFToolTranslationContext::tr("Output '%1' already exists. Use --overwrite to overwrite.").arg(outputPath));
        return PDFToolExitCode::InvalidInvocation;
    }

    return PDFToolExitCode::Success;
}

PDFToolExitCode PDFToolAbstractApplication::validateDestructiveOutputs(const PDFToolOptions& options, const QStringList& outputPaths) const
{
    const QList<pdf::PDFOutputConflict> conflicts = pdf::PDFSafeFileWriter::findOutputConflicts(outputPaths, !options.destructiveOverwrite);
    for (const pdf::PDFOutputConflict& conflict : conflicts)
    {
        if (conflict.code == QStringLiteral("output.destination-exists"))
        {
            return validateDestructiveOutput(options, conflict.path);
        }

        QString message;
        if (conflict.code == QStringLiteral("output.empty-path"))
        {
            message = PDFToolTranslationContext::tr("Output document file name is not set.");
        }
        else if (conflict.code == QStringLiteral("output.destination-is-directory"))
        {
            message = PDFToolTranslationContext::tr("Output '%1' is a directory.").arg(conflict.path);
        }
        else
        {
            message = PDFToolTranslationContext::tr("Output '%1' is planned more than once.").arg(conflict.path);
        }

        reportDiagnostic(options, PDFToolDiagnosticSeverity::Error, conflict.code, message,
                         QJsonObject{ { QStringLiteral("path"), conflict.path } });
        return PDFToolExitCode::InvalidInvocation;
    }

    return PDFToolExitCode::Success;
}

}   // pdftool
