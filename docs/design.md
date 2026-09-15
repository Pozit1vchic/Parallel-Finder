# Parallel Finder — visual system

Status: **source of truth for stage 5 UI**. This document translates the product
brief into an original Qt/QML interface. It takes atmosphere from the supplied
Claude references, but never copies their page layout; workspace geometry and
interaction follow the Parallel-Alpha prototype.

## Product character

Parallel Finder is an editorial motion-analysis workstation, not a generic
dashboard. It should feel like a quiet, precise film tool: the video and its
motion traces are the visual event; chrome is supporting equipment.

One memorable visual per screen is enough. In the analysis state it is the
selected A/B pair and its exact timecodes. The empty state stays
quiet and action-first: no aura, particle field, radar rings, fake target or
perpetual decorative animation.

## Visual tokens

| Role | Token | Value |
| --- | --- | --- |
| true canvas | `canvas` | `#0C0D0B` |
| app rail | `rail` | `#11120F` |
| primary surface | `surface` | `#171813` |
| raised surface | `surfaceRaised` | `#1E1F19` |
| inset / video well | `well` | `#0A0B09` |
| warm text | `ink` | `#F5F1EC` |
| muted text | `muted` | `#ACA89F` |
| terracotta action | `terracotta` | `#D97757` |
| sage context | `sage` | `#7C9885` |
| hairline | `hairline` | `rgba(245,241,236,0.10)` |

Shadows are black, soft and layered: panels use a 16–24px soft shadow at
0.12–0.22 opacity; overlays use 28–36px at 0.45–0.65. Light is not a border
replacement. Use a 1px low-contrast hairline for containment, then a shadow
only where an object is physically elevated.

Terracotta communicates action and the A side of a pair. Sage communicates
context, readiness and the B side. They must not be sprayed around the screen.
No blue, cyan, generic neon, gradients, or arbitrary grey cards.

## Typography

Use an editorial serif (`Georgia` fallback until a bundled licensed-safe font
is chosen) only for the product wordmark, empty-state title, selected pair
title and restrained dialogue headings. Use `Segoe UI` for data and controls.

- wordmark: 22px serif, normal weight;
- section heading: 16px sans, semibold;
- important value: 22–25px serif, normal weight;
- body: 12–13px sans;
- metadata: 10–11px sans, sentence case.

Do not make every metadata label all-caps. Avoid fake product language and
unnecessary dots or separators.

## Main workspace

The layout remains three columns and is never replaced by a marketing hero:

```
┌────────────── material ─────────────┬────── analysis / comparison ──────┬──── results ────┐
│ sources + queue                     │ six compact stats, not six cards  │ sort + list      │
│ compact controls in a scroll rail    │ full-height viewport, pan + zoom    │ selected export │
│ quality / backend / start             │ progress / A-B preview             │ virtual list    │
└──────────────────────────────────────┴───────────────────────────────────┴─────────────────┘
```

All three regions align to the same outer inset. Their top edges align. The
left and right rail have the same width; the center takes all remaining room.
At 1100px minimum window width, the rails remain usable and their internal
content scrolls instead of being clipped.

The six statistics sit as a single information strip divided by hairlines, not
as a row of unrelated rounded cards. The comparison surface is the only large
elevated surface in the center column.

## Components and states

### Sources and controls

The primary action is `Добавить видео`. Folder selection and clearing are
secondary. The source list is compact and visibly selected. The rail starts
with two high-level controls: an accuracy preset (quick, balanced, high
precision) and frame-processing quality (fast, balanced, maximum). The main
rail exposes only the controls a user can tune without knowing matcher
internals: similarity threshold, repeat gap and scene threshold. Each control
has an inline editable value and a short tooltip. Hidden implementation
parameters are selected by the accuracy preset and are not duplicated in the
UI. These compact controls live in a closed-by-default “Advanced analysis
settings” disclosure. Model selection and all operational controls stay on the
main workspace; Settings remains focused on provider and system resources.

Controls are grouped by intent rather than a wall of duplicated labels:

1. Similarity: similarity threshold.
2. Spacing: repeat gap.
3. Scene detection: scene-change threshold.

### Empty analysis state

Explain the next action in one sentence: `Добавьте видео, чтобы начать`.
Keep the video well clean and readable, with the primary action button close
to the explanation. Do not use rings, radar imagery, placeholder letters,
particle aura, motion blur or a progress spinner as hero art.

### Selected pair

Show actual A/B frames at equal size. A is terracotta, B is sage. Each preview
has source name and local timecode. The viewport is the primary workspace: the
mouse wheel zooms, dragging pans the frame, double-click restores fit-to-window,
and the frame surface clips all content to its border. The former bottom
timeline panel is intentionally removed; result records still retain exact
start/end values for export and inspection.

### Results

Rows are 32px virtualized by default and use checkboxes that start unchecked.
One selected row controls the A/B comparison; checkbox state controls export.
Result controls include sort and Prev/Next. The list is intentionally
monolithic; movement category filters were removed because they fragmented the
small results rail. Export is inactive until at least one checkbox is selected.

### Settings and export

Settings and export are custom movable overlays, not native Qt dialogs. They
have a deliberate header, 14–16px corner radius, a dark overlay, deep shadow,
keyboard close, and no platform chrome. Settings are split into two focused
tabs: Analysis (provider, cache path and limit, processing threads) and
Appearance (language, bundled/custom font, panel transparency and reset).
Model selection and matcher presets do not appear in Settings; they stay in
the main workspace.
Scene/matcher controls remain in the main workspace. Export contains format,
numbering, cut mode, output folder and prefix.

## Interaction / motion

The workspace is intentionally static. State changes replace content directly
so analysis remains legible and deterministic. There are no perpetual floating
cards, pulsing status dots, rainbow glows or motion blur. The only allowed
motion is a short 150–180ms opacity/scale transition when a user opens or
closes a modal, because it confirms the change of focus without decorating the
analysis surface.

## Explicit rejections

- generic SaaS card soup;
- equal-radius grey rectangles everywhere;
- copied Claude login-page layout;
- blue/cyan accent or random neon;
- target/radar rings as the empty state;
- native-looking dialog chrome;
- clipping a control rail instead of scrolling it;
- UI text that claims a feature works when it does not.
