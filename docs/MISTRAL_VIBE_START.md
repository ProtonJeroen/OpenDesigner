# Mistral Vibe — OpenDesigner startup and safety contract
**Start here.** Local/offline C++20 Qt6/OCCT project; source is the authority.

## First session
```sh
cd ~/OpenDesigner
git status --short --branch
git log -1 --oneline
git branch -vv
sed -n '1,160p' README.md
sed -n '1,180p' docs/LOCAL_AI_HANDOVER.md
```
If the repository is in another location, locate it first; never create a second copy blindly.

Read `docs/LOCAL_AI_HANDOVER.md`, `docs/ROADMAP.md`, `CMakeLists.txt`, relevant feature docs and tests. The handover is an index, not an alternative truth source.

## Rules
- **Geometry-first:** current B-Rep determines direct modeling; no primitive/history shortcuts.
- **Safety:** never run `git clean`, `git reset --hard`, force push, or delete private/untracked `.odc` fixtures.
- **Git:** work on a dedicated branch; do not merge or push protected branches without approval.
- **Quality:** preserve preview/Cancel/OK/Undo/Redo, Part identity, metadata, native persistence and material validity.
- **Tests:** run focused CTest, then relevant broader suite. Record failures and environment; do not alter tests to hide defects.
- **UI:** distinguish headless/Xvfb tests from actual Fedora KDE manual acceptance.
- **Scope:** small reversible commits, no broad refactors without explicit approval.
- **Autonomy:** overnight coding may proceed only within the user-approved task and safe boundaries; stop on ambiguous geometry or destructive actions.
- **Privacy:** never commit private CAD fixtures or user documents.
- **Offline:** no external AI APIs; MCP deferred.

## Default build
```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DOPENDESIGNER_BUILD_TESTS=ON
cmake --build build -j2
ctest --test-dir build --output-on-failure
```
Adjust parallelism for local RAM/thermals. Some GUI tests require a display or Xvfb.

## Required end-of-session report
```text
Task / acceptance criteria:
Branch and HEAD:
Working tree status:
Changed files and design rationale:
Build command and result:
Targeted tests (exact command, pass/fail):
Full tests (exact command, pass/fail/not run):
Manual Fedora KDE acceptance (passed/pending):
Known risks / unsupported cases:
Next smallest safe step:
```

## Current assignment (analysis only)
Audit the actual `develop` HEAD (expected `fb370e5`, PR #79 merged, project version `0.5.6`,
v0.5.7 merged but unreleased), the open v0.5.7/v0.5.8 acceptance gates in `docs/ROADMAP.md`,
the current CTest registration and the `rotation-pivot` CI timeout, then produce a proposed
prioritized plan. Do not modify code until the user approves the task.
