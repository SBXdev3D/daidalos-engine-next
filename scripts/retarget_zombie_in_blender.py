"""Blender: gives the hazmat zombie (a Mixamo rig) Quaternius' CC0 animations, and exports it without Mixamo's.

    blender -b --factory-startup -P scripts/retarget_zombie_in_blender.py -- hazmat.gltf ual1.glb ual2.glb out_dir

Run by scripts/make_hazmat_zombie.py. Each mapped bone takes the source bone's rotation away from its rest pose,
in world space; both rigs rest in a T-pose facing the same way, and the limbs are first turned to point where the
source's point at rest. The hips also take the source's movement, scaled by the ratio of leg lengths. The model is
stood on the ground and turned to face the way the game's models face, and exported as walk, wait, attack, die
and hit.
"""
import math
import os
import re
import sys

import bpy
from mathutils import Matrix, Quaternion, Vector

argv = sys.argv[sys.argv.index('--') + 1:]
HAZMAT, UAL1, UAL2, OUT = argv

# The game's clip name: (library, source clip). 'hit' is spare: the game does not play it yet.
CLIPS = {
    'walk': ('ual2', 'Zombie_Walk_Fwd_Loop'),
    'wait': ('ual2', 'Zombie_Idle_Loop'),
    'attack': ('ual2', 'Zombie_Scratch'),
    'die': ('ual1', 'Death01'),
    'hit': ('ual1', 'Hit_Chest'),
}

SIDES = {'Left': 'l', 'Right': 'r'}
BODY = {'Hips': 'pelvis', 'Spine': 'spine_01', 'Spine1': 'spine_02', 'Spine2': 'spine_03', 'Neck': 'neck_01',
        'Head': 'Head'}
LIMBS = {'Shoulder': 'clavicle', 'Arm': 'upperarm', 'ForeArm': 'lowerarm', 'Hand': 'hand', 'UpLeg': 'thigh',
         'Leg': 'calf', 'Foot': 'foot', 'ToeBase': 'ball'}
FINGERS = {'Thumb': 'thumb', 'Index': 'index', 'Middle': 'middle', 'Ring': 'ring', 'Pinky': 'pinky'}


def source_name(mixamo):
    """The Quaternius bone for a Mixamo one ('mixamorig:LeftForeArm_08' -> 'lowerarm_l'), or None."""
    m = re.match(r'mixamorig:([A-Za-z0-9]+?)(_\d+)?$', mixamo)
    if not m:
        return None
    name = m.group(1)
    if name in BODY:
        return BODY[name]
    for side, s in SIDES.items():
        if name.startswith(side):
            rest = name[len(side):]
            if rest in LIMBS:
                return f'{LIMBS[rest]}_{s}'
            m2 = re.match(r'Hand(Thumb|Index|Middle|Ring|Pinky)(\d)$', rest)
            if m2:
                return f'{FINGERS[m2.group(1)]}_0{m2.group(2)}_{s}'
    return None


bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
scene.render.fps = 30


def load(path):
    before_objects, before_actions = set(bpy.data.objects), set(bpy.data.actions)
    bpy.ops.import_scene.gltf(filepath=path)
    new = [o for o in bpy.data.objects if o not in before_objects]
    arm = next(o for o in new if o.type == 'ARMATURE')
    actions = {a.name: a for a in bpy.data.actions if a not in before_actions}
    return arm, new, actions


tgt, tgt_objects, mixamo_actions = load(HAZMAT)
sources = {}
for key, path in (('ual1', UAL1), ('ual2', UAL2)):
    sources[key] = load(path)

# Mixamo's clips go: none of them is exported.
if tgt.animation_data:
    tgt.animation_data.action = None
for action in mixamo_actions.values():
    bpy.data.actions.remove(action)

# Stand the zombie on the ground: its lowest vertex at height 0.
bpy.context.view_layer.update()
depsgraph = bpy.context.evaluated_depsgraph_get()
lowest = min((o.matrix_world @ v.co).z for o in tgt_objects if o.type == 'MESH'
             for v in o.evaluated_get(depsgraph).to_mesh().vertices)
for o in tgt_objects:
    if o.parent is None:
        o.location.z -= lowest
bpy.context.view_layer.update()
print(f'raised by {-lowest:.3f} m')

mapping = {b.name: source_name(b.name) for b in tgt.data.bones}
mapped = {t: s for t, s in mapping.items() if s}
print('mapped', len(mapped), 'of', len(tgt.data.bones), 'unmapped:', [t for t, s in mapping.items() if not s])


def world_rotation(arm, matrix):
    return (arm.matrix_world @ matrix).to_3x3().normalized().to_quaternion()


def bone_order(arm):
    order = []

    def visit(bone):
        order.append(bone)
        for child in bone.children:
            visit(child)

    for bone in arm.data.bones:
        if bone.parent is None:
            visit(bone)
    return order


order = bone_order(tgt)
tgt_world = tgt.matrix_world
tgt_world_inv = tgt_world.inverted()
tgt_rest_rot = {b.name: world_rotation(tgt, b.matrix_local) for b in tgt.data.bones}
hips_t = next(t for t, s in mapped.items() if s == 'pelvis')
foot_t = next(t for t, s in mapped.items() if s == 'foot_l')
hips_t_world = tgt_world @ tgt.data.bones[hips_t].head_local
leg_t = hips_t_world.z - (tgt_world @ tgt.data.bones[foot_t].head_local).z

for pb in tgt.pose.bones:
    pb.rotation_mode = 'QUATERNION'

for clip, (library, source_clip) in CLIPS.items():
    src, _, actions = sources[library]
    action = actions[source_clip]
    src.animation_data.action = action
    if hasattr(src.animation_data, 'action_slot') and src.animation_data.action_slot is None:
        src.animation_data.action_slot = action.slots[0]
    src_rest_rot = {b.name: world_rotation(src, b.matrix_local) for b in src.data.bones}
    src_world = src.matrix_world
    # The limbs first point where the source's point at rest: the hazmat's legs are splayed and its toes out.
    align = {}
    for target, source in mapped.items():
        align[target] = Quaternion()
        if source.startswith(('upperarm', 'lowerarm', 'hand', 'thigh', 'calf', 'foot', 'ball')):
            tb, sb = tgt.data.bones[target], src.data.bones[source]
            dt = (tgt_world.to_3x3() @ (tb.tail_local - tb.head_local)).normalized()
            ds = (src_world.to_3x3() @ (sb.tail_local - sb.head_local)).normalized()
            align[target] = dt.rotation_difference(ds)
    hips_s_rest = src_world @ src.data.bones['pelvis'].head_local
    leg_s = hips_s_rest.z - (src_world @ src.data.bones['foot_l'].head_local).z
    ratio = leg_t / leg_s

    out = bpy.data.actions.new(clip)
    tgt.animation_data_create()
    tgt.animation_data.action = out
    first, last = (int(round(f)) for f in action.frame_range)
    previous = {}
    for frame in range(first, last + 1):
        scene.frame_set(frame)
        pose = {}  # Target armature-space pose matrices, parents first.
        for bone in order:
            rest_rel = bone.matrix_local if bone.parent is None else \
                bone.parent.matrix_local.inverted() @ bone.matrix_local
            parent_pose = Matrix.Identity(4) if bone.parent is None else pose[bone.parent.name]
            unposed = parent_pose @ rest_rel
            pb = tgt.pose.bones[bone.name]
            s = mapped.get(bone.name)
            if s is None:
                pose[bone.name] = unposed
                pb.rotation_quaternion = (1, 0, 0, 0)
                continue
            spb = src.pose.bones[s]
            delta = world_rotation(src, spb.matrix) @ src_rest_rot[s].inverted()
            desired_world = delta @ align[bone.name] @ tgt_rest_rot[bone.name]
            desired = (tgt_world_inv.to_3x3().normalized().to_quaternion() @ desired_world).to_matrix().to_4x4()
            head = unposed.to_translation()
            if s == 'pelvis':
                moved = (src_world @ spb.head) - hips_s_rest
                head = tgt_world_inv @ (hips_t_world + moved * ratio)
            scale = unposed.to_scale()
            m = Matrix.Translation(head) @ desired @ Matrix.Diagonal(scale.to_4d())
            pose[bone.name] = m
            basis = unposed.inverted() @ m
            q = basis.to_quaternion()
            if bone.name in previous and previous[bone.name].dot(q) < 0:
                q.negate()
            previous[bone.name] = q
            pb.rotation_quaternion = q
            pb.keyframe_insert('rotation_quaternion', frame=frame - first + 1, group=bone.name)
            if s == 'pelvis':
                pb.location = basis.to_translation()
                pb.keyframe_insert('location', frame=frame - first + 1, group=bone.name)
    out.use_fake_user = True
    print(f'clip {clip} <- {source_clip}: {last - first + 1} frames, legs x{ratio:.2f}')
    tgt.animation_data.action = None

# The game's models face -Z in glTF (+Y in Blender): turn it round, its clips with it.
turn = Matrix.Rotation(math.pi, 4, 'Z')
for o in tgt_objects:
    if o.parent is None:
        o.matrix_world = turn @ o.matrix_world

# Only the zombie and its new clips are exported.
for key, (arm, objects, actions) in sources.items():
    for o in objects:
        bpy.data.objects.remove(o, do_unlink=True)
    for a in actions.values():
        bpy.data.actions.remove(a)
for o in tgt_objects:
    o.select_set(True)
os.makedirs(OUT, exist_ok=True)
bpy.ops.export_scene.gltf(filepath=os.path.join(OUT, 'zombie_hazmat.gltf'), export_format='GLTF_SEPARATE',
                          export_texture_dir='textures', use_selection=True, export_animation_mode='ACTIONS',
                          export_skins=True, export_tangents=True, export_apply=False,
                          export_vertex_color='NONE')
print('exported', [a.name for a in bpy.data.actions])
