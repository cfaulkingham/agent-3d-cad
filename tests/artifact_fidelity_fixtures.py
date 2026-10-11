"""Independent source-format fixtures. Developer generator; no product invocation."""
from pathlib import Path
import base64,json,struct,zlib,binascii
root=Path(__file__).resolve().parent/'artifact_fixtures'
def glb_file(name,configure):
    data=bytearray();views=[];accessors=[]
    def accessor(values,kind,component=5126,normalized=False):
        data.extend(b'\0'*(-len(data)%4));offset=len(data)
        width={'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4,'MAT4':16}[kind]
        values=[values[i:i+width] for i in range(0,len(values),width)]
        raw=struct.pack('<'+{5126:'f',5123:'H',5121:'B',5125:'I'}[component]*sum(map(len,values)),*[x for v in values for x in v]);data.extend(raw)
        views.append({'buffer':0,'byteOffset':offset,'byteLength':len(raw)})
        accessors.append({'bufferView':len(views)-1,'componentType':component,'count':len(values),'type':kind,**({'normalized':True} if normalized else {})})
        return len(accessors)-1
    position=accessor([0,0,0,1,0,0,0,1,0],'VEC3');indices=accessor([0,1,2],'SCALAR',5123)
    doc={'asset':{'version':'2.0'},'bufferViews':views,'accessors':accessors,'meshes':[{'primitives':[{'attributes':{'POSITION':position},'indices':indices}]}],'nodes':[{'mesh':0}],'scenes':[{'nodes':[0]}],'scene':0}
    configure(doc,accessor,data,views)
    doc['buffers']=[{'byteLength':len(data)}]
    text=json.dumps(doc,separators=(',',':')).encode();text+=b' '*(-len(text)%4);data.extend(b'\0'*(-len(data)%4))
    (root/name).write_bytes(struct.pack('<III',0x46546c67,2,28+len(text)+len(data))+struct.pack('<II',len(text),0x4e4f534a)+text+struct.pack('<II',len(data),0x004e4942)+data)
def png():
    def chunk(name,data):return struct.pack('>I',len(data))+name+data+struct.pack('>I',binascii.crc32(name+data)&0xffffffff)
    return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',2,2,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(b'\0'+bytes([255,0,0,255,0,255,0,255])+b'\0'+bytes([0,0,255,255,255,255,255,128])))+chunk(b'IEND',b'')
def appearance(doc,access,data,views):
    p=doc['meshes'][0]['primitives'][0];p['attributes']['TEXCOORD_0']=access([0,0,1,0,0,1],'VEC2');p['attributes']['NORMAL']=access([0,0,1]*3,'VEC3');p['attributes']['COLOR_0']=access([255,255,255,255,128,255,255,255,255,128,255,128],'VEC4',5121,True)
    data.extend(b'\0'*(-len(data)%4));offset=len(data);raw=png();data.extend(raw);views.append({'buffer':0,'byteOffset':offset,'byteLength':len(raw)})
    doc['images']=[{'bufferView':len(views)-1,'mimeType':'image/png'}];doc['textures']=[{'source':0,'sampler':0}];doc['samplers']=[{'wrapS':33071,'wrapT':33648,'magFilter':9728,'minFilter':9729}]
    doc['materials']=[{'pbrMetallicRoughness':{'baseColorFactor':[.2,.4,.8,.5],'metallicFactor':.3,'roughnessFactor':.6,'baseColorTexture':{'index':0}},'emissiveFactor':[.1,0,0],'alphaMode':'BLEND','doubleSided':True}];p['material']=0
    doc['nodes'][0]['scale']=[2,1,1]
glb_file('appearance.glb',appearance)
def motion(doc,access,data,views):
    p=doc['meshes'][0]['primitives'][0];p['attributes']['NORMAL']=access([0,0,1]*3,'VEC3');times=access([0,1],'SCALAR')
    def animation(name,path,values,kind='VEC3',interpolation='LINEAR'):
        output=access(values,kind);return {'name':name,'samplers':[{'input':times,'output':output,'interpolation':interpolation}],'channels':[{'sampler':0,'target':{'node':0,'path':path}}]}
    doc['animations']=[animation('Translate','translation',[0,0,0,2,0,0]),animation('Step','translation',[0,0,0,1,0,0],interpolation='STEP'),animation('Cubic','translation',[0,0,0,0,0,0,2,0,0,0,0,0,1,0,0,0,0,0],interpolation='CUBICSPLINE'),animation('Rotate','rotation',[0,0,0,1,0,0,1,0],kind='VEC4'),animation('Scale','scale',[1,1,1,3,1,1])]
    doc['animations'] += [animation('Cubic rotation','rotation',[0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0],kind='VEC4',interpolation='CUBICSPLINE'),animation('Cubic scale','scale',[0,0,0,1,1,1,0,0,0,0,0,0,3,1,1,0,0,0],interpolation='CUBICSPLINE'),animation('Step rotation','rotation',[0,0,0,1,0,0,1,0],kind='VEC4',interpolation='STEP'),animation('Step scale','scale',[1,1,1,3,1,1],interpolation='STEP')]
glb_file('motion.glb',motion)
def deformation(doc,access,data,views):
    p=doc['meshes'][0]['primitives'][0];p['attributes']['JOINTS_0']=access([0,0,0,0]*3,'VEC4',5121);p['attributes']['WEIGHTS_0']=access([1,0,0,0]*3,'VEC4');p['attributes']['NORMAL']=access([0,0,1]*3,'VEC3')
    p['targets']=[{'POSITION':access([0,.5,0]*3,'VEC3'),'NORMAL':access([0,.2,0]*3,'VEC3')}];doc['meshes'][0]['weights']=[.2]
    doc['nodes']=[{'mesh':0,'skin':0,'translation':[99,0,0]},{'translation':[1,0,0]}];doc['scenes'][0]['nodes']=[0,1]
    inverse=access([1,0,0,0,0,1,0,0,0,0,1,0,-1,0,0,1],'MAT4');doc['skins']=[{'joints':[1],'inverseBindMatrices':inverse}]
    times=access([0,1],'SCALAR');move=access([1,0,0,2,0,0],'VEC3');weights=access([0,1],'SCALAR');doc['animations']=[{'name':'Skin and morph','samplers':[{'input':times,'output':move},{'input':times,'output':weights}],'channels':[{'sampler':0,'target':{'node':1,'path':'translation'}},{'sampler':1,'target':{'node':0,'path':'weights'}}]}]
    for name,interpolation,values in [('Step morph','STEP',[0,1]),('Cubic morph','CUBICSPLINE',[0,0,2,0,1,0])]:
        doc['animations'].append({'name':name,'samplers':[{'input':times,'output':access(values,'SCALAR'),'interpolation':interpolation}],'channels':[{'sampler':0,'target':{'node':0,'path':'weights'}}]})
glb_file('deformation.glb',deformation)

def entity(kind,*pairs):return '0\n'+kind+'\n'+''.join(f'{code}\n{value}\n' for code,value in pairs)
block=entity('BLOCK',(2,'unit'),(10,1),(20,0))+entity('LINE',(10,1),(20,0),(11,2),(21,0))+entity('TEXT',(10,1),(20,0),(40,1),(1,'Block label'))+entity('ENDBLK')
dimension=entity('BLOCK',(2,'*D1'))+entity('LINE',(10,0),(20,5),(11,10),(21,5))+entity('TEXT',(10,5),(20,5),(40,1),(1,'10'))+entity('ENDBLK')
entities=entity('INSERT',(2,'unit'),(10,10),(20,20),(41,2),(42,3),(50,90))+entity('LWPOLYLINE',(90,2),(70,0),(10,0),(20,0),(42,1),(10,10),(20,0))+entity('SPLINE',(70,4),(71,2),(72,6),(73,3),(74,0),(40,0),(40,0),(40,0),(40,1),(40,1),(40,1),(41,1),(41,1),(41,1),(10,0),(20,0),(10,5),(20,10),(10,10),(20,0))+entity('MTEXT',(10,20),(20,0),(40,2),(1,'Line 1\\PLine 2'))+entity('DIMENSION',(2,'*D1'),(70,32),(10,10),(20,5),(42,10))
(root/'fidelity.dxf').write_text('0\nSECTION\n2\nBLOCKS\n'+block+dimension+'0\nENDSEC\n0\nSECTION\n2\nENTITIES\n'+entities+'0\nENDSEC\n0\nEOF\n')
loops=[[(0,0),(10,0),(10,10),(0,10)],[(3,3),(7,3),(7,7),(3,7)]]
boundary=[(91,len(loops))]
for loop in loops:
    boundary.extend([(92,2),(72,0),(73,1),(93,len(loop))])
    for x,y in loop:boundary.extend([(10,x),(20,y)])
    boundary.extend([(97,0)])
solid=entity('HATCH',(2,'SOLID'),(70,1),*boundary,(75,0));pattern=entity('HATCH',(2,'ANSI31'),(70,0),*boundary,(75,0),(78,1),(53,0),(43,0),(44,0),(45,0),(46,1),(79,2),(49,2),(49,-1))
(root/'hatches.dxf').write_text('0\nSECTION\n2\nENTITIES\n'+solid+pattern+'0\nENDSEC\n0\nEOF\n')
(root/'joints.urdf').write_text('''<robot name="motion"><material name="red"><color rgba="1 0 0 0.5"/></material><link name="base"/><link name="arm"><visual><origin xyz="1 0 0"/><geometry><box size="0.1 0.2 0.3"/></geometry><material name="red"/></visual></link><link name="finger"><visual><geometry><sphere radius="0.01"/></geometry></visual></link><joint name="hinge" type="revolute"><parent link="base"/><child link="arm"/><axis xyz="0 0 2"/><limit lower="-2" upper="2"/></joint><joint name="follower" type="revolute"><parent link="arm"/><child link="finger"/><origin xyz="2 0 0"/><axis xyz="0 0 1"/><limit lower="-4" upper="4"/><mimic joint="hinge" multiplier="2"/></joint></robot>''')
(root/'joints.srdf').write_text('''<robot name="motion"><group name="arm"><chain base_link="base" tip_link="finger"/></group><group_state name="raised" group="arm"><joint name="hinge" value="1"/><joint name="follower" value="2"/></group_state><disable_collisions link1="arm" link2="finger" reason="Adjacent"/><passive_joint name="follower"/></robot>''')
(root/'frames.sdf').write_text('''<sdf version="1.12"><model name="framed"><pose>1 0 0 0 0 0</pose><frame name="pivot" attached_to="base"><pose>0.5 0 0 0 0 0</pose></frame><link name="base"/><link name="arm"><pose relative_to="pivot">0.5 0 0 0 0 0</pose><visual name="box"><geometry><box><size>0.1 0.2 0.3</size></box></geometry></visual></link><joint name="hinge" type="revolute"><parent>base</parent><child>arm</child><pose relative_to="pivot">0 0 0 0 0 0</pose><axis><xyz expressed_in="__model__">0 0 1</xyz><limit><lower>-2</lower><upper>2</upper></limit></axis></joint></model></sdf>''')
print('Generated independent appearance, motion, deformation, drafting and robot fixtures')
