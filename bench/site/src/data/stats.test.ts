/**
 * Unit tests for trajectory distillation and percentile helpers.
 * Run: npm test (from bench/site)
 */
import assert from "node:assert/strict";
import { describe, it } from "node:test";
import type { AccuracyResult, AccuracyTask } from "../types/benchmark";
import {
  buildTrajectoryDistillationReport,
  buildTrajectorySummary,
  computeStats,
} from "./stats";

const mockTask: AccuracyTask = {
  taskId: "t1",
  instruction: "do thing",
  website: "https://example.com",
  difficulty: "easy",
  domain: "shopping",
};

function result(overrides: Partial<AccuracyResult> = {}): AccuracyResult {
  return {
    taskId: "t1",
    success: true,
    steps: 4,
    totalTimeMs: 1000,
    llmTimeMs: 600,
    browserTimeMs: 400,
    llmCalls: 3,
    tokensUsed: 1200,
    judgeVerdict: "pass",
    judgeConfidence: 0.9,
    ...overrides,
  };
}

describe("buildTrajectorySummary", () => {
  it("returns empty summary for no results", () => {
    const s = buildTrajectorySummary([]);
    assert.equal(s.sampleCount, 0);
    assert.equal(s.successRate, 0);
    assert.equal(s.judgeAdjustedSuccessRate, 0);
  });

  it("computes judge-adjusted rate with partial verdicts", () => {
    const s = buildTrajectorySummary([
      result({ success: true, judgeVerdict: "pass" }),
      result({ success: false, judgeVerdict: "partial" }),
      result({ success: false, judgeVerdict: "fail" }),
    ]);
    assert.equal(s.sampleCount, 3);
    assert.equal(s.successRate, 1 / 3);
    assert.ok(Math.abs(s.judgeAdjustedSuccessRate - (1 + 0.5 + 0) / 3) < 1e-9);
    assert.equal(s.partialRate, 1 / 3);
  });

  it("uses success fallback when judgeVerdict is absent", () => {
    const s = buildTrajectorySummary([
      result({ success: true, judgeVerdict: undefined }),
      result({ success: false, judgeVerdict: undefined }),
    ]);
    assert.equal(s.judgeAdjustedSuccessRate, 0.5);
  });
});

describe("buildTrajectoryDistillationReport", () => {
  it("includes only trusted drivers in aggregates", () => {
    const accuracy = {
      runId: "r1",
      timestamp: "2026-01-01T00:00:00Z",
      hardware: {
        cpu: "x",
        cores: 1,
        ramGb: 8,
        os: "linux",
        disk: "ssd",
      },
      llmModel: "mock",
      provenance: {
        origin: "first_party" as const,
        trust: "trusted" as const,
        collectedFrom: "test",
      },
      tasks: [mockTask],
      drivers: [
        {
          driverName: "trusted_drv",
          displayName: "Trusted",
          version: "1",
          llmModel: "m",
          provenance: {
            origin: "first_party" as const,
            trust: "trusted" as const,
            collectedFrom: "us",
          },
          results: [result()],
          overallAccuracy: 1,
          accuracyByDifficulty: { easy: 1, medium: 0, hard: 0 },
          accuracyByDomain: { shopping: 1 },
          avgSteps: 4,
          avgTimeMs: 1000,
          avgTokens: 1200,
        },
        {
          driverName: "untrusted_drv",
          displayName: "Untrusted",
          version: "1",
          llmModel: "m",
          provenance: {
            origin: "vendor_report" as const,
            trust: "untrusted" as const,
            collectedFrom: "vendor",
          },
          results: [result({ success: false, judgeVerdict: "fail" })],
          overallAccuracy: 0,
          accuracyByDifficulty: { easy: 0, medium: 0, hard: 0 },
          accuracyByDomain: { shopping: 0 },
          avgSteps: 4,
          avgTimeMs: 1000,
          avgTokens: 1200,
        },
      ],
      ranking: [],
    };

    const report = buildTrajectoryDistillationReport(accuracy, [
      {
        benchmark: "X",
        agent: "Y",
        score: 99,
        sourceUrl: "https://example.com",
        date: "2026-03",
        provenance: {
          origin: "public_leaderboard",
          trust: "untrusted",
          collectedFrom: "lb",
        },
      },
    ]);

    assert.equal(report.drivers.length, 1);
    assert.equal(report.drivers[0]?.driverName, "trusted_drv");
    assert.equal(report.excludedExternalScores.length, 1);
    assert.ok(
      report.methodology.includes("trusted first-party"),
      "methodology should describe trusted-only aggregation"
    );
  });

  it("drops results whose taskId is not in the task index", () => {
    const accuracy = {
      runId: "r2",
      timestamp: "2026-01-01T00:00:00Z",
      hardware: {
        cpu: "x",
        cores: 1,
        ramGb: 8,
        os: "linux",
        disk: "ssd",
      },
      llmModel: "mock",
      provenance: {
        origin: "first_party" as const,
        trust: "trusted" as const,
        collectedFrom: "test",
      },
      tasks: [mockTask],
      drivers: [
        {
          driverName: "only",
          displayName: "Only",
          version: "1",
          llmModel: "m",
          provenance: {
            origin: "first_party" as const,
            trust: "trusted" as const,
            collectedFrom: "us",
          },
          results: [
            result({ taskId: "t1" }),
            result({ taskId: "unknown-task", success: false }),
          ],
          overallAccuracy: 0.5,
          accuracyByDifficulty: { easy: 0.5, medium: 0, hard: 0 },
          accuracyByDomain: { shopping: 0.5 },
          avgSteps: 4,
          avgTimeMs: 1000,
          avgTokens: 1200,
        },
      ],
      ranking: [],
    };

    const drivers = buildTrajectoryDistillationReport(accuracy).drivers;
    assert.equal(drivers.length, 1);
    assert.equal(drivers[0]!.overall.sampleCount, 1);
  });
});

describe("computeStats", () => {
  it("returns zeros for empty input", () => {
    const s = computeStats([]);
    assert.equal(s.p50, 0);
    assert.equal(s.mean, 0);
  });

  it("interpolates percentiles", () => {
    const s = computeStats([10, 20, 30, 40]);
    assert.equal(s.p50, 25);
    assert.ok(s.p95 > s.p50);
  });
});
