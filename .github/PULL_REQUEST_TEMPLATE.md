## Summary

<!-- One or two sentences: what changes and why. Link the milestone / issue. -->

## Module(s) touched

- [ ] `network` (Member 1)
- [ ] `http` / `proxy` (Member 2)
- [ ] `balancer` / `datastructures` / `health` (Member 3)
- [ ] `common` / build / docs (shared — needs Technical Lead review)

## Checklist (author)

- [ ] `cmake --build build -j` succeeds with `-DAEGIS_WARNINGS_AS_ERRORS=ON`
- [ ] `ctest --test-dir build --output-on-failure` passes (including with the `asan` preset)
- [ ] New behaviour has a test; previously `DISABLED_` tests that now pass are re-enabled
- [ ] Unimplemented paths return `ErrorCode::NotImplemented`, never silent success
- [ ] No new public-header dependency between modules without updating `docs/interfaces.md`
- [ ] Ownership / lifetime changes are reflected in `docs/ownership.md`
- [ ] `clang-format` applied (`scripts/format.sh`)

## Reviewer checklist

See `CONTRIBUTING.md` → "Code review checklist".

## Notes for reviewers

<!-- Anything surprising, trade-offs, follow-ups. -->
