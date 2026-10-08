# Developer-only fixture generation. Runtime review uses no Python or zlib API.
from pathlib import Path
import io,json,struct,zipfile,zlib
root=Path(__file__).resolve().parent/'artifact_fixtures'
root.mkdir(exist_ok=True)
(root/'triangle.stl').write_text('solid external\nfacet normal 0 0 1\nouter loop\nvertex 0 0 0\nvertex 1 0 0\nvertex 0 1 0\nendloop\nendfacet\nendsolid external\n')
binary=b'Binary external STL'.ljust(80,b'\0')+struct.pack('<I12fH',1,0,0,1,0,0,0,1,0,0,0,1,0,0)
(root/'triangle-binary.stl').write_bytes(binary)
bin=struct.pack('<9f3H',0,0,0,1,0,0,0,1,0,0,1,2)
doc={'asset':{'version':'2.0'},'buffers':[{'byteLength':len(bin)}],'bufferViews':[{'buffer':0,'byteOffset':0,'byteLength':36},{'buffer':0,'byteOffset':36,'byteLength':6}],'accessors':[{'bufferView':0,'componentType':5126,'count':3,'type':'VEC3'},{'bufferView':1,'componentType':5123,'count':3,'type':'SCALAR'}],'meshes':[{'primitives':[{'attributes':{'POSITION':0},'indices':1}]}],'nodes':[{'mesh':0,'translation':[2,3,4],'scale':[-1,1,1]}],'scenes':[{'nodes':[0]}],'scene':0}
def glb(doc):
    text=json.dumps(doc,separators=(',',':')).encode();text+=b' '*(-len(text)%4)
    data=bin+b'\0'*(-len(bin)%4)
    return struct.pack('<III',0x46546c67,2,28+len(text)+len(data))+struct.pack('<II',len(text),0x4e4f534a)+text+struct.pack('<II',len(data),0x004e4942)+data
(root/'triangle.glb').write_bytes(glb(doc))
doc['nodes']=[{'mesh':0,'matrix':[-1,0,0,0,0,1,0,0,0,0,1,0,2,3,4,1]}]
(root/'triangle-matrix.glb').write_bytes(glb(doc))
model='''<?xml version="1.0" encoding="UTF-8"?><model xmlns="http://schemas.microsoft.com/3dmanufacturing/core/2015/02" unit="centimeter"><resources><object id="1" type="model"><mesh><vertices><vertex x="0" y="0" z="0"/><vertex x="1" y="0" z="0"/><vertex x="0" y="1" z="0"/></vertices><triangles><triangle v1="0" v2="1" v3="2"/></triangles></mesh></object><object id="2" type="model"><components><component objectid="1" transform="1 0 0 0 1 0 0 0 1 2 3 4"/></components></object></resources><build><item objectid="2" transform="1 0 0 0 1 0 0 0 1 10 0 0"/></build></model>'''
rels='''<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="r1" Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel" Target="/3D/3dmodel.model"/></Relationships>'''
content='''<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="model" ContentType="application/vnd.ms-package.3dmanufacturing-3dmodel+xml"/></Types>'''
def archive(path,method=zipfile.ZIP_DEFLATED,extra=None):
    with zipfile.ZipFile(root/path,'w',compression=method) as file:
        for name,text in {'[Content_Types].xml':content,'_rels/.rels':rels,'3D/3dmodel.model':model}.items():file.writestr(name,text)
        if extra:
            for name,text in extra:file.writestr(name,text)
archive('nested-deflate.3mf')
archive('nested-stored.3mf',zipfile.ZIP_STORED)
archive('traversal.3mf',extra=[('../outside','x')])
archive('duplicate.3mf',extra=[('3D/3dmodel.model',model)])
with zipfile.ZipFile(root/'symlink.3mf','w') as file:
    entry=zipfile.ZipInfo('link');entry.create_system=3;entry.external_attr=(0o120777<<16);file.writestr(entry,'outside')
for kind,level in [('dynamic',6),('stored',0),('fixed',6)]:
    text=(b'bounded independent deflate '+bytes(range(256)))*150
    codec=zlib.compressobj(level,zlib.DEFLATED,-15,8,zlib.Z_FIXED if kind=='fixed' else zlib.Z_DEFAULT_STRATEGY)
    raw=codec.compress(text)+codec.flush()
    name=b'probe.txt';crc=zlib.crc32(text);header=struct.pack('<I5H3I2H',0x04034b50,20,0,8,0,0,crc,len(raw),len(text),len(name),0)+name
    central=struct.pack('<I6H3I5H2I',0x02014b50,20,20,0,8,0,0,crc,len(raw),len(text),len(name),0,0,0,0,0,0)+name
    blob=header+raw+central+struct.pack('<I4H2IH',0x06054b50,0,0,1,1,len(central),len(header)+len(raw),0)
    (root/f'deflate-{kind}.zip').write_bytes(blob)
(root/'drawing.dxf').write_text('0\nSECTION\n2\nHEADER\n9\n$INSUNITS\n70\n4\n0\nENDSEC\n0\nSECTION\n2\nENTITIES\n0\nLINE\n5\nA1\n8\nCUT\n10\n0\n20\n0\n30\n0\n11\n10\n21\n5\n31\n0\n0\nLWPOLYLINE\n90\n3\n70\n1\n10\n0\n20\n0\n10\n2\n20\n0\n10\n2\n20\n2\n0\nCIRCLE\n10\n20\n20\n0\n40\n2\n0\nENDSEC\n0\nEOF\n')
(root/'robot.urdf').write_text('''<robot name="external"><link name="base"><visual><geometry><box size="0.002 0.004 0.006"/></geometry></visual></link><link name="arm"><visual><geometry><mesh filename="triangle.stl" scale="1 1 1"/></geometry></visual></link><joint name="hinge" type="revolute"><parent link="base"/><child link="arm"/><origin xyz="1 0 0" rpy="0 0 0"/><axis xyz="0 0 1"/><limit lower="-1" upper="1" effort="10" velocity="2"/></joint></robot>''')
(root/'robot.sdf').write_text('''<sdf version="1.12"><model name="external"><pose>1 0 0 0 0 0</pose><link name="base"><pose>2 0 0 0 0 0</pose><visual name="box"><pose>0.002 0 0 0 0 0</pose><geometry><box><size>0.002 0.004 0.006</size></box></geometry></visual></link></model></sdf>''')
(root/'robot.srdf').write_text('''<robot name="external"><group name="arm"><chain base_link="base" tip_link="arm"/></group><group name="all"><group name="arm"/></group><group_state name="rest" group="arm"><joint name="hinge" value="0"/></group_state><disable_collisions link1="base" link2="arm" reason="Adjacent"/></robot>''')
print('Wrote',len(list(root.iterdir())),'independent format fixtures')

(root/'primitives.urdf').write_text('''<robot name="primitives"><link name="base"><visual><geometry><sphere radius="0.01"/></geometry></visual><collision><origin xyz="0.03 0 0"/><geometry><cylinder radius="0.005" length="0.02"/></geometry></collision></link></robot>''')
# Five individually valid 32 MiB members exceed the 128 MiB total expansion budget.
with zipfile.ZipFile(root/'total-expansion.zip','w',compression=zipfile.ZIP_DEFLATED,compresslevel=9) as file:
    for i in range(5):file.writestr(f'entry-{i}',b'A'*(32*1024*1024))
