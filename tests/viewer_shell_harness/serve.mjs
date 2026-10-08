// Dev-only harness. Serves the assembled viewer inside a minimal MCP Apps host
// page, backed by the real native stdio service. Nothing here ships or is a test.
//   node tests/viewer_shell_harness/serve.mjs build-package/agent-3d-cad [port]
// Then open the printed URL. Query: ?doc=live_plate&theme=auto|light|dark&width=900&height=620
import http from 'node:http';
import { spawn } from 'node:child_process';
import { mkdtempSync, readFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { createInterface } from 'node:readline';
import { fileURLToPath } from 'node:url';

const root = fileURLToPath(new URL('../../', import.meta.url));
const exe = resolve(process.argv[2] ?? 'build-package/agent-3d-cad');
const requestedPort = Number(process.argv[3] ?? 0);
const workspace = mkdtempSync(join(tmpdir(), 'cad-shell-harness-'));
const seeds = ['live-plate', 'bracket', 'assembly', 'articulated-arm', 'nested-assembly'];

const child = spawn(exe, ['serve', '--workspace', workspace], { stdio: ['pipe', 'pipe', 'pipe'] });
const waiting = new Map();
let next = 1, stderr = '';
child.stderr.on('data', value => { stderr += value; });
createInterface({ input: child.stdout }).on('line', line => {
  try {
    const message = JSON.parse(line), waiter = waiting.get(message.id);
    if (waiter) { waiting.delete(message.id); clearTimeout(waiter.timer); message.error ? waiter.reject(Error(message.error.message)) : waiter.resolve(message.result); }
  } catch (error) { for (const waiter of waiting.values()) waiter.reject(error); }
});
child.on('exit', code => { for (const waiter of waiting.values()) { clearTimeout(waiter.timer); waiter.reject(Error(`Service exited ${code}: ${stderr}`)); } waiting.clear(); });
const rpc = (method, params = {}) => new Promise((resolveRpc, reject) => {
  const id = next++, timer = setTimeout(() => { waiting.delete(id); reject(Error(`Timeout: ${method}: ${stderr}`)); }, 60000);
  waiting.set(id, { resolve: resolveRpc, reject, timer });
  child.stdin.write(JSON.stringify({ jsonrpc: '2.0', id, method, params }) + '\n');
});

// Same substitution as cmake/EmbedViewerApp.cmake: one pass, data not directives.
function assembledViewer() {
  let html = readFileSync(join(root, 'web/viewer.html'), 'utf8').replace(/\r\n/g, '\n');
  for (const [token, file] of [['STYLES', 'styles.css'], ['BRIDGE', 'bridge.js'], ['RENDERER', 'renderer.js'], ['STATE', 'state.js'], ['SHELL', 'shell.js'], ['APP', 'app.js']]) {
    const content = readFileSync(join(root, 'web', file), 'utf8').replace(/\r\n/g, '\n');
    html = html.replace(`@VIEWER_${token}@`, () => content);
  }
  return html;
}

const server = http.createServer((request, response) => {
  const url = new URL(request.url, 'http://127.0.0.1');
  const send = (status, type, body) => { response.writeHead(status, { 'content-type': type, 'cache-control': 'no-store' }); response.end(body); };
  if (request.method === 'GET' && url.pathname === '/') return send(200, 'text/html; charset=utf-8', readFileSync(new URL('./host.html', import.meta.url)));
  if (request.method === 'GET' && url.pathname === '/viewer') return send(200, 'text/html; charset=utf-8', assembledViewer());
  if (request.method === 'GET' && url.pathname === '/docs') return send(200, 'application/json', JSON.stringify(seeds.map(name => JSON.parse(readFileSync(join(root, 'examples', `${name}.create.json`), 'utf8')).document_id)));
  if (request.method === 'POST' && url.pathname === '/rpc') {
    let body = '';
    request.on('data', chunk => { body += chunk; });
    request.on('end', async () => {
      try {
        const { method, params } = JSON.parse(body);
        send(200, 'application/json', JSON.stringify({ result: await rpc(method, params) }));
      } catch (error) { send(200, 'application/json', JSON.stringify({ error: { message: error.message } })); }
    });
    return undefined;
  }
  return send(404, 'text/plain', 'Not found');
});

const cleanup = () => { child.kill(); rmSync(workspace, { recursive: true, force: true }); };
process.on('SIGINT', () => { cleanup(); process.exit(0); });
process.on('SIGTERM', () => { cleanup(); process.exit(0); });
process.on('exit', cleanup);

await rpc('initialize', { protocolVersion: '2025-11-25', capabilities: { extensions: { 'io.modelcontextprotocol/ui': { mimeTypes: ['text/html;profile=mcp-app'] } } }, clientInfo: { name: 'shell-harness', version: '1' } });
child.stdin.write(JSON.stringify({ jsonrpc: '2.0', method: 'notifications/initialized' }) + '\n');
for (const name of seeds) {
  const create = JSON.parse(readFileSync(join(root, 'examples', `${name}.create.json`), 'utf8'));
  await rpc('tools/call', { name: 'cad_create', arguments: create });
}
server.listen(requestedPort, '127.0.0.1', () => console.log(JSON.stringify({ url: `http://127.0.0.1:${server.address().port}/`, workspace })));
