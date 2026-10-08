"""New artifact tools through the pinned official MCP SDK (developer-only).
Run: mcp-sdk-python tests/artifact_mcp_sdk_smoke.py native-executable [evidence.json]
"""
import asyncio
import hashlib
import importlib.metadata
import json
from pathlib import Path
import shutil
import sys
import tempfile

from mcp import Client, StdioServerParameters
from jsonschema import Draft202012Validator
import mcp_sdk_smoke as support


def require(condition, message):
    support.require(condition, message)


async def smoke(executable, temporary):
    workspace=temporary/'workspace';workspace.mkdir()
    fixtures=temporary/'sources';shutil.copytree(Path(__file__).parent/'artifact_fixtures',fixtures)
    params=StdioServerParameters(command=str(executable),args=['serve','--workspace',str(workspace)])
    evidence={'executable_sha256':hashlib.sha256(executable.read_bytes()).hexdigest(),'sdk':'2.3.0','calls':[],'discovery':None}
    async with Client(params,mode='auto',read_timeout_seconds=30) as client:
        definitions=await support.discover(client)
        evidence['discovery']=[tool.model_dump(by_alias=True,mode='json') for tool in definitions.values()]
        require('cad_artifact' in definitions and 'cad_artifact_show' in definitions,'SDK discovered both new artifact tools')
        require(definitions['cad_artifact_show'].model_dump(by_alias=True)['_meta']['ui']['resourceUri']==support.APP_URI,'SDK artifact show associates actual embedded app')
        async def call(name,args,**kwargs):
            result=await support.call(client,definitions,name,args,**kwargs)
            evidence['calls'].append({'name':name,'arguments':args,'result':result})
            return result
        model=support.box_model();await call('cad_create',{'document_id':'Source','model':model})
        head=await call('cad_read',{'document_id':'Source'})
        exported=await call('cad_export',{'document_id':'Source','revision':1,'format':'step'})
        cases=[('triangle.stl','stl','mm'),('triangle.glb','glb','m'),('nested-deflate.3mf','3mf','file'),('drawing.dxf','dxf','mm'),('primitives.urdf','urdf','m'),('robot.sdf','sdf','m'),('robot.srdf','srdf','m')]
        inputs=[{'action':'review','path':str(fixtures/name),'format':fmt,'units':units,'expected_sha256':hashlib.sha256((fixtures/name).read_bytes()).hexdigest()} for name,fmt,units in cases]
        inputs.append({'action':'review','path':exported['path'],'format':'step','units':'file','expected_sha256':hashlib.sha256(Path(exported['path']).read_bytes()).hexdigest()})
        for args in inputs:
            review=await call('cad_artifact',args)
            require(review['read_only'] and not review['editable_history_recovered'] and not review['native_selection_references'],'SDK artifact representation remains read-only without recovered native selectors')
            verified=await call('cad_artifact',{'action':'verify','review_path':review['path'],'expected_sha256':review['sha256']})
            require(verified['sha256']==review['sha256'],'SDK portable verify preserves exact reviewed identity')
            displayed=await call('cad_artifact_show',{'review_path':review['path'],'expected_sha256':review['sha256'],'view_id':'sdk_artifact'})
            require(displayed['read_only'] and displayed['document_id'] is None,'SDK artifact viewer carries explicit null native source identity')
            context=await call('cad_context',{'view_id':'sdk_artifact'})
            require(context['artifact']['review_sha256']==review['sha256'] and context['selection'] is None and context['revision'] is None and context['feature_id'] is None,'SDK read-only context qualifies complete review hash')
        # Explicit source/reference qualification and an asynchronous typed result.
        robot_path=fixtures/'robot.urdf';reference=fixtures/'triangle.stl'
        robot={'action':'review','path':str(robot_path),'format':'urdf','units':'m','expected_sha256':hashlib.sha256(robot_path.read_bytes()).hexdigest(),
               'references':[{'uri':'triangle.stl','path':str(reference),'expected_sha256':hashlib.sha256(reference.read_bytes()).hexdigest(),'units':'cm'}],
               'native_source':{'document_id':'Source','revision':1,'feature_id':'base'}}
        submission={'action':'submit','request_id':'sdk_artifact_job','tool':'cad_artifact','arguments':robot}
        job=await call('cad_job',submission);done=await support.poll(client,definitions,job['job_id'])
        require(done['state']=='succeeded' and done['result']['source']['references'][0]['units']=='cm','SDK typed artifact job captures explicitly qualified reference')
        review=done['result'];association=review['source']['native_source_association']
        require(association['qualification']=='caller_declared' and association['source_record_sha256']==hashlib.sha256(json.dumps(head,separators=(',',':'),sort_keys=True).encode()).hexdigest(),'SDK declaration binds real historical source bytes without equivalence claim')
        await call('cad_artifact_show',{'review_path':review['path'],'expected_sha256':review['sha256'],'view_id':'sdk_artifact'})
        sync=await call('cad_viewer',{'action':'sync','view_id':'sdk_artifact'})
        for action,extra in [('measure',{}),('section',{}),('preset',{'operation':'list'}),('motion_reset',{})]:
            await call('cad_viewer',{'action':action,'view_id':'sdk_artifact','evaluation_id':sync['evaluation_id'],**extra},expected_error='read_only_artifact')
        await call('cad_artifact',{**inputs[0],'expected_sha256':'0'*64},expected_error='artifact_mismatch')
        invalid={**inputs[0],'units':'millimeter'}
        require(not Draft202012Validator(definitions['cad_artifact'].input_schema).is_valid(invalid),'SDK discovered schema rejects undeclared unit spelling')
        require(await call('cad_read',{'document_id':'Source'})==head,'SDK reviews and jobs preserve authoritative native HEAD')
    # Reopen using the SDK legacy lifecycle: no private transport recreation.
    async with Client(params,mode='legacy',read_timeout_seconds=30) as reopened:
        definitions=await support.discover(reopened)
        persisted=await support.call(reopened,definitions,'cad_job',{'action':'get','job_id':'sdk_artifact_job'})
        replayed=await support.call(reopened,definitions,'cad_job',submission)
        require(persisted['state']=='succeeded' and replayed['result']==persisted['result'],'SDK artifact durable result replays across process restart')
        context=await support.call(reopened,definitions,'cad_context',{'view_id':'sdk_artifact'})
        require(context['read_only'] and context['document_id'] is None and context['artifact']['review_sha256']==review['sha256'],'SDK reopened artifact context retains hash and no native identity')
        await support.call(reopened,definitions,'cad_show',{'document_id':'Source','view_id':'sdk_artifact'})
        for _ in range(500):
            restored=await support.call(reopened,definitions,'cad_viewer',{'action':'sync','view_id':'sdk_artifact'})
            if restored['state']=='ready':break
            await asyncio.sleep(.01)
        require(restored['state']=='ready' and restored['document_id']=='Source' and not restored.get('read_only',False),'SDK real native source restores native viewer after artifact session')
    evidence['checks']=support.checks
    return evidence


def main():
    if len(sys.argv) not in {2,3}:raise SystemExit('Usage: artifact_mcp_sdk_smoke.py executable [evidence.json]')
    installed=importlib.metadata.version('mcp')
    if installed!=support.EXPECTED_SDK:raise SystemExit(f'Expected pinned mcp=={support.EXPECTED_SDK}, got {installed}')
    with tempfile.TemporaryDirectory(prefix='cad-artifact-sdk-') as directory:
        evidence=asyncio.run(asyncio.wait_for(smoke(Path(sys.argv[1]).resolve(strict=True),Path(directory).resolve()),timeout=90))
    if len(sys.argv)==3:Path(sys.argv[2]).write_text(json.dumps(evidence,indent=2)+'\n')
    print(f'MCP SDK {installed}: {support.checks} artifact interoperability checks passed (all original formats, auto + legacy lifecycle)')

if __name__=='__main__':main()
