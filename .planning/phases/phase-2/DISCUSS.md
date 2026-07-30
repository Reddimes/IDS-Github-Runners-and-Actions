# Phase 2 Discussion

## Decisions

D-01: Scope — Refactor schema and runner to support multiple inputs/outputs. Include Node.js orchestration.
      Rationale: Support complex orchestration and multiple data flows.

D-02: Schema Versioning — No `version` field.
      Rationale: Simplified schema for the current phase.

D-03: Configuration Locality — `fuzzy_match` moved to individual steps.
      Rationale: Increases granularity of matching configuration.

D-04: Verification Control — `skip_verification` added to individual steps.
      Rationale: Provides flexibility in skipping verification for specific steps.

D-05: Acceptance Criteria — Success = successful GitHub workflow run.
      Rationale: Defines clear success metric tied to the workflow execution.

D-06: Risk Management — No formal schema evolution strategy at this stage.
      Rationale: Focusing on core functionality first.

D-07: UI Classification — `standard` (No UI changes).
      Rationale: The changes are backend/logic-only.

## Open Questions
- None

## Out of Scope
- UI/UX design and implementation
