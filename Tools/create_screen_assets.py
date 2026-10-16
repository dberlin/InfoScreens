"""Generate original InfoScreens source art and import it in the Unreal editor.

Source art only (ordinary Python, no editor required)::
    python Tools/create_screen_assets.py --source-only

Import after compiling the native module, with the plugin installed in the SDK::
    UnrealEditor-Cmd FactoryGame.uproject -run=pythonscript \
        -script=/absolute/path/Tools/create_screen_assets.py -unattended -NullRHI

The editor needs PythonScriptPlugin and EditorScriptingUtilities enabled. Imported
assets are saved beneath /InfoScreens and must be generated before cooking.
No game or third-party mod artwork is read or copied.
"""

from __future__ import annotations

import argparse
from pathlib import Path
import struct
import zlib


ROOT = Path(__file__).resolve().parents[1]
SOURCE_ART = ROOT / "SourceArt"


def make_mesh() -> Path:
    """A chamfered 248 x 188 cm frame, with a recessed flat front at X=-1."""
    outline = [
        (-120, -94), (120, -94), (124, -90), (124, 90),
        (120, 94), (-120, 94), (-124, 90), (-124, -90),
    ]
    lines = [
        "# Original InfoScreens frame. Centimeters; +X faces the viewer; +Z up.",
        "# Widget opening is 240 x 180 cm. Front X=-1; back X=-5.",
        "o InfoScreenFrame",
    ]
    for x in (-5, -1):
        lines.extend(f"v {x} {y} {z}" for y, z in outline)
    lines.append("s off")
    # Opposite cap winding and outward side winding. The importer triangulates caps.
    lines.append("f 8 7 6 5 4 3 2 1")
    lines.append("f 9 10 11 12 13 14 15 16")
    for index in range(8):
        following = (index + 1) % 8
        lines.append(f"f {index + 1} {following + 1} {following + 9} {index + 9}")
    path = SOURCE_ART / "InfoScreenFrame.obj"
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return path


def make_icon() -> Path:
    """Draw a small screen/bar-chart icon with only the Python standard library."""
    width = height = 256
    pixels = bytearray(width * height * 4)

    def rectangle(left: int, top: int, right: int, bottom: int, color: tuple[int, ...]) -> None:
        for y in range(top, bottom):
            for x in range(left, right):
                offset = (y * width + x) * 4
                pixels[offset:offset + 4] = bytes(color)

    rectangle(18, 30, 238, 208, (43, 52, 63, 255))
    rectangle(26, 38, 230, 200, (7, 15, 24, 255))
    rectangle(40, 52, 216, 69, (91, 225, 245, 255))
    rectangle(43, 135, 82, 179, (66, 166, 200, 255))
    rectangle(99, 108, 138, 179, (67, 195, 217, 255))
    rectangle(155, 86, 194, 179, (91, 225, 245, 255))
    rectangle(109, 208, 147, 224, (43, 52, 63, 255))
    rectangle(78, 224, 178, 234, (43, 52, 63, 255))
    raw = b"".join(b"\x00" + pixels[y * width * 4:(y + 1) * width * 4] for y in range(height))

    def chunk(kind: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    image = b"\x89PNG\r\n\x1a\n"
    image += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
    image += chunk(b"IDAT", zlib.compress(raw, 9))
    image += chunk(b"IEND", b"")
    path = SOURCE_ART / "InfoScreenIcon.png"
    path.write_bytes(image)
    return path


def import_assets(mesh_path: Path, icon_path: Path) -> None:
    import unreal

    tools = unreal.AssetToolsHelpers.get_asset_tools()
    mesh_task = unreal.AssetImportTask()
    mesh_task.filename = str(mesh_path)
    mesh_task.destination_path = "/InfoScreens/Meshes"
    mesh_task.destination_name = "SM_InfoScreenFrame"

    mesh_task.automated = True
    mesh_task.replace_existing = True
    mesh_task.save = True
    mesh_task.factory = unreal.FbxFactory()
    options = unreal.FbxImportUI()
    options.import_mesh = True
    options.import_materials = False
    options.import_textures = False
    options.import_as_skeletal = False
    options.mesh_type_to_import = unreal.FBXImportType.FBXIT_STATIC_MESH
    options.automated_import_should_detect_type = False
    options.static_mesh_import_data.import_uniform_scale = 1.0
    options.static_mesh_import_data.convert_scene = False
    options.static_mesh_import_data.convert_scene_unit = False
    options.static_mesh_import_data.normal_import_method = unreal.FBXNormalImportMethod.FBXNIM_COMPUTE_NORMALS
    options.static_mesh_import_data.generate_lightmap_u_vs = False
    mesh_task.options = options

    icon_task = unreal.AssetImportTask()
    icon_task.filename = str(icon_path)
    icon_task.destination_path = "/InfoScreens/UI"
    icon_task.destination_name = "T_InfoScreenIcon"
    icon_task.automated = True
    icon_task.replace_existing = True
    icon_task.save = True
    tools.import_asset_tasks([mesh_task, icon_task])

    mesh = unreal.load_asset("/InfoScreens/Meshes/SM_InfoScreenFrame")
    icon = unreal.load_asset("/InfoScreens/UI/T_InfoScreenIcon")
    if not isinstance(mesh, unreal.StaticMesh) or not isinstance(icon, unreal.Texture2D):
        raise RuntimeError("InfoScreens mesh/texture import did not produce the required assets")

    material_path = "/InfoScreens/Materials/M_InfoScreenFrame"
    material = unreal.load_asset(material_path) if unreal.EditorAssetLibrary.does_asset_exist(material_path) else None
    if material is None:
        material = tools.create_asset("M_InfoScreenFrame", "/InfoScreens/Materials", unreal.Material, unreal.MaterialFactoryNew())
    if not isinstance(material, unreal.Material):
        raise RuntimeError("InfoScreens frame material path is occupied by a non-material asset")
    editing = unreal.MaterialEditingLibrary
    color = editing.get_material_property_input_node(material, unreal.MaterialProperty.MP_BASE_COLOR)
    if color is None:
        color = editing.create_material_expression(material, unreal.MaterialExpressionConstant3Vector, -300, 0)
    if not isinstance(color, unreal.MaterialExpressionConstant3Vector):
        raise RuntimeError("Frame base color must be a constant vector")
    color.set_editor_property("constant", unreal.LinearColor(0.035, 0.05, 0.065, 1.0))
    editing.connect_material_property(color, "", unreal.MaterialProperty.MP_BASE_COLOR)
    metallic = editing.get_material_property_input_node(material, unreal.MaterialProperty.MP_METALLIC)
    if metallic is None:
        metallic = editing.create_material_expression(material, unreal.MaterialExpressionConstant, -300, 120)
    if not isinstance(metallic, unreal.MaterialExpressionConstant):
        raise RuntimeError("Frame metallic input must be a scalar constant")
    metallic.set_editor_property("r", 0.6)
    editing.connect_material_property(metallic, "", unreal.MaterialProperty.MP_METALLIC)
    roughness = editing.get_material_property_input_node(material, unreal.MaterialProperty.MP_ROUGHNESS)
    if roughness is None:
        roughness = editing.create_material_expression(material, unreal.MaterialExpressionConstant, -300, 240)
    if not isinstance(roughness, unreal.MaterialExpressionConstant):
        raise RuntimeError("Frame roughness input must be a scalar constant")
    roughness.set_editor_property("r", 0.4)
    editing.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)
    editing.recompile_material(material)
    mesh.set_material(0, material)
    icon.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
    icon.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
    icon.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    icon.set_editor_property("srgb", True)
    for asset in (material, mesh, icon):
        if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
            raise RuntimeError(f"Could not save {asset.get_path_name()}")
    unreal.log("InfoScreens original frame, material and icon imported and saved")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-only", action="store_true", help="Generate OBJ and PNG without Unreal")
    args, _ = parser.parse_known_args()
    SOURCE_ART.mkdir(parents=True, exist_ok=True)
    mesh_path = make_mesh()
    icon_path = make_icon()
    if args.source_only:
        print(f"Generated {mesh_path} and {icon_path}")
    else:
        import_assets(mesh_path, icon_path)


if __name__ == "__main__":
    main()
