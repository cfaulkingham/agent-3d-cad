"""Independent XML parsing and FK against native posed solids; stdlib only.

No generated ledger frames are used to compute expected placements. URDF and
SDF are each parsed using their own frame conventions, then compared to native
queries and source mesh vertices. Physical values below are test data only.
"""
import copy
import hashlib
import json
import math
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import xml.etree.ElementTree as ET

EXE = str(Path(sys.argv[1]).resolve())
checks = 0


def check(value, message):
    global checks
    checks += 1
    assert value, message


def identity():
    return [[float(i == j) for j in range(4)] for i in range(4)]


def mul(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(4)) for j in range(4)] for i in range(4)]


def inverse(m):
    out = identity()
    for i in range(3):
        for j in range(3):
            out[i][j] = m[j][i]
        out[i][3] = -sum(m[j][i] * m[j][3] for j in range(3))
    return out


def frame(xyz, rpy):
    x, y, z = xyz
    r, p, yv = rpy
    cr, sr, cp, sp, cy, sy = math.cos(r), math.sin(r), math.cos(p), math.sin(p), math.cos(yv), math.sin(yv)
    return [[cy*cp, cy*sp*sr-sy*cr, cy*sp*cr+sy*sr, x],
            [sy*cp, sy*sp*sr+cy*cr, sy*sp*cr-cy*sr, y],
            [-sp, cp*sr, cp*cr, z], [0, 0, 0, 1]]


def vec(text):
    return [float(v) for v in text.split()]


def urdf_origin(element):
    return identity() if element is None else frame(vec(element.get('xyz', '0 0 0')), vec(element.get('rpy', '0 0 0')))


def sdf_origin(element):
    v = vec(element.text)
    return frame(v[:3], v[3:])


def read_robot(path):
    root = ET.parse(path).getroot()
    sdf = root.tag == 'sdf'
    if sdf:
        check(root.get('version') == '1.12', 'SDF version is explicit')
        root = root.find('model')
    links = {e.get('name'): e for e in root.findall('link')}
    rests = {'world': identity()}
    if sdf:
        for name, link in links.items():
            check(link.find('pose').get('relative_to') == '__model__', 'Explicit model-relative link pose')
            rests[name] = sdf_origin(link.find('pose'))
    joints = {}
    for e in root.findall('joint'):
        name, kind = e.get('name'), e.get('type')
        if sdf:
            parent, child = e.findtext('parent'), e.findtext('child')
            check(e.find('pose').get('relative_to') == child, 'Joint expressed in its child frame')
            check(sdf_origin(e.find('pose')) == identity(), 'Joint frame is child frame')
            origin = mul(inverse(rests[parent]), rests[child])
        else:
            parent, child = e.find('parent').get('link'), e.find('child').get('link')
            origin = urdf_origin(e.find('origin'))
        joint = dict(kind=kind, parent=parent, child=child, origin=origin)
        if kind != 'fixed':
            axis = vec(e.findtext('axis/xyz')) if sdf else vec(e.find('axis').get('xyz'))
            check(axis == [0, 0, 1], 'Expected explicit local Z axis')
            limit = e.find('axis/limit') if sdf else e.find('limit')
            joint['limits'] = {k: float(limit.findtext(k) if sdf else limit.get(k)) for k in ['lower', 'upper', 'effort', 'velocity']}
            mimic = e.find('axis/mimic') if sdf else e.find('mimic')
            if mimic is not None:
                if sdf:
                    check(mimic.get('axis') == 'axis' and float(mimic.findtext('reference')) == 0, 'SDF mimic source axis and reference')
                joint['mimic'] = dict(joint=mimic.get('joint'), multiplier=float(mimic.findtext('multiplier') if sdf else mimic.get('multiplier')), offset=float(mimic.findtext('offset') if sdf else mimic.get('offset')))
        check(name not in joints, 'Unique joint names')
        joints[name] = joint
    return sdf, links, joints


def forward(joints, supplied):
    values = {}
    def coordinate(name):
        if name in values:
            return values[name]
        joint = joints[name]
        if 'mimic' in joint:
            mimic = joint['mimic']
            value = coordinate(mimic['joint'])*mimic['multiplier']+mimic['offset']
        else:
            value = supplied.get(name, 0)
        if joint['kind'] != 'fixed':
            check(joint['limits']['lower']-1e-10 <= value <= joint['limits']['upper']+1e-10, 'Exported limits contain pose')
        values[name] = value
        return value
    world = {'world': identity()}
    pending = dict(joints)
    while pending:
        progress = False
        for name, joint in list(pending.items()):
            if joint['parent'] not in world:
                continue
            q = coordinate(name)
            motion = frame([0, 0, q], [0, 0, 0]) if joint['kind'] == 'prismatic' else frame([0, 0, 0], [0, 0, q]) if joint['kind'] == 'revolute' else identity()
            check(joint['child'] not in world, 'One parent per link')
            world[joint['child']] = mul(mul(world[joint['parent']], joint['origin']), motion)
            del pending[name]
            progress = True
        check(progress, 'Acyclic connected robot tree')
    return world, values


def plane(origin, normal=(0, 0, 1), x=(1, 0, 0)):
    return dict(origin=origin, normal=normal, x_direction=x)


def fixture():
    mates = [dict(id='hinge', type='revolute', parent='base', child='arm', angle_deg=30, angle_limits_deg=[-40, 110],
                  parent_frame=plane([3, 4, 5], (0, 1, 0)), child_frame=plane([1, 2, 3], (1, 0, 0), (0, 1, 0)), offset=[2, -3, 4]),
             dict(id='rail', type='slider', parent='arm', child='slide', travel_limits_mm=[-10, 10], angle_deg=17,
                  parent_frame=plane([8, 2, -1], (1, 0, 0), (0, 0, 1)), child_frame=plane([-2, 5, 1])),
             dict(id='shaft', type='cylindrical', parent='slide', child='shaft_part', angle_limits_deg=[-80, 100], travel_limits_mm=[-4, 20], travel_mm=6,
                  parent_frame=plane([7, 6, -2]), child_frame=plane([4, 1, 3], (0, -1, 0))),
             dict(id='bolt', type='rigid', parent='shaft_part', child='bolt_part', angle_deg=-24, offset=[-1, 2, 5],
                  parent_frame=plane([-1, 8, 4], (0, 0, -1)), child_frame=plane([2, -4, 1]))]
    coupling = lambda id, source, target, ratio, offset: dict(id=id, source=dict(mate_id=source, coordinate='angle_deg'), target=dict(mate_id=target, coordinate='travel_mm' if target=='rail' else 'angle_deg'), ratio=ratio, offset=offset)
    values = lambda angle, travel: [dict(mate_id='hinge', coordinate='angle_deg', value=angle), dict(mate_id='shaft', coordinate='travel_mm', value=travel)]
    parts = [dict(id=id, input='box') for id in ['base', 'arm', 'slide', 'shaft_part', 'bolt_part', 'island']]
    parts[0]['placement'] = dict(translation=[10, -20, 30], rotation=dict(origin=[1, 2, 3], axis=[0, 1, 0], angle_deg=90))
    parts[-1]['placement'] = dict(translation=[-20, 10, 30], rotation=dict(origin=[0, 0, 0], axis=[1, 2, 3], angle_deg=-31))
    assembly = dict(id='mechanism', type='assembly', parts=list(reversed(parts)), mates=list(reversed(mates)),
                    couplings=[coupling('feed', 'hinge', 'rail', .05, 2), coupling('gear', 'hinge', 'shaft', -.5, 10)],
                    poses=[dict(id='home', values=values(0, 0)), dict(id='extended', values=values(100, 18))])
    model = dict(schema_version=1, units='mm', parameters={}, features=[dict(id='box', type='box', size=[10, 7, 4]), assembly], output='mechanism')
    properties = [dict(mate_id=id, coordinate=c, effort=5, velocity=.2) for id, c in [('hinge', 'angle_deg'), ('rail', 'travel_mm'), ('shaft', 'angle_deg'), ('shaft', 'travel_mm')]]
    inertials = [dict(link=name, mass_kg=.1, center_of_mass_m=[.001, -.002, .003], inertia_kg_m2=[.00002, .00003, .00004, .000001, -.000001, .000002]) for name in ['part_'+p['id'] for p in parts]+['carrier_shaft']]
    return model, dict(format='urdf', joint_properties=properties, inertials=inertials)


with tempfile.TemporaryDirectory(prefix='cad-robot-') as workspace:
    def call(tool, args, expected=None):
        for attempt in range(250):
            run = subprocess.run([EXE, 'call', tool, '--workspace', workspace, '--input', '-'], input=json.dumps(args), text=True, capture_output=True, timeout=45)
            if run.returncode and json.loads(run.stderr)['error']['code'] == 'workspace_busy':
                time.sleep(.02)
                continue
            break
        if expected:
            check(run.returncode != 0 and json.loads(run.stderr)['error']['code'] == expected, run.stdout+run.stderr)
            return json.loads(run.stderr)['error']
        check(run.returncode == 0, run.stderr)
        return json.loads(run.stdout)

    model, options = fixture()
    created = call('cad_create', dict(document_id='robot', model=model))
    saved = Path(workspace, 'exports')
    results = {}
    for fmt in ['urdf', 'srdf', 'sdf']:
        opts = dict(options, format=fmt)
        result = call('cad_robot_export', dict(document_id='robot', revision=1, robot=opts))
        results[fmt] = result
        root = Path(result['directory'])
        manifest = json.loads(Path(result['manifest_path']).read_text())
        for artifact in manifest['artifacts']:
            path = root / artifact['path']
            check(path.is_file() and not Path(artifact['path']).is_absolute(), 'Portable complete manifest')
            check(hashlib.sha256(path.read_bytes()).hexdigest() == artifact['sha256'], 'Artifact hash matches')
        relocated = Path(workspace)/f'relocated-{fmt}'
        shutil.copytree(root, relocated)
        root = relocated
        check(len(list((root/'meshes').glob('*.stl'))) == 1, 'Repeated source mesh exported once')
        blob = (root/'meshes/mesh_0.stl').read_bytes()
        n = struct.unpack_from('<I', blob, 80)[0]
        check(len(blob) == 84+n*50, 'Native binary STL structure')
        vertices = [struct.unpack_from('<3f', blob, 84+i*50+12+j*12) for i in range(n) for j in range(3)]
        check([max(v[k] for v in vertices)-min(v[k] for v in vertices) for k in range(3)] == [10, 7, 4], 'Mesh retains source mm coordinates')
        sdf, links, joints = read_robot(root/('model.sdf' if fmt=='sdf' else 'model.urdf'))
        check(len([j for j in joints.values() if j['kind']!='fixed']) == 4, 'Both cylindrical coordinates preserved')
        for supplied in opts['inertials']:
            inertial = links[supplied['link']].find('inertial')
            mass = float(inertial.findtext('mass')) if sdf else float(inertial.find('mass').get('value'))
            check(mass == supplied['mass_kg'], 'Supplied mass preserved in kg')
            com = sdf_origin(inertial.find('pose')) if sdf else urdf_origin(inertial.find('origin'))
            check(com == frame(supplied['center_of_mass_m'], [0,0,0]), 'COM and inertia axes preserved in link frame')
            tensor = inertial.find('inertia')
            numbers = [float(tensor.findtext(k) if sdf else tensor.get(k)) for k in ['ixx','iyy','izz','ixy','ixz','iyz']]
            check(numbers == supplied['inertia_kg_m2'], 'Supplied tensor preserved in kg m²')
        if not sdf:
            srdf = ET.parse(root/'model.srdf').getroot()
            check(srdf.get('name') == ET.parse(root/'model.urdf').getroot().get('name'), 'SRDF matches URDF')
            check(not srdf.findall('disable_collisions') and not srdf.findall('end_effector'), 'No invented planning semantics')
        poses = [('rest', 30, 6), ('home', 0, 0), ('extended', 100, 18), ('sample', -20, -2), ('sample2', 71, 11)]
        for pose_name, angle, travel in poses:
            q = {'mate_hinge_angle': math.radians(angle-30), 'mate_shaft_travel': (travel-6)/1000}
            worlds, coordinates = forward(joints, q)
            edits = [dict(op='set_joint_value', assembly_id='mechanism', mate_id=id, coordinate=c, value=v) for id,c,v in [('hinge','angle_deg',angle),('shaft','travel_mm',travel)]]
            native = call('cad_preview', dict(document_id='robot', expected_revision=1, operations=edits, kind='mesh'))
            for part in native['summary']['assembly']['parts']:
                link = links['part_'+part['id']]
                visual = link.find('visual')
                mesh = visual.find('geometry/mesh')
                mesh_path = mesh.findtext('uri') if sdf else mesh.get('filename')
                check((root/mesh_path).is_file(), 'Relative mesh resolves')
                check(vec(mesh.findtext('scale') if sdf else mesh.get('scale')) == [.001]*3, 'Explicit millimeter-to-meter scaling')
                visual_origin = sdf_origin(visual.find('pose')) if sdf else urdf_origin(visual.find('origin'))
                actual = mul(worlds['part_'+part['id']], visual_origin)
                expected = [part['transform'][i:i+4] for i in range(0,16,4)]
                for i in range(3):
                    expected[i][3] /= 1000
                check(max(abs(actual[i][j]-expected[i][j]) for i in range(4) for j in range(4)) < 2e-10, f'{fmt}/{pose_name}/{part["id"]}: independently parsed FK matches native')
                collision = link.find('collision')
                check(ET.tostring(visual.find('geometry')) == ET.tostring(collision.find('geometry')), 'Visual/collision meshes agree')
                collision_origin = sdf_origin(collision.find('pose')) if sdf else urdf_origin(collision.find('origin'))
                check(collision_origin == visual_origin, 'Collision frame agrees with native geometry too')
            if not sdf and pose_name in ['home','extended']:
                state = srdf.find(f'group_state[@name="{pose_name}"]')
                check(state is not None, 'Named pose exported')
                for item in state.findall('joint'):
                    check('mimic' not in joints[item.get('name')], 'SRDF pose does not override mimic joints')
                    check(abs(float(item.get('value'))-coordinates[item.get('name')]) < 1e-10, 'SRDF pose includes converted independent coordinate')
                check(len(state.findall('joint')) == 2, 'Both independent coordinates in named pose')
        check(call('cad_read', dict(document_id='robot'))['revision'] == 1, 'Export and inspection preserve HEAD')

    # Invalid physical data must leave both HEAD and published bundles unchanged.
    before = set(saved.iterdir())
    bad_options = [dict(options, joint_properties=options['joint_properties'][:-1]), dict(options, format='sdf', inertials=options['inertials'][:-1])]
    for tensor in [[1,1,10,0,0,0], [1,1,1,2,0,0], [0,0,0,0,0,0]]:
        bad = copy.deepcopy(options);bad['inertials'][0]['inertia_kg_m2'] = tensor;bad_options.append(bad)
    bad = copy.deepcopy(options);bad['joint_properties'][0]['velocity'] = 0;bad_options.append(bad)
    bad = copy.deepcopy(options);bad['inertials'][0]['link'] = 'part_missing';bad_options.append(bad)
    for bad in bad_options:
        call('cad_robot_export', dict(document_id='robot', revision=1, robot=bad), 'invalid_argument')
        check(set(saved.iterdir()) == before, 'Failed export publishes no files and cleans staging')
    call('cad_robot_export', dict(document_id='robot', revision=1, feature_id='box', robot=options), 'invalid_argument')
    # URDF intentionally permits kinematics-only exports without invented inertia.
    kinematic = call('cad_robot_export', dict(document_id='robot', revision=1, robot={k:v for k,v in options.items() if k!='inertials'}))
    check(not ET.parse(kinematic['path']).getroot().findall('link/inertial'), 'No inferred URDF inertials')
    # Rigid forests have no physical joint properties, and remain valid exports.
    rigid = copy.deepcopy(model);rigid['features'][-1].pop('mates');rigid['features'][-1].pop('poses');rigid['features'][-1].pop('couplings')
    call('cad_create', dict(document_id='rigid', model=rigid))
    r = call('cad_robot_export', dict(document_id='rigid', revision=1, robot=dict(format='urdf', joint_properties=[])))
    check(all(j.get('type')=='fixed' for j in ET.parse(r['path']).getroot().findall('joint')), 'Rigid assembly exports without moving coordinates')
    # Asynchronous path and immutable historical export after a committed pose edit.
    call('cad_apply', dict(document_id='robot', expected_revision=1, operations=[dict(op='apply_pose', assembly_id='mechanism', pose_id='extended')]))
    job = call('cad_job', dict(action='submit', request_id='robot_export_job', tool='cad_robot_export', arguments=dict(document_id='robot', revision=1, robot=options)))
    for _ in range(1500):
        job = call('cad_job', dict(action='get', job_id=job['job_id']))
        if job['state'] not in ['queued','running','cancelling']:
            break
        time.sleep(.01)
    check(job['state']=='succeeded', 'Robot export works through bounded jobs')
    check(Path(job['result']['path']).read_bytes() == Path(results['urdf']['path']).read_bytes(), 'Historical export preserves frames after later pose commit')
    check(call('cad_read', dict(document_id='robot'))['revision']==2, 'Historical export preserves current HEAD')

    # Three-level composition: repeated articulated definitions, independently
    # posed definitions, moving whole subassemblies and multiple grounded roots.
    # Expected placements come from native geometry, never exported frame data.
    nested = copy.deepcopy(model)
    alternate = copy.deepcopy(model['features'][1]);alternate['id'] = 'other_mechanism'
    for mate in alternate['mates']:
        if mate['id'] == 'hinge': mate['angle_deg'] = -10
        if mate['id'] == 'shaft': mate['travel_mm'] = 2
    nested['features'].append(alternate)
    machine = dict(id='machine', type='assembly', parts=[
        dict(id='left', input='mechanism', placement=dict(translation=[10,80,-20], rotation=dict(origin=[3,4,2],axis=[1,2,3],angle_deg=17))),
        dict(id='right', input='mechanism'), dict(id='other',input='other_mechanism'),
        dict(id='spare', input='box',placement=dict(translation=[200,30,-20]))], mates=[
        dict(id='swing',type='revolute',parent='left',child='right',angle_deg=-15,angle_limits_deg=[-80,80],
             parent_frame=plane([20,2,7],(1,0,0),(0,0,1)),child_frame=plane([1,-2,4],(0,1,0)),offset=[1,2,-3]),
        dict(id='lift',type='cylindrical',parent='right',child='other',angle_deg=5,angle_limits_deg=[-30,30],travel_mm=7,travel_limits_mm=[-10,25],
             parent_frame=plane([5,6,7],(0,1,0)),child_frame=plane([-1,2,3]))],
        poses=[dict(id='park',values=[dict(mate_id=m,coordinate=c,value=v) for m,c,v in [('swing','angle_deg',0),('lift','angle_deg',0),('lift','travel_mm',2)]])])
    nested['features'] += [machine,dict(id='station',type='assembly',parts=[dict(id='setup',input='machine',placement=dict(translation=[-20,3,11],rotation=dict(origin=[0,0,0],axis=[1,0,0],angle_deg=42)))])]
    nested['output'] = 'station'
    # Compare with the actual JSON request, where datum-frame tuples are arrays.
    nested = json.loads(json.dumps(nested))
    call('cad_create',dict(document_id='nested_robot',model=nested))

    def exported_name(kind, occurrence):
        return kind+'_'+occurrence if '/' not in occurrence else 'nested_'+kind+''.join('_'+str(len(s))+'_'+s for s in occurrence.split('/'))

    props = []
    inertials = []
    physical = options['inertials'][0]
    for occurrence in ['setup/left','setup/right','setup/other']:
        props += [dict(p,mate_id=occurrence+'/'+p['mate_id']) for p in options['joint_properties']]
        inertials += [dict(physical,link=exported_name('part',occurrence+'/'+p['id'])) for p in model['features'][1]['parts']]
        inertials.append(dict(physical,link=exported_name('carrier',occurrence+'/shaft')))
    props += [dict(mate_id='setup/'+m,coordinate=c,effort=8,velocity=.1) for m,c in [('swing','angle_deg'),('lift','angle_deg'),('lift','travel_mm')]]
    inertials += [dict(physical,link=exported_name('part','setup/spare')),dict(physical,link=exported_name('carrier','setup/lift'))]
    for fmt in ['urdf','sdf']:
        opts = dict(format=fmt,joint_properties=props,inertials=inertials)
        result = call('cad_robot_export',dict(document_id='nested_robot',revision=1,robot=opts))
        root = Path(result['directory']);sdf,links,joints = read_robot(Path(result['path']))
        check(len(links)==23+(not sdf), 'Only 19 real leaves, four cylindrical carriers and the URDF world frame')
        check(len([j for j in joints.values() if j['kind']!='fixed'])==15, 'Every nested and parent moving coordinate retained')
        check(len(list((root/'meshes').glob('*.stl')))==1, 'All composed occurrences reuse their true leaf mesh')
        check(len(links)==len(set(links)) and len(joints)==len(set(joints)), 'Nested XML identifiers remain unique')
        check(len([j for j in joints.values() if 'mimic' in j])==8, 'Authored couplings and repeated definition coordinates preserved')
        ledger=json.loads((root/'robot.json').read_text())
        check(len(ledger['frames']['assemblies'])==5, 'Ledger preserves root and every subassembly frame')
        check(len(ledger['frames']['pose_sources'])==5, 'Every definition-scoped named pose is exported')

        def compare_nested(supplied, operations, label):
            worlds,_=forward(joints,supplied)
            native=call('cad_preview',dict(document_id='nested_robot',expected_revision=1,operations=operations,kind='mesh'))
            for part in native['summary']['assembly']['parts']:
                name=exported_name('part',part['id']);visual=links[name].find('visual')
                visual_origin=sdf_origin(visual.find('pose')) if sdf else urdf_origin(visual.find('origin'))
                actual=mul(worlds[name],visual_origin)
                expected=[part['transform'][i:i+4] for i in range(0,16,4)]
                for i in range(3): expected[i][3]/=1000
                error=max(abs(actual[i][j]-expected[i][j]) for i in range(4) for j in range(4))
                check(error<3e-10,f'nested {fmt}/{label}/{part["id"]}: independent FK, error={error}, actual={actual}, expected={expected}')

        for values in [(30,6,-10,2,-15,5,7),(-20,-2,70,11,37,-11,16),(100,18,0,0,-60,21,-3)]:
            a,t,b,u,s,r,z=values
            controls=[('mechanism','setup/left','hinge','angle_deg',a,30),('mechanism','setup/left','shaft','travel_mm',t,6),
                      ('other_mechanism','setup/other','hinge','angle_deg',b,-10),('other_mechanism','setup/other','shaft','travel_mm',u,2),
                      ('machine','setup','swing','angle_deg',s,-15),('machine','setup','lift','angle_deg',r,5),('machine','setup','lift','travel_mm',z,7)]
            supplied={exported_name('mate',p+'/'+m)+('_angle' if c=='angle_deg' else '_travel'):(math.radians(v-rest) if c=='angle_deg' else (v-rest)/1000) for _,p,m,c,v,rest in controls}
            edits=[dict(op='set_joint_value',assembly_id=d,mate_id=m,coordinate=c,value=v) for d,_,m,c,v,_ in controls]
            compare_nested(supplied,edits,str(values))
        for definition,pose_id in [('mechanism','home'),('mechanism','extended'),('other_mechanism','home'),('other_mechanism','extended'),('machine','park')]:
            name=exported_name('pose',definition+'/'+pose_id)
            if sdf: supplied=ledger['poses'][name]
            else:
                state=ET.parse(root/'model.srdf').getroot().find(f'group_state[@name="{name}"]')
                check(state is not None,'Namespaced child pose exists in SRDF')
                supplied={j.get('name'):float(j.get('value')) for j in state.findall('joint')}
                check(all('mimic' not in joints[j] for j in supplied),'Composed presets never override shared/mimic coordinates')
            compare_nested(supplied,[dict(op='apply_pose',assembly_id=definition,pose_id=pose_id)],name)
        before=set(saved.iterdir())
        call('cad_robot_export',dict(document_id='nested_robot',revision=1,robot=dict(opts,joint_properties=props[:-1])),'invalid_argument')
        check(set(saved.iterdir())==before,'Missing composed physical data publishes no partial bundle')
    check(call('cad_read',dict(document_id='nested_robot'))['model']==nested,'Composed export and all child pose previews preserve editable source')
print(f'robot export: {checks} independent XML/FK/artifact checks passed')
