"""Create three original-UV0 pattern examples, or verify exact owned packages.
Run in Unreal Editor Python; add -BreachPatternExamplesVerify for read-only verification.
No map, pre-existing asset or SurfaceSet package is saved.
"""
import json
import math
import struct
import zlib
from pathlib import Path
import unreal

ROOT = "/BreachSystem/PatternExamples"
REPORT = Path(unreal.Paths.project_saved_dir()) / "Automation/PatternExamples"
KINDS = ("Basic", "Brick", "Wood")
ASSETS = [f"{ROOT}/{name}_{kind}" for kind in KINDS for name in ("BPAT", "BSM", "M", "T")]


def require(value, message):
    if not value:
        raise RuntimeError(message)
    return value


def brick_regions():
    result = []
    for row in range(4):
        offset = .125 if row % 2 else 0
        for column in range(4):
            x = column * .25 + offset
            # One canonical polygon per brick; odd courses cross the periodic seam.
            region = unreal.BreachPatternRegion()
            region.set_editor_property("outer", [unreal.Vector2D(x, row * .25), unreal.Vector2D(x + .25, row * .25), unreal.Vector2D(x + .25, row * .25 + .25), unreal.Vector2D(x, row * .25 + .25)])
            result.append(region)
    return result


def wood_guides():
    result = []
    for index in range(8):
        guide = unreal.BreachPatternGuide()
        guide.set_editor_property("points", [unreal.Vector2D(index / 8 + .0625, y / 8) for y in range(9)])
        result.append(guide)
    return result


def write_texture(kind):
    # Pure standard-library PNG, independent of machine-specific source-art paths.
    size = 512
    rows = bytearray()
    for y in range(size):
        rows.append(0)
        v = (y + .5) / size
        for x in range(size):
            u = (x + .5) / size
            if kind == "Brick":
                row = int(v * 4)
                local_u = (u - (.125 if row % 2 else 0)) % .25
                local_v = v % .25
                solid = .008 < local_u < .242 and .008 < local_v < .242
                noise = ((x * 17 + y * 31) % 19) - 9
                color = (148 + noise, 65 + noise, 39 + noise) if solid else (160, 153, 140)
            elif kind == "Wood":
                grain = math.cos((u - .0625) * math.tau * 8)
                fine = math.sin(u * math.tau * 75 + math.sin(v * 11))
                color = (int(148 - grain * 23 + fine * 5), int(94 - grain * 18 + fine * 4), int(46 - grain * 10 + fine * 3))
            else:
                color = (146, 151, 156)
            rows.extend((*color, 255))
    def chunk(kind_bytes, payload):
        return struct.pack(">I", len(payload)) + kind_bytes + payload + struct.pack(">I", zlib.crc32(kind_bytes + payload) & 0xffffffff)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(bytes(rows))) + chunk(b"IEND", b"")
    path = REPORT / f"T_{kind}.png"
    path.write_bytes(png)
    return path


def inspect():
    rows = []
    for kind, mode in zip(KINDS, (unreal.BreachPatternMode.NONE, unreal.BreachPatternMode.REGIONS, unreal.BreachPatternMode.GRAIN_GUIDES)):
        pattern = require(unreal.load_asset(f"{ROOT}/BPAT_{kind}"), f"Missing pattern: {kind}")
        surface = require(unreal.load_asset(f"{ROOT}/BSM_{kind}"), f"Missing surface: {kind}")
        material = require(unreal.load_asset(f"{ROOT}/M_{kind}"), f"Missing display material: {kind}")
        texture = require(unreal.load_asset(f"{ROOT}/T_{kind}"), f"Missing texture: {kind}")
        require(pattern.get_editor_property("mode") == mode, f"Wrong mode: {kind}")
        require(surface.get_editor_property("pattern") == pattern, f"Wrong pattern reference: {kind}")
        require(abs(surface.get_editor_property("grain_influence") - .6) < .0001, f"Wrong influence: {kind}")
        require(surface.get_editor_property("accumulation").get_editor_property("enabled"), f"Accumulation disabled: {kind}")
        require(pattern.get_editor_property("reference_texture") == texture, f"Wrong reference texture: {kind}")
        node = require(unreal.MaterialEditingLibrary.get_material_property_input_node(material, unreal.MaterialProperty.MP_BASE_COLOR), f"Missing BaseColor: {kind}")
        require(node.get_editor_property("texture") == texture, f"Wrong display texture: {kind}")
        require(not any(unreal.MaterialEditingLibrary.get_inputs_for_material_expression(material, node)), f"Texture must use unmodified original UV0: {kind}")
        count = len(pattern.get_editor_property("regions")) if kind == "Brick" else len(pattern.get_editor_property("guides"))
        require(count == (16 if kind == "Brick" else 8 if kind == "Wood" else 0), f"Wrong geometry count: {kind}: {count}")
        rows.append({"kind": kind, "mode": str(mode), "geometry_count": count, "pattern": pattern.get_path_name(), "surface": surface.get_path_name(), "material": material.get_path_name()})
    return rows


def main():
    REPORT.mkdir(parents=True, exist_ok=True)
    verify = "-BreachPatternExamplesVerify" in unreal.SystemLibrary.get_command_line()
    unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous([ROOT], True)
    if not verify:
        require(all(not unreal.EditorAssetLibrary.does_asset_exist(path) for path in ASSETS), "Example destination exists; refusing to overwrite. Use -BreachPatternExamplesVerify.")
        tools = unreal.AssetToolsHelpers.get_asset_tools()
        pending = []
        for kind, mode in zip(KINDS, (unreal.BreachPatternMode.NONE, unreal.BreachPatternMode.REGIONS, unreal.BreachPatternMode.GRAIN_GUIDES)):
            task = unreal.AssetImportTask()
            for prop, value in (("filename", str(write_texture(kind))), ("destination_path", ROOT), ("destination_name", f"T_{kind}"), ("automated", True), ("replace_existing", False), ("save", False)):
                task.set_editor_property(prop, value)
            tools.import_asset_tasks([task])
            texture = require(unreal.load_asset(f"{ROOT}/T_{kind}"), f"Texture import failed: {kind}")
            texture.set_editor_property("srgb", True)
            texture.set_editor_property("address_x", unreal.TextureAddress.TA_WRAP)
            texture.set_editor_property("address_y", unreal.TextureAddress.TA_WRAP)
            pattern = require(tools.create_asset(f"BPAT_{kind}", ROOT, unreal.BreachPattern, unreal.BreachPatternFactory()), f"Pattern creation failed: {kind}")
            pattern.set_editor_property("mode", mode)
            pattern.set_editor_property("reference_texture", texture)
            if kind == "Brick":
                pattern.set_editor_property("regions", brick_regions())
            if kind == "Wood":
                pattern.set_editor_property("guides", wood_guides())
            factory = unreal.DataAssetFactory()
            factory.set_editor_property("data_asset_class", unreal.BreachSurfaceMaterial)
            surface = require(tools.create_asset(f"BSM_{kind}", ROOT, unreal.BreachSurfaceMaterial, factory), f"Surface creation failed: {kind}")
            surface.set_editor_property("pattern", pattern)
            surface.set_editor_property("grain_influence", .6)
            accumulation = surface.get_editor_property("accumulation")
            accumulation.set_editor_property("enabled", True)
            accumulation.set_editor_property("breach_hp", 3.)
            accumulation.set_editor_property("window_size", unreal.Vector2D(40, 40))
            surface.set_editor_property("accumulation", accumulation)
            material = require(tools.create_asset(f"M_{kind}", ROOT, unreal.Material, unreal.MaterialFactoryNew()), f"Display material creation failed: {kind}")
            sample = unreal.MaterialEditingLibrary.create_material_expression(material, unreal.MaterialExpressionTextureSample)
            sample.set_editor_property("texture", texture)
            require(unreal.MaterialEditingLibrary.connect_material_property(sample, "RGB", unreal.MaterialProperty.MP_BASE_COLOR), f"Connect failed: {kind}")
            require(not unreal.MaterialEditingLibrary.recompile_material(material), f"Material compilation failed: {kind}")
            pending.extend((texture, pattern, surface, material))
        # Validate before saving. Only these twelve newly created exact packages are saved.
        inspect()
        for asset in pending:
            require(asset.get_path_name().startswith(ROOT + "/"), "Save boundary violation")
            require(unreal.EditorAssetLibrary.save_loaded_asset(asset), f"Save failed: {asset.get_path_name()}")
    report = {"mode": "verify" if verify else "create", "assets": inspect(), "saved_only": [] if verify else ASSETS, "map_saved": False, "uv": "Original mesh UV0, unit-period pattern, no shader coordinate transform"}
    path = REPORT / ("verified.json" if verify else "created.json")
    path.write_text(json.dumps(report, indent=2), encoding="utf-8")
    unreal.log(f"BREACH_PATTERN_EXAMPLES_OK: {path}")


try:
    main()
except Exception as error:
    REPORT.mkdir(parents=True, exist_ok=True)
    (REPORT / "error.txt").write_text(str(error), encoding="utf-8")
    raise
