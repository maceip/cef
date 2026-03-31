# agent-browser-bench site (TypeScript)

Types, mock benchmark data, and **trajectory distillation** helpers. Summaries use only **trusted** first-party accuracy runs; external leaderboard scores stay in `excludedExternalScores` and are never blended into aggregates.

## Commands

```bash
cd bench/site
npm ci
npm run typecheck   # strict TypeScript
npm test            # distillation + stats unit tests
npm run build       # emit dist/ (tests excluded via tsconfig.build.json)
```

## Layout

- `src/types/benchmark.ts` — schema (speed, accuracy, provenance, distillation)
- `src/data/stats.ts` — `buildTrajectoryDistillationReport`, `buildTrajectorySummary`, percentile helpers
- `src/data/mock-results.ts` — `MOCK_REPORT` for development
