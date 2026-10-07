(() => {
  'use strict';
  const $ = id => document.getElementById(id), bridge = new CadBridge();
  let renderer = null, rendered = null, drawnEvaluation = null, sending = false, contextTimer = null, libraryTimer = null, disposed = false, libraryBusy = false, rendererError = null;
  let sizeObserver = null, resizeTimer = null, partsKey = null, exporting = false;
  const fmt = value => Number.isFinite(value) ? value.toLocaleString(undefined, { maximumFractionDigits: 3 }) : '—';
  const status = text => { $('edit-status').textContent = text; };
  const fail = error => { $('view-error').hidden = false; $('view-error').textContent = error.message; };
  function facts(node, items) {
    node.replaceChildren();
    for (const [label, value] of items) {
      const dt = document.createElement('dt'), dd = document.createElement('dd');
      dt.textContent = label; dd.textContent = value; node.append(dt, dd);
    }
  }
  function inspect(v) {
    const s = v.selection, summary = v.payload?.summary;
    $('clear-selection').hidden = !s; $('reference-details').hidden = !s;
    $('selection-badge').textContent = s ? `Selected ${s.reference.kind}` : 'Whole model';
    $('selection-name').textContent = s ? `${s.reference.kind === 'edge' ? 'Edge' : 'Face'} · ${s.geometry.curve_kind || s.geometry.surface_kind || s.reference.entity_id}` : 'Model overview';
    $('selection-help').textContent = s ? 'Ask your agent to use this selection. Copy request includes its precise face or edge reference.' : 'Select a face or edge, then ask your connected agent to edit it.';
    $('reference').textContent = s ? JSON.stringify(s.reference, null, 2) : '';
    let rows = [];
    if (s) {
      const e = s.geometry;
      if (e.part_id) rows.push(['Part', e.part_id]);
      if (Number.isFinite(e.length_mm)) rows.push(['Length', `${fmt(e.length_mm)} mm`]);
      if (Number.isFinite(e.area_mm2)) rows.push(['Area', `${fmt(e.area_mm2)} mm²`]);
      if (Number.isFinite(e.radius_mm)) rows.push(['Radius', `${fmt(e.radius_mm)} mm`]);
      if (e.center_mm) rows.push(['Center', e.center_mm.map(fmt).join(', ')]);
      rows.push(['Feature', s.reference.feature_id], ['Revision', s.reference.revision]);
    } else if (summary) {
      rows = [['Volume', `${fmt(summary.volume_mm3)} mm³`], ['Surface area', `${fmt(summary.area_mm2)} mm²`], ['Solids', summary.solid_count], ['Faces / edges', `${summary.face_count} / ${summary.edge_count}`]];
      if (summary.assembly) rows.unshift(['Parts', summary.assembly.parts.length], ['Rigid mates', summary.assembly.mates.length]);
    }
    facts($('measurements'), rows);
  }
  function modelTree(model) {
    $('features').replaceChildren(); $('feature-count').textContent = model?.features?.length || 0;
    for (const feature of model?.features || []) {
      const detail = document.createElement('details'), title = document.createElement('summary'), type = document.createElement('span'), code = document.createElement('pre');
      title.append(document.createTextNode(feature.id)); type.className = 'feature-type';
      type.textContent = `${feature.type}${feature.id === model.output ? ' · output' : ''}`; title.append(type);
      const shown = { ...feature }; if (shown.content) shown.content = '(embedded STEP content)';
      code.textContent = JSON.stringify(shown, null, 2); detail.append(title);
      if (feature.type === 'assembly') {
        for (const part of feature.parts) {
          const row = document.createElement('p'), mate = feature.mates?.find(item => item.child === part.id);
          row.className = 'assembly-part';
          row.textContent = `${part.id} → ${part.input} · ${mate ? `mated to ${mate.parent}` : 'placed root'}`;
          detail.append(row);
        }
      }
      detail.append(code); $('features').append(detail);
    }
    facts($('parameters'), Object.entries(model?.parameters || {}).map(([name, value]) => [name, fmt(value)]));
  }
  function partControls(v) {
    const parts = v.payload?.summary?.assembly?.parts || [], hidden = v.hidden_part_ids || [], ready = v.status === 'ready' && !sending;
    const key = JSON.stringify([v.payload?.evaluation_id, hidden, ready, v.visibility_unsaved]);
    if (key === partsKey) return; partsKey = key;
    $('parts-panel').hidden = !parts.length; $('parts-list').replaceChildren();
    $('parts-status').textContent = `${hidden.length} of ${parts.length} parts hidden`;
    $('show-all-parts').disabled = !ready || (!hidden.length && !v.visibility_unsaved);
    $('all-hidden').hidden = !parts.length || hidden.length !== parts.length;
    $('restore-parts').disabled = !ready;
    for (const part of parts) {
      const row = document.createElement('div'), name = document.createElement('span'), toggle = document.createElement('button'), isolate = document.createElement('button');
      const isHidden = hidden.includes(part.id);
      row.className = `part-control${isHidden ? ' part-hidden' : ''}`; name.textContent = part.id; name.title = `${part.id} → ${part.input}`;
      toggle.textContent = isHidden ? 'Show' : 'Hide'; toggle.setAttribute('aria-label', `${isHidden ? 'Show' : 'Hide'} part ${part.id}`);
      isolate.textContent = 'Isolate'; isolate.setAttribute('aria-label', `Isolate part ${part.id}`);
      toggle.disabled = isolate.disabled = !ready;
      toggle.onclick = () => state.setPartVisible(part.id, isHidden).catch(error => status(error.message));
      isolate.onclick = () => state.isolatePart(part.id).catch(error => status(error.message));
      row.append(name, toggle, isolate); $('parts-list').append(row);
    }
  }
  function update(v) {
    const ready = v.status === 'ready' && !!v.payload;
    $('export-model').disabled = !ready || exporting;
    $('connection').textContent = ({ connecting: 'Connecting', loading: 'Updating', ready: 'Live', empty: 'Connected', error: 'Needs attention' })[v.status] || v.status;
    $('connection').className = `connection${ready ? ' ready' : ''}`;
    const displayed = v.payload || v;
    $('document-title').textContent = displayed.document_id || 'Your workspace';
    $('revision').textContent = displayed.revision ? `Revision ${displayed.revision} · mm` : 'Editable native CAD';
    $('loading').hidden = !['connecting', 'loading'].includes(v.status);
    $('empty-state').hidden = !!v.payload || !['empty', 'connecting'].includes(v.status);
    const graphicsReady = !!v.payload && drawnEvaluation === v.payload.evaluation_id;
    $('viewport').style.visibility = graphicsReady ? 'visible' : 'hidden';
    $('send').disabled = !ready || sending || !bridge.capabilities.message || !$('prompt').value.trim();
    $('copy-request').disabled = !ready || sending;
    $('include-capture').disabled = !ready || sending || !graphicsReady;
    if (!graphicsReady) $('include-capture').checked = false;
    $('prompt').disabled = sending;
    $('update-status').textContent = ready ? 'Following saved revisions' : v.error || (v.status === 'empty' ? 'Choose a model or ask your agent to create one' : 'Preparing current revision');
    if (v.error || rendererError) fail(Error(v.error || rendererError));
    else if (v.status !== 'error') $('view-error').hidden = true;
    if (v.payload !== rendered && v.payload) {
      const same = rendered?.document_id === v.payload.document_id;
      drawnEvaluation = null;
      try {
        renderer?.load(v.payload, { preserveCamera: same, hiddenPartIds: v.hidden_part_ids || [] });
        if (v.camera) renderer?.setCamera(v.camera);
        v.camera = renderer?.getCamera() || null;
        rendered = v.payload; modelTree(v.model); status('');
      } catch (error) { rendererError = error.message; fail(error); }
    }
    if (!v.payload) { modelTree(null); rendered = null; drawnEvaluation = null; }
    partControls(v);
    $('loading-text').textContent = v.payload ? 'Updating… Previous revision shown' : 'Building current revision…';
    if (v.context_error) status(v.context_error);
    try { renderer?.setHiddenParts(v.hidden_part_ids || []); renderer?.setSelection(v.selection?.reference || null); } catch (error) { fail(error); }
    inspect(v);
    const s = v.payload?.summary;
    $('model-facts').textContent = s ? `${s.assembly ? `${s.assembly.parts.length} parts · ${(v.hidden_part_ids || []).length} hidden · ` : ''}${s.solid_count} solid${s.solid_count === 1 ? '' : 's'} · ${s.face_count} faces · ${s.edge_count} edges` : 'Ready when you are';
    $('dimensions').textContent = s ? s.bounds_mm.min.map((n, i) => fmt(s.bounds_mm.max[i] - n)).join(' × ') + ' mm' : '';
    for (const button of $('documents').querySelectorAll('button')) button.setAttribute('aria-current', String(button.dataset.document === v.document_id));
  }
  const state = new CadLiveState(bridge, update);
  for (const id of ['show-all-parts', 'restore-parts']) $(id).onclick = () => state.showAll().catch(error => status(error.message));
  function saveSoon() {
    clearTimeout(contextTimer);
    contextTimer = setTimeout(() => {
      if (state.value.status === 'ready') state.publishContext().catch(error => status(error.message));
    }, 350);
  }
  try {
    renderer = new CadRenderer($('viewport'), {
      onPick(selection, detail) {
        if (state.value.status !== 'ready') { renderer?.setSelection(null); return; }
        if (selection && ['document_id', 'revision', 'evaluation_id', 'feature_id'].some(key => selection.reference[key] !== state.value.payload?.[key])) {
          renderer?.setSelection(null); status('That selection belongs to a previous model view. Wait for the current view.'); return;
        }
        state.update({ selection }); saveSoon();
        if (detail?.ambiguous) status(detail.message || 'This geometry overlaps. Rotate the model and select again.');
      },
      onCamera(camera) { state.value.camera = camera; if (state.value.status === 'ready') saveSoon(); },
      onError(error) { rendererError = error.message; drawnEvaluation = null; $('viewport').style.visibility = 'hidden'; fail(error); },
      onReady(identity) { if (identity.evaluation_id === state.value.payload?.evaluation_id) { rendererError = null; drawnEvaluation = identity.evaluation_id; update(state.value); } }
    });
  } catch (error) { fail(error); }
  async function refreshLibrary() {
    if (libraryBusy || disposed) return;
    libraryBusy = true;
    try {
      const result = await bridge.tool('cad_list');
      if (disposed) return;
      $('documents').replaceChildren();
      for (const doc of result.documents) {
        const b = document.createElement('button'), icon = document.createElement('span'), name = document.createElement('span'), revision = document.createElement('span');
        b.hidden = !doc.document_id.toLowerCase().includes($('project-search').value.toLowerCase());
        b.dataset.document = doc.document_id; b.setAttribute('aria-current', String(doc.document_id === state.value.document_id));
        icon.className = 'doc-glyph'; icon.textContent = '◇'; name.className = 'doc-name'; name.textContent = doc.document_id; revision.className = 'doc-revision'; revision.textContent = `r${doc.revision}`;
        b.append(icon, name, revision);
        b.onclick = async () => {
          b.disabled = true;
          try {
            state.invalidate();
            await bridge.tool('cad_show', { view_id: state.value.view_id, document_id: doc.document_id });
            state.update({ status: 'loading', selection: null, error: null }); await state.pollOnce();
          } catch (error) { fail(error); } finally { b.disabled = false; }
        };
        $('documents').append(b);
      }
      if (!result.documents.length) { const p = document.createElement('p'); p.className = 'muted'; p.textContent = 'No saved models yet. Ask your agent to create one.'; $('documents').append(p); }
      if (result.truncated) { const p = document.createElement('p'); p.className = 'muted'; p.textContent = 'Showing the first 1,000 models.'; $('documents').append(p); }
    } catch (error) { status(error.message); } finally { libraryBusy = false; }
  }
  $('refresh-library').onclick = refreshLibrary;
  $('project-search').oninput = () => { for (const button of $('documents').querySelectorAll('button')) button.hidden = !button.dataset.document.toLowerCase().includes($('project-search').value.toLowerCase()); };
  $('open-workspace').onclick = () => bridge.desktop?.openWorkspace().catch(error => status(error.message));
  $('export-model').onclick = async () => {
    if (!state.value.payload || exporting) return;
    const { document_id, revision } = state.value.payload, format = $('export-format').value;
    exporting = true; update(state.value); status(`Preparing ${format.toUpperCase()} for revision ${revision}…`);
    try {
      if (bridge.desktop) {
        const result = await bridge.desktop.export({ document_id, revision, format });
        status(result.cancelled ? 'Save cancelled. The export remains in your workspace.' : `Saved: ${result.paths.join(', ')}`);
      } else {
        const drawing = ['pdf', 'svg', 'dxf'].includes(format);
        const args = { document_id, revision, ...(drawing ? { drawing: { formats: [format] } } : { format }) };
        let job = await bridge.tool('cad_job', { action: 'submit', request_id: `export_${crypto.randomUUID()}`, tool: drawing ? 'cad_drawing' : 'cad_export', arguments: args, budget: { timeout_ms: 300000, memory_mb: 2048 } });
        const deadline = Date.now() + 310000;
        while (['queued', 'running', 'cancelling'].includes(job.state) && !disposed && Date.now() < deadline) {
          await new Promise(resolve => setTimeout(resolve, 500));
          job = await bridge.tool('cad_job', { action: 'get', job_id: job.job_id });
        }
        if (disposed) return;
        if (job.state !== 'succeeded') throw Error(job.error?.message || `Export ${job.state}. Job: ${job.job_id}`);
        const paths = drawing ? job.result.artifacts.map(item => item.path) : [job.result.path];
        $('copy-fallback').hidden = false; $('copy-fallback').open = true; $('copy-text').value = paths.join('\n');
        status('Export saved in the CAD workspace. File paths are below.');
      }
    } catch (error) { status(error.message); }
    finally { exporting = false; if (!disposed) update(state.value); }
  };
  for (const mode of ['face', 'edge']) $(`mode-${mode}`).onclick = () => {
    renderer?.setMode(mode); state.update({ selection: null }); saveSoon();
    for (const other of ['face', 'edge']) $(`mode-${other}`).setAttribute('aria-pressed', String(other === mode));
  };
  $('fit').onclick = () => renderer?.reset();
  for (const view of ['iso', 'top', 'front', 'right']) $(`view-${view}`).onclick = () => renderer?.setView(view);
  $('clear-selection').onclick = () => { state.update({ selection: null }); saveSoon(); };
  $('prompt').addEventListener('input', () => update(state.value));
  $('send').onclick = async () => {
    if (sending) return;
    sending = true; update(state.value); status('Sending request…');
    try {
      const image = $('include-capture').checked ? renderer?.capture() : null;
      await state.sendPrompt($('prompt').value, image);
      $('prompt').value = ''; status('Request handed to chat. If it appears in the composer, press Send there.');
    } catch (error) { status(error.message); }
    finally { sending = false; update(state.value); }
  };
  $('copy-request').onclick = async () => {
    try {
      const snapshot = state.snapshot($('prompt').value.trim());
      const text = CadLiveState.promptText(snapshot, bridge.hostContext.workspace);
      await state.saveContext(snapshot);
      try { await navigator.clipboard.writeText(text); status('Request copied. Paste it into your chat.'); }
      catch { $('copy-fallback').hidden = false; $('copy-fallback').open = true; $('copy-text').value = text; $('copy-text').focus(); $('copy-text').select(); status('Select and copy the request below.'); }
    } catch (error) { status(error.message); }
  };
  $('fullscreen').onclick = async () => {
    try {
      const result = await bridge.request('ui/request-display-mode', { mode: 'fullscreen' });
      if (result.mode !== 'fullscreen') status('The host kept this view inline.');
    } catch (error) { status(error.message); }
  };
  function dispose() {
    if (disposed) return; disposed = true; clearTimeout(contextTimer); clearInterval(libraryTimer); clearTimeout(resizeTimer); sizeObserver?.disconnect();
    state.dispose(); renderer?.destroy(); bridge.dispose();
  }
  bridge.on('ui/resource-teardown', dispose); window.addEventListener('pagehide', dispose, { once: true });
  function hostLayout() {
    const context = bridge.hostContext;
    $('fullscreen').hidden = context.displayMode === 'fullscreen' || !context.availableDisplayModes?.includes('fullscreen');
    clearTimeout(resizeTimer);
    resizeTimer = setTimeout(() => {
      if (!disposed) bridge.reportSize(document.body.scrollWidth, document.body.scrollHeight);
    }, 50);
  }
  bridge.on('ui/notifications/host-context-changed', hostLayout);
  bridge.on('ui/notifications/tool-result', result => {
    try { const value = CadBridge.value(result); if (value.view_id) state.attach(value.view_id); }
    catch (error) { fail(error); }
  });
  bridge.initialize().then(async () => {
    $('capture-option').hidden = !bridge.capabilities.message?.image;
    $('open-workspace').hidden = !bridge.desktop;
    $('workspace-label').textContent = bridge.hostContext.workspace || 'Saved projects';
    if (bridge.desktop) {
      const recent = await bridge.desktop.recentWorkspaces();
      $('recent-workspaces').hidden = recent.length < 2;
      for (const [index, folder] of recent.entries()) {
        const option = document.createElement('option'); option.value = index; option.textContent = folder;
        $('recent-workspaces').append(option);
      }
      $('recent-workspaces').onchange = () => { if ($('recent-workspaces').value !== '') bridge.desktop.openRecent(Number($('recent-workspaces').value)).catch(error => status(error.message)); };
      $('send').hidden = true;
      document.querySelector('.fine-print').textContent = `Selections are shared with agents using this workspace and view “${state.value.view_id}”. Ask in chat to use the selected face or edge, or paste a copied request. Saved edits appear automatically.`;
    }
    hostLayout();
    if (typeof ResizeObserver !== 'undefined') { sizeObserver = new ResizeObserver(hostLayout); sizeObserver.observe(document.body); }
    if (!bridge.capabilities.message) status('This host supports Copy request. Paste the request into your chat.');
    update(state.value); await refreshLibrary();
    libraryTimer = setInterval(refreshLibrary, 8000);
    state.start();
  }).catch(error => { state.update({ status: 'error', error: error.message }); });
})();
