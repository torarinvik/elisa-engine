"""Small deterministic ASCII FBX fixtures for importer and cooker tests."""

from pathlib import Path


def write_two_mesh_scene(path: Path) -> None:
    """Write one triangle and one quad under distinct mesh and node names."""
    path.write_text(
        '; FBX 7.4.0 project file\n'
        'FBXHeaderExtension: { FBXHeaderVersion: 1003 FBXVersion: 7400 }\n'
        'GlobalSettings: { Version: 1000 Properties70: { '
        'P: "UpAxis", "int", "Integer", "", 1 '
        'P: "UpAxisSign", "int", "Integer", "", 1 '
        'P: "FrontAxis", "int", "Integer", "", 2 '
        'P: "FrontAxisSign", "int", "Integer", "", 1 '
        'P: "CoordAxis", "int", "Integer", "", 0 '
        'P: "CoordAxisSign", "int", "Integer", "", 1 '
        'P: "UnitScaleFactor", "double", "Number", "", 1 } }\n'
        'Definitions: { Version: 100 Count: 4 '
        'ObjectType: "Geometry" { Count: 2 } ObjectType: "Model" { Count: 2 } }\n'
        'Objects: { '
        'Geometry: 1001, "Geometry::SmallTriangle", "Mesh" { GeometryVersion: 124 '
        'Vertices: *9 { a: 0,0,0,1,0,0,0,1,0 } '
        'PolygonVertexIndex: *3 { a: 0,1,-3 } } '
        'Geometry: 1003, "Geometry::SelectedQuad", "Mesh" { GeometryVersion: 124 '
        'Vertices: *12 { a: 2,0,0,4,0,0,4,2,0,2,2,0 } '
        'PolygonVertexIndex: *4 { a: 0,1,2,-4 } } '
        'Model: 1002, "Model::SmallNode", "Mesh" { Version: 232 } '
        'Model: 1004, "Model::SelectedNode", "Mesh" { Version: 232 Properties70: { '
        'P: "Lcl Translation", "Lcl Translation", "", "A", 10,0,0 } } }\n'
        'Connections: { C: "OO",1001,1002 C: "OO",1002,0 '
        'C: "OO",1003,1004 C: "OO",1004,0 }\n'
        'Takes: { Current: "" }\n',
        encoding="ascii")


def write_two_material_mesh(path: Path, base_color_texture: str | None = None) -> None:
    """Write two triangles in one mesh with separate polygon material slots."""
    texture_definition = ('ObjectType: "Texture" { Count: 1 } ' if base_color_texture is not None else "")
    texture_object = ""
    texture_connection = ""
    texture_uv = ""
    texture_mesh_tail = "} } "
    definitions_count = 5 if base_color_texture is not None else 4
    if base_color_texture is not None:
        if not base_color_texture or any(character in base_color_texture for character in '\"\r\n\0'):
            raise ValueError("fixture texture path must be a non-empty quoted FBX path")
        texture_object = (
            f'Texture: 1105, "Texture::BaseColor", "" {{ Version: 202 '
            f'FileName: "{base_color_texture}" RelativeFilename: "{base_color_texture}" '
            'WrapModeU: 0 WrapModeV: 0 } ')
        texture_connection = ' C: "OP",1105,1103,"DiffuseColor"'
        texture_uv = (
            'LayerElementUV: 0 { Version: 101 Name: "UVMap" '
            'MappingInformationType: "ByVertice" ReferenceInformationType: "Direct" '
            'UV: *12 { a: 0,0,1,0,0,1,0,0,1,0,0,1 } } '
            'Layer: 0 { Version: 100 LayerElement: { Type: "LayerElementUV" TypedIndex: 0 } } ')
        texture_mesh_tail = "} " + texture_uv + "} "
    path.write_text(
        '; FBX 7.4.0 project file\n'
        'FBXHeaderExtension: { FBXHeaderVersion: 1003 FBXVersion: 7400 }\n'
        'GlobalSettings: { Version: 1000 Properties70: { '
        'P: "UpAxis", "int", "Integer", "", 1 '
        'P: "UpAxisSign", "int", "Integer", "", 1 '
        'P: "FrontAxis", "int", "Integer", "", 2 '
        'P: "FrontAxisSign", "int", "Integer", "", 1 '
        'P: "CoordAxis", "int", "Integer", "", 0 '
        'P: "CoordAxisSign", "int", "Integer", "", 1 '
        'P: "UnitScaleFactor", "double", "Number", "", 1 } }\n'
        f'Definitions: {{ Version: 100 Count: {definitions_count} '
        'ObjectType: "Geometry" { Count: 1 } ObjectType: "Model" { Count: 1 } '
        'ObjectType: "Material" { Count: 2 } ' + texture_definition + '}\n' +
        'Objects: { '
        'Geometry: 1101, "Geometry::TwoMaterial", "Mesh" { GeometryVersion: 124 '
        'Vertices: *18 { a: 0,0,0,1,0,0,0,1,0,2,0,0,3,0,0,2,1,0 } '
        'PolygonVertexIndex: *6 { a: 0,1,-3,3,4,-6 } '
        'LayerElementMaterial: 0 { Version: 101 Name: "" MappingInformationType: "ByPolygon" '
        'ReferenceInformationType: "IndexToDirect" Materials: *2 { a: 0,1 } ' + texture_mesh_tail +
        'Model: 1102, "Model::TwoMaterialNode", "Mesh" { Version: 232 } '
        'Material: 1103, "Material::First", "" { Version: 102 ShadingModel: "phong" Properties70: { '
        'P: "DiffuseColor", "Color", "", "A", 0.2,0.4,0.6 '
        'P: "DiffuseFactor", "Number", "", "A", 0.8 '
        'P: "Shininess", "Number", "", "A", 32 '
        'P: "EmissiveColor", "Color", "", "A", 0.3,0.2,0.1 '
        'P: "TransparencyFactor", "Number", "", "A", 0.25 } } '
        'Material: 1104, "Material::Second", "" { Version: 102 ShadingModel: "phong" Properties70: { '
        'P: "DiffuseColor", "Color", "", "A", 0.7,0.2,0.1 '
        'P: "Shininess", "Number", "", "A", 12 } } ' + texture_object + '}\n' +
        'Connections: { C: "OO",1101,1102 C: "OO",1102,0 '
        'C: "OO",1103,1102 C: "OO",1104,1102' + texture_connection + ' }\n' +
        'Takes: { Current: "" }\n',
        encoding="ascii")
