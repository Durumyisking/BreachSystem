"""Create or independently inspect the five requested generated surface materials."""
import json
from pathlib import Path
import unreal

PROJECT = Path(unreal.Paths.project_dir()).resolve()
SOURCE = PROJECT / "Plugins/BreachSystem/SourceArt/SurfaceSet_20261114"
ROOT = "/BreachSystem/SurfaceSet"
REPORT = PROJECT / "Saved/Automation/SurfaceSet"
KINDS = ("DecoWood", "WallBrick", "DecoConcrete", "WallConcrete", "WallWood")
ROUGHNESS = {"DecoWood": .86, "WallBrick": .9, "DecoConcrete": .92, "WallConcrete": .88, "WallWood": .76}
ASSETS = [f"{ROOT}/Textures/T_{kind}_{suffix}" for kind in KINDS for suffix in ("BaseColor", "Normal")]
ASSETS += [f"{ROOT}/Materials/M_{kind}" for kind in KINDS]


def require(value, message):
    if not value:
        raise RuntimeError(message)
    return value


def inspect():
    rows = []
    for kind in KINDS:
        masked = kind == "DecoWood"
        material = require(unreal.load_asset(f"{ROOT}/Materials/M_{kind}"), f"Missing material: {kind}")
        expected_mode = unreal.BlendMode.BLEND_MASKED if masked else unreal.BlendMode.BLEND_OPAQUE
        require(material.get_editor_property("blend_mode") == expected_mode, f"Wrong blend mode: {kind}")
        require(material.get_editor_property("two_sided") == kind.startswith("Deco"), f"Wrong sidedness: {kind}")
        row = {"material": material.get_path_name(), "blend_mode": str(expected_mode), "two_sided": material.get_editor_property("two_sided"), "textures": []}
        for suffix, prop in (("BaseColor", unreal.MaterialProperty.MP_BASE_COLOR), ("Normal", unreal.MaterialProperty.MP_NORMAL)):
            path = f"{ROOT}/Textures/T_{kind}_{suffix}"
            texture = require(unreal.load_asset(path), f"Missing texture: {path}")
            node = require(unreal.MaterialEditingLibrary.get_material_property_input_node(material, prop), f"Missing input: {kind}/{suffix}")
            require(node.get_editor_property("texture") == texture, f"Wrong texture connection: {kind}/{suffix}")
            require(texture.get_editor_property("srgb") == (suffix == "BaseColor"), f"Wrong color space: {path}")
            expected_compression = unreal.TextureCompressionSettings.TC_NORMALMAP if suffix == "Normal" else unreal.TextureCompressionSettings.TC_DEFAULT
            require(texture.get_editor_property("compression_settings") == expected_compression, f"Wrong compression: {path}")
            require(texture.blueprint_get_size_x() == 1024 and texture.blueprint_get_size_y() == 1024, f"Wrong texture size: {path}")
            row["textures"].append({"path": path, "size": [texture.blueprint_get_size_x(), texture.blueprint_get_size_y()], "srgb": texture.get_editor_property("srgb"), "compression": str(expected_compression)})
        mask = unreal.MaterialEditingLibrary.get_material_property_input_node(material, unreal.MaterialProperty.MP_OPACITY_MASK)
        require(bool(mask) == masked, f"Wrong mask input: {kind}")
        if masked:
            require(mask.get_editor_property("texture").get_path_name().endswith("T_DecoWood_BaseColor.T_DecoWood_BaseColor"), "Wrong wood mask texture")
            require(unreal.MaterialEditingLibrary.get_material_property_input_node_output_name(material, unreal.MaterialProperty.MP_OPACITY_MASK) == "A", "Wood mask must use source alpha")
        require(unreal.MaterialEditingLibrary.get_material_property_input_node(material, unreal.MaterialProperty.MP_ROUGHNESS), f"Missing roughness: {kind}")
        errors = unreal.MaterialEditingLibrary.recompile_material(material)
        require(not errors, f"Shader errors: {kind}: {errors}")
        row["compile_errors"] = list(errors)
        rows.append(row)
    return rows


def main():
    require(PROJECT.name == "BreachTest", f"Wrong project: {PROJECT}")
    REPORT.mkdir(parents=True, exist_ok=True)
    verifying = "-SurfaceSetVerify" in unreal.SystemLibrary.get_command_line()
    unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous([ROOT], True)
    if not verifying:
        require(all(not unreal.EditorAssetLibrary.does_asset_exist(path) for path in ASSETS), "Destination assets already exist; refusing to overwrite")
        tools = unreal.AssetToolsHelpers.get_asset_tools()
        textures = {}
        for kind in KINDS:
            for suffix in ("BaseColor", "Normal"):
                name = f"T_{kind}_{suffix}"
                path = SOURCE / f"{name}.png"
                require(path.is_file(), f"Missing source image: {path}")
                task = unreal.AssetImportTask()
                for prop, value in (("filename", str(path)), ("destination_path", f"{ROOT}/Textures"), ("destination_name", name), ("automated", True), ("replace_existing", False), ("save", False)):
                    task.set_editor_property(prop, value)
                tools.import_asset_tasks([task])
                texture = require(unreal.load_asset(f"{ROOT}/Textures/{name}"), f"Import failed: {name}")
                texture.set_editor_property("srgb", suffix == "BaseColor")
                texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP if suffix == "Normal" else unreal.TextureCompressionSettings.TC_DEFAULT)
                texture.set_editor_property("compression_no_alpha", suffix == "Normal" or kind != "DecoWood")
                address = unreal.TextureAddress.TA_CLAMP if kind.startswith("Deco") else unreal.TextureAddress.TA_WRAP
                texture.set_editor_property("address_x", address)
                texture.set_editor_property("address_y", address)
                if suffix == "Normal":
                    texture.set_editor_property("flip_green_channel", False)
                require(unreal.EditorAssetLibrary.save_loaded_asset(texture), f"Save failed: {name}")
                textures[(kind, suffix)] = texture
        for kind in KINDS:
            material = require(tools.create_asset(f"M_{kind}", f"{ROOT}/Materials", unreal.Material, unreal.MaterialFactoryNew()), f"Create failed: {kind}")
            material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED if kind == "DecoWood" else unreal.BlendMode.BLEND_OPAQUE)
            material.set_editor_property("two_sided", kind.startswith("Deco"))
            material.set_editor_property("opacity_mask_clip_value", .5)
            for index, suffix in enumerate(("BaseColor", "Normal")):
                node = unreal.MaterialEditingLibrary.create_material_expression(material, unreal.MaterialExpressionTextureSample, -500, index * 220)
                node.set_editor_property("texture", textures[(kind, suffix)])
                if kind.startswith("Deco"):
                    node.set_editor_property("sampler_source", unreal.SamplerSourceMode.SSM_WRAP_WORLD_GROUP_SETTINGS)
                node.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL if suffix == "Normal" else unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
                prop = unreal.MaterialProperty.MP_NORMAL if suffix == "Normal" else unreal.MaterialProperty.MP_BASE_COLOR
                require(unreal.MaterialEditingLibrary.connect_material_property(node, "RGB", prop), f"Connect failed: {kind}/{suffix}")
                if kind == "DecoWood" and suffix == "BaseColor":
                    require(unreal.MaterialEditingLibrary.connect_material_property(node, "A", unreal.MaterialProperty.MP_OPACITY_MASK), "Mask connect failed")
            roughness = unreal.MaterialEditingLibrary.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -250, 450)
            roughness.set_editor_property("parameter_name", "Roughness")
            roughness.set_editor_property("default_value", ROUGHNESS[kind])
            require(unreal.MaterialEditingLibrary.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS), f"Roughness connect failed: {kind}")
            errors = unreal.MaterialEditingLibrary.recompile_material(material)
            require(not errors, f"Shader errors: {kind}: {errors}")
            require(unreal.EditorAssetLibrary.save_loaded_asset(material), f"Material save failed: {kind}")
    report = {"mode": "verify" if verifying else "create", "assets": inspect(), "saved_only": [] if verifying else ASSETS, "normal_source": "Color-derived approximate relief; original generated BaseColor maps retained.", "source_pixel_reuse": False}
    path = REPORT / ("verified.json" if verifying else "created.json")
    path.write_text(json.dumps(report, indent=2), encoding="utf-8")
    unreal.log(f"SURFACE_SET_OK: {path}")


try:
    main()
except Exception as error:
    REPORT.mkdir(parents=True, exist_ok=True)
    (REPORT / "error.txt").write_text(str(error), encoding="utf-8")
    raise
