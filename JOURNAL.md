# Project journal — trajectory distillation & benchmark site

This file tracks blockers, risks, and prioritized work for the `bench/site` accuracy layer and trusted trajectory distillation.

## Where we are stuck

- **No in-repo journal before this change**: Action items lived only in branch context; there was no `JOURNAL.md` in the tree to anchor “stuck” items. This file is now the canonical place.

## What's concerning

- **Untrusted external scores**: Vendor and leaderboard numbers in `bench/site/src/data/contestants.ts` must stay out of aggregates. The distillation report keeps them in `excludedExternalScores` only; any future UI must not fold them into rankings.
- **Mock vs real data**: `MOCK_REPORT` is synthetic. Production paths should load real benchmark result JSON once a stable export path exists (the Python `bench/` runner was removed on `master`; define a new ingestion path when runs resume).
- **Strict TypeScript**: `noUncheckedIndexedAccess` is on; new code should avoid silent undefined access (tests use non-null assertions only where guarded).

## Completed this iteration

- **Overall summary aligned with task filter**: `distillDriverTrajectories` now builds `overall` from results whose `taskId` exists in the run’s task list, matching `byDifficulty`, `byDomain`, and `perTask` (see `bench/site/src/data/stats.ts`).
- **Removed dead code**: Deleted unused `emptyDifficultyBreakdown` in `stats.ts`.
- **Automated tests**: Added `bench/site/src/data/stats.test.ts` (Node test runner + `tsx`). Run `npm test` from `bench/site`.
- **Build split**: `tsconfig.build.json` excludes `*.test.ts` so `npm run build` emits only library output.
- **Documentation**: `bench/site/README.md` documents the site package, typecheck, test, and build commands (repo `master` removed the top-level `bench/README.md` with the old Python suite).

## # Next Steps (Prioritized)

1. **Wire real accuracy JSON** into the site data layer (replace or augment `MOCK_REPORT`) with the same provenance and distillation rules.
2. **Add a minimal HTML or framework UI** that renders speed + accuracy + distillation side by side, with external scores clearly labeled untrusted.
3. **CI**: Run `cd bench/site && npm ci && npm run typecheck && npm test` on pull requests touching `bench/site`.
4. **Extend tests** for edge cases (empty trusted driver list, all tasks unknown, mixed provenance in one file).
