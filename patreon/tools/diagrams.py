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
    export_png()


if __name__ == "__main__":
    main()
