"""Configure exact existing wall surfaces and author their patterns. Run only in Editor Python.

-BreachWallPatternsVerify performs read-only verification. No map/weapon or new BSM is saved.
"""
import json
import math
import struct
import zlib
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.project_dir()).resolve()
OUT = ROOT / 'Saved/Automation/WallMaterialPatterns'
OUT.mkdir(parents=True, exist_ok=True)
MAT = '/BreachSystem/SurfaceSet/Materials'
TEX = '/BreachSystem/SurfaceSet/Textures'
PAT = '/BreachSystem/PatternExamples'
SURFACES = {'Brick':'SurfaceMaterial003_Brick', 'Wood':'SurfaceMaterial001_Wood', 'Concrete':'SurfaceMaterial002_Concrete'}
L = unreal.MaterialEditingLibrary
A = unreal.AssetToolsHelpers.get_asset_tools()
VERIFY = '-BreachWallPatternsVerify' in unreal.SystemLibrary.get_command_line()
pending = []
unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous([MAT,TEX,PAT,'/BreachSystem/SurfaceMaterial'], True)

# Observed mortar centers in the actual 1024px T_WallBrick_BaseColor, not a synthetic running bond.
YS = [0,122.5,246,366,477.5,588.5,695.5,804,912.5,1024]
XS = [[0,253,521.5,775.5,1024], [0,206.5,479.5,820,1024], [0,263.5,519,776,1024],
      [0,220,486.5,757,1024], [0,271,523.5,800.5,1024], [0,215,495,765,1024],
      [0,260.5,521,798,1024], [0,211,484.5,766,1024], [0,261,525.5,795,1024]]
BRICK_ONLY = '-BreachBrickMortarOnly' in unreal.SystemLibrary.get_command_line()


def brick_regions():
    regions = []
    for row in range(9):
        # Include adjacent-row junctions on shared horizontal edges, including tile seams.
        top_x = sorted(set(XS[row] + XS[(row-1) % 9]))
        bottom_x = sorted(set(XS[row] + XS[(row+1) % 9]))
        for col in range(4):
            x0, x1 = XS[row][col:col+2]
            top = [(x, YS[row]) for x in top_x if x0 <= x <= x1]
            bottom = [(x, YS[row+1]) for x in reversed(bottom_x) if x0 <= x <= x1]
            region = unreal.BreachPatternRegion()
            region.set_editor_property('outer', [unreal.Vector2D(x/1024, y/1024) for x,y in top+bottom])
            regions.append(region)
    return regions

def require(value, message):
    if not value:
        raise RuntimeError(message)
    return value

def load(path):
    return require(unreal.load_asset(path), 'Missing ' + path)

def node(material, cls, **props):
    item = L.create_material_expression(material, cls)
    for key, value in props.items():
        item.set_editor_property(key, value)
    return item

def connect(src, dst, pin, output=''):
    names = list(L.get_material_expression_input_names(dst))
    if pin == 'Input' and pin not in names and len(names) == 1:
        pin = names[0]
    require(L.connect_material_expressions(src, output, dst, pin), 'Connection failed: '+dst.get_class().get_name()+'.'+pin+' available='+str(names))

def scalar(material, name, value):
    return node(material, unreal.MaterialExpressionScalarParameter, parameter_name=name, default_value=value)

def build_parent(material):
    expressions = list(L.get_material_expressions(material))
    if any(isinstance(e, unreal.MaterialExpressionScalarParameter) and str(e.get_editor_property('parameter_name')) == 'BreachUseWorldSizeUV' for e in expressions):
        return
    samples = [e for e in expressions if isinstance(e, unreal.MaterialExpressionTextureSample)]
    require(len(samples) == 2 and len(expressions) == 3, 'Unexpected existing graph, refusing replacement: '+material.get_path_name())
    world = node(material, unreal.MaterialExpressionWorldPosition)
    local = node(material, unreal.MaterialExpressionTransformPosition,
                 transform_source_type=unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_WORLD,
                 transform_type=unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_LOCAL)
    connect(world, local, 'Input')
    scale = node(material, unreal.MaterialExpressionMaterialFunctionCall)
    require(scale.set_material_function(load('/Engine/Functions/Engine_MaterialFunctions02/WorldPositionOffset/ObjectScale')), 'ObjectScale function setup failed')
    outputs = list(L.get_material_expression_output_names(scale))
    unreal.log('Breach ObjectScale outputs: '+str(outputs))
    absolute = node(material, unreal.MaterialExpressionAbs)
    connect(scale, absolute, 'Input', outputs[0] if outputs else '')
    physical = node(material, unreal.MaterialExpressionMultiply)
    connect(local, physical, 'A')
    connect(absolute, physical, 'B')
    u = node(material, unreal.MaterialExpressionComponentMask, r=True, g=False, b=False, a=False)
    v = node(material, unreal.MaterialExpressionComponentMask, r=False, g=False, b=True, a=False)
    connect(physical, u, 'Input')
    connect(physical, v, 'Input')
    du = node(material, unreal.MaterialExpressionDivide)
    dv = node(material, unreal.MaterialExpressionDivide)
    connect(u, du, 'A')
    connect(v, dv, 'A')
    connect(scalar(material, 'BreachTileSizeU', 100.), du, 'B')
    connect(scalar(material, 'BreachTileSizeV', 100.), dv, 'B')
    negative_v = node(material, unreal.MaterialExpressionMultiply, const_b=-1.)
    connect(dv, negative_v, 'A')
    add_u = node(material, unreal.MaterialExpressionAdd, const_b=.5)
    add_v = node(material, unreal.MaterialExpressionAdd, const_b=.5)
    connect(du, add_u, 'A')
    connect(negative_v, add_v, 'A')
    uv = node(material, unreal.MaterialExpressionAppendVector)
    connect(add_u, uv, 'A')
    connect(add_v, uv, 'B')
    source = node(material, unreal.MaterialExpressionTextureCoordinate)
    choice = node(material, unreal.MaterialExpressionLinearInterpolate, desc='BreachPhysicalUV: local XZ, physical cm, origin center')
    connect(source, choice, 'A')
    connect(uv, choice, 'B')
    connect(scalar(material, 'BreachUseWorldSizeUV', 0.), choice, 'Alpha')
    for old in samples:
        texture = old.get_editor_property('texture')
        normal = str(texture.get_name()).endswith('_Normal')
        param = node(material, unreal.MaterialExpressionTextureSampleParameter2D,
                     parameter_name='Normal' if normal else 'BaseColor', texture=texture,
                     sampler_type=old.get_editor_property('sampler_type'))
        connect(choice, param, 'UVs')
        require(L.connect_material_property(param, 'RGB', unreal.MaterialProperty.MP_NORMAL if normal else unreal.MaterialProperty.MP_BASE_COLOR), 'Material output connection failed')
        L.delete_material_expression(material, old)
    L.layout_material_expressions(material)
    errors = L.recompile_material(material)
    require(not errors, 'Material compilation failed: '+str(errors))
    pending.append(material)

def write_ids():
    width = height = 2048
    colors = [((i*79+31)%256, (i*137+67)%256, (i*193+113)%256) for i in range(36)]
    require(len(set(colors)) == 36, 'Duplicate region color')
    raw = bytearray()
    for y in range(height):
        row = next(r for r in range(9) if 2*YS[r] <= y < 2*YS[r+1])
        raw.append(0)
        for col in range(4):
            raw.extend(bytes(colors[row*4+col]) * int(2*(XS[row][col+1]-XS[row][col])))
    def chunk(kind, data):
        return struct.pack('>I', len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data)&0xffffffff)
    path = OUT/'T_WallBrick_RegionIDs.png'
    path.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',width,height,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND',b''))
    return path

def configure():
    rule_path = TEX+'/T_WallBrick_RegionIDs'
    task = unreal.AssetImportTask()
    for key,value in {'filename':str(write_ids()),'destination_path':TEX,'destination_name':'T_WallBrick_RegionIDs','automated':True,'replace_existing':True,'save':False}.items():
        task.set_editor_property(key,value)
    A.import_asset_tasks([task])
    rule = load(rule_path)
    rule.set_editor_property('srgb',False)
    rule.set_editor_property('compression_settings',unreal.TextureCompressionSettings.TC_VECTOR_DISPLACEMENTMAP)
    rule.set_editor_property('filter',unreal.TextureFilter.TF_NEAREST)
    rule.set_editor_property('mip_gen_settings',unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    pending.append(rule)
    if BRICK_ONLY:
        pattern = load(PAT+'/BPAT_Brick')
        require(pattern.get_editor_property('reference_texture') == load(TEX+'/T_WallBrick_BaseColor'), 'Unexpected brick background')
        pattern.set_editor_property('regions', brick_regions())
        pattern.set_editor_property('guides', [])
        pattern.set_editor_property('mode', unreal.BreachPatternMode.REGIONS)
        pattern.set_editor_property('authoring_type', unreal.BreachPatternAuthoringType.POINTS_AND_LINES)
        pattern.set_editor_property('rule_texture', rule)
        pending.append(pattern)
        return
    for kind,surface_name in SURFACES.items():
        parent = load(MAT+'/M_Wall'+kind)
        build_parent(parent)
        mi_path = MAT+'/MI_Wall'+kind
        mi = unreal.load_asset(mi_path) if unreal.EditorAssetLibrary.does_asset_exist(mi_path) else A.create_asset('MI_Wall'+kind,MAT,unreal.MaterialInstanceConstant,unreal.MaterialInstanceConstantFactoryNew())
        require(isinstance(mi,unreal.MaterialInstanceConstant),'Unexpected MI asset type')
        old_parent = mi.get_editor_property('parent')
        require(old_parent is None or old_parent == parent,'Unexpected MI parent, refusing overwrite')
        L.set_material_instance_parent(mi,parent)
        for name,value in [('BreachUseWorldSizeUV',1.),('BreachTileSizeU',100.),('BreachTileSizeV',100.)]:
            # UE 5.8.2's setter always returns false; verify the resulting value instead.
            L.set_material_instance_scalar_parameter_value(mi,name,value)
            require(abs(L.get_material_instance_scalar_parameter_value(mi,name)-value)<1.e-6,'MI parameter failed '+name)
        L.update_material_instance(mi)
        pending.append(mi)
        surface = load('/BreachSystem/SurfaceMaterial/'+surface_name)
        pattern = require(surface.get_editor_property('pattern'),'Existing surface pattern required: '+surface_name)
        require(pattern.get_path_name().startswith(PAT+'/'),'Unexpected authored pattern owner')
        pattern.set_editor_property('reference_texture',load(TEX+'/T_Wall'+kind+'_BaseColor'))
        pattern.set_editor_property('preview_surface_material',surface)
        pattern.set_editor_property('authoring_type',unreal.BreachPatternAuthoringType.POINTS_AND_LINES)
        if kind == 'Brick':
            regions=brick_regions()
            pattern.set_editor_property('mode',unreal.BreachPatternMode.REGIONS)
            pattern.set_editor_property('regions',regions)
            pattern.set_editor_property('guides',[])
            pattern.set_editor_property('rule_texture',rule)
        elif kind == 'Wood':
            guides=[]
            for i,y in enumerate([.07,.15,.235,.32,.405,.50,.605,.72,.83,.945]):
                guide=unreal.BreachPatternGuide()
                guide.set_editor_property('points',[unreal.Vector2D(x/8,y+math.sin(x*.8+i)*.008) for x in range(9)])
                guides.append(guide)
            pattern.set_editor_property('mode',unreal.BreachPatternMode.GRAIN_GUIDES)
            pattern.set_editor_property('regions',[])
            pattern.set_editor_property('guides',guides)
            pattern.set_editor_property('rule_texture',None)
        else:
            pattern.set_editor_property('mode',unreal.BreachPatternMode.NONE)
            pattern.set_editor_property('regions',[])
            pattern.set_editor_property('guides',[])
            pattern.set_editor_property('rule_texture',None)
        surface.set_editor_property('display_material',mi)
        surface.set_editor_property('use_world_size_uv',True)
        surface.set_editor_property('tile_size_cm',unreal.Vector2D(100,100))
        pending.extend([pattern,surface])

def inspect():
    rows=[]
    for kind,surface_name in SURFACES.items():
        surface=load('/BreachSystem/SurfaceMaterial/'+surface_name)
        pattern=require(surface.get_editor_property('pattern'),'Missing pattern')
        mi=require(surface.get_editor_property('display_material'),'Missing display instance')
        require(isinstance(mi,unreal.MaterialInstanceConstant),'Display must be an instance')
        require(mi.get_editor_property('parent')==load(MAT+'/M_Wall'+kind),'Instance parent mismatch')
        require(surface.get_editor_property('use_world_size_uv'),'Physical tiling disabled')
        require(abs(L.get_material_instance_scalar_parameter_value(mi,'BreachUseWorldSizeUV')-1)<1.e-6,'Instance tiling not enabled')
        ref=pattern.get_editor_property('reference_texture')
        require(ref==load(TEX+'/T_Wall'+kind+'_BaseColor'),'Pattern source texture mismatch')
        regions=pattern.get_editor_property('regions')
        guides=pattern.get_editor_property('guides')
        mode=pattern.get_editor_property('mode')
        if kind=='Brick':
            require(mode==unreal.BreachPatternMode.REGIONS and len(regions)==36 and not guides,'Brick lines mismatch')
            require(pattern.get_editor_property('rule_texture')==load(TEX+'/T_WallBrick_RegionIDs'),'Missing ID example')
            expected = brick_regions()
            for index, region in enumerate(regions):
                actual = list(region.get_editor_property('outer'))
                target = list(expected[index].get_editor_property('outer'))
                require(len(actual) == len(target), 'Brick junction count mismatch')
                require(all(abs(a.x-b.x)<1e-8 and abs(a.y-b.y)<1e-8 for a,b in zip(actual,target)), 'Approved mortar coordinates mismatch')

        elif kind=='Wood':
            require(mode==unreal.BreachPatternMode.GRAIN_GUIDES and len(guides)==10 and not regions,'Wood guide mismatch')
            for guide in guides:
                pts=guide.get_editor_property('points')
                require(abs(pts[-1].x-pts[0].x)>.9 and abs(pts[-1].y-pts[0].y)<.05,'Wood guides must follow actual horizontal grain')
        else:
            require(mode==unreal.BreachPatternMode.NONE,'Concrete must retain general contour')
        rows.append({'kind':kind,'surface':surface.get_path_name(),'pattern':pattern.get_path_name(),'instance':mi.get_path_name(),'source':ref.get_path_name(),'mode':str(mode),'regions':len(regions),'guides':len(guides)})
    return rows

try:
    if not VERIFY:
        configure()
    rows=inspect()
    if not VERIFY:
        allowed=[TEX+'/T_WallBrick_RegionIDs', PAT+'/BPAT_Brick'] if BRICK_ONLY else [MAT+'/',TEX+'/T_WallBrick_RegionIDs',PAT+'/','/BreachSystem/SurfaceMaterial/']
        for asset in dict.fromkeys(pending):
            require(any(asset.get_path_name().startswith(prefix) for prefix in allowed),'Save ownership violation')
            require(unreal.EditorAssetLibrary.save_loaded_asset(asset),'Save failed: '+asset.get_path_name())
    (OUT/('mortar-verified.json' if VERIFY and BRICK_ONLY else 'mortar-configured.json' if BRICK_ONLY else 'verified.json' if VERIFY else 'configured.json')).write_text(json.dumps({'assets':rows,'saved':[] if VERIFY else [p.get_path_name() for p in dict.fromkeys(pending)],'map_saved':False,'new_surface_materials':[]},indent=2),encoding='utf8')
    unreal.log('WALL_PATTERNS_OK')
except Exception:
    import traceback
    (OUT/'configuration-error.txt').write_text(traceback.format_exc(),encoding='utf8')
    raise
