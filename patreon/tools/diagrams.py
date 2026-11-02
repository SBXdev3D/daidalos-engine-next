#!/usr/bin/env python3
"""Generates the Patreon diagrams as SVG, then exports them to PNG with a headless browser.

Patreon does not accept SVG, so the SVG files are the sources and the PNG files are what gets uploaded.

    python patreon/tools/diagrams.py
"""

import shutil
import subprocess
import tempfile
from html import escape
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SVG_DIR = ROOT / "diagrams"
PNG_DIR = ROOT / "images"

FONT = "Segoe UI, Helvetica Neue, Arial, sans-serif"
BACKGROUND = "#14171f"
PANEL = "#1f2430"
PANEL_EDGE = "#3a4254"
TEXT = "#eef1f6"
MUTED = "#a3adbf"
DONE = "#3fb68b"
NEXT = "#f2b84b"
PLANNED = "#6c7a94"
ACCENT = "#5aa2f0"
ALERT = "#ef7a6a"


class Svg:
    def __init__(self, width, height):
        self.width = width
        self.height = height
        self.parts = [
            f'<rect width="{width}" height="{height}" fill="{BACKGROUND}"/>',
            f'<defs><marker id="arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" '
            f'orient="auto-start-reverse"><path d="M0 0L10 5L0 10z" fill="{MUTED}"/></marker></defs>',
        ]

    def text(self, x, y, value, size=18, fill=TEXT, weight=400, anchor="start"):
        self.parts.append(
            f'<text x="{x}" y="{y}" font-family="{FONT}" font-size="{size}" font-weight="{weight}" '
            f'fill="{fill}" text-anchor="{anchor}">{escape(value)}</text>'
        )

    def box(self, x, y, width, height, fill=PANEL, stroke=PANEL_EDGE, dashed=False, radius=10, stroke_width=1.5):
        dash = ' stroke-dasharray="7 5"' if dashed else ""
        self.parts.append(
            f'<rect x="{x}" y="{y}" width="{width}" height="{height}" rx="{radius}" fill="{fill}" '
            f'stroke="{stroke}" stroke-width="{stroke_width}"{dash}/>'
        )

    def line(self, x1, y1, x2, y2, stroke=MUTED, width=2, arrow=True, dashed=False):
        marker = ' marker-end="url(#arrow)"' if arrow else ""
        dash = ' stroke-dasharray="6 5"' if dashed else ""
        self.parts.append(
            f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" stroke="{stroke}" stroke-width="{width}"{marker}{dash}/>'
        )

    def write(self, name):
        SVG_DIR.mkdir(parents=True, exist_ok=True)
        content = (
            f'<svg xmlns="http://www.w3.org/2000/svg" width="{self.width}" height="{self.height}" '
            f'viewBox="0 0 {self.width} {self.height}">' + "".join(self.parts) + "</svg>\n"
        )
        (SVG_DIR / f"{name}.svg").write_text(content, encoding="utf-8")


def module(svg, x, y, width, name, detail, planned=False):
    height = 72
    if planned:
        svg.box(x, y, width, height, fill=BACKGROUND, stroke=PLANNED, dashed=True)
        svg.text(x + width / 2, y + 31, name, 19, MUTED, 600, "middle")
    else:
        svg.box(x, y, width, height, stroke=DONE)
        svg.text(x + width / 2, y + 31, name, 19, TEXT, 600, "middle")
    svg.text(x + width / 2, y + 54, detail, 13.5, MUTED, 400, "middle")


def architecture():
    svg = Svg(1400, 840)
    svg.text(50, 58, "Daidalos Engine Next: the module stack", 32, TEXT, 700)
    svg.text(50, 90, "A module may only use the layers beneath it. CMake links only what a module declares.", 18, MUTED)

    # (layer label, [(name, detail, planned)])
    layers = [
        ("Applications", [("sandbox", "demo: the old factory level", False), ("editor", "Phase 11", True)]),
        (
            "Game layer",
            [
                ("app", "startup, main loop, shutdown", False),
                ("gameplay", "Phases 5, 9", True),
                ("ai", "Phase 9", True),
            ],
        ),
        (
            "Systems",
            [
                ("renderer", "scene drawing, shader reload", False),
                ("debugui", "Dear ImGui overlay", False),
                ("input", "keyboard, mouse, gamepad", False),
                ("scene", "world, assets, systems", False),
                ("physics", "Phase 6", True),
                ("audio", "Phase 6", True),
                ("gui", "Phase 8", True),
            ],
        ),
        (
            "Foundations",
            [
                ("rhi", "Vulkan 1.3, VMA", False),
                ("ecs", "sparse-set components", False),
                ("platform", "SDL3 window and events", False),
                ("resources", "glTF, images, caches", False),
            ],
        ),
        ("Basics", [("math", "GLM + conventions", False), ("logging", "spdlog facade", False)]),
        ("Base", [("core", "Result, handles, cvars, profiling", False)]),
    ]

    top = 122
    row = 108
    left = 200
    right = 1350
    for index, (label, modules) in enumerate(layers):
        y = top + index * row
        svg.box(30, y - 8, right - 10, row - 8, fill="#181c26", stroke="#262c3a", radius=12)
        svg.text(50, y + 44, label, 17, MUTED, 600)
        gap = 12
        available = right - left - 10
        # Implemented modules get more room than planned ones.
        weights = [3 if planned else 5 for _, _, planned in modules]
        unit = (available - gap * (len(modules) - 1)) / sum(weights)
        unit = min(unit, 62)
        x = left
        for (name, detail, planned), weight in zip(modules, weights):
            width = weight * unit
            module(svg, x, y + 6, width, name, detail, planned)
            x += width + gap

    legend_y = 806
    svg.box(50, legend_y - 16, 26, 20, stroke=DONE, radius=5)
    svg.text(86, legend_y, "Implemented (Phases 0 to 5)", 16, MUTED)
    svg.box(350, legend_y - 16, 26, 20, fill=BACKGROUND, stroke=PLANNED, dashed=True, radius=5)
    svg.text(386, legend_y, "Planned, not written yet", 16, MUTED)
    svg.write("architecture")


def frame():
    svg = Svg(1400, 640)
    svg.text(50, 58, "One frame, from the player's hand to the screen", 32, TEXT, 700)
    svg.text(50, 90, "Everything runs on the main thread. The GPU works in parallel, with up to two frames in flight.", 18, MUTED)

    steps = [
        ("Events", "SDL3 events become", "engine events"),
        ("Input", "held, pressed and", "released state"),
        ("Update", "game logic edits", "ECS components"),
        ("Propagate", "local transforms", "become world ones"),
        ("Extract", "ECS is copied into", "a SceneView"),
        ("Render", "SceneView becomes", "Vulkan commands"),
    ]
    width = 196
    gap = 30
    x = 50
    y = 170
    for index, (name, line1, line2) in enumerate(steps):
        stroke = ACCENT if index == 4 else PANEL_EDGE
        svg.box(x, y, width, 130, stroke=stroke, stroke_width=2 if index == 4 else 1.5)
        svg.text(x + width / 2, y + 44, name, 22, TEXT, 700, "middle")
        svg.text(x + width / 2, y + 78, line1, 15.5, MUTED, 400, "middle")
        svg.text(x + width / 2, y + 100, line2, 15.5, MUTED, 400, "middle")
        if index < len(steps) - 1:
            svg.line(x + width + 2, y + 65, x + width + gap - 2, y + 65)
        x += width + gap

    extract_x = 50 + 4 * (width + gap)
    svg.line(extract_x + width / 2, 310, extract_x + width / 2, 366, stroke=ACCENT, dashed=True, arrow=False)
    svg.box(350, 366, 700, 104, fill="#17212f", stroke=ACCENT)
    svg.text(700, 404, "The boundary that matters", 20, TEXT, 700, "middle")
    svg.text(700, 436, "The renderer never reads the ECS. It only sees a copy: a camera and a list of draws.", 16, MUTED, 400, "middle")
    svg.text(700, 458, "Gameplay can never reach into Vulkan, and Vulkan can never reach into gameplay.", 16, MUTED, 400, "middle")

    svg.box(50, 510, 1300, 86, stroke=PANEL_EDGE)
    svg.text(80, 545, "GPU", 20, TEXT, 700)
    svg.text(80, 573, "up to 2 frames in flight", 15, MUTED)
    for index, (label, color) in enumerate([("frame N-1: still running", PLANNED), ("frame N: just submitted", DONE)]):
        box_x = 480 + index * 420
        svg.box(box_x, 527, 390, 52, fill="#1a2030", stroke=color)
        svg.text(box_x + 195, 559, label, 16.5, TEXT, 500, "middle")
    svg.write("frame")


def roadmap():
    phases = [
        ("0", "Build foundation", "CMake, vcpkg, tests, CI", "done"),
        ("1", "Platform and lifecycle", "window, cvars, crash dumps", "done"),
        ("2", "Math, ECS, input", "sparse sets, hierarchy", "done"),
        ("3", "First renderer", "Vulkan 1.3, ImGui, hot reload", "done"),
        ("4", "Resources and materials", "glTF, textures, caching", "done"),
        ("5", "Interactive scene", "scenes, the old factory", "done"),
        ("6", "Physics, audio, animation", "Jolt, miniaudio, skinning", "next"),
        ("7", "Modern rendering", "PBR, clustered lights, shadows", "planned"),
        ("8", "GUI and tools", "menus, HUD, debug tools", "planned"),
        ("9", "AI, cinematics, indoors", "visibility, cameras", "planned"),
        ("10", "Linux validation", "full Linux pass", "planned"),
        ("11", "Editor, production", "editor, packaging", "planned"),
    ]
    svg = Svg(1400, 730)
    svg.text(50, 58, "The road ahead: twelve phases", 32, TEXT, 700)
    svg.text(50, 90, "A sequence, not a calendar. Each phase ends with something I can run.", 18, MUTED)
    columns = 4
    width = 310
    height = 150
    gap_x = 20
    gap_y = 40
    for index, (number, name, detail, state) in enumerate(phases):
        row = index // columns
        column = index % columns
        x = 50 + column * (width + gap_x)
        y = 130 + row * (height + gap_y)
        color = {"done": DONE, "next": NEXT, "planned": PLANNED}[state]
        svg.box(x, y, width, height, fill=PANEL if state != "planned" else BACKGROUND, stroke=color, dashed=state == "planned", stroke_width=2)
        svg.text(x + 22, y + 52, number, 34, color, 700)
        svg.text(x + 80, y + 48, name, 19, TEXT if state != "planned" else MUTED, 600)
        svg.text(x + 22, y + 92, detail, 16.5, MUTED)
        label = {"done": "DONE", "next": "NEXT", "planned": "PLANNED"}[state]
        svg.text(x + 22, y + 128, label, 13.5, color, 700)
        if column < columns - 1 and index < len(phases) - 1:
            svg.line(x + width + 2, y + height / 2, x + width + gap_x - 2, y + height / 2)
    svg.write("roadmap")


def deferred_destruction():
    svg = Svg(1400, 560)
    svg.text(50, 58, "Why a texture can be deleted while the GPU is still using it", 32, TEXT, 700)
    svg.text(50, 90, "One timeline semaphore: a counter the GPU raises each time it finishes a submission.", 18, MUTED)

    axis_y = 330
    svg.line(90, axis_y, 1330, axis_y, width=3)
    svg.text(90, axis_y + 74, "GPU timeline value", 15, MUTED)
    positions = [(230, "41"), (520, "42"), (810, "43"), (1100, "44")]
    for x, value in positions:
        svg.line(x, axis_y - 12, x, axis_y + 12, arrow=False, width=2.5)
        svg.text(x, axis_y + 42, value, 22, TEXT, 700, "middle")

    # The call on the CPU.
    svg.box(330, 140, 380, 100, stroke=ALERT, stroke_width=2)
    svg.text(350, 176, "CPU, recording frame 43", 16, MUTED)
    svg.text(350, 208, "device.DestroyTexture(handle)", 20, TEXT, 600)
    svg.line(520, 240, 520, axis_y - 16, stroke=ALERT)
    svg.text(535, 288, "the handle is invalid from now on", 15.5, ALERT)

    # The actual release.
    svg.box(800, 140, 500, 100, stroke=DONE, stroke_width=2)
    svg.text(820, 176, "Later, when the semaphore reads 43", 16, MUTED)
    svg.text(820, 208, "the VkImage and its memory are freed", 20, TEXT, 600)
    svg.line(810, 240, 810, axis_y - 16, stroke=DONE)

    svg.box(90, 440, 1240, 84, stroke=PANEL_EDGE)
    svg.text(116, 474, "Gameplay code destroys what it likes, whenever it likes.", 18, TEXT, 500)
    svg.text(116, 502, "Frames in flight, uploads and destruction all share this one ordering, so there is no fence per frame to manage.", 16, MUTED)
    svg.write("deferred-destruction")


def asset_pipeline():
    svg = Svg(1400, 640)
    svg.text(50, 58, "From a file on disk to a pixel on screen", 32, TEXT, 700)
    svg.text(50, 90, "The left half knows nothing about Vulkan, so it can be tested without a graphics card.", 18, MUTED)

    svg.box(30, 120, 610, 400, fill="#181c26", stroke="#262c3a", radius=12)
    svg.text(55, 156, "resources: plain CPU data", 19, DONE, 700)
    svg.box(660, 120, 710, 400, fill="#181c26", stroke="#262c3a", radius=12)
    svg.text(685, 156, "renderer: GPU objects", 19, ACCENT, 700)

    def step(x, y, width, title, line, stroke=PANEL_EDGE):
        svg.box(x, y, width, 92, stroke=stroke)
        svg.text(x + width / 2, y + 38, title, 20, TEXT, 700, "middle")
        svg.text(x + width / 2, y + 66, line, 15, MUTED, 400, "middle")

    step(55, 190, 250, "Lantern.gltf", "geometry, materials, nodes")
    step(55, 310, 250, "wall_baseColor.png", "or KTX2, transcoded to BC7")
    step(350, 190, 260, "ModelData", "meshes, material values")
    step(350, 310, 260, "ImageData", "pixels with a mip chain")
    svg.line(307, 236, 348, 236)
    svg.line(307, 356, 348, 356)

    step(690, 190, 300, "AssetLibrary", "loads each asset once, owns it", ACCENT)
    step(690, 310, 300, "Instantiate", "one entity per node of the model")
    step(1040, 190, 300, "Vulkan buffers, textures", "vertices, indices, bindless slots")
    step(1040, 310, 300, "ECS entities", "transforms and MeshRenderers")
    svg.line(612, 236, 688, 236)
    svg.line(612, 356, 660, 356)
    svg.line(660, 356, 660, 262, arrow=False)
    svg.line(660, 262, 688, 262)
    svg.line(992, 236, 1038, 236)
    svg.line(840, 282, 840, 308)
    svg.line(992, 356, 1038, 356)

    svg.box(55, 438, 1285, 62, stroke=PANEL_EDGE)
    svg.text(80, 476, "Cached by identity: 'models\\wall.png' and './models/wall.png' are one asset, and a texture shared by two models loads once.", 17, TEXT, 500)

    svg.box(30, 540, 1340, 66, stroke=ALERT)
    svg.text(55, 580, "Missing or broken file: the load fails with an error that names it. No magenta placeholder quietly hides the problem.", 17, TEXT, 500)
    svg.write("asset-pipeline")


def cells(svg, x, y, values, width=64, height=52, stroke=PANEL_EDGE, highlight=None, labels=None):
    """A row of array cells. Empty values are drawn dimmed; labels go beneath the cells."""
    for index, value in enumerate(values):
        color = ACCENT if highlight is not None and index in highlight else stroke
        fill = BACKGROUND if value == "" else PANEL
        svg.box(x + index * width, y, width, height, fill=fill, stroke=color, radius=4,
                stroke_width=2 if highlight is not None and index in highlight else 1.5)
        svg.text(x + index * width + width / 2, y + height / 2 + 7, value, 19, TEXT, 600, "middle")
        if labels:
            svg.text(x + index * width + width / 2, y + height + 20, labels[index], 13.5, MUTED, 400, "middle")


def ecs_sparse_set():
    svg = Svg(1400, 760)
    svg.text(50, 58, "One component type, one sparse set", 32, TEXT, 700)
    svg.text(50, 90, "Example with the Door component: entities 1, 5 and 3 have one. The others do not.", 18, MUTED)

    svg.text(50, 160, "Sparse array", 20, TEXT, 700)
    svg.text(50, 186, "Indexed by entity.", 15.5, MUTED)
    svg.text(50, 208, "Holds a position.", 15.5, MUTED)
    cells(svg, 300, 150, ["", "0", "", "2", "", "1", "", ""], labels=[str(i) for i in range(8)], highlight={1, 3, 5})
    svg.text(840, 182, "Question: does entity 5 have a door?", 16, MUTED)
    svg.text(840, 206, "Answer in one array read.", 16, MUTED)

    svg.text(50, 294, "Dense arrays", 20, TEXT, 700)
    svg.text(50, 320, "Packed side by side,", 15.5, MUTED)
    svg.text(50, 342, "with no holes.", 15.5, MUTED)
    cells(svg, 300, 276, ["1", "5", "3"], labels=["0", "1", "2"])
    cells(svg, 300, 356, ["A", "B", "C"], labels=["", "", ""])
    svg.text(516, 308, "entities", 16, MUTED)
    svg.text(516, 388, "Door components", 16, MUTED)
    svg.text(840, 306, "A system walks these from left to right,", 16, MUTED)
    svg.text(840, 330, "so the CPU reads memory in order,", 16, MUTED)
    svg.text(840, 354, "which is what it does fastest.", 16, MUTED)

    svg.box(30, 450, 1340, 270, fill="#181c26", stroke="#262c3a", radius=12)
    svg.text(55, 490, "Removing the door of entity 1: swap and pop", 20, ALERT, 700)
    svg.text(55, 520, "The hole is filled by the last element, so the array stays packed.", 16, MUTED)

    cells(svg, 80, 560, ["1", "5", "3"], highlight={0, 2})
    cells(svg, 80, 620, ["A", "B", "C"], highlight={0, 2})
    svg.text(80, 696, "before", 15, MUTED)
    svg.line(310, 616, 430, 616)
    svg.text(370, 600, "C moves", 15, MUTED, 400, "middle")
    cells(svg, 450, 560, ["3", "5"])
    cells(svg, 450, 620, ["C", "B"])
    svg.text(450, 696, "after", 15, MUTED)
    svg.text(680, 590, "Only two entries of the sparse array change:", 17, TEXT, 500)
    svg.text(680, 624, "entity 3 now points to position 0", 16, MUTED)
    svg.text(680, 650, "entity 1 is marked as absent", 16, MUTED)
    svg.text(680, 684, "No loop over the other components, however many there are.", 16, MUTED)
    svg.write("ecs-sparse-set")


def stale_handles():
    svg = Svg(1400, 620)
    svg.text(50, 58, "Why an old handle can never reach the wrong object", 32, TEXT, 700)
    svg.text(50, 90, "A handle is two numbers: a slot, and the generation of that slot.", 18, MUTED)

    states = [
        ("1. Created", "slot 7, generation 3", "Door", "A system keeps the handle (7, 3)", DONE),
        ("2. Destroyed", "slot 7, generation 4", "empty", "The generation goes up by one", ALERT),
        ("3. Slot reused", "slot 7, generation 4", "Zombie", "A new handle: (7, 4)", ACCENT),
    ]
    for index, (title, slot, content, note, color) in enumerate(states):
        x = 50 + index * 440
        svg.box(x, 130, 420, 210, stroke=color, stroke_width=2)
        svg.text(x + 24, 172, title, 21, TEXT, 700)
        svg.text(x + 24, 202, slot, 16, MUTED)
        svg.box(x + 24, 222, 372, 56, fill="#1a2030", stroke=PANEL_EDGE, radius=6)
        svg.text(x + 210, 258, content, 21, TEXT if content != "empty" else MUTED, 600, "middle")
        svg.text(x + 24, 312, note, 16, MUTED)
        if index < 2:
            svg.line(x + 422, 235, x + 438, 235)

    svg.box(50, 376, 650, 200, stroke=ALERT, stroke_width=2)
    svg.text(76, 416, "With a plain pointer", 20, ALERT, 700)
    svg.text(76, 452, "The old system still holds the address of slot 7.", 17, TEXT)
    svg.text(76, 482, "It opens a door that is now a zombie, or crashes.", 17, TEXT)
    svg.text(76, 518, "The bug shows up far from where it was made,", 16, MUTED)
    svg.text(76, 544, "and only on some runs.", 16, MUTED)

    svg.box(720, 376, 630, 200, stroke=DONE, stroke_width=2)
    svg.text(746, 416, "With a generational handle", 20, DONE, 700)
    svg.text(746, 452, "The old system asks for (7, 3). The slot says 4.", 17, TEXT)
    svg.text(746, 482, "3 is not 4, so the answer is: that object is gone.", 17, TEXT)
    svg.text(746, 518, "Entities, meshes, materials, textures and buffers", 16, MUTED)
    svg.text(746, 544, "all use this same idea.", 16, MUTED)
    svg.write("stale-handles")


def render_pipeline():
    svg = Svg(1400, 700)
    svg.text(50, 58, "The render pipeline today", 32, TEXT, 700)
    svg.text(50, 90, "Deliberately small: one pass for the world, one for the debug overlay. Phase 7 fills in the dashed part.", 18, MUTED)

    steps = [
        ("ECS", "entities with", "WorldTransform and", "MeshRenderer", PANEL_EDGE),
        ("Extract", "copied into a", "SceneView: camera,", "fog, draw list", ACCENT),
        ("Mesh pass", "one draw per item,", "depth test, reverse-Z,", "materials", DONE),
        ("Overlay", "Dear ImGui, in", "development", "builds", DONE),
        ("Present", "the finished image", "goes to the", "screen", PANEL_EDGE),
    ]
    width = 240
    gap = 26
    for index, (name, line1, line2, line3, stroke) in enumerate(steps):
        x = 50 + index * (width + gap)
        svg.box(x, 130, width, 158, stroke=stroke, stroke_width=2)
        svg.text(x + width / 2, 172, name, 22, TEXT, 700, "middle")
        svg.text(x + width / 2, 208, line1, 15.5, MUTED, 400, "middle")
        svg.text(x + width / 2, 232, line2, 15.5, MUTED, 400, "middle")
        svg.text(x + width / 2, 256, line3, 15.5, MUTED, 400, "middle")
        if index < len(steps) - 1:
            svg.line(x + width + 2, 209, x + width + gap - 2, 209)

    svg.box(30, 322, 1340, 150, fill="#181c26", stroke="#262c3a", radius=12)
    svg.text(55, 358, "Inside the shader, for each pixel of each mesh", 19, TEXT, 700)
    shading = [
        ("Base color", "texture × factor"),
        ("Cut-out", "discard if see-through"),
        ("Fake light", "one fixed direction"),
        ("Fog", "fades with distance"),
    ]
    for index, (name, detail) in enumerate(shading):
        x = 55 + index * 325
        svg.box(x, 376, 300, 76, stroke=PANEL_EDGE)
        svg.text(x + 150, 408, name, 18, TEXT, 600, "middle")
        svg.text(x + 150, 434, detail, 15, MUTED, 400, "middle")
        if index < len(shading) - 1:
            svg.line(x + 301, 414, x + 323, 414)

    svg.box(30, 506, 1340, 158, fill=BACKGROUND, stroke=PLANNED, dashed=True, radius=12)
    svg.text(55, 542, "Planned for Phase 7", 19, MUTED, 700)
    planned = ["Depth and normals", "Light clustering", "Ambient occlusion", "PBR shading", "Transparency", "Post-processing"]
    for index, name in enumerate(planned):
        x = 55 + index * 217
        svg.box(x, 562, 200, 72, fill=BACKGROUND, stroke=PLANNED, dashed=True)
        svg.text(x + 100, 604, name, 16, MUTED, 600, "middle")
    svg.write("render-pipeline")


def bindless():
    svg = Svg(1400, 560)
    svg.text(50, 58, "How one draw finds its data", 32, TEXT, 700)
    svg.text(50, 90, "The draw carries five small numbers. The GPU follows them to everything else.", 18, MUTED)

    svg.box(50, 130, 330, 370, stroke=ACCENT, stroke_width=2)
    svg.text(215, 172, "Push constants", 21, TEXT, 700, "middle")
    svg.text(215, 198, "20 bytes, set for each draw", 15, MUTED, 400, "middle")
    rows = ["frame table", "instance table", "instance number", "material table", "material number"]
    for index, name in enumerate(rows):
        svg.box(80, 218 + index * 52, 270, 42, fill="#1a2030", stroke=PANEL_EDGE, radius=6)
        svg.text(215, 245 + index * 52, name, 16, TEXT, 500, "middle")

    targets = [
        (130, "FrameData", "view-projection matrix, camera, fog"),
        (262, "InstanceData", "world matrix of this draw"),
        (394, "MaterialData", "factors and texture numbers"),
    ]
    for y, name, detail in targets:
        svg.box(520, y, 420, 106, stroke=PANEL_EDGE)
        svg.text(730, y + 44, name, 20, TEXT, 700, "middle")
        svg.text(730, y + 74, detail, 15.5, MUTED, 400, "middle")
    svg.line(351, 239, 518, 183)
    svg.line(351, 317, 518, 315)
    svg.line(351, 421, 518, 447)

    svg.box(1060, 262, 290, 238, stroke=DONE, stroke_width=2)
    svg.text(1205, 304, "All textures", 20, TEXT, 700, "middle")
    svg.text(1205, 330, "one big array", 15, MUTED, 400, "middle")
    svg.text(1205, 356, "up to 4096", 15, MUTED, 400, "middle")
    for row in range(3):
        for column in range(5):
            svg.box(1086 + column * 48, 376 + row * 36, 42, 30, fill="#1a2030", stroke=PANEL_EDGE, radius=4)
    svg.line(941, 447, 1058, 420)
    svg.write("bindless")


def character_move():
    svg = Svg(1400, 660)
    svg.text(50, 58, "One step of walking, 60 times per second", 32, TEXT, 700)
    svg.text(50, 90, "The game decides where the player wants to go. Jolt decides how far the world lets them.", 18, MUTED)

    steps = [
        ("Wish", "keys become a direction,", "speed from walk, run, crouch", ACCENT),
        ("Gravity", "added by the game,", "skipped on a ladder", ACCENT),
        ("Move the capsule", "Jolt sweeps it through", "the level and the doors", DONE),
        ("Measure", "real speed = distance moved", "÷ time of the step", DONE),
    ]
    width = 310
    gap = 20
    for index, (name, line1, line2, color) in enumerate(steps):
        x = 50 + index * (width + gap)
        svg.box(x, 130, width, 132, stroke=color, stroke_width=2)
        svg.text(x + width / 2, 172, name, 21, TEXT, 700, "middle")
        svg.text(x + width / 2, 206, line1, 15.5, MUTED, 400, "middle")
        svg.text(x + width / 2, 232, line2, 15.5, MUTED, 400, "middle")
        if index < len(steps) - 1:
            svg.line(x + width + 2, 196, x + width + gap - 2, 196)

    svg.text(205, 296, "game code", 15, ACCENT, 600, "middle")
    svg.text(865, 296, "physics module", 15, DONE, 600, "middle")

    cards = [
        ("Walls", "Slide along them", "instead of stopping dead"),
        ("Steps", "Climbed up to 0.4 m", "with no jump needed"),
        ("Slopes", "Walkable up to 50°", "steeper ones are walls"),
        ("Doors", "Boxes that move", "with their animation"),
    ]
    for index, (name, line1, line2) in enumerate(cards):
        x = 50 + index * (width + gap)
        svg.box(x, 326, width, 132, stroke=PANEL_EDGE)
        svg.text(x + width / 2, 366, name, 20, TEXT, 700, "middle")
        svg.text(x + width / 2, 400, line1, 16, TEXT, 400, "middle")
        svg.text(x + width / 2, 426, line2, 15, MUTED, 400, "middle")

    svg.box(50, 492, 1300, 130, stroke=PANEL_EDGE)
    svg.text(80, 532, "What the capsule moves through", 19, TEXT, 700)
    svg.text(80, 568, "The factory's visible level, 10,282 triangles, is also its collision mesh. There is no separate invisible version to keep in step.", 16.5, MUTED)
    svg.text(80, 596, "The capsule is 1.7 m tall and 0.35 m in radius, and shrinks to 0.9 m when crouching. It only stands up if there is room.", 16.5, MUTED)
    svg.write("character-move")


def skinning():
    svg = Svg(1400, 640)
    svg.text(50, 58, "How a zombie moves", 32, TEXT, 700)
    svg.text(50, 90, "Animation only moves invisible joints. The graphics card bends the skin to follow them.", 18, MUTED)

    steps = [
        ("Clips", "keys read from the model:", "wait, walk, attack, die", ACCENT),
        ("Animator", "plays one clip, and fades", "the previous one out", ACCENT),
        ("Joints", "ordinary entities, placed", "by the transform pass", DONE),
        ("Vertex shader", "each vertex follows up to", "four joints, by weight", DONE),
    ]
    width = 310
    gap = 20
    for index, (name, line1, line2, color) in enumerate(steps):
        x = 50 + index * (width + gap)
        svg.box(x, 130, width, 132, stroke=color, stroke_width=2)
        svg.text(x + width / 2, 172, name, 21, TEXT, 700, "middle")
        svg.text(x + width / 2, 206, line1, 15.5, MUTED, 400, "middle")
        svg.text(x + width / 2, 232, line2, 15.5, MUTED, 400, "middle")
        if index < len(steps) - 1:
            svg.line(x + width + 2, 196, x + width + gap - 2, 196)

    svg.text(380, 296, "processor, once per frame", 15, ACCENT, 600, "middle")
    svg.text(1040, 296, "graphics card, for every vertex", 15, DONE, 600, "middle")

    cards = [
        ("19 joints", "in the zombie's skeleton,", "each one an entity"),
        ("735 triangles", "bent on the graphics card,", "never rewritten"),
        ("4 clips", "cut from one long timeline", "at the old frame ranges"),
        ("0.3 s", "to fade from one clip", "to the next"),
    ]
    for index, (name, line1, line2) in enumerate(cards):
        x = 50 + index * (width + gap)
        svg.box(x, 326, width, 132, stroke=PANEL_EDGE)
        svg.text(x + width / 2, 366, name, 20, TEXT, 700, "middle")
        svg.text(x + width / 2, 400, line1, 16, TEXT, 400, "middle")
        svg.text(x + width / 2, 426, line2, 15, MUTED, 400, "middle")

    svg.box(50, 492, 1300, 110, stroke=PANEL_EDGE)
    svg.text(80, 532, "Then: the old engine bent every vertex on the processor, for every frame of every clip, at load time.", 16.5, MUTED)
    svg.text(80, 564, "Now: the converter reproduces the old math, checks the vertices of each clip against it, and saves standard glTF files.", 16.5, MUTED)
    svg.write("skinning")


def fixed_step():
    svg = Svg(1400, 640)
    svg.text(50, 58, "Two clocks: the world's and the screen's", 32, TEXT, 700)
    svg.text(50, 90, "The world ticks at a steady 60 Hz. The screen draws whenever it can, and shows the moment between two ticks.", 18, MUTED)

    left = 170
    tick = 165
    svg.text(50, 196, "Fixed steps", 17, TEXT, 700)
    svg.line(left - 20, 190, 1350, 190, arrow=True)
    for index in range(7):
        x = left + index * tick
        svg.parts.append(f'<circle cx="{x}" cy="190" r="9" fill="{DONE}"/>')
        svg.text(x, 170, f"{index * 1000 / 60:.1f}", 14, MUTED, 400, "middle")
    svg.text(1270, 170, "ms", 14, MUTED)

    svg.text(50, 306, "Frames", 17, TEXT, 700)
    svg.line(left - 20, 300, 1350, 300, arrow=True)
    frames = [0.0, 0.8, 1.5, 3.4, 4.3, 5.6]
    for index, position in enumerate(frames):
        x = left + position * tick
        color = ACCENT
        svg.box(x - 12, 284, 24, 32, fill=color, stroke=color, radius=4)
        svg.line(x, 282, x, 204, stroke=PANEL_EDGE, width=1.5, arrow=False, dashed=True)
    svg.text(left + 2.45 * tick, 344, "a slow frame: two steps to catch up", 15.5, ALERT, 500, "middle")

    svg.box(50, 376, 640, 224, stroke=PANEL_EDGE)
    svg.text(76, 416, "Worked example: a frame took 40 ms", 20, TEXT, 700)
    svg.text(76, 454, "With nothing owed before, 40 ms are now owed.", 17, TEXT)
    svg.text(76, 484, "Two steps of 16.7 ms fit, so the world ticks twice.", 17, TEXT)
    svg.text(76, 514, "6.7 ms stay in the account for the next frame.", 17, TEXT)
    svg.text(76, 544, "6.7 ÷ 16.7 = 0.4: the screen is 40% of the way", 17, TEXT)
    svg.text(76, 570, "from the last position to the current one.", 17, TEXT)

    svg.box(710, 376, 640, 224, stroke=PANEL_EDGE)
    svg.text(736, 416, "What the camera draws", 20, TEXT, 700)
    a = 790
    b = 1270
    svg.line(a, 490, b, 490, arrow=False, width=3)
    svg.parts.append(f'<circle cx="{a}" cy="490" r="11" fill="{DONE}"/>')
    svg.parts.append(f'<circle cx="{b}" cy="490" r="11" fill="{DONE}"/>')
    middle = a + 0.4 * (b - a)
    svg.parts.append(f'<circle cx="{middle}" cy="490" r="11" fill="{ACCENT}"/>')
    svg.text(a, 462, "previous step", 15, MUTED, 400, "middle")
    svg.text(b, 462, "latest step", 15, MUTED, 400, "middle")
    svg.text(middle, 462, "drawn: 0.4", 15, ACCENT, 600, "middle")
    svg.text(736, 548, "Smooth on any monitor, at the price of showing", 16.5, MUTED)
    svg.text(736, 574, "the player up to one step (16.7 ms) in the past.", 16.5, MUTED)
    svg.write("fixed-step")


OLD = NEXT
NEW = DONE


def compare_table(name, title, subtitle, rows, old_label="Old Daidalos", new_label="Daidalos Engine Next"):
    """A table of "then and now" rows: (topic, old line, new line)."""
    row_height = 88
    top = 178
    height = top + len(rows) * row_height + 40
    svg = Svg(1400, height)
    svg.text(50, 58, title, 32, TEXT, 700)
    svg.text(50, 90, subtitle, 18, MUTED)
    svg.text(280, 150, old_label, 20, OLD, 700)
    svg.text(840, 150, new_label, 20, NEW, 700)
    for index, (topic, old, new) in enumerate(rows):
        y = top + index * row_height
        svg.box(50, y, 1300, row_height - 10, fill=PANEL if index % 2 == 0 else "#191d28", stroke="#262c3a", radius=8)
        svg.text(72, y + 46, topic, 17.5, TEXT, 600)
        for x, value in ((280, old), (840, new)):
            lines = value if isinstance(value, tuple) else (value,)
            start = y + (46 if len(lines) == 1 else 34)
            for line_index, line in enumerate(lines):
                svg.text(x, start + line_index * 25, line, 16, TEXT if line_index == 0 else MUTED)
    svg.write(name)


def compare_objects():
    svg = Svg(1400, 760)
    svg.text(50, 58, "A door, then and now", 32, TEXT, 700)
    svg.text(50, 90, "On the left, a door is a kind of something. On the right, a door is a list of parts.", 18, MUTED)

    svg.box(30, 120, 620, 600, fill="#181c26", stroke="#262c3a", radius=12)
    svg.text(55, 158, "Old Daidalos: a family tree", 20, OLD, 700)

    def node(x, y, width, label, stroke=PANEL_EDGE):
        svg.box(x, y, width, 54, stroke=stroke)
        svg.text(x + width / 2, y + 34, label, 16, TEXT, 600, "middle")

    node(225, 186, 170, "IEntity", OLD)
    svg.text(405, 208, "health, particles,", 14, MUTED)
    svg.text(405, 228, "id, flag...", 14, MUTED)
    node(70, 286, 220, "ICollidableEntity")
    node(350, 286, 220, "ITriggerEntity")
    svg.line(285, 241, 190, 284)
    svg.line(335, 241, 450, 284)
    node(70, 376, 220, "IEnemyEntity")
    svg.line(180, 341, 180, 374)
    node(70, 466, 220, "CZombieEntity")
    svg.line(180, 431, 180, 464)
    node(350, 376, 220, "CDoorEntity", OLD)
    node(350, 442, 220, "CLadderEntity")
    node(350, 508, 220, "CHandgunAmmoEntity")
    svg.line(450, 341, 450, 374)
    svg.text(460, 590, "...and 21 entity classes in all", 15, MUTED, 400, "middle")
    svg.text(55, 640, "A door inherits a health value it has no use for,", 16, MUTED)
    svg.text(55, 666, "and each new mix of abilities needs a new place in the tree.", 16, MUTED)

    svg.box(670, 120, 700, 600, fill="#181c26", stroke="#262c3a", radius=12)
    svg.text(695, 158, "Daidalos Engine Next: a table", 20, NEW, 700)
    components = ["Transform", "Mesh", "Door", "Body", "Pickup", "Ladder", "Camera", "Player"]
    entities = [
        ("Door", {0, 1, 2, 3}),
        ("Pickup", {0, 1, 4}),
        ("Ladder", {0, 1, 5}),
        ("Player", {0, 6, 7}),
    ]
    left = 790
    width = 70
    for index, name in enumerate(components):
        svg.parts.append(
            f'<text transform="translate({left + index * width + 42},232) rotate(-38)" font-family="{FONT}" '
            f'font-size="14.5" fill="{MUTED}">{name}</text>'
        )
    for row, (name, has) in enumerate(entities):
        y = 250 + row * 76
        svg.text(695, y + 42, name, 17, TEXT, 600)
        for column in range(len(components)):
            present = column in has
            svg.box(left + column * width + 6, y, width - 12, 62, fill="#21403a" if present else BACKGROUND,
                    stroke=NEW if present else "#262c3a", radius=6)
    svg.text(695, 588, "Entities are only numbers. Any mix of components is allowed,", 16, MUTED)
    svg.text(695, 614, "and giving a crate a health value later touches no other class.", 16, MUTED)
    svg.text(695, 666, "Zombies join this table in Phase 6.", 16, MUTED)
    svg.write("compare-objects")


def compare_collision():
    compare_table(
        "compare-collision",
        "Collision, then and now",
        "From my own BSP code to a library, and what that trade really cost.",
        [
            ("Written by", ("Me: about 2,400 lines of BSP code", "licence headers included"), ("Jolt, an open-source library", "a 370-line wrapper around it")),
            ("The world", ("A BSP tree of brushes", "convex shapes, split by planes"), ("A triangle mesh", "the level as it is drawn")),
            ("The player", "An ellipse, slid along planes", "A capsule, moved by Jolt each step"),
            ("Steps", ("Retry 0.4 units higher", "when a wall stops the move"), ("Built in, up to 0.4 m", "plus sticking to the floor going down")),
            ("Doors", ("Collision switched on or off", "solid until fully open, solid again as it starts closing"), ("A box that follows the door", "each step of its animation")),
            ("Timing", ("Every 16 ms of elapsed time", "with a variable step, per entity"), ("Exactly 60 times per second", "for everything")),
            ("Queries", "Slide and collision point", "Rays, returning a surface and a normal"),
        ],
    )


def compare_rendering():
    svg = Svg(1400, 790)
    svg.text(50, 58, "A frame, then and now", 32, TEXT, 700)
    svg.text(50, 90, "Same goal: many lights in dark corridors. The new frame is much simpler today, and it has not earned the comparison yet.", 18, MUTED)

    svg.box(30, 120, 660, 630, fill="#181c26", stroke="#262c3a", radius=12)
    svg.text(55, 158, "Old Daidalos: deferred shading on DirectX 9", 20, OLD, 700)
    old = [
        ("Fill", "color, normal, specular, depth into 4 textures"),
        ("Spot lights", "shadow map if it casts one, then one draw per light"),
        ("Point lights", "one draw per light, limited to its area on screen"),
        ("Directional lights", "shadow map if it casts one, then one draw per light"),
        ("Combine", "color × accumulated light"),
        ("Post-processing", "glow and blur"),
        ("Skybox", "drawn behind what is already there"),
        ("Transparent, particles", "a second, forward path"),
    ]
    for index, (name, detail) in enumerate(old):
        y = 178 + index * 68
        svg.box(55, y, 610, 58, stroke=PANEL_EDGE)
        svg.text(75, y + 25, name, 17, TEXT, 600)
        svg.text(75, y + 47, detail, 14.5, MUTED)

    svg.box(710, 120, 660, 630, fill="#181c26", stroke="#262c3a", radius=12)
    svg.text(735, 158, "Daidalos Engine Next: forward on Vulkan 1.3", 20, NEW, 700)
    svg.text(735, 192, "Today", 16, NEW, 700)
    now = [("Mesh pass", "every mesh, fixed light, fog"), ("Overlay", "debug interface")]
    for index, (name, detail) in enumerate(now):
        y = 206 + index * 68
        svg.box(735, y, 610, 58, stroke=NEW)
        svg.text(755, y + 25, name, 17, TEXT, 600)
        svg.text(755, y + 47, detail, 14.5, MUTED)
    svg.text(735, 366, "Phase 7, not written yet", 16, MUTED, 700)
    later = [
        ("Depth and normals", "a quick first pass"),
        ("Clusters", "the view cut into cells, each with its light list"),
        ("Ambient occlusion", "contact shadows, from the first pass"),
        ("PBR shading, then transparency", "one material path for both"),
    ]
    for index, (name, detail) in enumerate(later):
        y = 380 + index * 68
        svg.box(735, y, 610, 58, fill=BACKGROUND, stroke=PLANNED, dashed=True)
        svg.text(755, y + 25, name, 17, MUTED, 600)
        svg.text(755, y + 47, detail, 14.5, MUTED)
    svg.write("compare-rendering")


def compare_errors():
    svg = Svg(1400, 620)
    svg.text(50, 58, "When a file is missing, then and now", 32, TEXT, 700)
    svg.text(50, 90, "The old engine threw exceptions. The new one returns failure as a value.", 18, MUTED)

    def flow(x, title, color, steps, note):
        svg.box(x, 120, 650, 460, fill="#181c26", stroke="#262c3a", radius=12)
        svg.text(x + 25, 158, title, 20, color, 700)
        for index, (name, detail) in enumerate(steps):
            y = 178 + index * 88
            svg.box(x + 25, y, 600, 68, stroke=PANEL_EDGE)
            svg.text(x + 45, y + 30, name, 17, TEXT, 600)
            svg.text(x + 45, y + 54, detail, 14.5, MUTED)
            if index < len(steps) - 1:
                svg.line(x + 325, y + 69, x + 325, y + 87)
        svg.text(x + 25, 554, note, 16, MUTED)

    flow(
        40,
        "Old Daidalos",
        OLD,
        [
            ("Loader", "throw CExceptionLoad(filename)"),
            ("Each function in between", "looks as if nothing can go wrong"),
            ("No catch anywhere in the game path", "only the thread pool catches anything"),
            ("The program stops", "an uncaught exception ends it"),
        ],
        "Nothing in a function's signature says that it can fail.",
    )
    flow(
        710,
        "Daidalos Engine Next",
        NEW,
        [
            ("Loader", "return tl::unexpected(Error{NotFound, module, message})"),
            ("Result<Model*>", "each caller must look before using the value"),
            ("The caller decides", "report, retry, or pass it up with more context"),
            ("Startup failed? Roll back", "started steps stop in reverse order"),
        ],
        "The signature says it: this call can fail, and here is what you get.",
    )
    svg.write("compare-errors")


def compare_ownership():
    compare_table(
        "compare-ownership",
        "Who owns what, then and now",
        "Assets and global services. One old idea survived: the path of a file is its identity.",
        [
            ("Finding the renderer", ("IRenderer::GetRenderer(), a global", "99 calls in 32 source files"), ("Passed in where it is needed", "you can see who uses it")),
            ("Global managers", ("5 singletons", "resources, media, config, input, sound"), ("None of these", "logging stays global")),
            ("Finding a loader", ("Registered by file extension", "at run time"), ("Typed functions", "LoadModel, LoadImageFile")),
            ("Asset lifetime", ("Reference count per resource", "AddReference, Release, by hand"), ("AssetLibrary owns all of them", "released together with the scene")),
            ("Using an asset", "A raw pointer", ("A generational handle", "a stale one is detected")),
            ("Destroying GPU data", ("Immediate", "DirectX 9 tracks what the GPU uses"), ("Deferred", "until the GPU has finished with it")),
        ],
    )


def find_browser():
    candidates = [
        shutil.which("msedge"),
        shutil.which("chrome"),
        shutil.which("chromium"),
        shutil.which("google-chrome"),
        r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
        r"C:\Program Files\Google\Chrome\Application\chrome.exe",
    ]
    for candidate in candidates:
        if candidate and Path(candidate).exists():
            return candidate
    raise SystemExit("No Chromium-based browser found to export the PNG files")


def export_png():
    PNG_DIR.mkdir(parents=True, exist_ok=True)
    browser = find_browser()
    for svg_path in sorted(SVG_DIR.glob("*.svg")):
        content = svg_path.read_text(encoding="utf-8")
        size = content.split('width="', 1)[1].split('"', 1)[0], content.split('height="', 1)[1].split('"', 1)[0]
        with tempfile.TemporaryDirectory() as temp:
            page = Path(temp) / "page.html"
            page.write_text(
                f"<!DOCTYPE html><html><body style='margin:0'>{content}</body></html>", encoding="utf-8"
            )
            subprocess.run(
                [
                    browser,
                    "--headless=new",
                    "--disable-gpu",
                    "--hide-scrollbars",
                    "--force-device-scale-factor=2",
                    f"--user-data-dir={temp}/profile",
                    f"--window-size={size[0]},{size[1]}",
                    f"--screenshot={PNG_DIR / (svg_path.stem + '.png')}",
                    page.as_uri(),
                ],
                check=True,
                capture_output=True,
                timeout=60,
            )
        print(f"{svg_path.stem}.png")


def main():
    architecture()
    frame()
    roadmap()
    deferred_destruction()
    asset_pipeline()
    ecs_sparse_set()
    stale_handles()
    render_pipeline()
    bindless()
    character_move()
    skinning()
    fixed_step()
    compare_objects()
    compare_collision()
    compare_rendering()
    compare_errors()
    compare_ownership()
    export_png()


if __name__ == "__main__":
    main()
