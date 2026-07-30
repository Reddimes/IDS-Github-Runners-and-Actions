# Plan: Refactor Interaction Test Schema and Runner

## Overview
This plan details the refactoring of the interaction test suite to support multi-step test cases. Currently, test cases are limited to a single input/expected pair. This change enables complex user interactions by introducing a sequence of steps within each test case.

## Requirements
- **D-01**: Convert `input` and `expected` strings in `tests/interaction_test_cases.json` into a `steps` array of objects.
- **D-02**: Each step object supports `input`, `expected` (preserving `\r\n`), `fuzzy_match` (optional), and `skip_verification` (optional).
- **D-03**: Update `scripts/interact.py` to iterate through the `steps` array and apply step-specific logic.
- **D-04**: Ensure tests pass in the `windows_test.yml` GitHub Action.

## Implementation Steps

### Step 1 — Update Test Case Schema
**File**: `tests/interaction_test_cases.json`
**Task**: Transform the `test_cases` array to use a `steps` array instead of single `input`/`expected` fields.
- For each test case, replace `input` and `expected` with a `steps` array.
- Example step object: `{"input": "hello", "expected": "hello\r\nolleh", "fuzzy_match": true}`.
- Migrate all existing test data to this new format.
**Verify**: `jq '.test_cases[0].steps[0].input' tests/interaction_test_cases.json` returns the first input.

### Step 2 — Refactor Python Runner
**File**: `scripts/interact.py`
**Task**: Update the execution loop to process the new `steps` array.
- Replace the single input/expected logic with a loop: `for step in tc['steps']:`.
- Within the loop, use `step['input']` and `step['expected']`.
- Apply step-level configuration:
    - Use `step.get('fuzzy_match', False)` for matching logic.
    - Skip matching if `step.get('skip_verification', False)` is `True`.
- Ensure the runner handles `\r\n` in `expected` strings correctly.
- Update logging and results reporting to reflect step-level outcomes.
**Verify**: `python scripts/interact.py tests/interaction_test_cases.json` runs without errors and reports correct results.

### Step 3 — Verification
**Task**: Confirm the implementation via the existing GitHub Action.
- Trigger `windows_test.yml` via GitHub Actions.
**Verify**: Workflow completes successfully.

## Success Criteria

- [ ] `tests/interaction_test_cases.json` uses the `steps` array format.
- [ ] `scripts/interact.py` correctly executes multiple steps per test case.
- [ ] `fuzzy_match` flag is respected at the step level.
- [ ] `skip_verification` flag is respected at the step level.
- [ ] `windows_test.yml` workflow passes.

## Test Plan

| Step | Test Type | File |
|------|-----------|------|
| Schema Validation | Manual/JSON Schema | `tests/interaction_test_cases.json` |
| Runner Logic | Unit/Integration | `scripts/interact.py` |
| End-to-End | CI (GitHub Actions) | `windows_test.yml` |

## Rollback Plan

If the refactor breaks the test suite:
1. Revert `tests/interaction_test_cases.json` to its original schema.
2. Revert `scripts/interact.py` to its previous state.
3. Verify that the original single-step tests pass.
