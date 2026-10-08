"""Independent Draft 2020-12 checks over actual artifact MCP evidence.
Run after artifact_mcp_flow.mjs: python artifact_schema_tests.py evidence.json
"""
from pathlib import Path
import json, sys
from jsonschema import Draft202012Validator

data=json.loads(Path(sys.argv[1]).read_text());tools={x['name']:x for x in data['discovery']};checks=0
for name in ['cad_artifact','cad_artifact_show','cad_job','cad_viewer','cad_context']:
    for key in ['inputSchema','outputSchema']:
        Draft202012Validator.check_schema(tools[name][key]);checks+=1
for call in data['calls']:
    tool=tools[call['name']]
    Draft202012Validator(tool['inputSchema']).validate(call['arguments']);checks+=1
    Draft202012Validator(tool['outputSchema']).validate(call['result']);checks+=1
review=next(c['arguments'] for c in data['calls'] if c['name']=='cad_artifact' and c['arguments']['action']=='review')
validator=Draft202012Validator(tools['cad_artifact']['inputSchema'])
for key,value in [('units','millimeter'),('expected_sha256','a'*63),('format','obj'),('execute',True),('shell','command'),('document_id','Fake')]:
    invalid=dict(review);invalid[key]=value
    assert not validator.is_valid(invalid),(key,'must reject');checks+=1
show=next(c['arguments'] for c in data['calls'] if c['name']=='cad_artifact_show')
validator=Draft202012Validator(tools['cad_artifact_show']['inputSchema'])
for key,value in [('expected_sha256','A'*64),('document_id','Fake'),('revision',1),('selection',{'kind':'face','entity_id':'face-1'})]:
    invalid=dict(show);invalid[key]=value
    assert not validator.is_valid(invalid),(key,'must reject');checks+=1
result=next(c['result'] for c in data['calls'] if c['name']=='cad_artifact')
validator=Draft202012Validator(tools['cad_artifact']['outputSchema'])
for key in ['read_only','editable_history_recovered','native_selection_references']:
    invalid=dict(result);invalid[key]=not result[key]
    assert not validator.is_valid(invalid),(key,'must reject');checks+=1
print(f'{checks} independent actual artifact MCP schema checks passed')
