"""Create, update or verify the exact Deco sample assets inside the BreachTest Editor."""
import json
from pathlib import Path

import unreal

ROOT = "/BreachSystem/Decoration"
PROJECT = Path(unreal.Paths.project_dir()).resolve()
SOURCE = PROJECT / "Plugins/BreachSystem/SourceArt/Decoration"
REPORT = PROJECT / "Saved/Automation/DecoMaterials/assets.json"
KINDS = ("Concrete", "Wood")
ASSETS = [f"{ROOT}/Textures/T_Deco_{kind}_{suffix}" for kind in KINDS for suffix in ("BaseColor", "Normal", "Masks")]
ASSETS += [f"{ROOT}/Materials/M_Deco_{kind}" for kind in KINDS]


def require(value, message):
    if not value:
        raise RuntimeError(message)
    return value


def inspect():
    result = []
    for asset_path in ASSETS:
        asset = require(unreal.load_asset(asset_path), f"Missing asset: {asset_path}")
        row = {"path": asset_path, "class": asset.get_class().get_name()}
        if isinstance(asset, unreal.Material):
            row["blend_mode"] = str(asset.get_editor_property("blend_mode"))
            row["connections"] = {}
            expected_mode = unreal.BlendMode.BLEND_OPAQUE if asset_path.endswith("Concrete") else unreal.BlendMode.BLEND_MASKED
            require(asset.get_editor_property("blend_mode") == expected_mode, f"{asset_path}: unexpected blend mode")
            for name, prop in (("base_color", unreal.MaterialProperty.MP_BASE_COLOR), ("normal", unreal.MaterialProperty.MP_NORMAL), ("roughness", unreal.MaterialProperty.MP_ROUGHNESS)):
                node = unreal.MaterialEditingLibrary.get_material_property_input_node(asset, prop)
                require(node, f"{asset_path}: missing {name} input")
                row["connections"][name] = node.get_class().get_name()
            mask = unreal.MaterialEditingLibrary.get_material_property_input_node(asset, unreal.MaterialProperty.MP_OPACITY_MASK)
            require(mask is None if asset_path.endswith("Concrete") else mask is not None, f"{asset_path}: incorrect mask connection")
            row["connections"]["mask"] = mask.get_class().get_name() if mask else None
            errors = unreal.MaterialEditingLibrary.recompile_material(asset)
            require(not errors, f"Material compile errors: {errors}")
        elif isinstance(asset, unreal.Texture2D):
            row["srgb"] = asset.get_editor_property("srgb")
            row["compression"] = str(asset.get_editor_property("compression_settings"))
        result.append(row)
    return result


def main():
    require(PROJECT.name == "BreachTest", f"Wrong project: {PROJECT}")
    command_line = unreal.SystemLibrary.get_command_line()
    updating = "-DecoUpdate" in command_line
    unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous([ROOT], True)
    if "-DecoVerify" not in command_line:
        for asset_path in ASSETS:
            exists = unreal.EditorAssetLibrary.does_asset_exist(asset_path)
            require(exists if updating else not exists, f"Unexpected existing/missing asset: {asset_path}")
        tools = unreal.AssetToolsHelpers.get_asset_tools()
        textures = {}
        for kind in KINDS:
            if updating and kind == "Concrete":
                continue
            for suffix in ("BaseColor", "Normal", "Masks"):
                name = f"T_Deco_{kind}_{suffix}"
                task = unreal.AssetImportTask()
                task.set_editor_property("filename", str(SOURCE / f"{name}.png"))
                task.set_editor_property("destination_path", f"{ROOT}/Textures")
                task.set_editor_property("destination_name", name)
                task.set_editor_property("automated", True)
                task.set_editor_property("replace_existing", updating)
                task.set_editor_property("save", False)
                tools.import_asset_tasks([task])
                texture = require(unreal.load_asset(f"{ROOT}/Textures/{name}"), f"Texture import failed: {name}")
                texture.set_editor_property("srgb", suffix == "BaseColor")
                compression = {"BaseColor": unreal.TextureCompressionSettings.TC_DEFAULT, "Normal": unreal.TextureCompressionSettings.TC_NORMALMAP, "Masks": unreal.TextureCompressionSettings.TC_MASKS}[suffix]
                texture.set_editor_property("compression_settings", compression)
                texture.set_editor_property("address_x", unreal.TextureAddress.TA_CLAMP)
                texture.set_editor_property("address_y", unreal.TextureAddress.TA_CLAMP)
                require(unreal.EditorAssetLibrary.save_loaded_asset(texture), f"Texture save failed: {name}")
                textures[(kind, suffix)] = texture
        for kind in KINDS:
            if updating:
                material = require(unreal.load_asset(f"{ROOT}/Materials/M_Deco_{kind}"), f"Material missing: {kind}")
                if kind == "Concrete":
                    unreal.MaterialEditingLibrary.disconnect_material_property(material, unreal.MaterialProperty.MP_OPACITY_MASK)
                material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE if kind == "Concrete" else unreal.BlendMode.BLEND_MASKED)
                errors = unreal.MaterialEditingLibrary.recompile_material(material)
                require(not errors, f"{kind} compile errors: {errors}")
                require(unreal.EditorAssetLibrary.save_loaded_asset(material), f"Material save failed: {kind}")
                continue
            material = require(tools.create_asset(f"M_Deco_{kind}", f"{ROOT}/Materials", unreal.Material, unreal.MaterialFactoryNew()), f"Material creation failed: {kind}")
            material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE if kind == "Concrete" else unreal.BlendMode.BLEND_MASKED)
            material.set_editor_property("opacity_mask_clip_value", 0.5)
            for index, suffix in enumerate(("BaseColor", "Normal", "Masks")):
                node = unreal.MaterialEditingLibrary.create_material_expression(material, unreal.MaterialExpressionTextureSample, -600, index * 230)
                node.set_editor_property("texture", textures[(kind, suffix)])
                sampler = {"BaseColor": unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, "Normal": unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, "Masks": unreal.MaterialSamplerType.SAMPLERTYPE_MASKS}[suffix]
                node.set_editor_property("sampler_type", sampler)
                links = {"BaseColor": (("RGB", unreal.MaterialProperty.MP_BASE_COLOR),), "Normal": (("RGB", unreal.MaterialProperty.MP_NORMAL),), "Masks": (("R", unreal.MaterialProperty.MP_ROUGHNESS), ("G", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION))}[suffix]
                if suffix == "Masks" and kind == "Wood":
                    links += (("B", unreal.MaterialProperty.MP_OPACITY_MASK),)
                for output, prop in links:
                    require(unreal.MaterialEditingLibrary.connect_material_property(node, output, prop), f"Failed connecting {kind}/{suffix}/{output}")
            errors = unreal.MaterialEditingLibrary.recompile_material(material)
            require(not errors, f"{kind} compile errors: {errors}")
            require(unreal.EditorAssetLibrary.save_loaded_asset(material), f"Material save failed: {kind}")
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    saved = [] if "-DecoVerify" in command_line else [p for p in ASSETS if not updating or "/Materials/" in p or "/T_Deco_Wood_" in p]
    REPORT.write_text(json.dumps({"assets": inspect(), "saved_only": saved}, indent=2), encoding="utf-8")
    unreal.log(f"DECO_MATERIALS_OK: {REPORT}")


main()
