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

#ifndef OCRSIDECARPROTOCOL_H
#define OCRSIDECARPROTOCOL_H

#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

#include <cmath>

namespace pdftool::ocr
{

/// The OCR option defaults, in one place. They are consumed both by the
/// capability-discovery table (which tells callers what the defaults are) and by
/// the command-line parser (which applies them), so a single definition is what
/// keeps the advertised default and the applied default from drifting apart.
///
/// Language codes are ISO 639-1 ("en", "de"), matching what the loop-ocr sidecar
/// normalizes to in engine.py::normalize_languages. Nothing in Loop emits ISO
/// 639-2 ("eng", "deu"); if that ever changes, convert at this boundary rather
/// than teaching downstream consumers both code sets.
inline constexpr QLatin1StringView DEFAULT_OCR_LANGUAGES = QLatin1StringView("en");
inline constexpr QLatin1StringView DEFAULT_OCR_DPI = QLatin1StringView("300");
inline constexpr QLatin1StringView DEFAULT_OCR_MIN_TEXT_CHARS = QLatin1StringView("20");

inline QStringList normalizeLanguages(const QString& specification)
{
    QStringList languages;
    for (const QString& value : specification.split(',', Qt::SkipEmptyParts))
    {
        const QString language = value.trimmed().toLower();
        if (!language.isEmpty() && !languages.contains(language))
        {
            languages.append(language);
        }
    }

    if (languages.isEmpty())
    {
        languages.append(QString(DEFAULT_OCR_LANGUAGES));
    }
    else
    {
        languages.sort();
    }
    return languages;
}

inline bool isFiniteNumber(const QJsonValue& value)
{
    return value.isDouble() && std::isfinite(value.toDouble());
}

inline bool setValidationError(QString* errorMessage, const QString& message)
{
    if (errorMessage)
    {
        *errorMessage = message;
    }
    return false;
}

inline bool validateSidecarBbox(const QJsonValue& value, QString* errorMessage)
{
    if (!value.isObject())
    {
        return setValidationError(errorMessage, QStringLiteral("OCR sidecar line is missing a bbox object."));
    }

    const QJsonObject bbox = value.toObject();
    for (const QString& field : { QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("width"), QStringLiteral("height") })
    {
        if (!isFiniteNumber(bbox.value(field)))
        {
            return setValidationError(errorMessage, QStringLiteral("OCR sidecar bbox field '%1' must be finite.").arg(field));
        }
    }

    if (bbox.value(QStringLiteral("width")).toDouble() < 0.0 || bbox.value(QStringLiteral("height")).toDouble() < 0.0)
    {
        return setValidationError(errorMessage, QStringLiteral("OCR sidecar bbox width and height must be non-negative."));
    }
    return true;
}

inline bool validateSidecarResponse(const QJsonObject& response,
                                    int expectedPage,
                                    QString* errorMessage = nullptr)
{
    if (!isFiniteNumber(response.value(QStringLiteral("page"))) || response.value(QStringLiteral("page")).toDouble() != expectedPage)
    {
        return setValidationError(errorMessage, QStringLiteral("OCR sidecar returned the wrong page number."));
    }

    const QJsonValue okValue = response.value(QStringLiteral("ok"));
    if (!okValue.isBool())
    {
        return setValidationError(errorMessage, QStringLiteral("OCR sidecar response missing boolean 'ok'."));
    }

    if (!okValue.toBool())
    {
        if (!response.value(QStringLiteral("error")).isString() || response.value(QStringLiteral("error")).toString().trimmed().isEmpty())
        {
            return setValidationError(errorMessage, QStringLiteral("OCR sidecar failure response missing error text."));
        }
        return true;
    }

    if (!response.value(QStringLiteral("text")).isString() || !response.value(QStringLiteral("lines")).isArray())
    {
        return setValidationError(errorMessage, QStringLiteral("Malformed successful OCR sidecar response."));
    }

    const QJsonArray lines = response.value(QStringLiteral("lines")).toArray();
    for (const QJsonValue& lineValue : lines)
    {
        if (!lineValue.isObject())
        {
            return setValidationError(errorMessage, QStringLiteral("OCR sidecar line must be an object."));
        }

        const QJsonObject line = lineValue.toObject();
        if (!line.value(QStringLiteral("text")).isString() || !isFiniteNumber(line.value(QStringLiteral("confidence"))))
        {
            return setValidationError(errorMessage, QStringLiteral("Malformed OCR sidecar line."));
        }

        if (!validateSidecarBbox(line.value(QStringLiteral("bbox")), errorMessage))
        {
            return false;
        }
    }

    return true;
}

}   // namespace pdftool::ocr

#endif   // OCRSIDECARPROTOCOL_H
