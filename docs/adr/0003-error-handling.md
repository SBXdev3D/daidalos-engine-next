# ADR-0003: Error handling

- **Status:** Accepted
- **Date:** 2026-10-27

## Context

OVERVIEW §7.3 requires an explicit error strategy and left the use of exceptions open. The legacy code used an exception hierarchy.

## Decision

- Fallible operations return `Result<T>`, an alias of `tl::expected<T, Error>`. `Error` contains a code, the module name, and a message with context.
- `DAI_ASSERT` checks programming errors and invariants. Its handler can be replaced in tests.
- Exceptions stay enabled in the compiler because some third-party and standard-library code throws, but no exception crosses a module boundary. Calls to throwing APIs are wrapped and converted to `Error`.
- Libraries that do not throw are preferred (for example glaze instead of nlohmann/json).

## Consequences

- Error paths are visible in function signatures and can be tested.
- The legacy `CException*` classes are deleted.
- When the project moves to C++23, `tl::expected` can be replaced by `std::expected` with little change.

## Alternatives considered

- Exceptions everywhere: hidden control flow, harder to reason about across module and thread boundaries.
- Error codes with out-parameters: easy to ignore and verbose.

## Amendment (2026-11-03): one-line forwarding, literal module names

Most failures are passed on unchanged, which took four lines each time. `DAI_TRY(expression)` returns the error of a `Result`, and `DAI_TRY_ASSIGN(Type name, expression)` also declares its value. A step that must undo something before returning keeps an explicit `if`. `Error::module` is a `std::string_view`, because module names are always string literals.
