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

#include "pdfrgbtocmykfixup.h"

#include "pdfcms.h"
#include "pdfdocumentbuilder.h"
#include "pdfexception.h"
#include "pdfimage.h"
#include "pdfparser.h"
#include "pdfstreamfilters.h"

#include <QCryptographicHash>

#include <lcms2.h>

#include <cmath>
#include <limits>
#include <set>
#include <utility>
#include <vector>

namespace pdf
{

namespace
{

struct StreamReference
{
    PDFObjectReference reference;
    PDFInteger pageIndex = -1;
    PDFRgbToCmykObjectKind kind = PDFRgbToCmykObjectKind::VectorPaint;
};

struct Token
{
    PDFLexicalAnalyzer::TokenType type = PDFLexicalAnalyzer::TokenType::EndOfFile;
    QVariant data;
};

bool isNumber(const Token& token)
{
    return token.type == PDFLexicalAnalyzer::TokenType::Integer || token.type == PDFLexicalAnalyzer::TokenType::Real;
}

PDFReal numberValue(const Token& token)
{
    return token.type == PDFLexicalAnalyzer::TokenType::Integer
               ? PDFReal(token.data.toLongLong())
               : token.data.toDouble();
}

QByteArray formatNumber(PDFReal value)
{
    if (!std::isfinite(value))
    {
        return QByteArrayLiteral("0");
    }

    QByteArray result = QByteArray::number(value, 'f', 8);
    while (result.endsWith('0'))
    {
        result.chop(1);
    }
    if (result.endsWith('.'))
    {
        result.chop(1);
    }
    if (result.isEmpty() || result == QByteArrayLiteral("-0"))
    {
        result = QByteArrayLiteral("0");
    }
    return result;
}

QByteArray serializeToken(const Token& token)
{
    switch (token.type)
    {
        case PDFLexicalAnalyzer::TokenType::Boolean:
            return token.data.toBool() ? QByteArrayLiteral("true") : QByteArrayLiteral("false");

        case PDFLexicalAnalyzer::TokenType::Integer:
            return QByteArray::number(token.data.toLongLong());

        case PDFLexicalAnalyzer::TokenType::Real:
            return formatNumber(token.data.toDouble());

        case PDFLexicalAnalyzer::TokenType::String:
            return QByteArrayLiteral("<") + token.data.toByteArray().toHex() + QByteArrayLiteral(">");

        case PDFLexicalAnalyzer::TokenType::Name:
            return QByteArrayLiteral("/") + token.data.toByteArray();

        case PDFLexicalAnalyzer::TokenType::ArrayStart:
            return QByteArrayLiteral("[");

        case PDFLexicalAnalyzer::TokenType::ArrayEnd:
            return QByteArrayLiteral("]");

        case PDFLexicalAnalyzer::TokenType::DictionaryStart:
            return QByteArrayLiteral("<<");

        case PDFLexicalAnalyzer::TokenType::DictionaryEnd:
            return QByteArrayLiteral(">>");

        case PDFLexicalAnalyzer::TokenType::Null:
            return QByteArrayLiteral("null");

        case PDFLexicalAnalyzer::TokenType::Command:
            return token.data.toByteArray();

        case PDFLexicalAnalyzer::TokenType::EndOfFile:
            break;
    }

    return QByteArray();
}

PDFOperationResult validateTargetProfile(const PDFRgbToCmykSettings& settings)
{
    if (settings.targetIccData.isEmpty())
    {
        return PDFTranslationContext::tr("A target CMYK ICC profile is required.");
    }

    cmsHPROFILE profile = cmsOpenProfileFromMem(settings.targetIccData.constData(),
                                                static_cast<cmsUInt32Number>(settings.targetIccData.size()));
    if (!profile)
    {
        return PDFTranslationContext::tr("The target ICC profile could not be opened.");
    }

    const bool isCmyk = cmsGetColorSpace(profile) == cmsSigCmykData;
    cmsCloseProfile(profile);
    if (!isCmyk)
    {
        return PDFTranslationContext::tr("The target ICC profile is not a CMYK profile.");
    }

    return true;
}

QByteArray targetProfileId(const PDFRgbToCmykSettings& settings)
{
    if (!settings.targetIccId.isEmpty())
    {
        return settings.targetIccId;
    }
    return QCryptographicHash::hash(settings.targetIccData, QCryptographicHash::Sha256);
}

void appendContentReferences(const PDFObject& contentObject,
                             const PDFObjectStorage* storage,
                             std::vector<PDFObjectReference>& references,
                             std::vector<PDFObject>* directStreams = nullptr)
{
    const PDFObject object = storage->getObject(contentObject);
    if (object.isStream())
    {
        if (contentObject.isReference())
        {
            references.push_back(contentObject.getReference());
        }
        else if (directStreams)
        {
            directStreams->push_back(object);
        }
        return;
    }

    if (!object.isArray())
    {
        return;
    }

    for (const PDFObject& item : *object.getArray())
    {
        const PDFObject dereferenced = storage->getObject(item);
        if (dereferenced.isStream() && item.isReference())
        {
            references.push_back(item.getReference());
        }
        else if (dereferenced.isStream() && directStreams)
        {
            directStreams->push_back(dereferenced);
        }
    }
}

bool isRgbColorSpaceName(const QByteArray& name)
{
    return name == QByteArrayLiteral("DeviceRGB") || name == QByteArrayLiteral("RGB");
}

struct RewriteResult
{
    QByteArray content;
    int converted = 0;
    bool changed = false;
};

PDFOperationResult rewriteRgbOperators(const QByteArray& input,
                                       const PDFRgbToCmykSettings& settings,
                                       const PDFCMS* cms,
                                       const StreamReference& owner,
                                       PDFRgbToCmykReport* report,
                                       RewriteResult* result)
{
    std::vector<Token> tokens;
    try
    {
        PDFLexicalAnalyzer analyzer(input.constData(), input.constData() + input.size());
        while (true)
        {
            const PDFLexicalAnalyzer::Token token = analyzer.fetch();
            if (token.type == PDFLexicalAnalyzer::TokenType::EndOfFile)
            {
                break;
            }
            tokens.push_back(Token{ token.type, token.data });
        }
    }
    catch (const PDFException& exception)
    {
        return PDFTranslationContext::tr("Unable to parse a PDF content stream: %1")
            .arg(QString::fromUtf8(exception.what()));
    }

    std::vector<Token> output;
    output.reserve(tokens.size() + 8);
    bool fillRgb = false;
    bool strokeRgb = false;
    int converted = 0;
    bool changed = false;

    auto addUnsupported = [&](PDFRgbToCmykObjectKind kind, const QString& reason)
    {
        if (report)
        {
            PDFRgbToCmykUnsupportedItem item;
            item.pageIndex = owner.pageIndex;
            item.objectReference = owner.reference;
            item.kind = kind;
            item.reason = reason;
            report->unsupported.append(item);
        }
    };

    for (const Token& token : tokens)
    {
        if (token.type != PDFLexicalAnalyzer::TokenType::Command)
        {
            output.push_back(token);
            continue;
        }

        const QByteArray command = token.data.toByteArray();
        const bool stroke = command == QByteArrayLiteral("RG") || command == QByteArrayLiteral("CS");
        const bool fill = command == QByteArrayLiteral("rg") || command == QByteArrayLiteral("cs");

        if ((command == QByteArrayLiteral("rg") || command == QByteArrayLiteral("RG")) && output.size() >= 3 && isNumber(output[output.size() - 1]) && isNumber(output[output.size() - 2]) && isNumber(output[output.size() - 3]))
        {
            std::vector<PDFColorComponent> source = {
                PDFColorComponent(numberValue(output[output.size() - 3])),
                PDFColorComponent(numberValue(output[output.size() - 2])),
                PDFColorComponent(numberValue(output[output.size() - 1]))
            };
            std::vector<PDFColorComponent> target(4);
            PDFCMS::ColorSpaceTransformParams params;
            params.sourceType = settings.fallbackRgbIccData.isEmpty()
                                    ? PDFCMS::ColorSpaceType::DeviceRGB
                                    : PDFCMS::ColorSpaceType::ICC;
            params.targetType = PDFCMS::ColorSpaceType::ICC;
            params.sourceIccId = settings.fallbackRgbIccId;
            params.sourceIccData = settings.fallbackRgbIccData;
            params.targetIccId = targetProfileId(settings);
            params.targetIccData = settings.targetIccData;
            params.input = PDFColorBuffer(source.data(), source.size());
            params.output = PDFColorBuffer(target.data(), target.size());
            params.intent = settings.intent;

            if (!cms->transformColorSpace(params))
            {
                return PDFTranslationContext::tr("LittleCMS could not convert an RGB vector paint.");
            }

            output.resize(output.size() - 3);
            for (PDFColorComponent component : target)
            {
                output.push_back(Token{ PDFLexicalAnalyzer::TokenType::Real, QVariant(double(component)) });
            }
            output.push_back(Token{ PDFLexicalAnalyzer::TokenType::Command,
                                    command == QByteArrayLiteral("RG") ? QByteArrayLiteral("K") : QByteArrayLiteral("k") });
            ++converted;
            changed = true;
            if (command == QByteArrayLiteral("RG"))
            {
                strokeRgb = false;
            }
            else
            {
                fillRgb = false;
            }
            continue;
        }

        if ((command == QByteArrayLiteral("sc") || command == QByteArrayLiteral("SC")) && output.size() >= 3 && isNumber(output[output.size() - 1]) && isNumber(output[output.size() - 2]) && isNumber(output[output.size() - 3]) && ((command == QByteArrayLiteral("sc") && fillRgb) || (command == QByteArrayLiteral("SC") && strokeRgb)))
        {
            std::vector<PDFColorComponent> source = {
                PDFColorComponent(numberValue(output[output.size() - 3])),
                PDFColorComponent(numberValue(output[output.size() - 2])),
                PDFColorComponent(numberValue(output[output.size() - 1]))
            };
            std::vector<PDFColorComponent> target(4);
            PDFCMS::ColorSpaceTransformParams params;
            params.sourceType = settings.fallbackRgbIccData.isEmpty()
                                    ? PDFCMS::ColorSpaceType::DeviceRGB
                                    : PDFCMS::ColorSpaceType::ICC;
            params.targetType = PDFCMS::ColorSpaceType::ICC;
            params.sourceIccId = settings.fallbackRgbIccId;
            params.sourceIccData = settings.fallbackRgbIccData;
            params.targetIccId = targetProfileId(settings);
            params.targetIccData = settings.targetIccData;
            params.input = PDFColorBuffer(source.data(), source.size());
            params.output = PDFColorBuffer(target.data(), target.size());
            params.intent = settings.intent;

            if (!cms->transformColorSpace(params))
            {
                return PDFTranslationContext::tr("LittleCMS could not convert an RGB color-space paint.");
            }

            output.resize(output.size() - 3);
            for (PDFColorComponent component : target)
            {
                output.push_back(Token{ PDFLexicalAnalyzer::TokenType::Real, QVariant(double(component)) });
            }
            output.push_back(token);
            ++converted;
            changed = true;
            continue;
        }

        if ((command == QByteArrayLiteral("cs") || command == QByteArrayLiteral("CS")) && !output.empty() && output.back().type == PDFLexicalAnalyzer::TokenType::Name)
        {
            const QByteArray name = output.back().data.toByteArray();
            if (isRgbColorSpaceName(name))
            {
                output.back().data = QByteArrayLiteral("DeviceCMYK");
                if (fill)
                {
                    fillRgb = true;
                }
                else if (stroke)
                {
                    strokeRgb = true;
                }
                changed = true;
                output.push_back(token);
                continue;
            }
            if (name == QByteArrayLiteral("DeviceCMYK"))
            {
                if (fill)
                {
                    fillRgb = false;
                }
                else if (stroke)
                {
                    strokeRgb = false;
                }
            }
        }

        if ((command == QByteArrayLiteral("scn") || command == QByteArrayLiteral("SCN")) && ((command == QByteArrayLiteral("scn") && fillRgb) || (command == QByteArrayLiteral("SCN") && strokeRgb)))
        {
            addUnsupported(PDFRgbToCmykObjectKind::VectorPaint,
                           PDFTranslationContext::tr("RGB pattern or extended color paint is not supported."));
        }

        output.push_back(token);
    }

    if (result)
    {
        result->converted = converted;
        result->changed = changed;
        if (changed)
        {
            QByteArray serialized;
            for (const Token& token : output)
            {
                if (!serialized.isEmpty())
                {
                    serialized.append('\n');
                }
                serialized.append(serializeToken(token));
            }
            result->content = qMove(serialized);
        }
        else
        {
            result->content = input;
        }
    }

    return true;
}

bool isConvertibleRgbImage(const PDFDocument* document,
                           const PDFObject& object,
                           PDFRgbToCmykUnsupportedItem* unsupported)
{
    if (!object.isStream())
    {
        return false;
    }

    try
    {
        const PDFImage image = PDFImage::createImage(document,
                                                     object.getStream(),
                                                     PDFColorSpacePointer(new PDFDeviceRGBColorSpace()),
                                                     false,
                                                     RenderingIntent::Perceptual,
                                                     nullptr);
        const PDFImageData& imageData = image.getImageData();
        if (imageData.getComponents() == 3 && imageData.getWidth() > 0 && imageData.getHeight() > 0 &&
            imageData.getBitsPerComponent() > 0 && imageData.getBitsPerComponent() <= 16 &&
            imageData.getMaskingType() != PDFImageData::MaskingType::ColorKeyMasking)
        {
            return true;
        }
    }
    catch (const PDFException&)
    {
    }

    if (unsupported)
    {
        unsupported->kind = PDFRgbToCmykObjectKind::Image;
        unsupported->reason = PDFTranslationContext::tr(
            "RGB image XObjects must contain convertible RGB image samples.");
    }
    return false;
}

void scanImageResources(const PDFObject& resourcesObject,
                        const PDFDocument* document,
                        PDFInteger pageIndex,
                        PDFRgbToCmykReport* report)
{
    const PDFObjectStorage* storage = &document->getStorage();
    const PDFObject resources = storage->getObject(resourcesObject);
    if (!resources.isDictionary())
    {
        return;
    }

    const PDFObject xObject = storage->getObject(resources.getDictionary()->get("XObject"));
    if (!xObject.isDictionary())
    {
        return;
    }

    for (size_t i = 0; i < xObject.getDictionary()->getCount(); ++i)
    {
        const PDFObject object = storage->getObject(xObject.getDictionary()->getValue(i));
        if (!object.isStream())
        {
            continue;
        }

        const PDFDictionary* dictionary = object.getStream()->getDictionary();
        if (dictionary->get("Subtype").isName() && dictionary->get("Subtype").getString() == QByteArrayLiteral("Image"))
        {
            const PDFObject colorSpace = storage->getObject(dictionary->get("ColorSpace"));
            if (colorSpace.isName() && isRgbColorSpaceName(colorSpace.getString()) && report)
            {
                PDFRgbToCmykUnsupportedItem item;
                item.pageIndex = pageIndex;
                if (xObject.getDictionary()->getValue(i).isReference())
                {
                    item.objectReference = xObject.getDictionary()->getValue(i).getReference();
                }
                if (isConvertibleRgbImage(document, object, &item))
                {
                    ++report->imagesConverted;
                }
                else
                {
                    report->unsupported.append(item);
                }
            }
        }
    }
}

PDFObjectReference addIccProfileObject(PDFDocumentBuilder* builder,
                                       const PDFRgbToCmykSettings& settings)
{
    QByteArray compressed = PDFFlateDecodeFilter::compress(settings.targetIccData);
    PDFDictionary profileDictionary;
    profileDictionary.addEntry(PDFInplaceOrMemoryString("N"), PDFObject::createInteger(4));
    profileDictionary.addEntry(PDFInplaceOrMemoryString("Length"), PDFObject::createInteger(compressed.size()));
    profileDictionary.addEntry(PDFInplaceOrMemoryString("Filter"), PDFObject::createName("FlateDecode"));
    return builder->addObject(
        PDFObject::createStream(std::make_shared<PDFStream>(qMove(profileDictionary), qMove(compressed))));
}

PDFObject createCmykImageColorSpace(PDFObjectReference profileReference)
{
    auto colorSpace = std::make_shared<PDFArray>();
    colorSpace->appendItem(PDFObject::createName("ICCBased"));
    colorSpace->appendItem(PDFObject::createReference(profileReference));
    return PDFObject::createArray(qMove(colorSpace));
}

PDFOperationResult convertRgbImage(PDFDocumentBuilder* builder,
                                   const PDFDocument* document,
                                   PDFObjectReference imageReference,
                                   PDFObject imageObject,
                                   const PDFRgbToCmykSettings& settings,
                                   const PDFCMS* cms,
                                   PDFObjectReference profileReference)
{
    if (!imageObject.isStream())
    {
        return PDFTranslationContext::tr("RGB image XObject is not a stream.");
    }

    PDFImage image;
    try
    {
        image = PDFImage::createImage(document,
                                      imageObject.getStream(),
                                      PDFColorSpacePointer(new PDFDeviceRGBColorSpace()),
                                      false,
                                      RenderingIntent::Perceptual,
                                      nullptr);
    }
    catch (const PDFException& exception)
    {
        return PDFTranslationContext::tr("Unable to decode an RGB image XObject: %1")
            .arg(QString::fromUtf8(exception.what()));
    }

    const PDFImageData& source = image.getImageData();
    if (source.getComponents() != 3 || source.getWidth() == 0 || source.getHeight() == 0 ||
        source.getBitsPerComponent() == 0 || source.getBitsPerComponent() > 16 ||
        source.getMaskingType() == PDFImageData::MaskingType::ColorKeyMasking)
    {
        return PDFTranslationContext::tr("RGB image XObject has unsupported image samples.");
    }

    const std::vector<PDFReal>& decode = source.getDecode();
    if (!decode.empty() && decode.size() != 6)
    {
        return PDFTranslationContext::tr("RGB image XObject has an invalid decode array.");
    }

    const size_t pixelCount = static_cast<size_t>(source.getWidth()) * static_cast<size_t>(source.getHeight());
    if (pixelCount > std::numeric_limits<size_t>::max() / 3)
    {
        return PDFTranslationContext::tr("RGB image XObject is too large to convert.");
    }

    std::vector<PDFColorComponent> input(pixelCount * 3);
    std::vector<PDFColorComponent> output(pixelCount * 4);
    PDFBitReader reader(&source.getData(), source.getBitsPerComponent());
    const double maximum = reader.max();
    if (maximum <= 0.0)
    {
        return PDFTranslationContext::tr("RGB image XObject has invalid sample precision.");
    }

    for (unsigned int y = 0; y < source.getHeight(); ++y)
    {
        reader.seek(y * source.getStride());
        for (unsigned int x = 0; x < source.getWidth(); ++x)
        {
            for (unsigned int component = 0; component < 3; ++component)
            {
                const double normalized = static_cast<double>(reader.read()) / maximum;
                input[(static_cast<size_t>(y) * source.getWidth() + x) * 3 + component] =
                    decode.empty()
                        ? static_cast<PDFColorComponent>(normalized)
                        : static_cast<PDFColorComponent>(decode[component * 2] + normalized * (decode[component * 2 + 1] - decode[component * 2]));
            }
        }
    }

    PDFCMS::ColorSpaceTransformParams params;
    params.sourceType = settings.fallbackRgbIccData.isEmpty()
                            ? PDFCMS::ColorSpaceType::DeviceRGB
                            : PDFCMS::ColorSpaceType::ICC;
    params.targetType = PDFCMS::ColorSpaceType::ICC;
    params.sourceIccId = settings.fallbackRgbIccId;
    params.sourceIccData = settings.fallbackRgbIccData;
    params.targetIccId = targetProfileId(settings);
    params.targetIccData = settings.targetIccData;
    params.input = PDFColorBuffer(input.data(), input.size());
    params.output = PDFColorBuffer(output.data(), output.size());
    params.intent = settings.intent;
    if (!cms->transformColorSpace(params))
    {
        return PDFTranslationContext::tr("LittleCMS could not convert RGB image samples.");
    }

    QByteArray encoded;
    encoded.resize(static_cast<qsizetype>(pixelCount * 4));
    for (size_t i = 0; i < output.size(); ++i)
    {
        const PDFColorComponent component = qBound<PDFColorComponent>(0.0f, output[i], 1.0f);
        encoded[static_cast<qsizetype>(i)] = static_cast<char>(qRound(component * 255.0f));
    }

    PDFDictionary dictionary = *imageObject.getStream()->getDictionary();
    dictionary.setEntry(PDFInplaceOrMemoryString("ColorSpace"), createCmykImageColorSpace(profileReference));
    dictionary.setEntry(PDFInplaceOrMemoryString("BitsPerComponent"), PDFObject::createInteger(8));
    dictionary.setEntry(PDFInplaceOrMemoryString("Filter"), PDFObject::createName("FlateDecode"));
    dictionary.removeEntry("DecodeParms");
    PDFArray decodeArray;
    for (int i = 0; i < 4; ++i)
    {
        decodeArray.appendItem(PDFObject::createReal(0.0));
        decodeArray.appendItem(PDFObject::createReal(1.0));
    }
    dictionary.setEntry(PDFInplaceOrMemoryString("Decode"),
                        PDFObject::createArray(std::make_shared<PDFArray>(qMove(decodeArray))));
    dictionary.setEntry(PDFInplaceOrMemoryString("Length"), PDFObject::createInteger(encoded.size()));
    dictionary.setEntry(PDFInplaceOrMemoryString("Filter"), PDFObject::createName("FlateDecode"));
    const QByteArray compressed = PDFFlateDecodeFilter::compress(encoded);
    dictionary.setEntry(PDFInplaceOrMemoryString("Length"), PDFObject::createInteger(compressed.size()));
    builder->setObject(imageReference,
                       PDFObject::createStream(std::make_shared<PDFStream>(qMove(dictionary), compressed)));
    return true;
}

PDFOperationResult convertRgbImages(const PDFObject& resourcesObject,
                                    const PDFDocument* document,
                                    PDFDocumentBuilder* builder,
                                    const PDFRgbToCmykSettings& settings,
                                    const PDFCMS* cms,
                                    PDFObjectReference profileReference,
                                    PDFRgbToCmykReport* report)
{
    const PDFObject resources = builder->getStorage()->getObject(resourcesObject);
    if (!resources.isDictionary())
    {
        return true;
    }
    const PDFObject xObject = builder->getStorage()->getObject(resources.getDictionary()->get("XObject"));
    if (!xObject.isDictionary())
    {
        return true;
    }

    for (size_t i = 0; i < xObject.getDictionary()->getCount(); ++i)
    {
        const PDFObject objectReference = xObject.getDictionary()->getValue(i);
        const PDFObject object = builder->getStorage()->getObject(objectReference);
        if (!object.isStream())
        {
            continue;
        }
        const PDFDictionary* dictionary = object.getStream()->getDictionary();
        const PDFObject colorSpace = builder->getStorage()->getObject(dictionary->get("ColorSpace"));
        if (!dictionary->get("Subtype").isName() || dictionary->get("Subtype").getString() != QByteArrayLiteral("Image") ||
            !colorSpace.isName() || !isRgbColorSpaceName(colorSpace.getString()))
        {
            continue;
        }
        if (!objectReference.isReference())
        {
            return PDFTranslationContext::tr("RGB image XObject must be referenceable before conversion.");
        }
        const PDFOperationResult result = convertRgbImage(builder,
                                                          document,
                                                          objectReference.getReference(),
                                                          object,
                                                          settings,
                                                          cms,
                                                          profileReference);
        if (!result)
        {
            return result;
        }
        if (report)
        {
            ++report->imagesConverted;
        }
    }
    return true;
}

std::vector<StreamReference> collectPageStreams(const PDFDocument* document,
                                                PDFInteger pageIndex,
                                                const PDFPage* page)
{
    std::vector<StreamReference> result;
    std::vector<PDFObjectReference> references;
    // PDFPage::getContents() returns the already-dereferenced stream object, which loses
    // reference identity and makes appendContentReferences() treat it as an inline stream
    // (it only checks contentObject.isReference()). Read the raw, still-possibly-a-reference
    // /Contents entry straight from the page dictionary instead, so the underlying object can
    // actually be found and later rewritten via its PDFObjectReference.
    const PDFObject pageDictionaryObject = document->getStorage().getObjectByReference(page->getPageReference());
    const PDFObject rawContents = pageDictionaryObject.isDictionary()
                                      ? pageDictionaryObject.getDictionary()->get("Contents")
                                      : PDFObject();
    appendContentReferences(rawContents, &document->getStorage(), references);
    for (const PDFObjectReference reference : references)
    {
        result.push_back(StreamReference{ reference, pageIndex, PDFRgbToCmykObjectKind::VectorPaint });
    }
    return result;
}

void collectFormStreamsFromResources(const PDFObject& resourcesObject,
                                     const PDFObjectStorage* storage,
                                     PDFInteger pageIndex,
                                     std::vector<StreamReference>& result,
                                     std::set<PDFObjectReference>& visited)
{
    const PDFObject resources = storage->getObject(resourcesObject);
    if (!resources.isDictionary())
    {
        return;
    }

    const PDFObject xObject = storage->getObject(resources.getDictionary()->get("XObject"));
    if (!xObject.isDictionary())
    {
        return;
    }

    for (size_t i = 0; i < xObject.getDictionary()->getCount(); ++i)
    {
        const PDFObject referenceObject = xObject.getDictionary()->getValue(i);
        const PDFObject object = storage->getObject(referenceObject);
        if (!object.isStream())
        {
            continue;
        }
        const PDFDictionary* dictionary = object.getStream()->getDictionary();
        if (!dictionary->get("Subtype").isName() || dictionary->get("Subtype").getString() != QByteArrayLiteral("Form") || !referenceObject.isReference())
        {
            continue;
        }

        const PDFObjectReference reference = referenceObject.getReference();
        if (visited.insert(reference).second)
        {
            result.push_back(StreamReference{ reference, pageIndex, PDFRgbToCmykObjectKind::Form });
            collectFormStreamsFromResources(dictionary->get("Resources"), storage, pageIndex, result, visited);
        }
    }
}

void collectFormStreamsFromAnnotations(const PDFDocument* document,
                                       const PDFPage* page,
                                       PDFInteger pageIndex,
                                       std::vector<StreamReference>& result,
                                       std::set<PDFObjectReference>& visited)
{
    const PDFObjectStorage* storage = &document->getStorage();
    for (const PDFObjectReference annotationReference : page->getAnnotations())
    {
        const PDFObject annotation = storage->getObjectByReference(annotationReference);
        if (!annotation.isDictionary())
        {
            continue;
        }
        const PDFObject appearance = storage->getObject(annotation.getDictionary()->get("AP"));
        if (!appearance.isDictionary())
        {
            continue;
        }
        const PDFDictionary* appearanceDictionary = appearance.getDictionary();
        for (size_t i = 0; i < appearanceDictionary->getCount(); ++i)
        {
            const PDFObject value = appearanceDictionary->getValue(i);
            const PDFObject appearanceStream = storage->getObject(value);
            if (!appearanceStream.isStream() || !value.isReference())
            {
                continue;
            }
            const PDFDictionary* dictionary = appearanceStream.getStream()->getDictionary();
            if (dictionary->get("Subtype").isName() && dictionary->get("Subtype").getString() == QByteArrayLiteral("Form") && visited.insert(value.getReference()).second)
            {
                result.push_back(StreamReference{ value.getReference(), pageIndex,
                                                  PDFRgbToCmykObjectKind::AnnotationAppearance });
                collectFormStreamsFromResources(dictionary->get("Resources"), storage, pageIndex, result, visited);
            }
        }
    }
}

std::vector<PDFInteger> selectPageIndices(const PDFDocument* document,
                                          const QString& pageRange,
                                          QString* errorMessage)
{
    std::vector<PDFInteger> result;
    const PDFInteger pageCount = document ? PDFInteger(document->getCatalog()->getPageCount()) : 0;
    if (!document)
    {
        if (errorMessage)
        {
            *errorMessage = PDFTranslationContext::tr("Invalid document.");
        }
        return result;
    }

    std::set<PDFInteger> selected;
    const QString rangeText = pageRange.simplified();
    if (!rangeText.isEmpty())
    {
        QString parseError;
        const PDFClosedIntervalSet ranges = PDFClosedIntervalSet::parse(1, pageCount, rangeText, &parseError);
        if (!parseError.isEmpty())
        {
            if (errorMessage)
            {
                *errorMessage = parseError;
            }
            return {};
        }
        for (const PDFInteger pageNumber : ranges.unfold())
        {
            selected.insert(pageNumber);
        }
    }

    for (PDFInteger pageIndex = 0; pageIndex < pageCount; ++pageIndex)
    {
        if (!selected.empty() && !selected.count(pageIndex + 1))
        {
            continue;
        }
        result.push_back(pageIndex);
    }
    return result;
}

PDFOperationResult embedOutputIntent(PDFDocumentBuilder* builder,
                                     const PDFRgbToCmykSettings& settings,
                                     PDFObjectReference profileReference,
                                     PDFRgbToCmykReport* report)
{
    if (!settings.embedOutputIntent)
    {
        return true;
    }

    PDFDictionary intentDictionary;
    intentDictionary.addEntry(PDFInplaceOrMemoryString("Type"), PDFObject::createName("OutputIntent"));
    intentDictionary.addEntry(PDFInplaceOrMemoryString("S"), PDFObject::createName("GTS_PDFX"));
    intentDictionary.addEntry(PDFInplaceOrMemoryString("OutputConditionIdentifier"),
                              PDFObject::createString((settings.targetProfileName.isEmpty()
                                                           ? QString::fromLatin1(targetProfileId(settings).toHex())
                                                           : settings.targetProfileName)
                                                          .toUtf8()));
    intentDictionary.addEntry(PDFInplaceOrMemoryString("OutputCondition"),
                              PDFObject::createString(settings.targetProfileName.toUtf8()));
    intentDictionary.addEntry(PDFInplaceOrMemoryString("DestOutputProfile"),
                              PDFObject::createReference(profileReference));
    const PDFObjectReference intentReference = builder->addObject(
        PDFObject::createDictionary(std::make_shared<PDFDictionary>(qMove(intentDictionary))));

    PDFArray outputIntents;
    outputIntents.appendItem(PDFObject::createReference(intentReference));
    PDFDictionary catalogUpdate;
    catalogUpdate.addEntry(PDFInplaceOrMemoryString("OutputIntents"),
                           PDFObject::createArray(std::make_shared<PDFArray>(qMove(outputIntents))));
    builder->mergeTo(builder->getCatalogReference(),
                     PDFObject::createDictionary(std::make_shared<PDFDictionary>(qMove(catalogUpdate))));
    if (report)
    {
        report->outputIntentChanged = true;
    }
    return true;
}

PDFOperationResult analyzeImpl(const PDFDocument* document,
                               const PDFRgbToCmykSettings& settings,
                               PDFRgbToCmykReport* report,
                               const PDFCMS* cms,
                               const PDFObjectStorage* storage)
{
    QString pageSelectionError;
    const std::vector<PDFInteger> pageIndices = selectPageIndices(document, settings.pageRange, &pageSelectionError);
    if (!pageSelectionError.isEmpty())
    {
        return pageSelectionError;
    }

    std::set<PDFObjectReference> visited;
    for (const PDFInteger pageIndex : pageIndices)
    {
        const PDFPage* page = document->getCatalog()->getPage(pageIndex);
        if (!page)
        {
            continue;
        }

        scanImageResources(page->getResources(), document, pageIndex, report);
        std::vector<StreamReference> streams = collectPageStreams(document, pageIndex, page);
        std::set<PDFObjectReference> formReferences;
        collectFormStreamsFromResources(page->getResources(), &document->getStorage(), pageIndex, streams, formReferences);
        collectFormStreamsFromAnnotations(document, page, pageIndex, streams, formReferences);
        for (const StreamReference& stream : streams)
        {
            if (!visited.insert(stream.reference).second)
            {
                continue;
            }
            const PDFObject object = storage->getObjectByReference(stream.reference);
            if (!object.isStream())
            {
                continue;
            }

            RewriteResult rewrite;
            const PDFOperationResult result = rewriteRgbOperators(
                storage->getDecodedStream(object.getStream()), settings, cms, stream, report, &rewrite);
            if (!result)
            {
                return result;
            }
            if (report)
            {
                report->vectorPaintsConverted += rewrite.converted;
                if (stream.kind == PDFRgbToCmykObjectKind::Form)
                {
                    ++report->formsVisited;
                }
                else if (stream.kind == PDFRgbToCmykObjectKind::AnnotationAppearance)
                {
                    ++report->annotationAppearancesVisited;
                }
            }
        }
    }
    return true;
}

}   // namespace

PDFOperationResult PDFRgbToCmykFixup::previewRgbToCmyk(const PDFDocument* document,
                                                       const PDFRgbToCmykSettings& settings,
                                                       PDFRgbToCmykReport* report)
{
    if (report)
    {
        *report = PDFRgbToCmykReport();
    }
    if (!document)
    {
        return PDFTranslationContext::tr("Invalid document.");
    }

    const PDFOperationResult profileResult = validateTargetProfile(settings);
    if (!profileResult)
    {
        return profileResult;
    }

    PDFCMSManager cmsManager(nullptr);
    cmsManager.setDocument(document);
    PDFCMSSettings cmsSettings = cmsManager.getDefaultSettings();
    cmsSettings.isBlackPointCompensationActive = settings.blackPointCompensation;
    cmsManager.setSettings(cmsSettings);
    const PDFCMSPointer cms = cmsManager.getCurrentCMS();
    if (!cms)
    {
        return PDFTranslationContext::tr("No color-management system is available.");
    }

    return analyzeImpl(document, settings, report, cms.data(), &document->getStorage());
}

PDFOperationResult PDFRgbToCmykFixup::writeRgbToCmyk(PDFDocument* document,
                                                     const PDFRgbToCmykSettings& settings,
                                                     PDFRgbToCmykReport* report)
{
    if (report)
    {
        *report = PDFRgbToCmykReport();
    }

    PDFRgbToCmykReport localReport;
    const PDFOperationResult analysisResult = previewRgbToCmyk(document, settings, &localReport);
    if (!analysisResult)
    {
        return analysisResult;
    }
    if (!localReport.unsupported.isEmpty())
    {
        return PDFTranslationContext::tr(
                   "RGB-to-CMYK conversion cannot be completed safely: %1 unsupported RGB object(s) were found.")
            .arg(localReport.unsupported.size());
    }
    if (settings.dryRunOnly)
    {
        localReport.postflightPassed = true;
        if (report)
        {
            *report = qMove(localReport);
        }
        return true;
    }

    PDFDocumentModifier modifier(document);
    PDFDocumentBuilder* builder = modifier.getBuilder();
    const PDFObjectReference profileReference = addIccProfileObject(builder, settings);
    std::set<PDFObjectReference> visited;
    PDFCMSManager cmsManager(nullptr);
    cmsManager.setDocument(document);
    PDFCMSSettings cmsSettings = cmsManager.getDefaultSettings();
    cmsSettings.isBlackPointCompensationActive = settings.blackPointCompensation;
    cmsManager.setSettings(cmsSettings);
    const PDFCMSPointer cms = cmsManager.getCurrentCMS();

    // The first analysis is a safety gate. Start the mutation report at zero
    // so conversion counts describe the committed pass rather than both passes.
    localReport.vectorPaintsConverted = 0;
    localReport.imagesConverted = 0;
    localReport.indexedPalettesConverted = 0;
    localReport.formsVisited = 0;
    localReport.annotationAppearancesVisited = 0;

    QString pageSelectionError;
    const std::vector<PDFInteger> pageIndices = selectPageIndices(document, settings.pageRange, &pageSelectionError);
    if (!pageSelectionError.isEmpty())
    {
        return pageSelectionError;
    }

    for (const PDFInteger pageIndex : pageIndices)
    {
        const PDFPage* page = document->getCatalog()->getPage(pageIndex);
        if (!page)
        {
            continue;
        }

        const PDFOperationResult imageResult = convertRgbImages(page->getResources(),
                                                                document,
                                                                builder,
                                                                settings,
                                                                cms.data(),
                                                                profileReference,
                                                                &localReport);
        if (!imageResult)
        {
            return imageResult;
        }

        std::vector<StreamReference> streams = collectPageStreams(document, pageIndex, page);
        std::set<PDFObjectReference> formReferences;
        collectFormStreamsFromResources(page->getResources(), &document->getStorage(), pageIndex, streams, formReferences);
        collectFormStreamsFromAnnotations(document, page, pageIndex, streams, formReferences);
        for (const StreamReference& stream : streams)
        {
            if (!visited.insert(stream.reference).second)
            {
                continue;
            }
            const PDFObject object = builder->getObjectByReference(stream.reference);
            if (!object.isStream())
            {
                continue;
            }

            RewriteResult rewrite;
            const PDFOperationResult result = rewriteRgbOperators(
                builder->getDecodedStream(object.getStream()), settings, cms.data(), stream, &localReport, &rewrite);
            if (!result)
            {
                return result;
            }
            if (!rewrite.changed)
            {
                continue;
            }

            PDFDictionary dictionary = *object.getStream()->getDictionary();
            QByteArray encoded = PDFFlateDecodeFilter::compress(rewrite.content);
            dictionary.setEntry(PDFInplaceOrMemoryString("Length"), PDFObject::createInteger(encoded.size()));
            dictionary.setEntry(PDFInplaceOrMemoryString("Filter"), PDFObject::createName("FlateDecode"));
            builder->setObject(stream.reference,
                               PDFObject::createStream(std::make_shared<PDFStream>(qMove(dictionary), qMove(encoded))));
        }
    }

    const PDFOperationResult outputIntentResult = embedOutputIntent(builder,
                                                                    settings,
                                                                    profileReference,
                                                                    &localReport);
    if (!outputIntentResult)
    {
        return outputIntentResult;
    }

    modifier.markReset();
    modifier.markPageContentsChanged();
    if (!modifier.finalize())
    {
        return PDFTranslationContext::tr("Failed to finalize RGB-to-CMYK conversion.");
    }

    PDFDocumentPointer candidate = modifier.getDocument();
    if (settings.revalidate)
    {
        PDFRgbToCmykReport postflight;
        const PDFOperationResult postflightResult = previewRgbToCmyk(candidate.data(), settings, &postflight);
        if (!postflightResult)
        {
            return postflightResult;
        }
        if (!postflight.unsupported.isEmpty() || postflight.vectorPaintsConverted > 0)
        {
            return PDFTranslationContext::tr("Post-conversion validation still found RGB content.");
        }
    }

    *document = *candidate;
    localReport.postflightPassed = true;
    if (report)
    {
        *report = qMove(localReport);
    }
    return true;
}

}   // namespace pdf
