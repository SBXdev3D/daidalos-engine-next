# Patreon content

Drafts of my Patreon posts about building Daidalos Engine Next. Each post is copied to Patreon by hand, with its images uploaded from `images/`.

## Context
Note to claude : take inspiration of the descriptions and images on the provided links, but don't publish the link in the posts (my real name is here).

I'm SBX, a software engineer of 14+ years.
In 2008, I starting getting interested in game development and decided to learn 3D programming.
While learning, I decided to take inspiration on of my favorite game : Doom, as my first project.
- Full 3D, OpenGL (without shaders), SDL, BSP Tree etc
- I coded everything myself
http://stephane-baudoux.com/#!/project/daidalos

In 2010, I started learning advanced 3D programming in university : Rasterization, DirectX9, Shaders.
I decided to push my project further with a DirectX9 rewritten Daidalos : DaidalosDX
http://stephane-baudoux.com/#!/project/daidalosdx

In 2011, I decided I wanted to go further : why not make my own 3D engine ?
I wanted to learn everything : I/O, texture loading, sounds, animations, dynamic lighning, collisions, gui etc.
http://stephane-baudoux.com/#!/project/daidalosengine
I continued working on it for several years, improving the engine, until my time constraints couldn't allow me anymore : I was not a student anymore but a software engineer.

After a hiatus of several years, wanting to work on the project but couldn't because of time constraints, came AI.
I decided in 2026 to resume working on my project, but it was old : old c++, old graphical library.
With my acquired knowledge and with the help of Claude Code, I decided to redo everything bug more modern : here comes Daidalos Engine Next.

## Posts

| # | Post | Topic | Status |
|---|---|---|---|
| 001 | [Why I'm rebuilding my engine](posts/001-why-im-rebuilding-my-engine.md) | Introduction | Draft, 1 passage for me to write |
| 002 | [How the engine fits together](posts/002-how-the-engine-fits-together.md) | Architecture | Draft |
| 003 | [The road ahead](posts/003-the-road-ahead.md) | Roadmap | Draft |
| 004 | [From an empty repository to a first frame](posts/004-from-empty-repository-to-first-frame.md) | Progress, Phases 0 to 3 | Draft, 1 passage for me to write |
| 005 | [Goodbye, cubes](posts/005-goodbye-cubes.md) | Progress, Phase 4 | Draft, 1 passage for me to write |
| 006 | [The old factory wakes up](posts/006-the-old-factory-wakes-up.md) | Progress, Phase 5 | Draft, 2 passages for me to write, no screenshot until the demo's assets are cleared |
| 007 | [What is a door made of?](posts/007-what-is-a-door-made-of.md) | Inside the engine: ECS | Draft |
| 008 | [The handle that knows its object is gone](posts/008-the-handle-that-knows-its-object-is-gone.md) | Inside the engine: handles | Draft |
| 009 | [From entity to pixel](posts/009-from-entity-to-pixel.md) | Inside the engine: render pipeline | Draft |
| 010 | [How the player walks](posts/010-how-the-player-walks.md) | Inside the engine: collision | Draft, 1 passage for me to write, publish after PR #5 is merged |
| 011 | [Two clocks](posts/011-two-clocks.md) | Inside the engine: fixed step | Draft, publish after PR #5 is merged |
| 012 | [The door, then and now](posts/012-the-door-then-and-now.md) | Then and now: entities | Draft, 1 passage for me to write |
| 013 | [My BSP tree and a library](posts/013-my-bsp-tree-and-a-library.md) | Then and now: collision | Draft, 2 passages for me to write, publish after PR #5 is merged |
| 014 | [A frame, then and now](posts/014-a-frame-then-and-now.md) | Then and now: rendering | Draft, 1 passage for me to write |
| 015 | [When a file is missing](posts/015-when-a-file-is-missing.md) | Then and now: errors | Draft |
| 016 | [Who owns this texture?](posts/016-who-owns-this-texture.md) | Then and now: ownership | Draft |
| 017 | [The factory finds its voice](posts/017-the-factory-finds-its-voice.md) | Progress, Phase 6.2 | Draft, 1 passage for me to write |
| 018 | [The zombies wake up](posts/018-the-zombies-wake-up.md) | Progress, Phase 6.3 | Draft, 1 passage for me to write |
| 019 | [Fighting back](posts/019-fighting-back.md) | Progress, Phase 6.4 | Draft, 1 passage for me to write |
| 020 | [Spring cleaning](posts/020-spring-cleaning.md) | Progress, audit before Phase 7 | Draft, 1 passage for me to write |
| 021 | [The lights come on](posts/021-the-lights-come-on.md) | Progress, Phase 7.1 | Draft, 1 passage for me to write |
| 022 | [Light stops at the walls](posts/022-shadows.md) | Progress, Phase 7.2 | Draft, 1 passage for me to write |
| 023 | [Corners and creases](posts/023-corners-and-creases.md) | Progress, Phase 7.3 | Draft, 1 passage for me to write |
| 024 | [Smoke and sparks](posts/024-smoke-and-sparks.md) | Progress, Phase 7.4 | Draft, 1 passage for me to write |
| 025 | [Settings, and smoother edges](posts/025-settings.md) | Progress, engine settings and antialiasing | Draft, 1 passage for me to write |

## Layout

```text
posts/      one Markdown file per post, numbered in publishing order
images/     PNG files to upload: exported diagrams and engine screenshots
diagrams/   SVG sources of the diagrams, generated by tools/diagrams.py
tools/      diagrams.py regenerates diagrams/ and images/ (needs Python and Edge or Chrome)
```

## How posts are produced

Every completed phase gets at least one progress post. A notable design decision can get its own architecture post. The roadmap post is updated when the plan changes. Between phases, two short series fill the gaps: "Inside the engine" explains one part, and "Then and now" compares one part with the old engine, using facts from its source code in `../daidalos-engine`.

1. Facts come from the repository: commits, ADRs, test runs, screenshots of the real engine.
2. Diagrams go in `tools/diagrams.py`, so that they stay editable and share one look.
3. A new post is added to the table above.

## Voice and rules

- First person: I am talking about the engine I am building.
- Written for curious people, not only for graphics programmers. Each technical term is explained where it first appears.
- Honest about the state of things: what was verified, what was not, what went wrong.
- Claude Code is named as my development assistant wherever the story touches on how the work gets done.
- No invented anecdotes. Anything only I can tell is marked `[TO WRITE: ...]` and written by me before publishing.
- Numbers come from a real run. Screenshots come from the real engine, with no paint-over.
- Screenshots of third-party assets are only published if their licence allows it on a paid platform, with attribution.
