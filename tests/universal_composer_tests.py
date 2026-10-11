"""Developer fixture tests for universal composition, never product-runtime evidence.

On macOS, compile tiny independent signed Mach-O executables/dylibs for both CPUs
and exercise real lipo/codesign. No CAD SDK, Rosetta, native execution or downloads.
Optional --validator PATH selects an independently developed validator during integration.
"""
import copy
import hashlib
import importlib.util
import json
import platform
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

repo=Path(__file__).resolve().parent.parent
if platform.system()!='Darwin':
    print('Universal composer real-tool fixtures require macOS; no execution evidence claimed')
    raise SystemExit(0)
validator=Path(sys.argv[2]) if len(sys.argv)==3 and sys.argv[1]=='--validator' else repo/'packaging/bundle_validation.py'
spec=importlib.util.spec_from_file_location('bundle_validation',validator);validation=importlib.util.module_from_spec(spec);sys.modules[spec.name]=validation;spec.loader.exec_module(validation)
spec=importlib.util.spec_from_file_location('universal_composer',repo/'packaging/compose-macos-universal.py');composer=importlib.util.module_from_spec(spec);spec.loader.exec_module(composer)
checks=0
version=(repo/'VERSION').read_text().strip()

def check(value,label):
    global checks
    assert value,label;checks+=1

def fails(function,text):
    global checks
    try:function()
    except (ValueError,KeyError,OSError) as error:
        assert text in str(error),(text,str(error));checks+=1
    else:raise AssertionError('Expected rejection: '+text)

def run(*args):
    result=subprocess.run(args,capture_output=True,text=True,timeout=120)
    assert result.returncode==0,result.stderr

def digest(data):return hashlib.sha256(data).hexdigest()

def inventory(root,provenance):
    provenance['files']=[{'path':p.relative_to(root).as_posix(),'sha256':validation.sha(p)} for p in sorted(root.rglob('*')) if p.is_file() and p.relative_to(root)!=composer.MANIFEST]
    (root/composer.MANIFEST).write_text(json.dumps(provenance,sort_keys=True,indent=2)+'\n')

with tempfile.TemporaryDirectory(prefix='cad-universal-compose-') as temporary:
    root=Path(temporary);source=root/'sources';source.mkdir()
    native_source=b'extern int hlr_fixture(void); extern int ft_fixture(void); const char cache[] = CACHE; int main(void){return hlr_fixture()+ft_fixture()+cache[0]-97;}\n'
    (source/'main.c').write_bytes(native_source)
    source_inputs={'CMakeLists.txt':b'fixture\n','VERSION':(version+'\n').encode(),'src/main.c':native_source}
    manifest=''.join(f'{digest(v)}  {p}\n' for p,v in sorted(source_inputs.items())).encode()
    source_hash=digest(manifest)
    roots={};provenances={}
    clang=subprocess.check_output(['/usr/bin/xcrun','--find','clang'],text=True).strip()
    sdk=subprocess.check_output(['/usr/bin/xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip()
    for arch,machine in [('arm64','arm64'),('x64','x86_64')]:
        bundle=root/arch;roots[arch]=bundle;(bundle/'bin').mkdir(parents=True);(bundle/'lib').mkdir()
        for stem,func in [('libTKHLR','hlr_fixture'),('libfreetype','ft_fixture')]:
            file=source/(func+'.c');file.write_text(f'int {func}(void){{return 0;}}\n')
            run(clang,'-isysroot',sdk,'-arch',machine,'-mmacosx-version-min=15.0','-dynamiclib',str(file),'-Wl,-install_name,@rpath/'+stem+'.dylib','-o',str(bundle/'lib'/(stem+'.dylib')))
        cache=('a' if arch=='arm64' else 'b')*64+'-Release'
        run(clang,'-isysroot',sdk,'-arch',machine,'-mmacosx-version-min=15.0','-DCACHE="'+cache+'"',str(source/'main.c'),'-L'+str(bundle/'lib'),'-lTKHLR','-lfreetype','-Wl,-rpath,@executable_path/../lib','-o',str(bundle/'bin/agent-3d-cad'))
        for file in [bundle/'bin/agent-3d-cad',*sorted((bundle/'lib').glob('*.dylib'))]:
            run('/usr/bin/codesign','--force','--sign','-','--timestamp=none',str(file))
        (bundle/composer.SOURCE).parent.mkdir(parents=True);(bundle/composer.SOURCE).write_bytes(manifest)
        notice=bundle/composer.HLR.parent;notice.mkdir(parents=True)
        for name in ['LICENSE_LGPL_21.txt','OCCT_LGPL_EXCEPTION.txt','PatchOcctHlr.cmake','OCCT-HLR-PATCH.md',*('modified/'+s for s in ('HLRBRep_Intersector.cxx','HLRBRep_Intersector.hxx','HLRBRep_Data.cxx','HLRBRep_Data.hxx','HLRBRep_Hider.cxx'))]:
            (notice/name).parent.mkdir(parents=True,exist_ok=True);(notice/name).write_text('Non-distributed independent fixture: '+name+'\n')
        hlr_hash=validation.sha(bundle/'lib/libTKHLR.dylib')
        (bundle/composer.HLR).write_text(json.dumps(dict(format_version=1,occt_version='8.0.1',modification='agentcad-hlr-midpoint-v2',binaries=[dict(path='lib/libTKHLR.dylib',sha256=hlr_hash)])))
        provenance={**composer.PINNED,'format_version':1,'project_version':version,'system':'Darwin','architecture':arch,'compiler':'fixture clang','occt_hlr_sdk_sha256':hlr_hash,
            'source_identity':dict(format_version=1,algorithm='sha256-file-list-v1',sha256=source_hash,manifest=composer.SOURCE.as_posix()),
            'build_identity':dict(format_version=1,source_sha256=source_hash,executable_sha256=validation.sha(bundle/'bin/agent-3d-cad'),cache_identity=cache,compiler_id='AppleClang',compiler_version='fixture',configuration='Release',system='Darwin',architecture=machine,osx_architectures=machine,deployment_target='15.0',cxx_flags='',configuration_flags=''),
            'minimum_macos':'15.0.0'}
        inventory(bundle,provenance);provenances[arch]=provenance
        check(composer.input_bundle(bundle,arch,version)[0]['source_identity']['sha256']==source_hash,'Complete signed thin input admitted')
    arm,x64=roots.values();output=root/'universal'
    composer.compose(arm,x64,output,version)
    final,system,arch,binary=validation.validate_bundle(output,version)
    check((system,arch,binary)==('Darwin','universal','bin/agent-3d-cad'),'Independently validated universal identity')
    for item in final['composition']['inputs']:
        check((output/item['provenance']).read_bytes()==(roots[item['architecture']]/composer.MANIFEST).read_bytes(),'Original provenance preserved byte-exact')
        check((output/item['hlr_manifest']).read_bytes()==(roots[item['architecture']]/composer.HLR).read_bytes(),'Original SDK HLR record preserved byte-exact')
    for record in final['composition']['native_files']:
        run('/usr/bin/codesign','--verify','--strict',str(output/record['path']));check(True,'Universal native signature verifies')
        slices=validation.native_slices(output/record['path']);check(set(slices)=={'arm64','x64'},'Every native image has both slices')
        for item in record['slices']:
            for kind in ('section_payload_sha256','signing_payload_sha256'):
                check(item['input_'+kind]==item[kind]==slices[item['architecture']][kind],'Only signature-associated bytes changed')
    check(len(final['composition']['native_files'])==3,'Executable and every dylib composed')
    fails(lambda:composer.compose(arm,x64,output,version),'new output')
    fails(lambda:composer.compose(x64,arm,root/'swapped',version),'Expected a verified thin')
    fails(lambda:composer.compose(arm,arm,root/'same',version),'distinct inputs')
    alias=root/'aliased-input';alias.symlink_to(arm,target_is_directory=True)
    before={p.relative_to(arm).as_posix():validation.sha(p) for p in arm.rglob('*') if p.is_file()}
    fails(lambda:composer.compose(arm,x64,alias/'output',version),'overlap an input')
    check(before=={p.relative_to(arm).as_posix():validation.sha(p) for p in arm.rglob('*') if p.is_file()},'Aliased overlap leaves original input unchanged')
    executable=x64/'bin/agent-3d-cad';original=executable.read_bytes()
    executable.write_bytes((x64/'lib/libfreetype.dylib').read_bytes());inventory(x64,provenances['x64'])
    fails(lambda:composer.compose(arm,x64,root/'library-entrypoint',version),'role differs');executable.write_bytes(original);inventory(x64,provenances['x64'])
    dependency=x64/'lib/libfreetype.dylib';original=dependency.read_bytes()
    dependency.write_bytes(executable.read_bytes());inventory(x64,provenances['x64'])
    fails(lambda:composer.compose(arm,x64,root/'executable-dependency',version),'role differs');dependency.write_bytes(original);inventory(x64,provenances['x64'])
    failures=[('missing-source',lambda p:p.pop('source_identity'),'production source identity'),
              ('source-hash',lambda p:p['source_identity'].update(sha256='0'*64),'production source identity'),
              ('source-build',lambda p:p['build_identity'].update(source_sha256='0'*64),'build/source'),
              ('cache-identity',lambda p:p['build_identity'].update(cache_identity='c'*64+'-Release'),'cache identity'),
              ('pinned-kernel',lambda p:p.update(occt_version='8.0.0'),'pinned dependency')]
    for name,change,error in failures:
        damaged=copy.deepcopy(provenances['x64']);change(damaged);inventory(x64,damaged)
        failed_output=root/name;fails(lambda:composer.compose(arm,x64,failed_output,version),error);check(not failed_output.exists(),'Failed composition is unpublished')
    inventory(x64,provenances['x64'])
    resource=x64/'share/agent-3d-cad/notices/occt/modified/HLRBRep_Data.cxx';original=resource.read_bytes();resource.write_bytes(original+b'different');inventory(x64,provenances['x64'])
    fails(lambda:composer.compose(arm,x64,root/'different-resource',version),'resource differs');resource.write_bytes(original);inventory(x64,provenances['x64'])
    library=x64/'lib/libfreetype.dylib';original=library.read_bytes();library.write_bytes(b'malformed Mach-O');inventory(x64,provenances['x64'])
    fails(lambda:composer.compose(arm,x64,root/'malformed',version),'native');library.write_bytes(original);inventory(x64,provenances['x64'])
    # Inconsistent permissions and original SDK metadata must fail before publication.
    mode=library.stat().st_mode & 0o777;library.chmod(mode ^ 0o100)
    fails(lambda:composer.compose(arm,x64,root/'permissions',version),'permissions differ');library.chmod(mode)
    hlr=x64/composer.HLR;original=hlr.read_bytes();record=json.loads(original);record['unexpected']='different metadata';hlr.write_text(json.dumps(record));inventory(x64,provenances['x64'])
    fails(lambda:composer.compose(arm,x64,root/'hlr-metadata',version),'SDK metadata');hlr.write_bytes(original);inventory(x64,provenances['x64'])
    source_file=x64/composer.SOURCE;original=source_file.read_bytes();source_file.write_bytes(original+original.splitlines(keepends=True)[0]);damaged=copy.deepcopy(provenances['x64']);new_hash=validation.sha(source_file);damaged['source_identity']['sha256']=new_hash;damaged['build_identity']['source_sha256']=new_hash;inventory(x64,damaged)
    fails(lambda:composer.compose(arm,x64,root/'duplicate-source',version),'sorted and unique');source_file.write_bytes(original);inventory(x64,provenances['x64'])
    # A failed/faulty native tool cannot publish output or bless changed code bytes.
    normal_command=composer.command
    def changed_payload(*args):
        result=normal_command(*args)
        if args[0]=='/usr/bin/lipo' and Path(args[-1]).name=='agent-3d-cad':
            output=Path(args[-1]);data=output.read_bytes();marker=b'a'*64+b'-Release';assert data.count(marker)==1
            output.write_bytes(data.replace(marker,b'd'+marker[1:]))
        return result
    composer.command=changed_payload
    fails(lambda:composer.compose(arm,x64,root/'changed-payload',version),'unsigned native payload')
    check(not (root/'changed-payload').exists(),'Changed native payload is unpublished')
    composer.command=normal_command
    check(composer.input_bundle(x64,'x64',version)[0]['source_identity']['sha256']==source_hash,'Original fixture restored after rejection tests')
print(f'{checks} universal composer fixture checks passed; actual lipo/codesign, fixture binaries never executed')
