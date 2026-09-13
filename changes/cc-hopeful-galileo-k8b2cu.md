# Loop UI design system tokens and canonical state mapping

Category: added
Audience: developers
Breaking-Change: no
Summary: Add the Loop UI design system's load-bearing pieces for issue #194:
`pdfquick::tokens` semantic spacing/colour-role tokens with dark, light, and
high-contrast values (`LoopLibQuick/sources/looptokens.h`), the canonical
`resolveStateVisual()` finding/check presentation mapping
(`LoopLibQuick/sources/loopstatevisual.h`) with a table-driven test asserting
incomplete checks and waived findings never resolve to the passed treatment,
and `docs/LOOP_DESIGN_SYSTEM.md` documenting the tokens, the mapping, and
current adoption state. Component implementations and their consuming
surfaces (#193, #195, #196, #127) are still open and out of this change's
scope.
