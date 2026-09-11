# Parallel Finder — visual system

Status: **source of truth for stage 5 UI**. This document translates the product
brief into an original Qt/QML interface. It takes atmosphere from the supplied
Claude references, but never copies their page layout; workspace geometry and
interaction follow the Parallel-Alpha prototype.

## Product character

Parallel Finder is an editorial motion-analysis workstation, not a generic
dashboard. It should feel like a quiet, precise film tool: the video and its
motion traces are the visual event; chrome is supporting equipment.

One memorable visual per screen is enough. In the main empty state it is a
restrained **motion field** — a few luminous trajectory strokes, not a logo,
spinner, radar, or decorative target. In the analysis state it is the selected
A/B pair and its four linked timeline markers.

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
│ sources + queue                     │ six compact stats, not six cards  │ filters/sort    │
│ nine controls in a scroll rail       │ progress / A-B preview             │ virtual list    │
│ quality / backend / start             │ real timeline, 4 linked markers    │ selected export │
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
secondary. The source list is compact and visibly selected. The full set of
nine matcher controls must map to `MotionMatcherParams`, show an inline value,
and expose a short tooltip. Controls are grouped by intent rather than a wall
of duplicated labels:

1. Similarity: similarity threshold, candidate threshold.
2. Spacing: repeat gap, same-file gap, cross-file gap, duplicate window.
3. Ranking: noise factor, maximum unique results, time weight.

### Empty analysis state

Explain the next action in one sentence: `Добавьте видео, чтобы начать`.
The background motion field is quiet and noninteractive. It disappears once a
pair is selected. Do not use rings, radar imagery, placeholder letters, or a
progress spinner as the hero art.

### Selected pair

Show actual A/B frames at equal size. A is terracotta, B is sage. Each preview
has source name, local timecode and a clear visual association with its two
timeline markers. Timeline uses real duration and has zoom, pan and fullscreen
controls. It contains four markers: A start/end in terracotta, B start/end in
sage.

### Results

Rows are 32px virtualized by default and use checkboxes that start unchecked.
One selected row controls the A/B comparison; checkbox state controls export.
Result controls include sort, movement category filters and Prev/Next. Export
is inactive until at least one checkbox is selected.

### Settings and export

Settings and export are custom movable overlays, not native Qt dialogs. They
have a deliberate header, 14–16px corner radius, a dark overlay, deep shadow,
keyboard close, and no platform chrome. Settings contain backend mode, cache
path and limit, theme, language and model location. Export contains format,
numbering, cut mode, output folder and prefix.

## Interaction / motion

Motion explains state changes: selected result fades into A/B comparison;
timeline markers slide only when a new pair is selected; popup opens with a
small opacity/scale transition. No perpetual floating cards, pulsing status
dots, rainbow glows, or repeated entrance animations.

## Explicit rejections

- generic SaaS card soup;
- equal-radius grey rectangles everywhere;
- copied Claude login-page layout;
- blue/cyan accent or random neon;
- target/radar rings as the empty state;
- native-looking dialog chrome;
- clipping a control rail instead of scrolling it;
- UI text that claims a feature works when it does not.
