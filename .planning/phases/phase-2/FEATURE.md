# Feature: Refactor interaction test case schema and runner to support multiple inputs/outputs

**Phase:** 2
**Created:** 2026-07-30T17:45:00Z
**Status:** defined

## Description

Refactor both `tests/interaction_test_cases.json` and `scripts/interact.py` to support multiple sequential inputs and expected outputs. This enables testing programs that require a series of inputs and allows for verifying the resulting output sequence within a single test case entry.

## Acceptance Criteria

- `tests/interaction_test_cases.json` updated to new schema (using `inputs` and `expected` arrays).
- `scripts/interact.py` refactored to iterate through the input array and match against the expected output array.
- Existing test cases converted to the new schema format.
- All interaction tests pass in the Windows CI environment.

## Out of Scope

- Changing the underlying interaction engine logic (beyond iterating through inputs).
- Adding new test case types.
