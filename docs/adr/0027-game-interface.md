# ADR-0027: The game's interface: a canvas, distance field fonts and a pass after antialiasing

- **Status:** Accepted
- **Date:** 2026-11-04

## Context

The sandbox drew its HUD with Dear ImGui, which only exists in development builds (ADR-0015): a build without dev tools had no health, ammunition or crosshair. ImGui also looks like a debugging tool, and styling it into a game interface fights its design. Phase 8 needs a HUD first, then menus and a loading screen (docs/DEMO.md). The legacy engine drew its interface with a tree of views and labels, and Arial bitmap fonts.

## Decision

- **Our own small interface layer, in the `renderer` module** under the `ui::` namespace, so no new module (ADR-0021). It is **immediate mode**: each frame, game code calls `FillRect`, `DrawImage` and `DrawText` on a `ui::Canvas`, and keeps its own state. There are no views, no tree and no layout engine.
- **Canvas units**: positions and sizes are those of a screen 1080 units high, scaled to the window's height; the width follows its aspect ratio. Y goes down from the top-left corner. The legacy HUD's pixel positions, laid out for 1920×1080, work as they are.
- **`Application` owns one canvas**, begun again before every frame's updates, and `Application::Canvas()` hands it to the game. `Renderer::Render` takes it as an optional argument.
- **Drawing**: the canvas fills vertex and index arrays and a list of commands, merging consecutive draws of the same texture and shading. `UiPass` copies them to per-frame buffers and draws them into the back buffer **after tonemapping and FXAA**, which would blur text, and before the debug overlay, with straight alpha blending. Colors are sRGB, as in image editors, and are converted to linear in the vertex shader for the sRGB back buffer. Its GPU time is the "Interface" timestamp.
- **Fonts are baked into signed distance fields when they load**, by `stb_truetype` (already a dependency through stb) in `resources::BakeFont`: printable ASCII, Latin-1 (French included) and a few typographic signs, at 48 pixels with a 6-pixel margin, packed by `stb_rect_pack` into one 8-bit atlas. Text stays sharp at any size; the shader turns the distance into coverage over about one screen pixel with `fwidth`. Kerning pairs from the font's `kern` table or GPOS are kept. Missing characters show as `?`. Text is UTF-8, on several lines with `\n`, and can have a drop shadow.
- **`scene::AssetLibrary::LoadFont`** caches fonts by asset path and releases their atlases with its other assets (ADR-0016). The sandbox keeps a library of its own for the interface, which outlives the levels' scenes.
- **The font is Rajdhani** (SIL Open Font License), in `assets/Fonts/` with its licence. The legacy Arial atlases are not used: Arial belongs to Microsoft and cannot be shared under our licence.
- **The sandbox's HUD** follows the legacy `CHudView`: health and armor with their icons at the bottom left, in red at 20 or less, yellow, green, and blue past 100; ammunition at the bottom right; the flashlight icon at the top right; the crosshair in the middle; the last message above it, fading out; a red flash on wounds and death. Its icons and crosshair are the author's legacy `hud.png` and `crosshair.tga`, converted to PNG. "LOADING..." covers the screen until the first level is built.

## Consequences

- Every build has the HUD; ImGui is back to developer information only.
- Baking costs 80 to 95 ms per font in Release at startup, on the main thread; Rajdhani gives 188 glyphs in a 512×512 atlas. The HUD draws in less than 0.01 ms on the GPU.
- Very small text, under about 12 pixels, loses detail, since one atlas serves every size.
- There is no clipping, no text shaping and no right-to-left text: none is needed for Latin-script menus and HUDs.
- Rajdhani has no kerning at all, so the kerning code is only tested on made-up pairs.
- A canvas only refers to textures and fonts: they must stay alive until the frame is rendered, which the asset libraries' lifetimes ensure.
- Menus, the loading screen and their input come in 8.2, on this canvas.

## Alternatives considered

- **ImGui for the game too**: it would need its own context next to the debug one, it stays in development builds by design, and a game look means fighting its style system.
- **RmlUi**: HTML and CSS for game interfaces, far more than a HUD and three menus need, and a large dependency.
- **Retained views, like the legacy engine**: more code, and state duplicated between the game and the views, for interfaces that change every frame anyway.
- **Bitmap fonts at fixed sizes**: blurry or blocky when scaled, and an atlas per size.
- **Multi-channel distance fields (msdfgen)**: sharper corners at very large sizes, but an offline tool or another library; single-channel fields are sharp enough at HUD and menu sizes.
- **FreeType**: better hinting at small sizes, but a large dependency for text we never draw small.
- **Fonts baked by a build tool**: no startup cost, but one more tool and generated files to keep in step with the fonts.
