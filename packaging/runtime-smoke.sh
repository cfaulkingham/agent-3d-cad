#!/bin/sh
# Run inside the minimal runtime container; only the OS shell/coreutils exist.
set -eu
bundle=${1:-/opt/agent-3d-cad}
work=${2:-/tmp/agentcad-runtime}
for tool in python python3 rustc cargo node npm cmake gcc g++ cc c++; do
  if command -v "$tool" >/dev/null 2>&1; then
    echo "Unexpected developer tool in clean runtime: $tool" >&2
    exit 1
  fi
done
mkdir -p "$work"
exe="$bundle/bin/agent-3d-cad"
examples="$bundle/share/agent-3d-cad/examples"
"$exe" --version > "$work/version.json"
"$exe" call cad_create --workspace "$work/workspace" --input "$examples/plate.create.json" > "$work/create.json"
"$exe" call cad_apply --workspace "$work/workspace" --input "$examples/plate.edit.json" > "$work/edit.json"
"$exe" call cad_query --workspace "$work/workspace" --input "$examples/plate.query.json" > "$work/query.json"
"$exe" call cad_export --workspace "$work/workspace" --input "$examples/plate.export.json" > "$work/export.json"
printf '%s\n' '{"document_id":"plate","revision":2,"format":"stl"}' | "$exe" call cad_export --workspace "$work/workspace" --input - > "$work/stl.json"
test -s "$work/workspace/exports/plate-r2.step"
test -s "$work/workspace/exports/plate-r2.stl"
grep -q '8.0.1' "$work/version.json"
grep -q '"revision": 2' "$work/query.json"
printf '%s\n' '{"document_id":"plate","expected_revision":2,"operations":[{"op":"replace_feature","id":"rounded","feature":{"id":"rounded","type":"fillet","input":"base","radius":1000,"edges":"all"}}]}' > "$work/invalid.json"
if "$exe" call cad_apply --workspace "$work/workspace" --input "$work/invalid.json" > "$work/invalid.stdout" 2> "$work/invalid.stderr"; then
  echo 'Invalid fillet unexpectedly committed' >&2
  exit 1
fi
grep -q 'rounded' "$work/invalid.stderr"
printf '%s\n' '{"document_id":"plate"}' | "$exe" call cad_read --workspace "$work/workspace" --input - > "$work/reopened.json"
grep -q '"revision": 2' "$work/reopened.json"
printf '%s\n' '{"document_id":"plate","revision":2}' | "$exe" call cad_view --workspace "$work/workspace" --input - > "$work/view.json"
for page in "$work/workspace/exports/"plate-*.html; do
  test -s "$page"
  grep -q 'evaluation_id' "$page"
done

printf '%s\n' '{"action":"submit","request_id":"runtimeQuery","tool":"cad_query","arguments":{"document_id":"plate","revision":2}}' > "$work/job.json"
call_job() {
  job_retry=0
  while [ "$job_retry" -lt 100 ]; do
    if "$exe" call cad_job --workspace "$work/workspace" --input "$1" > "$2" 2> "$work/job-stderr.json"; then
      return 0
    fi
    if ! grep -Eq '"code"[[:space:]]*:[[:space:]]*"workspace_busy"' "$work/job-stderr.json"; then
      cat "$work/job-stderr.json" >&2
      return 1
    fi
    job_retry=$((job_retry + 1))
    sleep 0.05
  done
  cat "$work/job-stderr.json" >&2
  return 1
}
call_job "$work/job.json" "$work/submitted.json"
call_job "$work/job.json" "$work/duplicate.json"
printf '%s\n' '{"action":"get","job_id":"runtimeQuery"}' > "$work/get-job.json"
attempt=0
while [ "$attempt" -lt 100 ]; do
  call_job "$work/get-job.json" "$work/job-result.json"
  if grep -q '"state": "succeeded"' "$work/job-result.json"; then break; fi
  if grep -Eq '"state": "(failed|cancelled|interrupted)"' "$work/job-result.json"; then
    cat "$work/job-result.json" >&2
    exit 1
  fi
  attempt=$((attempt + 1))
  sleep 0.1
done
grep -q '"state": "succeeded"' "$work/job-result.json"
grep -q '"revision": 2' "$work/job-result.json"

printf '%s\n' \
  '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-11-25","capabilities":{},"clientInfo":{"name":"runtime-smoke","version":"1"}}}' \
  '{"jsonrpc":"2.0","method":"notifications/initialized"}' \
  '{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"cad_read","arguments":{"document_id":"plate"}}}' \
  '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"cad_export","arguments":{"document_id":"plate","revision":1,"format":"stl"}}}' \
  '{"jsonrpc":"2.0","id":4,"method":"resources/read","params":{"uri":"ui://agent-3d-cad/viewer.html"}}' \
  | "$exe" serve --workspace "$work/workspace" > "$work/mcp.jsonl"
test "$(wc -l < "$work/mcp.jsonl")" -eq 4
if grep -Eq '"error"|"isError":true' "$work/mcp.jsonl"; then
  cat "$work/mcp.jsonl" >&2
  exit 1
fi
test -s "$work/workspace/exports/plate-r1.stl"
grep -q 'text/html;profile=mcp-app' "$work/mcp.jsonl"
grep -q 'CadBridge' "$work/mcp.jsonl"

# Exercise all drawing formats in the same tool-free image, not just the builder.
"$exe" call cad_drawing --workspace "$work/workspace" --input "$examples/plate.drawing.json" > "$work/drawing.json"
grep -q '"revision": 2' "$work/drawing.json"
grep -q '"projection_tolerance_mm": 0.02' "$work/drawing.json"
drawing_count=0
for drawing in "$work/workspace/exports/"plate-r2-drawing-*; do
  test -s "$drawing/manifest.json"
  test -s "$drawing/drawing.json"
  test -s "$drawing/drawing.pdf"
  test -s "$drawing/drawing.svg"
  grep -q '<svg' "$drawing/drawing.svg"
  dxf_count=0
  for dxf in "$drawing/"*.dxf; do
    test -s "$dxf"
    grep -q 'AC1015' "$dxf"
    dxf_count=$((dxf_count + 1))
  done
  test "$dxf_count" -eq 4
  drawing_count=$((drawing_count + 1))
done
test "$drawing_count" -eq 1
printf '%s\n' '{"document_id":"plate"}' | "$exe" call cad_read --workspace "$work/workspace" --input - > "$work/after-drawing.json"
cmp "$work/reopened.json" "$work/after-drawing.json"
printf '%s\n' 'Native bundle create/edit/reopen/rollback/view/jobs/STEP/STL/drawings/MCP/app-resource workflow passed in runtime-only container'
