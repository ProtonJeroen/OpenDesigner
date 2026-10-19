# OpenDesigner decision log

## Confirmed choices from prior ChatGPT project work
| Topic | Decision |
| --- | --- |
| Name | **OpenDesigner** (not 'OpenDesigner CAD') |
| License | **GPL-3.0** |
| Workflow reference | **CoCreate SolidDesigner / Modeling 2008 version 16.0**, superseding 15.x |
| Desktop targets | Linux, Windows, macOS |
| Tablet targets | iPadOS and Android tablets on roadmap |
| Touch | Optional via Settings, cross-platform, including touch-capable desktops |
| Desktop input | Mouse/keyboard interface remains available |
| Visualization formats | glTF/GLB and FBX import/export belong on roadmap alongside CAD formats |
| Reference ISO | May inform behavior if legally available; no proprietary code/assets in repo |

## Recent user-reported progress
- Local OpenDesigner application was built and launched.
- Lower-face arrow incorrectly moving top face was reportedly corrected.
- User then requested starting with a sphere primitive.

## Open technical decisions
UI and renderer, geometry kernel, native file format, CAD format priorities, exact parity implementation order. Inspect code before deciding.
