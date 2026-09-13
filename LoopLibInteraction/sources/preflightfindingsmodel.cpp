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

#include "preflightfindingsmodel.h"

#include <QSet>
#include <QVariant>

namespace pdfinteraction
{

namespace
{

OverlaySeverity severityFromString(const QString& severity)
{
    if (severity == QStringLiteral("error"))
    {
        return OverlaySeverity::Error;
    }
    if (severity == QStringLiteral("warning"))
    {
        return OverlaySeverity::Warning;
    }
    if (severity == QStringLiteral("info"))
    {
        return OverlaySeverity::Info;
    }
    return OverlaySeverity::Info;
}

bool hasRenderableBbox(const QRectF& bbox)
{
    return !bbox.isNull() && !bbox.isEmpty();
}

}   // namespace

PreflightFindingsModel::PreflightFindingsModel(QObject* parent) :
    QAbstractListModel(parent)
{
}

int PreflightFindingsModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_findings.size();
}

QVariant PreflightFindingsModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_findings.size())
    {
        return {};
    }

    const PreflightFindingView& finding = m_findings.at(index.row());
    switch (role)
    {
        case Qt::DisplayRole:
        case MessageRole:
            return finding.message;
        case FindingIdRole:
            return finding.id;
        case DocumentKeyRole:
            return finding.documentKey;
        case DocumentRevisionRole:
            return finding.documentRevision;
        case ScopeRole:
            return finding.scope;
        case PageRole:
            return finding.page;
        case ObjectIdRole:
            return finding.objectId;
        case SeverityRole:
            return finding.severity;
        case TypeRole:
            return finding.type;
        case CheckIdRole:
            return finding.checkId;
        case BoundingBoxRole:
            return finding.bbox;
        case EvidenceIdsRole:
            return finding.evidenceIds;
        case SelectedRole:
            return finding.selected;
        case WaivedRole:
            return finding.waived;
    }
    return {};
}

QHash<int, QByteArray> PreflightFindingsModel::roleNames() const
{
    return {
        { FindingIdRole, "findingId" },
        { DocumentKeyRole, "documentKey" },
        { DocumentRevisionRole, "documentRevision" },
        { ScopeRole, "scope" },
        { PageRole, "page" },
        { ObjectIdRole, "objectId" },
        { SeverityRole, "severity" },
        { TypeRole, "type" },
        { MessageRole, "message" },
        { CheckIdRole, "checkId" },
        { BoundingBoxRole, "boundingBox" },
        { EvidenceIdsRole, "evidenceIds" },
        { SelectedRole, "selected" },
        { WaivedRole, "waived" }
    };
}

PreflightFindingView PreflightFindingsModel::makeView(const QString& documentKey,
                                                      const QString& documentRevision,
                                                      const pdf::PreflightFinding& finding,
                                                      bool waived)
{
    return {
        finding.stableId(), documentKey, documentRevision, finding.scope, finding.page,
        finding.objectId, finding.severity, finding.type, finding.message, finding.checkId,
        finding.bbox, finding.evidenceIds, false, waived
    };
}

void PreflightFindingsModel::replace(QString documentKey,
                                     QString documentRevision,
                                     const QList<pdf::PreflightFinding>& errors,
                                     const QList<pdf::PreflightFinding>& warnings)
{
    replace(std::move(documentKey), std::move(documentRevision), errors, warnings, {});
}

void PreflightFindingsModel::replace(QString documentKey,
                                     QString documentRevision,
                                     const QList<pdf::PreflightFinding>& errors,
                                     const QList<pdf::PreflightFinding>& warnings,
                                     const QStringList& waivedFindingIds)
{
    const QSet<QString> waived(waivedFindingIds.cbegin(), waivedFindingIds.cend());

    QVector<PreflightFindingView> next;
    next.reserve(errors.size() + warnings.size());
    for (const pdf::PreflightFinding& finding : errors)
    {
        next.push_back(makeView(documentKey, documentRevision, finding, waived.contains(finding.stableId())));
    }
    for (const pdf::PreflightFinding& finding : warnings)
    {
        next.push_back(makeView(documentKey, documentRevision, finding, waived.contains(finding.stableId())));
    }

    beginResetModel();
    m_documentKey = std::move(documentKey);
    m_documentRevision = std::move(documentRevision);
    m_findings = std::move(next);
    m_selectedFindingId.clear();
    endResetModel();
    Q_EMIT findingsReplaced();
}

void PreflightFindingsModel::clear()
{
    beginResetModel();
    m_findings.clear();
    m_documentKey.clear();
    m_documentRevision.clear();
    m_selectedFindingId.clear();
    endResetModel();
    Q_EMIT findingsReplaced();
}

void PreflightFindingsModel::setSelectedFinding(const QString& findingId)
{
    if (!findingId.isEmpty() && !finding(findingId))
    {
        return;
    }

    const QString previous = m_selectedFindingId;
    m_selectedFindingId = findingId;
    for (PreflightFindingView& item : m_findings)
    {
        item.selected = item.id == m_selectedFindingId;
    }

    for (int row = 0; row < m_findings.size(); ++row)
    {
        if (m_findings.at(row).id == previous || m_findings.at(row).id == m_selectedFindingId)
        {
            Q_EMIT dataChanged(index(row), index(row), { SelectedRole });
        }
    }

    Q_EMIT selectedFindingIdChanged(m_selectedFindingId);
}

QString PreflightFindingsModel::findingIdAt(int row) const
{
    if (row < 0 || row >= m_findings.size())
    {
        return QString();
    }

    return m_findings.at(row).id;
}

bool PreflightFindingsModel::containsCurrent(const QString& findingId, const QString& documentRevision) const
{
    return documentRevision == m_documentRevision && finding(findingId) != nullptr;
}

const PreflightFindingView* PreflightFindingsModel::finding(const QString& findingId) const
{
    for (const PreflightFindingView& item : m_findings)
    {
        if (item.id == findingId)
        {
            return &item;
        }
    }
    return nullptr;
}

QVector<FindingOverlay> PreflightFindingsModel::overlays(const QString& documentRevision, int page) const
{
    QVector<FindingOverlay> result;
    if (documentRevision != m_documentRevision)
    {
        return result;
    }
    for (const PreflightFindingView& finding : m_findings)
    {
        if (finding.page == page)
        {
            result.push_back({ finding.id, finding.documentRevision, finding.page, finding.bbox,
                               finding.severity, finding.selected });
        }
    }
    return result;
}

QVector<PreflightFindingView> PreflightFindingsModel::filtered(QString severity, QString checkId, int page) const
{
    QVector<PreflightFindingView> result;
    for (const PreflightFindingView& finding : m_findings)
    {
        if (!severity.isEmpty() && finding.severity != severity)
        {
            continue;
        }
        if (!checkId.isEmpty() && finding.checkId != checkId)
        {
            continue;
        }
        if (page > 0 && finding.page != page)
        {
            continue;
        }
        result.push_back(finding);
    }
    return result;
}

QHash<QString, int> PreflightFindingsModel::groupCounts(QString severity) const
{
    QHash<QString, int> result;
    for (const PreflightFindingView& finding : m_findings)
    {
        if (!severity.isEmpty() && finding.severity != severity)
        {
            continue;
        }
        ++result[finding.checkId];
    }
    return result;
}

QList<InteractionTarget> PreflightFindingsModel::interactionTargets() const
{
    QList<InteractionTarget> targets;
    for (const PreflightFindingView& finding : m_findings)
    {
        if (!hasRenderableBbox(finding.bbox) || finding.page <= 0)
        {
            continue;
        }

        InteractionTarget target;
        target.kind = InteractionTargetKind::Finding;
        target.pageIndex = finding.page - 1;
        target.id = finding.id;
        target.pageBounds = finding.bbox.normalized();
        targets.push_back(target);
    }
    return targets;
}

QHash<QString, OverlaySeverity> PreflightFindingsModel::severityMap() const
{
    QHash<QString, OverlaySeverity> severities;
    for (const PreflightFindingView& finding : m_findings)
    {
        // An actively waived finding is a recorded disposition, not a blocker:
        // painting it as an error would contradict the PASS verdict the same
        // controller just announced. The overlay vocabulary has no Waived
        // severity of its own (adding one ripples through CanvasPalette and the
        // Quick token set), so the non-blocking treatment is what it maps to.
        severities.insert(finding.id,
                          finding.waived ? OverlaySeverity::Info : severityFromString(finding.severity));
    }
    return severities;
}

}   // namespace pdfinteraction
