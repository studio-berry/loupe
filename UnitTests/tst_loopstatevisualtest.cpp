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

#include "loopstatevisual.h"
#include "preflightengine.h"

#include <QtTest>

#include <cmath>
#include <set>

using pdfquick::tokens::ColorRole;
using pdfquick::tokens::FocusOutlineOffsetPx;
using pdfquick::tokens::FocusOutlineWidthPx;
using pdfquick::tokens::LoopStateVisual;
using pdfquick::tokens::LoopTheme;
using pdfquick::tokens::MinimumKeyboardTargetPx;
using pdfquick::tokens::MinimumPointerTargetPx;
using pdfquick::tokens::resolveStateVisual;
using pdfquick::tokens::SpaceL;
using pdfquick::tokens::SpaceM;
using pdfquick::tokens::SpaceS;
using pdfquick::tokens::SpaceXl;
using pdfquick::tokens::SpaceXs;
using pdfquick::tokens::SpaceXxl;
using pdfquick::tokens::stateAccessibleName;
using pdfquick::tokens::StateIcon;
using pdfquick::tokens::StateKind;
using pdfquick::tokens::TypeBodyPx;
using pdfquick::tokens::TypeHeadingPx;
using pdfquick::tokens::TypeSmallPx;

Q_DECLARE_METATYPE(StateKind)
Q_DECLARE_METATYPE(ColorRole)
Q_DECLARE_METATYPE(StateIcon)

namespace
{

QString documentDigestA()
{
    return QString(64, QLatin1Char('a'));
}

QString documentDigestB()
{
    return QString(64, QLatin1Char('b'));
}

QString profileDigest()
{
    return QString(64, QLatin1Char('c'));
}

pdf::PreflightFinding findingWithSeverity(const QString& severity)
{
    pdf::PreflightFinding finding;
    finding.scope = QStringLiteral("page");
    finding.page = 1;
    finding.type = QStringLiteral("color-mode");
    finding.severity = severity;
    finding.checkId = QStringLiteral("color-mode");
    finding.message = QStringLiteral("test finding");
    return finding;
}

pdf::PreflightCheckStatus statusWith(const QString& status)
{
    pdf::PreflightCheckStatus checkStatus;
    checkStatus.id = QStringLiteral("color-mode");
    checkStatus.status = status;
    return checkStatus;
}

pdf::PreflightDecision waiveDecision(const QString& documentDigest)
{
    pdf::PreflightDecision decision;
    decision.findingId = QStringLiteral("finding-1");
    decision.kind = pdf::PreflightDecisionKind::Waive;
    decision.justification = QStringLiteral("accepted for this release");
    decision.operatorIdentity = QStringLiteral("qa@example.com");
    decision.timestampUtc = QDateTime::currentDateTimeUtc();
    decision.documentRevisionDigest = documentDigest;
    decision.effectiveProfileDigest = profileDigest();
    return decision;
}

pdf::PreflightDecision decisionOfKind(pdf::PreflightDecisionKind kind)
{
    pdf::PreflightDecision decision = waiveDecision(documentDigestA());
    decision.kind = kind;
    return decision;
}

qreal channel(int value)
{
    const qreal normalised = static_cast<qreal>(value) / 255.0;
    return normalised <= 0.04045 ? normalised / 12.92 : std::pow((normalised + 0.055) / 1.055, 2.4);
}

qreal relativeLuminance(const QColor& color)
{
    return 0.2126 * channel(color.red()) + 0.7152 * channel(color.green()) + 0.0722 * channel(color.blue());
}

qreal contrastRatio(const QColor& first, const QColor& second)
{
    const qreal light = std::max(relativeLuminance(first), relativeLuminance(second));
    const qreal dark = std::min(relativeLuminance(first), relativeLuminance(second));
    return (light + 0.05) / (dark + 0.05);
}

const StateKind kAllKinds[] = {
    StateKind::Error,
    StateKind::Warning,
    StateKind::Info,
    StateKind::Incomplete,
    StateKind::NotChecked,
    StateKind::Passed,
    StateKind::Waived,
};

LoopStateVisual visualForKind(StateKind kind)
{
    switch (kind)
    {
        case StateKind::Error:
        {
            const pdf::PreflightFinding finding = findingWithSeverity(QStringLiteral("error"));
            return resolveStateVisual(&finding, nullptr, nullptr);
        }
        case StateKind::Warning:
        {
            const pdf::PreflightFinding finding = findingWithSeverity(QStringLiteral("warning"));
            return resolveStateVisual(&finding, nullptr, nullptr);
        }
        case StateKind::Info:
        {
            const pdf::PreflightFinding finding = findingWithSeverity(QStringLiteral("info"));
            return resolveStateVisual(&finding, nullptr, nullptr);
        }
        case StateKind::Incomplete:
        {
            const pdf::PreflightCheckStatus status = statusWith(QStringLiteral("incomplete"));
            return resolveStateVisual(nullptr, &status, nullptr);
        }
        case StateKind::NotChecked:
            return resolveStateVisual(nullptr, nullptr, nullptr);
        case StateKind::Passed:
        {
            const pdf::PreflightCheckStatus status = statusWith(QStringLiteral("ok"));
            return resolveStateVisual(nullptr, &status, nullptr);
        }
        case StateKind::Waived:
        {
            const pdf::PreflightFinding finding = findingWithSeverity(QStringLiteral("error"));
            const pdf::PreflightDecision decision = waiveDecision(documentDigestA());
            return resolveStateVisual(&finding, nullptr, &decision, documentDigestA(), profileDigest());
        }
    }

    return resolveStateVisual(nullptr, nullptr, nullptr);
}

}   // namespace

class LoopStateVisualTest : public QObject
{
    Q_OBJECT

private slots:
    void severityMapping_data();
    void severityMapping();

    void checkStatusMapping_data();
    void checkStatusMapping();

    void notChecked_whenNothingProvided();

    void activeWaive_overridesSeverity();
    void staleWaive_fallsThroughToSeverity();
    void nonWaiveDecision_doesNotWaive_data();
    void nonWaiveDecision_doesNotWaive();

    void incompleteNeverResolvesToPassed_data();
    void incompleteNeverResolvesToPassed();

    void waivedNeverResolvesToPassed();

    void tokenGeometryMatchesAdmissionContract();
    void contrastMeetsWcag_data();
    void contrastMeetsWcag();
    void destructiveActionTextContrast_data();
    void destructiveActionTextContrast();

    void everyStateHasUniqueNonColorCues();
    void greyscaleCollisionRequiresDistinctIcon_data();
    void greyscaleCollisionRequiresDistinctIcon();

    void stateKindName_data();
    void stateKindName();
    void stateIconName_data();
    void stateIconName();
    void colorRoleName_data();
    void colorRoleName();

    void preflightStateVisualMapping_data();
    void preflightStateVisualMapping();
    void preflightStateVisualNeverPassesWithoutAPass_data();
    void preflightStateVisualNeverPassesWithoutAPass();
};

void LoopStateVisualTest::severityMapping_data()
{
    QTest::addColumn<QString>("severity");
    QTest::addColumn<StateKind>("expectedKind");
    QTest::addColumn<ColorRole>("expectedRole");
    QTest::addColumn<StateIcon>("expectedIcon");
    QTest::addColumn<QString>("expectedName");

    QTest::newRow("error") << QStringLiteral("error") << StateKind::Error << ColorRole::SeverityError << StateIcon::FilledCircle << QStringLiteral("Error");
    QTest::newRow("warning") << QStringLiteral("warning") << StateKind::Warning << ColorRole::SeverityWarning << StateIcon::FilledTriangle << QStringLiteral("Warning");
    QTest::newRow("info") << QStringLiteral("info") << StateKind::Info << ColorRole::SeverityInfo << StateIcon::FilledSquare << QStringLiteral("Info");
    QTest::newRow("unrecognised severity") << QStringLiteral("catastrophic") << StateKind::Incomplete << ColorRole::StateIncomplete << StateIcon::Hatched << QStringLiteral("Incomplete");
    QTest::newRow("empty severity") << QString() << StateKind::Incomplete << ColorRole::StateIncomplete << StateIcon::Hatched << QStringLiteral("Incomplete");
}

void LoopStateVisualTest::severityMapping()
{
    QFETCH(QString, severity);
    QFETCH(StateKind, expectedKind);
    QFETCH(ColorRole, expectedRole);
    QFETCH(StateIcon, expectedIcon);
    QFETCH(QString, expectedName);

    const pdf::PreflightFinding finding = findingWithSeverity(severity);
    const LoopStateVisual visual = resolveStateVisual(&finding, nullptr, nullptr);

    QCOMPARE(visual.kind, expectedKind);
    QCOMPARE(visual.colorRole, expectedRole);
    QCOMPARE(visual.icon, expectedIcon);
    QCOMPARE(visual.accessibleName, expectedName);
}

void LoopStateVisualTest::checkStatusMapping_data()
{
    QTest::addColumn<QString>("status");
    QTest::addColumn<StateKind>("expectedKind");

    QTest::newRow("ok") << QStringLiteral("ok") << StateKind::Passed;
    QTest::newRow("failed") << QStringLiteral("failed") << StateKind::Incomplete;
    QTest::newRow("warning status") << QStringLiteral("warning") << StateKind::Incomplete;
    QTest::newRow("skipped") << QStringLiteral("skipped") << StateKind::Incomplete;
    QTest::newRow("incomplete") << QStringLiteral("incomplete") << StateKind::Incomplete;
    QTest::newRow("unsupported") << QStringLiteral("unsupported") << StateKind::Incomplete;
}

void LoopStateVisualTest::checkStatusMapping()
{
    QFETCH(QString, status);
    QFETCH(StateKind, expectedKind);

    const pdf::PreflightCheckStatus checkStatus = statusWith(status);
    const LoopStateVisual visual = resolveStateVisual(nullptr, &checkStatus, nullptr);

    QCOMPARE(visual.kind, expectedKind);
    if (expectedKind == StateKind::Passed)
    {
        QCOMPARE(visual.colorRole, ColorRole::Success);
        QCOMPARE(visual.icon, StateIcon::Checkmark);
        QCOMPARE(visual.accessibleName, QStringLiteral("Passed"));
    }
    else
    {
        QCOMPARE(visual.colorRole, ColorRole::StateIncomplete);
        QCOMPARE(visual.icon, StateIcon::Hatched);
        QCOMPARE(visual.accessibleName, QStringLiteral("Incomplete"));
    }
}

void LoopStateVisualTest::notChecked_whenNothingProvided()
{
    const LoopStateVisual visual = resolveStateVisual(nullptr, nullptr, nullptr);
    QCOMPARE(visual.kind, StateKind::NotChecked);
    QCOMPARE(visual.colorRole, ColorRole::StateNotChecked);
    QCOMPARE(visual.icon, StateIcon::Outline);
    QCOMPARE(visual.accessibleName, QStringLiteral("Not checked"));
}

void LoopStateVisualTest::activeWaive_overridesSeverity()
{
    const pdf::PreflightFinding finding = findingWithSeverity(QStringLiteral("error"));
    const pdf::PreflightDecision decision = waiveDecision(documentDigestA());

    const LoopStateVisual visual = resolveStateVisual(&finding, nullptr, &decision, documentDigestA(), profileDigest());

    QCOMPARE(visual.kind, StateKind::Waived);
    QCOMPARE(visual.colorRole, ColorRole::SeverityWarning);
    QCOMPARE(visual.icon, StateIcon::BadgeOverlay);
    QCOMPARE(visual.accessibleName, QStringLiteral("Waived"));
}

void LoopStateVisualTest::staleWaive_fallsThroughToSeverity()
{
    const pdf::PreflightFinding finding = findingWithSeverity(QStringLiteral("error"));
    const pdf::PreflightDecision decision = waiveDecision(documentDigestA());

    const LoopStateVisual visual = resolveStateVisual(&finding, nullptr, &decision, documentDigestB(), profileDigest());

    QCOMPARE(visual.kind, StateKind::Error);
    QCOMPARE(visual.colorRole, ColorRole::SeverityError);
    QCOMPARE(visual.accessibleName, QStringLiteral("Error"));
}

void LoopStateVisualTest::nonWaiveDecision_doesNotWaive_data()
{
    QTest::addColumn<int>("kind");

    QTest::newRow("Accept") << static_cast<int>(pdf::PreflightDecisionKind::Accept);
    QTest::newRow("Override") << static_cast<int>(pdf::PreflightDecisionKind::Override);
    QTest::newRow("Reject") << static_cast<int>(pdf::PreflightDecisionKind::Reject);
    QTest::newRow("Reopen") << static_cast<int>(pdf::PreflightDecisionKind::Reopen);
}

void LoopStateVisualTest::nonWaiveDecision_doesNotWaive()
{
    QFETCH(int, kind);

    const pdf::PreflightFinding finding = findingWithSeverity(QStringLiteral("warning"));
    const pdf::PreflightDecision decision = decisionOfKind(static_cast<pdf::PreflightDecisionKind>(kind));

    const LoopStateVisual visual = resolveStateVisual(&finding, nullptr, &decision, documentDigestA(), profileDigest());

    QCOMPARE(visual.kind, StateKind::Warning);
    QCOMPARE(visual.colorRole, ColorRole::SeverityWarning);
    QCOMPARE(visual.accessibleName, QStringLiteral("Warning"));
}

void LoopStateVisualTest::incompleteNeverResolvesToPassed_data()
{
    QTest::addColumn<QString>("status");

    QTest::newRow("failed") << QStringLiteral("failed");
    QTest::newRow("warning") << QStringLiteral("warning");
    QTest::newRow("skipped") << QStringLiteral("skipped");
    QTest::newRow("incomplete") << QStringLiteral("incomplete");
    QTest::newRow("unsupported") << QStringLiteral("unsupported");
    QTest::newRow("unrecognised") << QStringLiteral("not-a-real-status");
}

void LoopStateVisualTest::incompleteNeverResolvesToPassed()
{
    QFETCH(QString, status);

    const pdf::PreflightCheckStatus checkStatus = statusWith(status);
    const LoopStateVisual visual = resolveStateVisual(nullptr, &checkStatus, nullptr);

    QVERIFY(visual.kind != StateKind::Passed);
    QVERIFY(visual.colorRole != ColorRole::Success);
    QVERIFY(visual.icon != StateIcon::Checkmark);
    QVERIFY(visual.accessibleName != QStringLiteral("Passed"));
}

void LoopStateVisualTest::waivedNeverResolvesToPassed()
{
    for (const QString& severity : { QStringLiteral("error"), QStringLiteral("warning"), QStringLiteral("info") })
    {
        const pdf::PreflightFinding finding = findingWithSeverity(severity);
        const pdf::PreflightDecision decision = waiveDecision(documentDigestA());

        const LoopStateVisual visual = resolveStateVisual(&finding, nullptr, &decision, documentDigestA(), profileDigest());

        QVERIFY(visual.kind != StateKind::Passed);
        QVERIFY(visual.colorRole != ColorRole::Success);
        QVERIFY(visual.icon != StateIcon::Checkmark);
        QVERIFY(visual.accessibleName != QStringLiteral("Passed"));
        QCOMPARE(visual.kind, StateKind::Waived);
        QCOMPARE(visual.accessibleName, QStringLiteral("Waived"));
    }
}

void LoopStateVisualTest::tokenGeometryMatchesAdmissionContract()
{
    QCOMPARE(SpaceXs, 4);
    QCOMPARE(SpaceS, 8);
    QCOMPARE(SpaceM, 12);
    QCOMPARE(SpaceL, 16);
    QCOMPARE(SpaceXl, 24);
    QCOMPARE(SpaceXxl, 32);
    QCOMPARE(TypeBodyPx, 14);
    QCOMPARE(TypeSmallPx, 12);
    QCOMPARE(TypeHeadingPx, 24);
    QCOMPARE(FocusOutlineWidthPx, 2);
    QCOMPARE(FocusOutlineOffsetPx, 2);
    QCOMPARE(MinimumPointerTargetPx, 44);
    QCOMPARE(MinimumKeyboardTargetPx, 32);
}

void LoopStateVisualTest::contrastMeetsWcag_data()
{
    QTest::addColumn<int>("theme");
    QTest::addColumn<int>("role");
    QTest::addColumn<qreal>("minimum");

    const ColorRole textRoles[] = { ColorRole::TextPrimary, ColorRole::TextSecondary };
    const ColorRole nonTextRoles[] = {
        ColorRole::SeverityError,
        ColorRole::SeverityWarning,
        ColorRole::SeverityInfo,
        ColorRole::Success,
        ColorRole::StateIncomplete,
        ColorRole::StateNotChecked,
        ColorRole::FocusRing,
    };
    const LoopTheme themes[] = { LoopTheme::Dark, LoopTheme::Light, LoopTheme::HighContrast };

    for (const LoopTheme theme : themes)
    {
        const QByteArray themeName = QByteArray::number(static_cast<int>(theme));
        for (const ColorRole role : textRoles)
        {
            const QByteArray name = themeName + "-text-" + QByteArray::number(static_cast<int>(role));
            QTest::newRow(name.constData()) << static_cast<int>(theme) << static_cast<int>(role) << 4.5;
        }
        for (const ColorRole role : nonTextRoles)
        {
            const QByteArray name = themeName + "-nontext-" + QByteArray::number(static_cast<int>(role));
            QTest::newRow(name.constData()) << static_cast<int>(theme) << static_cast<int>(role) << 3.0;
        }
    }
}

void LoopStateVisualTest::contrastMeetsWcag()
{
    QFETCH(int, theme);
    QFETCH(int, role);
    QFETCH(qreal, minimum);

    const LoopTheme loopTheme = static_cast<LoopTheme>(theme);
    const ColorRole colorRole = static_cast<ColorRole>(role);
    const QColor foreground = pdfquick::tokens::color(colorRole, loopTheme);
    const QColor background = pdfquick::tokens::color(ColorRole::SurfaceBase, loopTheme);

    QVERIFY(contrastRatio(foreground, background) >= minimum);
}

void LoopStateVisualTest::destructiveActionTextContrast_data()
{
    QTest::addColumn<int>("theme");

    QTest::newRow("dark") << static_cast<int>(LoopTheme::Dark);
    QTest::newRow("light") << static_cast<int>(LoopTheme::Light);
    QTest::newRow("high-contrast") << static_cast<int>(LoopTheme::HighContrast);
}

void LoopStateVisualTest::destructiveActionTextContrast()
{
    QFETCH(int, theme);

    const LoopTheme loopTheme = static_cast<LoopTheme>(theme);
    const QColor fill = pdfquick::tokens::color(ColorRole::DestructiveAction, loopTheme);
    QVERIFY(contrastRatio(QColor(Qt::white), fill) >= 4.5);
}

void LoopStateVisualTest::everyStateHasUniqueNonColorCues()
{
    std::set<int> icons;
    std::set<QString> names;

    for (const StateKind kind : kAllKinds)
    {
        const LoopStateVisual visual = visualForKind(kind);
        QCOMPARE(visual.kind, kind);
        QVERIFY(!visual.accessibleName.isEmpty());
        QCOMPARE(visual.accessibleName, stateAccessibleName(kind));
        QVERIFY(icons.insert(static_cast<int>(visual.icon)).second);
        QVERIFY(names.insert(visual.accessibleName).second);
        if (kind == StateKind::Incomplete || kind == StateKind::Waived || kind == StateKind::NotChecked)
        {
            QVERIFY(visual.icon != StateIcon::Checkmark);
            QVERIFY(visual.colorRole != ColorRole::Success);
            QVERIFY(visual.accessibleName != QStringLiteral("Passed"));
        }
    }

    QCOMPARE(static_cast<int>(icons.size()), 7);
    QCOMPARE(static_cast<int>(names.size()), 7);
}

void LoopStateVisualTest::greyscaleCollisionRequiresDistinctIcon_data()
{
    QTest::addColumn<int>("theme");

    QTest::newRow("dark") << static_cast<int>(LoopTheme::Dark);
    QTest::newRow("light") << static_cast<int>(LoopTheme::Light);
    QTest::newRow("high-contrast") << static_cast<int>(LoopTheme::HighContrast);
}

void LoopStateVisualTest::greyscaleCollisionRequiresDistinctIcon()
{
    QFETCH(int, theme);

    const LoopTheme loopTheme = static_cast<LoopTheme>(theme);
    constexpr qreal kLuminanceEpsilon = 0.02;

    for (const StateKind leftKind : kAllKinds)
    {
        const LoopStateVisual left = visualForKind(leftKind);
        const qreal leftLuminance = relativeLuminance(pdfquick::tokens::color(left.colorRole, loopTheme));

        for (const StateKind rightKind : kAllKinds)
        {
            if (leftKind == rightKind)
            {
                continue;
            }

            const LoopStateVisual right = visualForKind(rightKind);
            const qreal rightLuminance = relativeLuminance(pdfquick::tokens::color(right.colorRole, loopTheme));
            if (std::abs(leftLuminance - rightLuminance) < kLuminanceEpsilon)
            {
                QVERIFY(left.icon != right.icon);
                QVERIFY(left.accessibleName != right.accessibleName);
            }
        }
    }
}

void LoopStateVisualTest::stateKindName_data()
{
    QTest::addColumn<StateKind>("kind");
    QTest::addColumn<QString>("expectedName");

    // The name is the stable, QML-facing key. It deliberately differs from the accessible name
    // ("Not checked" vs "NotChecked"): one is read by the operator, the other is compared by code.
    QTest::newRow("error") << StateKind::Error << QStringLiteral("Error");
    QTest::newRow("warning") << StateKind::Warning << QStringLiteral("Warning");
    QTest::newRow("info") << StateKind::Info << QStringLiteral("Info");
    QTest::newRow("incomplete") << StateKind::Incomplete << QStringLiteral("Incomplete");
    QTest::newRow("not-checked") << StateKind::NotChecked << QStringLiteral("NotChecked");
    QTest::newRow("passed") << StateKind::Passed << QStringLiteral("Passed");
    QTest::newRow("waived") << StateKind::Waived << QStringLiteral("Waived");
}

void LoopStateVisualTest::stateKindName()
{
    QFETCH(StateKind, kind);
    QFETCH(QString, expectedName);

    QCOMPARE(pdfquick::tokens::stateKindName(kind), expectedName);
}

void LoopStateVisualTest::stateIconName_data()
{
    QTest::addColumn<StateIcon>("icon");
    QTest::addColumn<QString>("expectedName");

    QTest::newRow("filled-circle") << StateIcon::FilledCircle << QStringLiteral("FilledCircle");
    QTest::newRow("filled-triangle") << StateIcon::FilledTriangle << QStringLiteral("FilledTriangle");
    QTest::newRow("filled-square") << StateIcon::FilledSquare << QStringLiteral("FilledSquare");
    QTest::newRow("hatched") << StateIcon::Hatched << QStringLiteral("Hatched");
    QTest::newRow("outline") << StateIcon::Outline << QStringLiteral("Outline");
    QTest::newRow("checkmark") << StateIcon::Checkmark << QStringLiteral("Checkmark");
    QTest::newRow("badge-overlay") << StateIcon::BadgeOverlay << QStringLiteral("BadgeOverlay");
}

void LoopStateVisualTest::stateIconName()
{
    QFETCH(StateIcon, icon);
    QFETCH(QString, expectedName);

    QCOMPARE(pdfquick::tokens::stateIconName(icon), expectedName);
}

void LoopStateVisualTest::colorRoleName_data()
{
    QTest::addColumn<ColorRole>("role");
    QTest::addColumn<QString>("expectedName");

    QTest::newRow("surface-base") << ColorRole::SurfaceBase << QStringLiteral("SurfaceBase");
    QTest::newRow("surface-panel") << ColorRole::SurfacePanel << QStringLiteral("SurfacePanel");
    QTest::newRow("surface-overlay") << ColorRole::SurfaceOverlay << QStringLiteral("SurfaceOverlay");
    QTest::newRow("text-primary") << ColorRole::TextPrimary << QStringLiteral("TextPrimary");
    QTest::newRow("text-secondary") << ColorRole::TextSecondary << QStringLiteral("TextSecondary");
    QTest::newRow("text-disabled") << ColorRole::TextDisabled << QStringLiteral("TextDisabled");
    QTest::newRow("severity-error") << ColorRole::SeverityError << QStringLiteral("SeverityError");
    QTest::newRow("severity-warning") << ColorRole::SeverityWarning << QStringLiteral("SeverityWarning");
    QTest::newRow("severity-info") << ColorRole::SeverityInfo << QStringLiteral("SeverityInfo");
    QTest::newRow("success") << ColorRole::Success << QStringLiteral("Success");
    QTest::newRow("state-incomplete") << ColorRole::StateIncomplete << QStringLiteral("StateIncomplete");
    QTest::newRow("state-not-checked") << ColorRole::StateNotChecked << QStringLiteral("StateNotChecked");
    QTest::newRow("focus-ring") << ColorRole::FocusRing << QStringLiteral("FocusRing");
    QTest::newRow("destructive-action") << ColorRole::DestructiveAction << QStringLiteral("DestructiveAction");
}

void LoopStateVisualTest::colorRoleName()
{
    QFETCH(ColorRole, role);
    QFETCH(QString, expectedName);

    QCOMPARE(pdfquick::tokens::colorRoleName(role), expectedName);
}

void LoopStateVisualTest::preflightStateVisualMapping_data()
{
    QTest::addColumn<QString>("stateName");
    QTest::addColumn<StateKind>("expectedKind");

    // The names are pdf::preflightVerdictStateToString() / EditorHost::preflightStateName() output.
    QTest::newRow("not-checked") << QStringLiteral("not-checked") << StateKind::NotChecked;
    QTest::newRow("running") << QStringLiteral("running") << StateKind::Info;
    QTest::newRow("pass") << QStringLiteral("pass") << StateKind::Passed;
    QTest::newRow("findings") << QStringLiteral("findings") << StateKind::Error;
    QTest::newRow("fail") << QStringLiteral("fail") << StateKind::Error;
    QTest::newRow("incomplete") << QStringLiteral("incomplete") << StateKind::Incomplete;
    QTest::newRow("stale") << QStringLiteral("stale") << StateKind::Incomplete;
    QTest::newRow("cancelled") << QStringLiteral("cancelled") << StateKind::NotChecked;
    QTest::newRow("error") << QStringLiteral("error") << StateKind::Error;
    QTest::newRow("unknown") << QStringLiteral("something-new") << StateKind::NotChecked;
    QTest::newRow("empty") << QString() << StateKind::NotChecked;
    // A caller may hand over a raw Core string; the mapping is still total and case/whitespace-safe.
    QTest::newRow("pass with padding and case") << QStringLiteral("  PASS ") << StateKind::Passed;
    QTest::newRow("stale with padding and case") << QStringLiteral(" Stale") << StateKind::Incomplete;
}

void LoopStateVisualTest::preflightStateVisualMapping()
{
    QFETCH(QString, stateName);
    QFETCH(StateKind, expectedKind);

    const pdfquick::tokens::LoopStateVisual visual = pdfquick::tokens::resolvePreflightStateVisual(stateName);

    QCOMPARE(visual.kind, expectedKind);
    QVERIFY2(!visual.accessibleName.isEmpty(), "every state needs an operator-readable name");

    // The colour/icon pair must be the one #194 already assigns to that kind, not a new one.
    const LoopStateVisual canonical = visualForKind(expectedKind);
    QCOMPARE(visual.colorRole, canonical.colorRole);
    QCOMPARE(visual.icon, canonical.icon);
    QCOMPARE(visual.accessibleName, canonical.accessibleName);
}

void LoopStateVisualTest::preflightStateVisualNeverPassesWithoutAPass_data()
{
    QTest::addColumn<QString>("stateName");

    // Every state the workflow can be in, other than a real pass.
    QTest::newRow("not-checked") << QStringLiteral("not-checked");
    QTest::newRow("running") << QStringLiteral("running");
    QTest::newRow("findings") << QStringLiteral("findings");
    QTest::newRow("incomplete") << QStringLiteral("incomplete");
    QTest::newRow("stale") << QStringLiteral("stale");
    QTest::newRow("cancelled") << QStringLiteral("cancelled");
    QTest::newRow("error") << QStringLiteral("error");
    QTest::newRow("fail") << QStringLiteral("fail");
    QTest::newRow("empty") << QString();   // before any run / no accepted result
}

void LoopStateVisualTest::preflightStateVisualNeverPassesWithoutAPass()
{
    QFETCH(QString, stateName);

    // #195 acceptance: "The GUI never displays PASS when inspectionComplete is false or any check
    // status is not ok." The badge must therefore only ever be Passed for the pass state - including
    // for the empty state, which is what a freshly opened document shows.
    const LoopStateVisual visual = pdfquick::tokens::resolvePreflightStateVisual(stateName);

    QVERIFY(visual.kind != StateKind::Passed);
    QVERIFY(visual.colorRole != ColorRole::Success);
    QVERIFY(visual.icon != StateIcon::Checkmark);
    QVERIFY(visual.accessibleName != QStringLiteral("Passed"));
}

QTEST_APPLESS_MAIN(LoopStateVisualTest)

#include "tst_loopstatevisualtest.moc"
