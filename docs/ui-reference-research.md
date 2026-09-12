# UI reference research

This note records the parts of the requested repositories that are useful for
Parallel Finder. The project is Qt 6/QML, so these references are translated
into QML primitives and state rules rather than copied as React or web code.

## Sources

- [Anthropic quickstarts](https://github.com/anthropics/anthropic-quickstarts) — small, explicit foundations with project-local setup and stateful workflows.
- [Anthropic courses](https://github.com/anthropics/courses) — prompt structure, explicit roles, separation of instructions from data, and predictable output sections.
- [shadcn/ui](https://github.com/shadcn-ui/ui) — open, composable primitives with understandable defaults and visible state styling.
- [awesome-claude-prompts](https://github.com/richardsimko/awesome-claude-prompts) — role/context/constraints/output discipline for design and frontend work.
- [Dub](https://github.com/steven-tey/dub) — repository-local rules and scoped conventions instead of one global, implicit style.
- [Naming cheatsheet](https://github.com/kettanaito/naming-cheatsheet) — English names, S-I-D (short, intuitive, descriptive), action/context naming, and explicit boolean prefixes.

## Applied rules

### 1. Make the UI state explicit

The main window now has named states instead of one decorative placeholder:

- empty: no sources, motion field and a clear “add video” action;
- ready: sources exist, start action becomes available;
- running: progress line and status text are visible;
- results: A/B previews, four timeline markers, selectable rows and export;
- settings/export overlays: movable, dismissible, keyboard-closeable surfaces.

The same state is reflected in text, enabled/disabled controls and color. No
critical state is communicated by color alone.

### 2. Use composable primitives

The UI owns small reusable components rather than styling every control inline:

- `PfButton.qml` for primary, quiet and sage actions;
- `PfIconButton.qml` for Lucide-style icon-only actions with an accessible name;
- `PfSlider.qml` for compact matcher controls and a restrained focus/hover glow;
- `Theme.qml` for palette, typography, radii, shadows and layout tokens;
- `L10n.qml` for all visible strings.

These components are deliberately open and local: a future change can be
reviewed in one file and does not depend on a hidden UI framework.

### 3. Preserve the product hierarchy

The window is a motion-analysis workbench, not a generic dashboard. Therefore:

- the central comparison stage is the visual anchor;
- the left rail owns sources and the nine matcher parameters;
- the right rail owns result selection, filters and export;
- the six statistics are a hairline-separated strip, not six floating cards;
- rails use equal vertical margins and the central stage gets the most space.

### 4. Name for intent

QML IDs and properties use English, descriptive names (`selectedExportRows`,
`previewA`, `resetMatcherSettings`, `hasSelectedExport`). Boolean properties
use `has/is/should` semantics. Paired values use `A/B`, `prev/next`, and
singular/plural names match the data they contain.

### 5. Keep visual language intentional

The palette follows the approved design tokens: true-black canvas, warm ink,
terracotta for A/actions, sage for B/readiness, quiet hairlines, and a single
soft neon treatment around the motion field and focused controls. Cards are
raised only when they establish hierarchy; the empty state is a motion
trajectory, not an arbitrary radar graphic.

## Verification checklist

- Every new visible string is routed through `L10n.qml` or is a data label.
- Every color used by a component comes from `Theme.qml` unless it is a
  semantic A/B stroke in the motion canvas.
- Popups have a dimmed backdrop, shadow, close action and a draggable header.
- Controls expose tooltip/accessibility text where an icon replaces a label.
- The layout has a minimum window size and does not rely on invisible overflow.
- UI smoke tests assert canonical theme tokens, while visual review remains the
  final acceptance check.
