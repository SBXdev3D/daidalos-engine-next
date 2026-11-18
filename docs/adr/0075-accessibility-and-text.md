# ADR-0075: Accessibility, subtitles, captions and translated text

- **Status:** Accepted
- **Date:** 2026-11-18

## Context

Phase 15 (ADR-0041) lists accessibility (subtitles with speakers and sound captions, colorblind modes, toggles for motion blur, camera shake, film grain and chromatic aberration) and localization of text and subtitles. The sandbox's text was English literals in the code; nothing said what a sound was; the lens effects had settings but no place in the options; and the options screen was full at twelve rows.

## Decision

- **Text tables:** `assets/Text/<language>.json`, a flat object of keys and strings (`scene::TextTable`). English loads first and the chosen language over it, so a missing translation shows English; a missing key shows itself. `game.language` chooses; the options list each language by its own name (`language.name`). The game's menus, messages, HUD words, subtitles and captions go through it; strings with values are format strings (`"Zombies killed: {} of {}"`), and a translation whose places do not fit shows its key rather than throwing. The sandbox ships English and French.
- **Subtitles and captions:** `scene::Caption`, a text key with a speaker (a subtitle) or without (a sound's caption), how long it shows, and, for a sound, where it comes from and how far it is heard. `Scene::ShowCaption` keeps the four latest; the same line again starts over. Sequences have a `subtitle` cue and an optional `caption` on sound cues; the game captions zombie groans, screams and falls, doors and stingers. The HUD draws them at the bottom on a dark band, a sound's caption only within its reach and with the side it comes from (`< [Groaning]`), as `game.subtitles` (on), `game.captions` (off) and `game.text_size` say.
- **Color blindness:** `renderer.color_vision` (normal, protanopia, deuteranopia, tritanopia) corrects the whole output, interface included, in the output pass: the view is simulated with Machado, Oliveira and Fernandes' matrices at full severity, and the difference lost is moved into channels seen (Fidaner's daltonization).
- **Motion:** `view.camera_shake` scales sequence shakes and the flashlight's sway (`Scene::SetShakeScale`); motion blur, film grain, chromatic aberration and lens dirt get sliders.
- **Options in tabs:** Video, Sound, Controls, Access; the toggle's words come from the text table (`WidgetStyle::onText`).

Tested: tables parse, overlay and fall back, and their mistakes name the key (`TextTableTest`); every language has exactly English's keys; subtitle and caption cues parse; captions last their time, once each, four at a time (`CaptionsLastTheirTimeOnceEachAndAFewAtATime`); `scripts/check_text_keys.py` (test `assets.text_keys_exist`) finds every key the game and sequences name in English. Seen: the options in French, Access tab, deuteranopia on; the rain room's subtitle.

## Consequences

- A new language is one file; the fonts carry Latin-1, so languages beyond it need fonts baked with their ranges.
- The engine's developer tools (ImGui, the editor) stay in English.
- Captions come from the game's code and sequences: a sound played without one is not captioned. Footsteps and the player's own shots are left out on purpose.
- Rebinding and rumble are the next step (15.2).

## Alternatives considered

- **gettext-style catalogs, English text as the key:** fewer keys to invent, but an English edit orphans every translation.
- **Color filters that only simulate:** they show what a viewer sees; they do not help them see it.
