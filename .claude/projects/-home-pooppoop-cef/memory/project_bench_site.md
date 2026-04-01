---
name: bench_site_requirements
description: Benchmark website name is surfcomp, uses COSS UI style system, light mode only
type: project
---

Benchmark website requirements:
- Name/URL: **surfcomp**
- UI framework: COSS style system (`@coss/style` via shadcn)
- Setup: `pnpm dlx shadcn@latest init @coss/style` then `pnpm dlx shadcn@latest add @coss/style`
- Style reference: https://coss.com/ui
- Light mode only
- Build last (after everything else works)

**Why:** The website showcases the benchmark results with two novel visualizations:
1. Stochastic Pachinko State Grid (three.js + @react-three/fiber, ~10k particles, GPGPU compute shaders)
2. DOM-State Cladogram (d3-hierarchy + SVG/Canvas, Brownian motion chaotic knots)

**How to apply:** When building the site, use `pnpm` not `npm`, init with COSS style, and implement both visualizations from bench/design/
