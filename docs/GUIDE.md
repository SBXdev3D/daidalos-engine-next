# Making a game with Daidalos Engine Next

This guide walks through how a game is put together on the engine, from the smallest program that shows a level to the parts the factory demo uses. It points to the code and to the decision records (ADRs) that say why each part is the way it is. [ARCHITECTURE.md](ARCHITECTURE.md) describes the engine's inside; this describes how to use it.

The two programs to read along are:

- `samples/level-viewer`: about 140 lines. It opens a level and flies a camera through it.
- `apps/sandbox`: the factory demo. It is a whole game: a player, weapons, zombies, menus, saves.

## 1. A program

A game is an executable that runs an `app::Application` with a handler of its own:

```cpp
class Viewer final : public app::IApplicationHandler {
    core::Result<void> OnStart(app::Application& app) override;               // once, after the window and renderer
    void OnUpdate(app::Application& app, const core::FrameTime& time) override; // every frame
    void OnFixedUpdate(app::Application& app, core::Seconds step) override;     // 60 times a second: physics, gameplay
    void OnStop(app::Application& app) override;
};

int main(int argc, char** argv) {
    app::ApplicationConfig config;
    config.log.filePath = "my-game.log";          // the last run's log stays as my-game.previous.log
    config.crashDumpDirectory = "crashdumps";      // dumps, and readable reports at the next start (ADR-0080)
    config.configFiles = {"daidalos.cfg", "my-game.cfg"};
    config.arguments.assign(argv + 1, argv + argc); // +name value on the command line overrides settings
    config.rendering = true;
    app::Application application(config);
    MyGame game;
    return application.Run(game) ? 0 : 1;
}
```

The application owns the window, the input, the renderer, the audio engine, a pool of worker threads and the settings. In development builds it also owns the debug overlay: the grave key opens it, with GPU times per pass and a console.

In CMake, `dai_add_app(NAME my-game SOURCES ... DEPS daidalos::app daidalos::scene CONFIG my-game.cfg)` builds it, copies the settings files beside it, and installs it into the release archive.

**Settings** are named values with a default and a description, registered where they are used:

```cpp
const std::string level = app.CVars().Register<std::string>("viewer.level", "Factory/Factory.level.json", "Level file").Get();
```

They come from `daidalos.cfg` (the engine's: renderer, audio, window), from the game's own files, from the player's settings file, and last from the command line. `Application::ChangeSetting` and `SaveSettings` are what an options menu uses. The quality presets (`renderer.preset low|medium|high|ultra`, ADR-0052) set many renderer settings at once.

## 2. A scene and a level

A `scene::Scene` holds an ECS world of entities and components, the assets they use (`AssetLibrary`), the physics world and the sound world. The application draws whichever scene `SetScene` gave it, from its active camera.

A level is a `.level.json` file (ADR-0054). It holds:

- the environment: sky, fog, wind, exposure;
- entities with ids, parents and transforms;
- typed components: a glTF model, a light, fog, decals, wet areas, particles, a trigger, a loose physics object, a prefab (ADR-0078);
- `game` tags, which only the game reads.

```cpp
DAI_TRY_ASSIGN(scene::Level level, scene::LoadLevelFile(assets / "Factory/Factory.level.json"));
m_level = std::make_unique<scene::LevelInstance>(*scene, "Factory/Factory.level.json");
m_level->Build(level);
```

`LevelInstance` makes the entities and keeps them in step with the file's data: the editor uses its `Sync` for every edit. Paths in a level are relative to the level's file, and every asset path is relative to `assets/`. A loading screen loads `scene::LevelModels(level, path)` on the workers first (`AssetLibrary::LoadModelAsync`), as the sandbox does.

## 3. Building levels in the editor

`apps/editor` (development builds) edits level files with the game's own renderer as its viewport (ADR-0055). Its shortcuts are listed in the README and under F1. For a game, the parts that matter are:

- **Models** come from Blender as glTF. Drop them on the editor's window to import them, and drag them from the asset browser into the view.
- **Lights** are in physical units: candela and lumens, kelvin, EV for exposure (ADR-0043). Create > Light presets has candles, buzzing tubes, failing lamps and more. Profiles (IES), cookies and power circuits are in ADR-0057.
- **Dressing:** decals, wet areas with puddles, fog volumes, particles and weather. The paint tool (B) stamps decals and wet patches along a stroke (ADR-0078).
- **Prefabs:** Edit > Make prefab saves a group of entities as a `.prefab.json` that levels place, such as the factory's alarm beams.
- **Materials:** click a surface, and the Materials tab edits its material live. Saving writes it back into the model's glTF file (ADR-0079).
- **Scares:** a trigger box plays a `.sequence.json` when the player walks in. Its "Edit its sequence" button opens the sequence as a timeline of cues: sound, light, power, particles, camera moves, shakes, mix, subtitles, events (ADR-0071, ADR-0079). Play previews it in the view.
- **Blocking out:** Create > Block out places boxes, walls, floors, stairs, ramps and pillars the game walks on, to try a room before modelling it (ADR-0115).
- **Prefab copies** can each move, relight, swap or leave out a part of their prefab in the inspector; Apply to the prefab writes it for every copy (ADR-0113).
- **Sound:** drop a `.wav` or `.ogg` in the view for an ambient sound; Show > Hear the level plays it (ADR-0116).
- **Effects:** right-click an effect in Assets > Edit, and change it while every emitter shows it (ADR-0114).
- **Seeing and placing:** Alt+1 to Alt+4 light the view (lit, unlit, lighting only, a lamp at the camera); Shift+1 to Shift+5 give the top, front and side views cut through the ceiling, and four panes; View > Snap to surfaces, End drops to the floor (ADR-0105, ADR-0108).
- **Checking:** Ctrl+Shift+C lists missing files, broken links and misplaced entities; the Statistics panel shows the frame and the level against their budgets (ADR-0111).
- **Alt+P** plays the level in the editor's window, Alt+S simulates it with the camera as the player; **F5** plays it in the sandbox as it is, saved or not (ADR-0110).
- **Ctrl+P** finds any command, entity, asset or setting; F1 lists every key and changes any (ADR-0112).

## 4. Gameplay

The engine runs what every level needs: physics, particles, lights and their animation, triggers and sequences, sound, AI perception. The game adds the rest in its own systems, which read and write components.

- **Game tags.** A level entity's `game` object (`{"kind": "door", "id": "3"}`) becomes `scene::Tags` on the entity. A model's node named `door;id=3` in Blender gives the same. The sandbox's `Factory.cpp` reads them once the level is made, and adds its own components: `Door`, `Pickup`, `Alarm`, zombies.
- **Events.** A sequence's `event` cue ends up in `Scene::TakeEvents()`, which the game reads each frame: `checkpoint`, `stinger`, anything the game defines.
- **Physics.** Jolt (ADR-0018). `scene.Physics()` casts rays and moves the player's character. Loose objects in a level are dynamic bodies (ADR-0073): a box, sphere, cylinder, capsule or hull of their model, of a surface that sets how they rub, bounce and sound when they hit, roll and scrape (ADR-0125).
- **Navigation and AI.** A navmesh built from the level (ADR-0069). Perception hears noises along it and sees by the light that falls on the player, and a utility chooser picks behaviors (ADR-0070).
- **Sound.** `scene.Sounds()` plays clips at entities, through Steam Audio's occlusion, reflections and pathing (ADR-0065, ADR-0066), on buses with snapshots (ADR-0067). The score is layered by tension (ADR-0068).
- **Input** goes through actions, so that players can rebind keys and pads (ADR-0076). The sandbox's `Controls.cpp` declares its actions and their default bindings.
- **Saves** are `key = value` text (ADR-0072). The engine saves what it owns: level entities by id, and others by a `SaveKey` given when the level is made. The game adds its own keys.
- **Text** goes through text tables in `assets/Text/<language>.json` (ADR-0075), for menus, subtitles and captions. `scripts/check_text_keys.py` checks that every key the game uses exists.
- **The interface** is drawn with the engine's `ui::` widgets on a canvas, in a font baked into a distance field (ADR-0027). The sandbox's `Hud.cpp` and `Menus.cpp` are examples.

## 5. Content pipeline

- **Models:** glTF 2.0, `.gltf` or `.glb`. Materials use `KHR_materials_*` (clear coat, sheen, transmission, volume, IOR, emissive strength). The engine's own values go in the material's extras: `surface`, `porosity`, `height`, `light`.
- **Textures:** `texture-tool` converts images to KTX2 (UASTC, transcoded to BC7 or BC5 at load), packs relief into the metallic-roughness alpha, and builds flipbooks (ADR-0046, ADR-0064).
- **Effects:** `.effect.json` and `.ribbon.json` files, read again when they change while the game runs (ADR-0053).
- **Not done yet**, and why:
  - Mesh optimization (meshoptimizer): the factory's 23,000 triangles cost under a tenth of a millisecond to rasterize, so there is nothing to gain yet.
  - BC6H for HDR textures: there are none.
  - Streaming: the factory fits in memory and loads in one go.

  Each will come with the content that needs it (ADR-0081).

## 6. Shipping

- `cmake --preset windows-msvc-shipping`, `cmake --build --preset windows-msvc-shipping`, then `cpack --preset windows-msvc-shipping` builds the archive (ADR-0038, ADR-0081). It holds the game, `assets/`, the settings files, the licences and the third-party notices, and leaves out the debug overlay, the editor, the tools and the tests.
- A `vX.Y.Z` tag runs the release workflow. It builds the Windows and Linux archives and drafts a GitHub release. It also keeps the Windows PDBs apart, for crash reports of that build (`crash-report --symbols`, ADR-0080).
- The archive is checked by unpacking it in a fresh folder and starting the game from there (`scripts/check_package.py`).
- When a player's game crashes, a report appears beside the dump in `crashdumps/` at the next start, and the menu says so. The player decides whether to send it: the engine sends nothing.
