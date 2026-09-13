# GitHub milestones

Canonical milestone text for [studio-berry/loop](https://github.com/studio-berry/loop) is maintained here and aligned with the Notion Loop [Roadmap](https://app.notion.com/p/38f9cb079ddb804a96dbe26b8d86e84f).

## Sequence

| Milestone | GitHub # | Status | Former alias |
|-----------|----------|--------|--------------|
| 0.0.1 | 1 | Historical | — |
| 0.0.2 | 2 | Historical (recovery baseline merged to `stable`) | — |
| 0.1.0 | 5 | Shipped as `0.1.0-alpha` | — |
| 0.1.1 | 4 | Living | 0.0.3 |
| 0.2.0 | 8 | Living | 0.0.4 (supersedes retired `0.1.2` title) |
| 0.2.1 | 17 | Living | — |
| 0.3.0 | 9 | Living | 0.0.5 (supersedes retired `0.1.3` title) |
| 0.4.0 | 10 | Living | 0.0.6 (supersedes retired `0.1.4` title) |
| 0.5.0 | 11 | Planned (proposed) | — |
| 0.6.0 | 12 | Planned (proposed) | 0.7.0 (retired 2026-09-06 consolidation title) |
| 0.7.0 | 13 | Planned (proposed) | 0.8.0 (former title; also consolidates retired `0.9.0`) |
| 0.8.0 | 14 | Planned (proposed) | 0.10.0 (retired 2026-09-06 consolidation title) |

The living release train is **0.1.1 → 0.2.0 → 0.2.1 → 0.3.0 → 0.4.0**, continuing into the amended
planned train **0.5.0 → 0.6.0 → 0.7.0 → 0.8.0**. Retired `0.1.2`–`0.1.4` GitHub milestone titles are closed by the sync script.

## Consolidation amendment (2026-09-06)

The previously proposed 0.5.0–0.10.0 train was consolidated into four releases per the
canonical Notion Roadmap amendment of the same date: **0.6.0 absorbs the former 0.7.0**
(production outcome reconciliation) and **0.7.0 absorbs the former 0.9.0** (workflow
promotion and verified repeat automation); the former **0.10.0 renumbers to 0.8.0**.
Ceremony sessions (S00 reconcile openers, vertical integrations, qualification lanes) were
folded into their owning sessions; nothing was dropped. Scope decomposition:
[`docs/ROADMAP_0.5.0-0.8.0.md`](../ROADMAP_0.5.0-0.8.0.md). Each planned milestone
activates only on its predecessor's release acceptance.

## Sync

Apply descriptions to GitHub with a token that has `issues: write` on the repository:

```bash
python scripts/github/sync_milestones.py --apply
```

Dry run (default):

```bash
python scripts/github/sync_milestones.py
```

The script matches milestones by title, creates missing canonical milestones, updates description plus optional open/closed state from [`manifest.json`](manifest.json), and closes retired titles listed under `retire` (currently `0.1.2`, `0.1.3`, `0.1.4`, `0.9.0`, `0.10.0`).
