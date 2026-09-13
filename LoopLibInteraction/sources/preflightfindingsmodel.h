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

#ifndef PREFLIGHTFINDINGSMODEL_H
#define PREFLIGHTFINDINGSMODEL_H

#include "interactionglobal.h"
#include "interactiontarget.h"
#include "overlayframe.h"
#include "preflightengine.h"

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QRectF>
#include <QStringList>

namespace pdfinteraction
{

struct PreflightFindingView
{
    QString id;
    QString documentKey;
    QString documentRevision;
    QString scope;
    int page = 0;
    QString objectId;
    QString severity;
    QString type;
    QString message;
    QString checkId;
    QRectF bbox;
    QStringList evidenceIds;
    bool selected = false;
    /// True when an active operator disposition covers this finding, so it no
    /// longer counts as blocking in the document's verdict.
    bool waived = false;
};

struct FindingOverlay
{
    QString findingId;
    QString documentRevision;
    int page = 0;
    QRectF bbox;
    QString severity;
    bool selected = false;
};

class PreflightFindingsModel final : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role
    {
        FindingIdRole = Qt::UserRole + 1,
        DocumentKeyRole,
        DocumentRevisionRole,
        ScopeRole,
        PageRole,
        ObjectIdRole,
        SeverityRole,
        TypeRole,
        MessageRole,
        CheckIdRole,
        BoundingBoxRole,
        EvidenceIdsRole,
        SelectedRole,
        WaivedRole
    };
    Q_ENUM(Role)

    explicit PreflightFindingsModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void replace(QString documentKey,
                 QString documentRevision,
                 const QList<pdf::PreflightFinding>& errors,
                 const QList<pdf::PreflightFinding>& warnings);

    /// As above, and marks the findings named by \p waivedFindingIds as covered
    /// by an active disposition. An overload rather than a defaulted parameter:
    /// a default argument changes the exported symbol and this library's
    /// callers link against it directly.
    void replace(QString documentKey,
                 QString documentRevision,
                 const QList<pdf::PreflightFinding>& errors,
                 const QList<pdf::PreflightFinding>& warnings,
                 const QStringList& waivedFindingIds);
    void clear();
    void setSelectedFinding(const QString& findingId);

    Q_INVOKABLE QString findingIdAt(int row) const;

    bool containsCurrent(const QString& findingId, const QString& documentRevision) const;
    const PreflightFindingView* finding(const QString& findingId) const;
    QVector<FindingOverlay> overlays(const QString& documentRevision, int page) const;
    QVector<PreflightFindingView> filtered(QString severity = {}, QString checkId = {}, int page = 0) const;
    QHash<QString, int> groupCounts(QString severity = {}) const;

    QList<InteractionTarget> interactionTargets() const;
    QHash<QString, OverlaySeverity> severityMap() const;

    QString selectedFindingId() const { return m_selectedFindingId; }
    QString documentKey() const { return m_documentKey; }
    QString documentRevision() const { return m_documentRevision; }

signals:
    void selectedFindingIdChanged(const QString& findingId);
    void findingsReplaced();

private:
    static PreflightFindingView makeView(const QString& documentKey,
                                         const QString& documentRevision,
                                         const pdf::PreflightFinding& finding,
                                         bool waived);

    QVector<PreflightFindingView> m_findings;
    QString m_documentKey;
    QString m_documentRevision;
    QString m_selectedFindingId;
};

}   // namespace pdfinteraction

Q_DECLARE_METATYPE(pdfinteraction::FindingOverlay)

#endif   // PREFLIGHTFINDINGSMODEL_H
