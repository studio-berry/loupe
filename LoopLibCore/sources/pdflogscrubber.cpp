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

#include "pdflogscrubber.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QSysInfo>

namespace pdf
{

namespace
{

/// Turns a canonical ('/'-separated) directory path into a regular expression
/// pattern that matches the same path written with either '/' or '\' as the
/// separator, so a Windows path logged with backslashes still matches a home
/// directory reported by Qt with forward slashes (and vice versa).
QString toSeparatorAgnosticPattern(const QString& canonicalPath)
{
    QString pattern;
    pattern.reserve(canonicalPath.size() * 2);

    for (const QChar ch : canonicalPath)
    {
        if (ch == QLatin1Char('/') || ch == QLatin1Char('\\'))
        {
            pattern += QStringLiteral("[\\\\/]");
        }
        else
        {
            pattern += QRegularExpression::escape(QString(ch));
        }
    }

    return pattern;
}

/// Replaces every occurrence of the absolute directory \p directoryPath in
/// \p text with \p replacement. Only the directory prefix is replaced - any
/// path segments following it are left untouched, so a leftover remainder
/// (e.g. "/Documents/report.pdf") can still be caught by a later, more
/// generic absolute-path pass.
QString replaceAbsoluteDirectory(const QString& text, const QString& directoryPath, const QString& replacement)
{
    if (directoryPath.isEmpty() || (!text.contains(QStringLiteral("/")) && !text.contains(QStringLiteral("\\"))))
    {
        return text;
    }

    const QString pattern = QStringLiteral("(?<![\\w.\\-])") +
                            toSeparatorAgnosticPattern(directoryPath) +
                            QStringLiteral("(?=[\\\\/]|[^\\w.\\-]|$)");

    QString result = text;
    result.replace(QRegularExpression(pattern), replacement);
    return result;
}

/// Replaces every whole-word occurrence of \p token in \p text with
/// \p replacement. Used for the login name and host name, which are ordinary
/// words rather than path fragments. Tokens shorter than two characters are
/// ignored - they are too common as substrings of unrelated words to scrub
/// safely.
QString replaceToken(const QString& text, const QString& token, const QString& replacement)
{
    if (token.size() < 2)
    {
        return text;
    }

    const QString pattern = QStringLiteral("(?<![\\w])") + QRegularExpression::escape(token) + QStringLiteral("(?![\\w])");

    QString result = text;
    result.replace(QRegularExpression(pattern), replacement);
    return result;
}

/// Returns the login name from the environment, checking the POSIX variable
/// before the Windows one.
QString loginName()
{
    const QByteArray posixUser = qgetenv("USER");
    if (!posixUser.isEmpty())
    {
        return QString::fromLocal8Bit(posixUser);
    }

    const QByteArray windowsUser = qgetenv("USERNAME");
    if (!windowsUser.isEmpty())
    {
        return QString::fromLocal8Bit(windowsUser);
    }

    return QString();
}

/// Replaces any remaining absolute path (Windows drive-letter, UNC, or POSIX) with
/// a placeholder that keeps only the extension and drops the basename - see
/// the class comment in pdflogscrubber.h for why the basename is the part
/// that has to go.
QString scrubRemainingAbsolutePaths(const QString& text)
{
    static const QRegularExpression pathPattern(
        QStringLiteral(R"(([A-Za-z]:[\\/][^\s"'<>()\[\]{},;]*)|(\\\\[^\s"'<>()\[\]{},;]*)|((?<![:/])/(?!/)[^\s"'<>()\[\]{},;]*))"));

    QString result;
    result.reserve(text.size());

    int lastEnd = 0;
    QRegularExpressionMatchIterator it = pathPattern.globalMatch(text);
    while (it.hasNext())
    {
        const QRegularExpressionMatch match = it.next();
        result += text.mid(lastEnd, match.capturedStart() - lastEnd);

        const QString matchedPath = match.captured();
        const QString suffix = QFileInfo(matchedPath).suffix();
        result += suffix.isEmpty() ? QStringLiteral("<PATH>")
                                   : QStringLiteral("<PATH:.%1>").arg(suffix);

        lastEnd = match.capturedEnd();
    }
    result += text.mid(lastEnd);

    return result;
}

/// Replaces credential material with a placeholder. Three shapes are covered,
/// all of which are routinely logged verbatim by libraries that assume their
/// own configuration is not sensitive:
///   - URL userinfo ("https://key:secret@host/..."), which is the shape of a
///     Sentry DSN and of most ingest/webhook endpoints;
///   - HTTP authorization values ("Bearer <token>", "Basic <base64>"), as they
///     appear in request dumps;
///   - key/value pairs whose key names a secret ("token=", "\"api_key\": ...",
///     "password => ..."), in JSON, assignment, or query-string form.
/// The key is kept and only the value is replaced - knowing *which* setting was
/// misconfigured is the diagnostic value; the value after it is what has to go.
/// The key vocabulary matches isSensitiveKey() in pdfartifactidentity.cpp, minus
/// the path-shaped keys that the absolute-path pass already covers.
QString scrubCredentials(const QString& text)
{
    static const QString secretKey = QStringLiteral(
        "[A-Za-z0-9_.-]*(?:password|passwd|pswd|passphrase|secret|token|api[_.-]?key|apikey|"
        "access[_.-]?key|private[_.-]?key|credential|authorization|dsn|license[_.-]?key)[A-Za-z0-9_.-]*");

    // Auth scheme prefixes are consumed together with the token that follows
    // them, so "Authorization: Bearer abc" collapses to a single placeholder
    // instead of redacting "Bearer" and leaving "abc" behind.
    static const QString authScheme = QStringLiteral("(?:Bearer|Basic|Token|Digest|APIKey)\\s+");

    // The same scheme list minus "Token", for the unanchored pass below: after
    // a secret-named key the word is unambiguous, but on its own "token" is
    // ordinary English ("Unexpected token appeared") and redacting it would
    // eat parser diagnostics.
    static const QString bareAuthScheme = QStringLiteral("(?:Bearer|Basic|Digest|APIKey)\\s+");

    // '<' and '>' are excluded from every value class so an already-substituted
    // "<CREDENTIAL>" is never matched again - scrub() must stay idempotent.
    static const QRegularExpression urlUserInfoPattern(
        QStringLiteral(R"((?<![\w.+-])([A-Za-z][A-Za-z0-9+.-]*://)[^\s/@:"'<]+(?::[^\s/@"'<]*)?@)"),
        QRegularExpression::CaseInsensitiveOption);

    static const QRegularExpression secretKeyValuePattern(
        QStringLiteral(R"(\b(%1)("?\s*(?:=>|[:=])\s*"?)(?:%2)?[^\s"',;&}\]<>]+)").arg(secretKey, authScheme),
        QRegularExpression::CaseInsensitiveOption);

    // Two shapes of scheme-delimited credential are recognized:
    //   - a mixed token carrying at least one digit or punctuation character -
    //     what a JWT, a base64 digest, or most API keys look like. The lookahead
    //     keeps prose ("Basic rendering enabled") from reading as a header;
    //   - a purely alphabetic token of 16 or more characters - what an opaque
    //     credential looks like. Length is the only thing separating it from an
    //     English word, so the floor is deliberately high: over-redacting a
    //     16-letter word after a scheme name is the safe direction, leaking the
    //     credential is not.
    static const QRegularExpression authorizationPattern(
        QStringLiteral(R"(\b(%1)(?:(?=[A-Za-z0-9._~+/=-]*[0-9._~+/=-])[A-Za-z0-9._~+/=-]{8,}|[A-Za-z]{16,}))").arg(bareAuthScheme),
        QRegularExpression::CaseInsensitiveOption);

    QString result = text;
    result.replace(urlUserInfoPattern, QStringLiteral("\\1<CREDENTIAL>@"));
    result.replace(secretKeyValuePattern, QStringLiteral("\\1\\2<CREDENTIAL>"));
    result.replace(authorizationPattern, QStringLiteral("\\1<CREDENTIAL>"));
    return result;
}

QString scrubEmailAddresses(const QString& text)
{
    static const QRegularExpression emailPattern(
        QStringLiteral(R"(\b[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}\b)"));

    QString result = text;
    result.replace(emailPattern, QStringLiteral("<EMAIL>"));
    return result;
}

QString scrubIPv4Literals(const QString& text)
{
    static const QRegularExpression ipPattern(
        QStringLiteral(R"(\b(?:(?:25[0-5]|2[0-4]\d|1?\d?\d)\.){3}(?:25[0-5]|2[0-4]\d|1?\d?\d)\b)"));

    QString result = text;
    result.replace(ipPattern, QStringLiteral("<IP>"));
    return result;
}

QString scrubIPv6Literals(const QString& text)
{
    // This deliberately matches only colon-rich hexadecimal tokens. It avoids
    // adding a QtNetwork dependency to Core while covering compressed and
    // expanded IPv6 forms commonly emitted by Qt/network diagnostics.
    static const QRegularExpression ipPattern(
        QStringLiteral(R"((?<![0-9A-Fa-f:])(?:[0-9A-Fa-f]{0,4}:){2,7}[0-9A-Fa-f]{0,4}(?![0-9A-Fa-f:]))"));

    QString result = text;
    result.replace(ipPattern, QStringLiteral("<IP>"));
    return result;
}

}   // namespace

QString PDFLogScrubber::scrub(const QString& text)
{
    if (text.isEmpty())
    {
        return text;
    }

    QString result = text;

    // Home/temp directories first: they are frequently the longest matching
    // prefix, and scrubbing them before the generic path pass leaves a
    // remainder (e.g. "<HOME>/Documents/x.pdf") that the generic pass no
    // longer recognizes as an absolute path.
    result = replaceAbsoluteDirectory(result, QStandardPaths::writableLocation(QStandardPaths::HomeLocation), QStringLiteral("<HOME>"));
    result = replaceAbsoluteDirectory(result, QDir::tempPath(), QStringLiteral("<TEMP>"));

    result = replaceToken(result, loginName(), QStringLiteral("<USER>"));
    result = replaceToken(result, QSysInfo::machineHostName(), QStringLiteral("<HOST>"));

    // Credentials before the path/email passes: a DSN like
    // "https://key@ingest.example.com/42" would otherwise have its key eaten by
    // the email pass (leaving "<EMAIL>", which reads like user data rather than
    // a leaked secret) and its project id eaten by the path pass.
    result = scrubCredentials(result);

    result = scrubRemainingAbsolutePaths(result);
    result = scrubEmailAddresses(result);
    result = scrubIPv4Literals(result);
    result = scrubIPv6Literals(result);

    return result;
}

}   // namespace pdf
