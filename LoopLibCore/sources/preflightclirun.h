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

#ifndef PREFLIGHTCLIRUN_H
#define PREFLIGHTCLIRUN_H

#include "pdfglobal.h"
#include "pdfdocumentreader.h"
#include "preflightengine.h"

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace pdf
{

class PDFOperationControl;
struct PreflightResolvedProfile;

struct LOOPLIBCORESHARED_EXPORT PreflightFileInspectionRequest
{
    QString documentPath;
    QString password;
    bool permissiveReading = false;
    QJsonObject profile;
    PDFRevalidationPlan plan;
    QJsonObject jobSpec;
    QJsonObject cliBindings;
    PDFOperationControl* cancellation = nullptr;
};

struct LOOPLIBCORESHARED_EXPORT PreflightFileInspectionOutcome
{
    PreflightResult report;
    QByteArray sourceData;
    bool documentReadOk = false;
    PDFDocumentReader::Result readResult = PDFDocumentReader::Result::Failed;
    QString readErrorMessage;
    QStringList readWarnings;
    bool inspectionRan = false;
};

/// Runs a file-backed preflight inspection entirely inside LoopLibCore so host
/// executables never own parsed PDF state across the MSVC DLL boundary.
LOOPLIBCORESHARED_EXPORT PreflightFileInspectionOutcome inspectPreflightFile(
    const PreflightFileInspectionRequest& request);

/// Adds the provenance fields which make a preflight result a normalized report.
/// Hosts must use this instead of assembling report identity independently; the
/// CLI and the interactive shell consequently export the identical JSON shape.
LOOPLIBCORESHARED_EXPORT void finalizePreflightResult(PreflightResult& result,
                                                      const QByteArray& documentRevisionHash,
                                                      const PreflightResolvedProfile& profile);

}   // namespace pdf

#endif   // PREFLIGHTCLIRUN_H
