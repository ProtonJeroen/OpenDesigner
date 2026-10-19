# ChatGPT to Codex — OpenDesigner handoff (2026-10-19)

These files transfer **project decisions and known context**, not full ChatGPT conversation history.

Repository: https://github.com/ProtonJeroen/OpenDesigner (main). At preparation, main's README described an open-source 3D direct-modeling app; no `CMakeLists.txt` or `src/main.cpp` was found at the expected paths. Code built locally in prior discussions may reside on the user's Fedora KDE computer or another branch, so **do not assume this repository contains the latest working source**.

## First Codex task (copy into Codex)
> Inspect this OpenDesigner checkout and read AGENTS.md, ROADMAP.md, ARCHITECTURE.md and DECISIONS.md. Compare the files and recent git history to the reported local build. Identify existing primitive, selection, manipulator and renderer implementations. Confirm the lower-face arrow fix remains intact. Then implement the next requested sphere primitive, using the established project patterns, with appropriate build and functional checks. Report modified files, test results and unresolved assumptions.

## Local preparation on Fedora KDE
```sh
git status
git remote -v
git branch -avv
git log -5 --oneline
```
If the local source is ahead of GitHub, commit/push to a suitable branch before expecting Codex cloud to use that code. Local Codex CLI can inspect local files directly from the project working directory.

Keep proprietary CoCreate reference installers/ISOs and assets out of this public repository.
