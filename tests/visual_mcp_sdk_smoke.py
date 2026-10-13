"""Visual tools through the pinned official MCP SDK (developer-only).

Run: python tests/visual_mcp_sdk_smoke.py native-executable [evidence.json]
Uses tests/mcp_sdk_requirements.txt. The SDK owns stdio, request correlation,
typed image/resource decoding and lifecycle. Screenshot chunks use a known PNG
app fixture; actual renderer pixels are tested by browser_visual_mcp.cjs.
No host plugin is installed and every source lives in a temporary workspace.
"""
import asyncio
import base64
from copy import deepcopy
import hashlib
import importlib.metadata
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
import time
import zlib
from jsonschema import Draft202012Validator

from mcp import Client, StdioServerParameters
import mcp_sdk_smoke as support


def require(condition, message):
    support.require(condition, message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def png(data):
    require(data[:8] == b'\x89PNG\r\n\x1a\n', 'SDK PNG has a real signature')
    offset = 8
    compressed = []
    dimensions = None
    while offset < len(data):
        size = struct.unpack_from('>I', data, offset)[0]
        kind = data[offset+4:offset+8]
        end = offset + 12 + size
        require(end <= len(data), 'SDK PNG chunk is bounded')
        body = data[offset+8:offset+8+size]
        require(zlib.crc32(kind + body) == struct.unpack_from('>I', data, end-4)[0],
                'SDK PNG chunk CRC independently matches')
        if kind == b'IHDR':
            width, height, depth, color, compression, filtering, interlace = struct.unpack('>IIBBBBB', body)
            require(depth == 8 and color in {2, 6} and compression == filtering == interlace == 0,
                    'SDK PNG has supported RGB/RGBA row encoding')
            dimensions = (width, height, 3 if color == 2 else 4)
        if kind == b'IDAT':
            compressed.append(body)
        offset = end
        if kind == b'IEND':
            break
    require(dimensions is not None and offset == len(data), 'SDK PNG terminates at its exact declared length')
    width, height, channels = dimensions
    require(0 < width * height <= 4*1024*1024, 'SDK PNG dimensions are bounded')
    rows = zlib.decompress(b''.join(compressed))
    require(len(rows) == height * (width*channels+1), 'SDK PNG zlib decodes exactly the declared rows')
    return {'width': width, 'height': height, 'decoded_row_bytes': len(rows)}


def gif(data):
    require(data[:6] == b'GIF89a' and data[-1:] == b';', 'SDK GIF has the actual GIF89a signature/trailer')
    width, height, packed = struct.unpack_from('<HHB', data, 6)
    require(width > 0 and height > 0 and packed & 128, 'SDK GIF has bounded dimensions and a global palette')
    offset = 13 + 3 * (2 ** ((packed & 7) + 1))
    delays, loop, frames = [], None, 0

    def blocks(position):
        parts = []
        while True:
            require(position < len(data), 'SDK GIF subblock length is bounded')
            size = data[position]
            position += 1
            require(position+size <= len(data), 'SDK GIF subblock data is bounded')
            if not size:
                return position, parts
            parts.append(data[position:position+size])
            position += size

    while offset < len(data):
        tag = data[offset]
        offset += 1
        if tag == 0x3b:
            break
        if tag == 0x21:
            label = data[offset]
            offset, parts = blocks(offset+1)
            if label == 0xf9:
                require(len(parts) == 1 and len(parts[0]) == 4, 'SDK GIF graphic control has four bytes')
                delays.append(struct.unpack_from('<H', parts[0], 1)[0])
            if label == 0xff:
                require(parts[0] == b'NETSCAPE2.0' and parts[1][:1] == b'\1', 'SDK GIF loop extension is standard')
                loop = struct.unpack_from('<H', parts[1], 1)[0]
        elif tag == 0x2c:
            left, top, fw, fh, fp = struct.unpack_from('<HHHHB', data, offset)
            offset += 9
            require((left, top, fw, fh) == (0, 0, width, height) and not fp & 64,
                    'SDK GIF frames occupy the declared noninterlaced canvas')
            if fp & 128:
                offset += 3 * (2 ** ((fp & 7) + 1))
            require(data[offset] == 8, 'SDK GIF uses the declared 8-bit palette LZW input')
            offset, parts = blocks(offset+1)
            require(bool(parts), 'SDK GIF frame contains compressed pixels')
            frames += 1
        else:
            raise AssertionError(f'Unexpected GIF block {tag}')
    require(offset == len(data) and frames == len(delays), 'SDK GIF frame controls and exact stream termination agree')
    return {'width': width, 'height': height, 'frame_count': frames, 'delays_cs': delays, 'loop_repetitions': loop}


def glb(data):
    magic, version, total = struct.unpack_from('<III', data)
    require((magic, version, total) == (0x46546c67, 2, len(data)), 'SDK GLB header has exact GLB2 identity/length')
    json_bytes, json_kind = struct.unpack_from('<II', data, 12)
    require(json_kind == 0x4e4f534a and json_bytes % 4 == 0, 'SDK GLB JSON chunk type/alignment match')
    document = json.loads(data[20:20+json_bytes])
    binary_bytes, binary_kind = struct.unpack_from('<II', data, 20+json_bytes)
    require(binary_kind == 0x004e4942 and 28+json_bytes+binary_bytes == len(data), 'SDK GLB embedded binary chunk is exact')
    require(document['asset']['version'] == '2.0' and len(document['meshes']) == 4 and len(document['animations']) == 1,
            'SDK GLB retains the actual four-leaf animated native assembly')
    return document


async def smoke(executable, workspace, evidence_path):
    params = StdioServerParameters(command=str(executable), args=['serve', '--workspace', str(workspace)], env=dict(os.environ))
    evidence = {'sdk': support.EXPECTED_SDK, 'executable_sha256': sha(executable.read_bytes()), 'calls': [], 'images': []}
    async with support.lifecycle('visual-auto'), Client(params, mode='auto', read_timeout_seconds=20) as client:
        definitions = await support.discover(client)
        require(len(definitions) == 34 and {'cad_render', 'cad_screenshot'} <= definitions.keys(), 'SDK discovers the final 34-tool visual catalog')

        async def call(name, arguments, *, expected_error=None, validate_input=True):
            if validate_input:
                definitions.validator(name, 'input_schema').validate(arguments)
                require(True, f'{name} arguments satisfy the independently discovered schema')
            for attempt in range(100):
                result = await client.call_tool(name, arguments, read_timeout_seconds=20)
                value = result.structured_content
                if result.is_error and isinstance(value, dict) and value.get('error', {}).get('code') == 'workspace_busy':
                    await asyncio.sleep(.01)
                    continue
                break
            else:
                raise AssertionError('SDK visual workspace stayed busy')
            require(isinstance(value, dict), f'{name} returns typed structured content')
            text_value = deepcopy(value)
            image = text_value.pop('image', None) if not result.is_error else None
            require(result.content[0].type == 'text' and json.loads(result.content[0].text) == text_value,
                    f'{name} JSON text preserves metadata without duplicating image base64')
            links = value.get('downloads', []) if not result.is_error else []
            require(len(result.content) == 1+len(links)+(1 if image else 0), f'{name} has exactly the declared typed content blocks')
            for item, link in zip(result.content[1:1+len(links)], links):
                require(item.type == 'resource_link' and item.model_dump(mode='json', by_alias=True, exclude_none=True) == link,
                        f'{name} typed resource links match their metadata')
                resource = (await client.read_resource(link['uri'])).model_dump(mode='json', by_alias=True)
                contents = resource['contents']
                require(len(contents) == 1 and contents[0]['mimeType'] == link['mimeType'], f'{name} resource MIME is preserved')
                binary = base64.b64decode(contents[0]['blob'], validate=True)
                require(binary == Path(value['path']).read_bytes() and len(binary) == link['size'], f'{name} resource independently reconstructs native bytes')
            if image:
                block = result.content[-1]
                require(block.type == 'image' and block.mime_type == image['mimeType'] and block.data == image['data'],
                        f'{name} official SDK decodes the actual image content')
                binary = base64.b64decode(block.data, validate=True)
                require(binary == Path(value['path']).read_bytes() and sha(binary) == value['sha256'] and len(binary) == value['bytes'],
                        f'{name} typed image, native file, size and SHA identify identical bytes')
                require(block.data not in result.content[0].text, f'{name} text does not duplicate encoded image bytes')
                details = png(binary) if block.mime_type == 'image/png' else gif(binary)
                require(details['width'] == value['width'] and details['height'] == value['height'], f'{name} independently parsed image dimensions agree')
                evidence['images'].append({'name': name, 'mime_type': block.mime_type, 'sha256': sha(binary), 'bytes': len(binary), **details})
                if evidence_path:
                    suffix = '.png' if block.mime_type == 'image/png' else '.gif'
                    image_path = evidence_path.with_name(evidence_path.stem+f'-{len(evidence["images"])}'+suffix)
                    image_path.write_bytes(binary)
                    evidence['images'][-1]['evidence_path'] = str(image_path)
            if expected_error:
                require(result.is_error and value['error']['code'] == expected_error, f'{name} rejects with the declared {expected_error} error')
            else:
                require(not result.is_error, f'{name} failed: {value}')
                definitions.validator(name, 'output_schema').validate(value)
                require(True, f'{name} result satisfies its independently discovered output schema')
            logged_arguments = deepcopy(arguments)
            if 'data' in logged_arguments:
                logged_arguments['data_length'] = len(logged_arguments.pop('data'))
            evidence['calls'].append({'name': name, 'arguments': logged_arguments, 'result': text_value})
            return value

        help_result = await call('cad_help', {'query': 'round the edges', 'limit': 5})
        require(any(hit['topic'] == 'feature/fillet' for hit in help_result['matches']), 'SDK help finds fillet through ordinary modeling language')
        fillet_help = (await call('cad_help', {'topic': 'feature/fillet'}))['entry']
        Draft202012Validator.check_schema(fillet_help['schemas']['input'])
        Draft202012Validator(fillet_help['schemas']['input']).validate(
            {'id': 'rounded', 'type': 'fillet', 'input': 'base', 'radius': 2,
             'edges': {'type': 'geometric', 'feature_id': 'base', 'curve_kind': 'line',
                       'expected_count': 4, 'direction': {'vector': [0, 0, 1], 'tolerance': .000001}}})
        render_help = (await call('cad_help', {'topic': 'tool/cad_render'}))['entry']
        require(render_help['schemas']['input'] == definitions['cad_render'].input_schema,
                'SDK help schema exactly matches the discovered render contract')
        for bad in [{'query': 'fillet', 'topic': 'feature/fillet'}, {'limit': 21}, {'offset': 0}, {'query': ''}]:
            require(not definitions.validator('cad_help', 'input_schema').is_valid(bad), 'Help schema rejects invalid input')
            await call('cad_help', bad, expected_error='invalid_argument', validate_input=False)
        await call('cad_help', {'topic': 'missing/topic'}, expected_error='not_found')

        recipe = (await call('cad_help', {'topic': 'guide/native-help/rounding-edges-with-fillet'}))['entry']['content']
        example = json.loads(recipe.split('```json\n', 1)[1].split('```', 1)[0])
        created_example = await call('cad_create', example)
        require(created_example['summary']['valid'] and created_example['summary']['solid_count'] == 1,
                'Bundled help example builds a valid solid through real native geometry')
        edit_guide = (await call('cad_help', {'topic': 'guide/native-help/edit-and-verify'}))['entry']['content']
        examples = [json.loads(block.split('```', 1)[0]) for block in edit_guide.split('```json\n')[1:]]
        edited_example = await call('cad_apply', examples[0])
        require(edited_example['revision'] == 2 and edited_example['summary']['bounds_mm']['max'][2] == 8,
                'Bundled edit recipe rebuilds and commits its intended thickness')
        await call('cad_render', examples[1])

        await call('cad_create', {'document_id': 'plate', 'model': support.box_model()})
        original = await call('cad_read', {'document_id': 'plate'})
        settings = {'width': 128, 'height': 128, 'camera': {'yaw': 0, 'pitch': 1.5707963267948966, 'zoom': 1, 'pan': [0, 0]},
                    'render': {'lighting': {'enabled': False}, 'background': {'color': [1, 1, 1]}, 'grid': False, 'axes': False, 'edges': False},
                    'appearance': {'default_color': [1, 0, 0], 'parts': []}}
        rendered = await call('cad_render', {'source': {'document_id': 'plate', 'revision': 1}, 'settings': settings})
        require(rendered['image']['mimeType'] == 'image/png' and rendered['method'] == 'native_cpu_rasterizer', 'SDK headless image is an actual native PNG')
        sheet_settings = {k: v for k, v in settings.items() if k != 'camera'}
        sheet_settings.update(view='sheet', width=801, height=601)
        sheet_args = {'source': {'document_id': 'plate', 'revision': 1}, 'settings': sheet_settings}
        sheet = await call('cad_render', sheet_args)
        require([view['name'] for view in sheet['sheet']['views']] == ['front', 'top', 'right', 'iso']
                and sheet['sheet']['pixels_per_mm'] > 0 and sheet['sheet']['projection'] == 'orthographic',
                'SDK sheet delivers four qualified shared-scale viewports and real PNG/download bytes')
        stl = workspace/'external-sheet.stl'
        stl.write_text('solid plate\nfacet normal 0 0 1\nouter loop\nvertex 0 0 0\nvertex 20 0 0\nvertex 20 10 0\nendloop\nendfacet\n'
                       'facet normal 0 0 1\nouter loop\nvertex 0 0 0\nvertex 20 10 0\nvertex 0 10 0\nendloop\nendfacet\nendsolid plate\n')
        review = await call('cad_artifact', {'action': 'review', 'path': str(stl), 'expected_sha256': sha(stl.read_bytes()), 'format': 'stl', 'units': 'mm'})
        external_sheet = await call('cad_render', {'source': {'review_path': review['path'], 'expected_sha256': review['sha256']}, 'settings': sheet_settings})
        require(external_sheet['source']['review_sha256'] == review['sha256'] and len(external_sheet['sheet']['views']) == 4,
                'Verified external artifact follows the native four-view PNG pipeline without creating a model')
        static_source = {'review_path': review['path'], 'expected_sha256': review['sha256'], 'configuration': {}}
        external_single = await call('cad_render', {'source': static_source, 'settings': settings})
        require('sheet' not in external_single and external_single['source']['configuration'] == {},
                'Static external reviews also render through the original single-view path with explicit empty configuration')
        await call('cad_render', {'source': dict(static_source, configuration={'joints': {}}), 'settings': sheet_settings},
                   expected_error='unsupported_feature')
        for patch in [{'camera': settings['camera']}, {'width': 127}, {'view': 'invalid'}, {'render': {'projection': 'perspective'}}]:
            bad = dict(sheet_args, settings=dict(sheet_settings, **patch))
            require(not definitions.validator('cad_render', 'input_schema').is_valid(bad), 'Sheet schema rejects contradictory view settings')
            await call('cad_render', bad, expected_error='invalid_argument', validate_input=False)
        invalid = {'source': {'document_id': 'plate', 'revision': 1}, 'settings': {'render': {'code': 'caller-code'}}}
        require(not definitions.validator('cad_render', 'input_schema').is_valid(invalid), 'SDK independently rejects caller-code render inputs')
        await call('cad_render', invalid, expected_error='invalid_argument', validate_input=False)

        fixture = json.loads((Path(__file__).parent.parent/'examples/articulated-arm.create.json').read_text())
        await call('cad_create', fixture)
        arm_original = await call('cad_read', {'document_id': 'articulated_arm'})
        await call('cad_open', {'document_id': 'articulated_arm', 'view_id': 'sequence'})

        async def ready(view):
            for _ in range(500):
                shown = await call('cad_viewer', {'action': 'sync', 'view_id': view})
                if shown['state'] == 'ready':
                    return shown
                require(shown['state'] == 'loading', f'{view} source remains loading until native completion')
                await asyncio.sleep(.01)
            raise AssertionError('SDK native view did not settle')

        shown = await ready('sequence')
        def frame(time_s, angle, travel):
            return {'time_s': time_s, 'presentation': {'clip': None, 'explode': {'distance_mm': 0, 'directions': []}},
                    'joints': [{'assembly_id': 'mechanism', 'values': [{'mate_id': 'hinge', 'coordinate': 'angle_deg', 'value': angle},
                                                                     {'mate_id': 'spindle_joint', 'coordinate': 'travel_mm', 'value': travel}]}]}
        await call('cad_viewer', {'action': 'sequence', 'operation': 'save', 'view_id': 'sequence', 'evaluation_id': shown['evaluation_id'],
                                  'sequence': {'name': 'SDK native motion', 'frames': [frame(0, 0, 0), frame(1, 90, 18)]}})
        animation = {'view_id': 'sequence', 'sequence_name': 'SDK native motion', 'sample_rate_hz': 2, 'max_frames': 8}
        bad_sheet_gif = dict(sheet_args, format='gif', animation=animation)
        require(not definitions.validator('cad_render', 'input_schema').is_valid(bad_sheet_gif), 'Sheet schema rejects GIF')
        await call('cad_render', bad_sheet_gif, expected_error='invalid_argument', validate_input=False)
        animated = await call('cad_render', {'source': {'document_id': 'articulated_arm', 'revision': 1}, 'format': 'gif', 'animation': animation,
                                           'loop': True, 'settings': settings})
        require(animated['image']['mimeType'] == 'image/gif' and animated['frame_count'] == 2 and animated['duration_s'] == 1,
                'SDK GIF delivers two actual end-exclusive sequence frames with declared duration')
        require(evidence['images'][-1]['delays_cs'] == [50, 50] and evidence['images'][-1]['loop_repetitions'] == 0,
                'Independent GIF parsing confirms centisecond delays and infinite loop semantics')
        exported = await call('cad_export', {'document_id': 'articulated_arm', 'revision': 1, 'format': 'glb', 'animation': animation})
        binary = Path(exported['path']).read_bytes()
        document = glb(binary)
        require(sha(binary) == exported['sha256'] and exported['downloads'][0]['mimeType'] == 'model/gltf-binary', 'SDK GLB hash and MIME match real portable binary')
        require(document['extras']['source']['sequence_sha256'] == exported['sequence_sha256'] == animated['source']['sequence_sha256']
                and exported['model_sha256'] == animated['source']['model_sha256'], 'SDK GIF and GLB bind the same saved sequence and frozen native model')
        evidence['glb'] = {'sha256': sha(binary), 'bytes': len(binary), 'sample_count': exported['sample_count'],
                           'model_sha256': exported['model_sha256'], 'sequence_sha256': exported['sequence_sha256']}
        if evidence_path:
            glb_path = evidence_path.with_suffix('.glb')
            glb_path.write_bytes(binary)
            evidence['glb']['evidence_path'] = str(glb_path)

        # The official session correlates a pending capture, app-only sync/chunks
        # and ping concurrently. A known fixture establishes transport identity;
        # browser_visual_mcp.cjs supplies the separate actual-pixel proof.
        await call('cad_open', {'document_id': 'plate', 'view_id': 'image'})
        image_view = await ready('image')
        fixture_png = (Path(__file__).parent/'image_fixtures/type-6.png').read_bytes()
        data = base64.b64encode(fixture_png).decode('ascii')
        captured_task = asyncio.create_task(call('cad_screenshot', {'view_id': 'image', 'timeout_ms': 10000}))
        request = None
        for _ in range(300):
            sync = await call('cad_viewer', {'action': 'sync', 'view_id': 'image'})
            if sync['capture_requests']:
                request = sync['capture_requests'][0]
                break
            await asyncio.sleep(.01)
        require(request is not None and not captured_task.done(), 'SDK screenshot genuinely waits while the app can poll its capture request')
        ping_start = time.perf_counter()
        await asyncio.wait_for(client.session.send_ping(), timeout=2)
        evidence['capture_ping_seconds'] = time.perf_counter()-ping_start
        require(evidence['capture_ping_seconds'] < 1 and not captured_task.done(), 'SDK ping remains responsive during the native screenshot wait')
        common = {'action': 'capture', 'view_id': 'image', 'evaluation_id': image_view['evaluation_id'], 'capture_id': request['capture_id']}
        partial = await call('cad_viewer', {**common, 'offset': 0, 'data': data[:32], 'final': False})
        require(partial['accepted'] and not partial['complete'] and not captured_task.done(), 'SDK first app PNG chunk is accepted without prematurely completing capture')
        await call('cad_viewer', {**common, 'offset': 32, 'data': data[32:], 'final': True})
        captured = await captured_task
        require(base64.b64decode(captured['image']['data']) == fixture_png and captured['source'] == request['source'], 'SDK screenshot returns exact chunked app bytes and frozen source qualification')
        require(captured['source']['model_sha256'] == rendered['source']['model_sha256']
                and captured['source']['pose_model_sha256'] == captured['source']['model_sha256'], 'SDK screenshot pins the same committed model and its actual unposed evaluation')
        require(await call('cad_read', {'document_id': 'plate'}) == original
                and await call('cad_read', {'document_id': 'articulated_arm'}) == arm_original, 'SDK visual operations preserve both committed editable source records')
    evidence['checks'] = support.checks
    return evidence


def main():
    if len(sys.argv) not in {2, 3}:
        raise SystemExit('Usage: visual_mcp_sdk_smoke.py native-executable [evidence.json]')
    installed = importlib.metadata.version('mcp')
    if installed != support.EXPECTED_SDK:
        raise SystemExit(f'Expected pinned mcp=={support.EXPECTED_SDK}, got {installed}')
    executable = Path(sys.argv[1]).resolve(strict=True)
    evidence_path = Path(sys.argv[2]).resolve() if len(sys.argv) == 3 else None
    if evidence_path:
        evidence_path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='cad-visual-sdk-') as directory:
        evidence = asyncio.run(asyncio.wait_for(smoke(executable, Path(directory).resolve(), evidence_path), timeout=90))
    if evidence_path:
        evidence_path.write_text(json.dumps(evidence, indent=2)+'\n')
    print(f'MCP SDK {installed}: {support.checks} visual interoperability checks passed (PNG/GIF/GLB/concurrent screenshot)')


if __name__ == '__main__':
    main()
