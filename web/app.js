(() => {
  'use strict';
  const $ = id => document.getElementById(id), bridge = new CadBridge();
  let renderer = null, rendered = null, drawnEvaluation = null, sending = false, contextTimer = null, libraryTimer = null, disposed = false, libraryBusy = false, rendererError = null;
  let sizeObserver = null, resizeTimer = null, partsKey = null, partsDocument = null, exporting = false, shell = null;
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
    if(v.payload?.read_only){
      $('clear-selection').hidden=!s;$('reference-details').hidden=!s;$('selection-badge').textContent=s?s.reference.kind.replace('_',' '):'Whole artifact';
      $('selection-name').textContent=s?(s.geometry.name||s.geometry.source_ref||s.reference.entity_id):'Artifact overview';
      $('selection-help').textContent='Inspect mesh groups or curves and discuss them with your agent. These labels belong to this captured review.';
      $('reference').textContent=s?JSON.stringify(s.reference,null,2):'';
      facts($('measurements'),s?[['Review label',s.reference.entity_id],['Source location',s.geometry.source_ref]]:[['Vertices',summary.vertices],['Triangles',summary.triangles],['Mesh groups',summary.groups],['Curves',summary.curves]]);return;
    }
    $('clear-selection').hidden = !s; $('reference-details').hidden = !s;
    $('selection-badge').textContent = s ? `Selected ${s.reference.kind}` : 'Whole model';
    $('selection-name').textContent = s ? `${s.reference.kind === 'edge' ? 'Edge' : 'Face'} · ${s.geometry.curve_kind || s.geometry.surface_kind || s.reference.entity_id}` : 'Model overview';
    $('selection-help').textContent = v.payload?.draft ? 'Save or reset this pose to select geometry. You can send the whole preview to your agent.' : s ? 'Ask your agent to use this selection. Copy request includes its precise face or edge reference.' : 'Select a face or edge, then ask your connected agent to edit it.';
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
      if (summary.assembly) rows.unshift(['Parts', summary.assembly.parts.length], ['Mates', summary.assembly.mates.length]);
    }
    facts($('measurements'), rows);
  }
  let artifactKey='';
  function artifactControls(v) {
    const p=v.payload,a=p?.artifact,readonly=p?.read_only===true;
    $('artifact-review-panel').hidden=!readonly;
    $('artifact-semantic-empty').hidden=!readonly||p.summary.bounds!==null;
    if(readonly)$('artifact-empty-help').textContent=p.summary.representation==='robot_semantics'?'This file contains robot semantics without geometry.':'This review contains no display geometry.';
    $('feature-heading-title').textContent=readonly?'Review data':'Features';
    $('library-foot').textContent=readonly?'Read-only artifact · Display in millimeters':'Exact geometry · OpenCascade 8.0.1 · Dimensions in millimeters';
    $('edit-heading').textContent=readonly?'Discuss review':'Quick Edit';
    $('prompt').placeholder=readonly?'Ask about this artifact…':'Describe a change…\ne.g. Round this edge to 2 mm';
    $('edit-help').textContent=readonly?'Your agent can inspect this captured artifact and its source references. Open its editable native source to make CAD edits.':'If your host puts the request in the chat composer, press Send there. Your agent reviews the request and makes the edit. This view updates when the new revision is saved.';
    $('export-panel').hidden=readonly;$('parameters-panel').hidden=readonly;
    const key=readonly?a.review_sha256:'';if(key===artifactKey)return;artifactKey=key;
    if(!readonly)return;
    facts($('artifact-source'),[['Format',a.source.format.toUpperCase()],['Source units',a.source.declared_units],['Display units','mm'],['Representation',p.summary.representation.replaceAll('_',' ')],['Captured references',a.source.references.length]]);
    $('artifact-hashes').textContent=`Source SHA-256\n${a.source.sha256}\n\nReview SHA-256\n${a.review_sha256}`;
    $('artifact-limitations').replaceChildren();for(const text of p.limitations){const item=document.createElement('li');item.textContent=text;$('artifact-limitations').append(item);}
    const association=a.source.native_source_association;
    $('artifact-association').textContent=association?`Declared source: ${association.reference.document_id} r${association.reference.revision} · ${association.reference.feature_id}. This association does not establish geometric provenance.`:'No editable native source is associated with this artifact.';
    $('features').replaceChildren();const groups=p.artifact_geometry.groups,curves=p.artifact_geometry.polylines;
    $('feature-count').textContent=groups.length+curves.length;
    for(const item of [...groups,...curves]){const detail=document.createElement('details'),title=document.createElement('summary'),body=document.createElement('pre');title.textContent=item.name||item.id;const shown={...item};if(shown.points){shown.point_count=shown.points.length;delete shown.points;}body.textContent=JSON.stringify(shown,null,2);detail.append(title,body);$('features').append(detail);}
    const metadata=document.createElement('details'),title=document.createElement('summary'),body=document.createElement('pre');title.textContent='Source metadata';body.textContent=JSON.stringify(p.metadata,null,2);metadata.append(title,body);$('features').append(metadata);
  }
  function modelTree(model, summary) {
    $('features').replaceChildren(); $('feature-count').textContent = model?.features?.length || 0;
    for (const feature of model?.features || []) {
      const detail = document.createElement('details'), title = document.createElement('summary'), type = document.createElement('span'), code = document.createElement('pre');
      title.append(document.createTextNode(feature.id)); type.className = 'feature-type';
      type.textContent = `${feature.type}${feature.id === model.output ? ' · output' : ''}`; title.append(type);
      const shown = { ...feature }; if (shown.content) shown.content = '(embedded STEP content)';
      code.textContent = JSON.stringify(shown, null, 2); detail.append(title);
      const component = summary?.components?.find(item => item.id === feature.id);
      if (component) {
        const source = document.createElement('p'); source.className = 'assembly-part';
        source.textContent = `Pinned from ${component.source.document_id} r${component.source.revision} · ${component.source.feature_id}${component.modified ? ' · local edits' : ''}`;
        detail.append(source);
      }
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
  }
  let parameterKey = '', partTabsKey = '', comparisonToken = 0, comparisonKey = '';
  const parameterDisplay = value => value !== 0 && Math.abs(value) < .001 ? value : Number(value.toFixed(3));
  function parameterControls(v) {
    const p = v.payload, native = !!p && !p.read_only && !v.read_only;
    $('parameters').dataset.readonly = native ? '' : 'true';
    const values = native ? v.model?.parameters || {} : {}, key = JSON.stringify([p?.document_id, p?.revision, values]);
    if (key !== parameterKey) {
      parameterKey = key; $('parameters').replaceChildren();
      for (const [name, value] of Object.entries(values)) {
        const row = document.createElement('div'), heading = document.createElement('div'), label = document.createElement('label'), number = document.createElement('input'), slider = document.createElement('input');
        row.className = 'parameter-row'; heading.className = 'parameter-label'; label.textContent = name.toUpperCase();
        number.type = 'number'; number.min = -1000000; number.max = 1000000; number.step = 'any'; number.value = parameterDisplay(value); number.id = `parameter-${name}`; label.htmlFor = number.id;
        const range = CadShell.parameterRange(value); slider.type = 'range'; slider.className = 'parameter-slider'; Object.assign(slider, range); slider.value = value;
        slider.setAttribute('aria-label', `Adjust ${name}`); slider.title = 'Release to rebuild and save'; number.title = 'Enter a value to rebuild and save';
        const fill = () => slider.style.setProperty('--fill', `${100 * (Number(slider.value) - range.min) / (range.max - range.min)}%`);
        fill(); number.onfocus = () => { number.value = value; }; slider.oninput = () => { number.value = parameterDisplay(Number(slider.value)); fill(); };
        const commit = async raw => {
          if (parameterKey !== key || state.value.payload?.document_id !== p.document_id || state.value.payload?.revision !== p.revision) return;
          try { if (raw === '') throw Error('Enter a number.'); await state.editParameter(name, Number(raw)); }
          catch (error) { if(parameterKey === key) state.update({parameter_source:p.document_id,parameter_error:error.message,parameter_message:null}); }
          finally { const saved = state.value.model?.parameters?.[name] ?? value; number.value = parameterDisplay(saved); slider.value = saved; fill(); }
        };
        slider.onchange = () => commit(slider.value); number.onblur = () => commit(number.value);
        number.onkeydown = event => { if(event.key === 'Enter') { event.preventDefault(); number.blur(); } };
        heading.append(label, number); row.append(heading, slider); $('parameters').append(row);
      }
    }
    const ready = native && v.status === 'ready' && !p.draft && !v.parameter_busy && !v.preset_busy && !v.sequence_busy && !sending;
    for (const input of $('parameters').querySelectorAll('input')) input.disabled = !ready;
    $('parameter-status').textContent = (v.parameter_source === p?.document_id ? v.parameter_error || v.parameter_message : '') || (p?.draft ? 'Save or reset the pose to edit parameters.' : '');
    const source = native ? `${p.document_id}/${p.revision}` : '';
    if (source !== comparisonKey) {
      comparisonKey = source; ++comparisonToken; $('compare-revision').replaceChildren();
      const none = document.createElement('option'); none.value = ''; none.textContent = 'None'; $('compare-revision').append(none);
      // Bound the menu for long histories; the most recent revisions are the useful comparisons.
      if (native) for (let revision = p.revision - 1; revision >= Math.max(1, p.revision - 50); revision--) {
        const option = document.createElement('option'); option.value = revision; option.textContent = `Revision ${revision}`; $('compare-revision').append(option);
      }
      $('compare-result').hidden = true; $('compare-result').textContent = '';
    }
    $('compare-revision').disabled = !ready || p.revision < 2;
  }
  $('compare-revision').onchange = async () => {
    const token = ++comparisonToken, revision = Number($('compare-revision').value), p = state.value.payload, values = state.value.model?.parameters || {};
    $('compare-result').hidden = !revision; if (!revision) return;
    $('compare-result').textContent = 'Loading revision…';
    try {
      const record = await bridge.tool('cad_read', { document_id: p.document_id, revision });
      if (token !== comparisonToken || state.value.payload?.document_id !== p.document_id || state.value.payload?.revision !== p.revision) return;
      if (record.document_id !== p.document_id || record.revision !== revision) throw Error('Comparison belongs to another revision.');
      const previous = record.model.parameters, names = [...new Set([...Object.keys(previous), ...Object.keys(values)])];
      const changes = names.filter(name => previous[name] !== values[name]).map(name => `${name}: ${fmt(previous[name])} → ${fmt(values[name])}`);
      $('compare-result').textContent = changes.length ? changes.join(' · ') : 'Parameters match this revision.';
    } catch (error) { if (token === comparisonToken) $('compare-result').textContent = error.message; }
  };
  function partTabs(v) {
    const p = v.payload, parts = p?.summary?.assembly?.parts || [], hidden = v.hidden_part_ids || [], ready = v.status === 'ready' && !v.preset_busy && !sending;
    const key = JSON.stringify([p?.document_id, p?.evaluation_id, hidden, ready]); if (key === partTabsKey) return; partTabsKey = key;
    $('part-tabs').replaceChildren();
    $('part-tabs').dataset.multiple = String(parts.length > 1);
    const all = document.createElement('button'); all.textContent = 'Assembled'; all.disabled = !ready;
    all.setAttribute('aria-pressed', String(!hidden.length)); all.onclick = () => { if(parts.length) state.showAll().catch(error => status(error.message)); else { for(const button of $('part-tabs').querySelectorAll('button')) button.setAttribute('aria-pressed',String(button===all)); } }; $('part-tabs').append(all);
    const leaves = parts.length ? parts : p ? [{ id: p.feature_id, input: v.model?.output || 'Model' }] : [];
    for (const part of leaves) {
      const button = document.createElement('button'); button.textContent = parts.length ? part.id.replaceAll('/', ' / ') : (v.model?.output || 'Model').replaceAll('_',' ').replace(/^./,letter=>letter.toUpperCase()); button.title = `${part.id} → ${part.input}`;
      button.disabled = !ready; button.setAttribute('aria-pressed', String(parts.length > 0 && hidden.length === parts.length - 1 && !hidden.includes(part.id)));
      button.onclick = () => { if(parts.length) state.isolatePart(part.id).catch(error => status(error.message)); else { for(const item of $('part-tabs').querySelectorAll('button')) item.setAttribute('aria-pressed',String(item===button)); } }; $('part-tabs').append(button);
    }
  }
  function partControls(v) {
    const assembly = v.payload?.summary?.assembly, parts = assembly?.parts || [], hidden = v.hidden_part_ids || [], ready = v.status === 'ready' && !sending && !v.preset_busy;
    if (partsDocument !== v.payload?.document_id) { partsDocument = v.payload?.document_id; $('parts-search').value = ''; }
    const search = $('parts-search').value.trim().toLowerCase();
    const key = JSON.stringify([v.payload?.evaluation_id, hidden, ready, v.visibility_unsaved, search]);
    if (key === partsKey) return; partsKey = key;
    $('parts-panel').hidden = !parts.length; $('parts-list').replaceChildren();
    $('parts-status').textContent = `${hidden.length} of ${parts.length} parts hidden`;
    $('show-all-parts').disabled = !ready || (!hidden.length && !v.visibility_unsaved);
    $('all-hidden').hidden = !parts.length || hidden.length !== parts.length;
    $('restore-parts').disabled = !ready;
    const nodes = assembly?.tree || parts.map(part => ({ ...part, kind: 'part' }));
    const matches = new Set(), ancestors = new Set();
    if (search) for (const node of nodes) if (node.id.toLowerCase().includes(search) || node.input.toLowerCase().includes(search)) {
      matches.add(node.id);
      let parent = node.id;
      while (parent.includes('/')) { parent = parent.slice(0, parent.lastIndexOf('/')); ancestors.add(parent); }
    }
    let shown = 0;
    for (const part of nodes) {
      let match = !search || matches.has(part.id) || ancestors.has(part.id), parent = part.id;
      while (!match && parent.includes('/')) { parent = parent.slice(0, parent.lastIndexOf('/')); match = matches.has(parent); }
      if (!match) continue;
      shown++;
      const row = document.createElement('div'), name = document.createElement('span'), toggle = document.createElement('button'), isolate = document.createElement('button');
      const leaves = parts.filter(leaf => leaf.id === part.id || leaf.id.startsWith(part.id + '/'));
      const hiddenCount = leaves.filter(leaf => hidden.includes(leaf.id)).length, isHidden = hiddenCount === leaves.length;
      const group = part.kind === 'assembly', kind = group ? 'subassembly' : 'part';
      row.className = `part-control${isHidden ? ' part-hidden' : ''}${group ? ' part-group' : ''}`;
      row.style.paddingLeft = `${Math.min(5, part.id.split('/').length - 1) * 10}px`;
      name.textContent = `${part.id.split('/').at(-1)}${group ? ` · ${leaves.length} parts` : ''}${hiddenCount && !isHidden ? ' · partly hidden' : ''}`;
      name.title = `${part.id} → ${part.input}`;
      toggle.textContent = isHidden ? 'Show' : 'Hide'; toggle.setAttribute('aria-label', `${isHidden ? 'Show' : 'Hide'} ${kind} ${part.id}`);
      isolate.textContent = 'Isolate'; isolate.setAttribute('aria-label', `Isolate ${kind} ${part.id}`);
      toggle.disabled = isolate.disabled = !ready;
      toggle.onclick = () => state.setPartVisible(part.id, isHidden).catch(error => status(error.message));
      isolate.onclick = () => state.isolatePart(part.id).catch(error => status(error.message));
      row.append(name, toggle, isolate); $('parts-list').append(row);
    }
    $('parts-empty').hidden = shown !== 0;
  }
  $('parts-search').oninput = () => partControls(state.value);
  let motionKey = '', motionDefinition = '', motionDocument = null;
  function motionControls(v) {
    const assembly = v.payload?.summary?.assembly, ready = v.status === 'ready' && !sending && !v.preset_busy;
    const scopes = assembly?.mechanisms || (assembly?.motion?.dofs?.length ? [{ assembly_id: v.payload.feature_id, occurrences: [''], motion: assembly.motion }] : []);
    if (motionDocument !== v.payload?.document_id) { motionDocument = v.payload?.document_id; motionDefinition = ''; }
    if (!scopes.some(scope => scope.assembly_id === motionDefinition)) motionDefinition = scopes[0]?.assembly_id || '';
    const scope = scopes.find(scope => scope.assembly_id === motionDefinition), motion = scope?.motion;
    const key = JSON.stringify([v.payload?.evaluation_id, ready, v.saving, v.status, v.motion_error, motionDefinition]);
    if (key === motionKey) return; motionKey = key;
    $('motion-panel').hidden = !motion?.dofs?.length;
    $('motion-values').replaceChildren(); $('motion-poses').replaceChildren();
    if (!motion?.dofs?.length) return;
    $('motion-mechanism').replaceChildren();
    for (const scope of scopes) {
      const option = document.createElement('option'); option.value = scope.assembly_id;
      option.textContent = `${scope.assembly_id} · ${scope.occurrences.length} occurrence${scope.occurrences.length === 1 ? '' : 's'}`;
      $('motion-mechanism').append(option);
    }
    $('motion-mechanism').value = motionDefinition; $('motion-mechanism').disabled = !ready;
    $('motion-occurrences').textContent = `Affects ${scope.occurrences.map(path => path || 'the displayed assembly').join(', ')}. Reused definitions share this pose.`;
    const draft = !!v.payload.draft;
    $('motion-status').textContent = v.motion_error || (v.saving ? 'Saving pose…' : v.status === 'loading' ? 'Evaluating pose…' : draft ? 'Unsaved pose · Save to keep it' : 'Saved pose');
    $('motion-save').disabled = !ready || !draft;
    $('motion-name').disabled = !ready || !draft;
    $('motion-reset').disabled = (v.saving && v.status !== 'error') || (!draft && v.status === 'ready');
    const option = document.createElement('option'); option.value = ''; option.textContent = 'Choose a saved pose…'; $('motion-poses').append(option);
    for (const id of motion.poses) { const item = document.createElement('option'); item.value = id; item.textContent = id; $('motion-poses').append(item); }
    $('motion-poses').disabled = !ready || !motion.poses.length;
    for (const dof of motion.dofs) {
      const row = document.createElement('div'), label = document.createElement('label'), input = document.createElement('input');
      row.className = 'joint-control'; label.textContent = `${dof.mate_id} · ${dof.unit}`;
      input.type = 'number'; input.min = dof.minimum; input.max = dof.maximum; input.step = 'any'; input.value = dof.value;
      input.setAttribute('aria-label', `${dof.mate_id} ${dof.coordinate}`); input.disabled = !ready || dof.driven;
      label.append(input); row.append(label);
      if (dof.driven) {
        const note = document.createElement('span'); note.className = 'muted'; note.textContent = `Driven by ${dof.coupling_id}`; row.append(note);
      } else {
        const slider = document.createElement('input'); slider.type = 'range'; slider.min = dof.minimum; slider.max = dof.maximum;
        slider.step = 'any'; slider.value = dof.value; slider.disabled = !ready || dof.minimum === dof.maximum;
        slider.setAttribute('aria-label', `Adjust ${dof.mate_id} ${dof.coordinate}`);
        slider.oninput = () => { input.value = slider.value; };
        const preview = raw => {
          const value = Number(raw);
          if (raw === '' || !Number.isFinite(value) || value < dof.minimum || value > dof.maximum) {
            $('motion-status').textContent = `Choose a value from ${fmt(dof.minimum)} to ${fmt(dof.maximum)} ${dof.unit}.`; return;
          }
          if (value === dof.value) return;
          const values = motion.dofs.filter(item => !item.driven).map(item => ({ mate_id: item.mate_id, coordinate: item.coordinate,
            value: item.mate_id === dof.mate_id && item.coordinate === dof.coordinate ? value : item.value }));
          state.previewValues(values, scope.assembly_id).catch(error => { $('motion-status').textContent = error.message; });
        };
        slider.onchange = () => preview(slider.value); input.onchange = () => preview(input.value); row.append(slider);
      }
      $('motion-values').append(row);
    }
  }
  function update(v) {
    const ready = v.status === 'ready' && !!v.payload;
    $('export-model').disabled = !ready || exporting || !!v.payload?.draft || !!v.payload?.read_only;
    $('connection').textContent = ({ connecting: 'Connecting', loading: 'Updating', ready: 'Live', empty: 'Connected', error: 'Needs attention' })[v.status] || v.status;
    $('connection').className = `connection${ready ? ' ready' : ''}`;
    const displayed = v.payload || v;
    $('document-title').textContent = displayed.read_only?`${displayed.artifact.source.format.toUpperCase()} review`:displayed.document_id || 'Your workspace';
    $('revision').textContent = displayed.read_only?'Read-only artifact · mm':displayed.revision ? `${displayed.draft ? 'Preview of revision' : 'Revision'} ${displayed.revision} · mm` : 'Editable native CAD';
    $('loading').hidden = !['connecting', 'loading'].includes(v.status);
    $('empty-state').hidden = !!v.payload || !['empty', 'connecting'].includes(v.status);
    const graphicsReady = !!v.payload && drawnEvaluation === v.payload.evaluation_id;
    $('viewport').style.visibility = graphicsReady ? 'visible' : 'hidden';
    $('send').disabled = !ready || sending || !bridge.capabilities.message || !$('prompt').value.trim();
    $('copy-request').disabled = !ready || sending;
    $('include-capture').disabled = !ready || sending || !graphicsReady;
    $('save-image').disabled = !ready || !graphicsReady || sending || !!v.preset_busy;
    if (!graphicsReady) $('include-capture').checked = false;
    $('prompt').disabled = sending;
    $('update-status').textContent = ready ? (v.payload.read_only ? 'Captured external artifact · Read-only review' : v.payload.draft ? 'Unsaved pose · Save or reset to select geometry and export' : 'Following saved revisions') : v.error || (v.status === 'empty' ? 'Choose a model or ask your agent to create one' : 'Preparing current revision');
    if (v.error || rendererError) fail(Error(v.error || rendererError));
    else if (v.status !== 'error') $('view-error').hidden = true;
    if (v.payload !== rendered && v.payload) {
      parameterControls(v); shell?.refresh();
      const same = v.payload.read_only?rendered?.artifact?.review_sha256===v.payload.artifact.review_sha256:!rendered?.read_only&&rendered?.document_id === v.payload.document_id;
      drawnEvaluation = null;
      try {
        renderer?.load(v.payload, { preserveCamera: same, hiddenPartIds: v.hidden_part_ids || [],presentation:v.presentation,appearance:v.appearance });
        if (v.camera) renderer?.setCamera(v.camera); else if (!same) renderer?.reset();
        if(renderer)state.setCamera(renderer.getCamera());
        rendered = v.payload; if(!v.payload.read_only)modelTree(v.model, v.payload.summary); artifactKey='';status('');
      } catch (error) { rendererError = error.message; fail(error); }
    }
    if (!v.payload) { modelTree(null); rendered = null; drawnEvaluation = null; }
    artifactControls(v);
    parameterControls(v); partTabs(v);
    partControls(v);
    motionControls(v);sequenceControls(v);
    $('loading-text').textContent = v.saving ? 'Saving pose…' : v.payload ? 'Updating… Previous geometry shown' : 'Building current revision…';
    if (v.context_error) status(v.context_error);
    try {
      renderer?.setPresentation(v.presentation);
      renderer?.setAppearance(v.appearance);
      renderer?.setAnnotations(ready&&v.read_only!==true&&v.payload?.read_only!==true?v.annotations:[]);
      renderer?.setHiddenParts(v.hidden_part_ids || []);
      if(v.camera&&renderer&&JSON.stringify(renderer.getCamera())!==JSON.stringify(v.camera))renderer.setCamera(v.camera);
      renderer?.setSelection(v.selection?.reference || null);
      renderer?.setSection(ready && !v.payload?.draft && v.section_status?.state === 'succeeded' ? v.section_status.result : null);
    } catch (error) { fail(error); }
    presentationControls(v);
    appearanceControls(v);
    presetControls(v);
    annotationControls(v);
    sectionControls(v);
    measurementControls(v);
    inspect(v);
    shell?.refresh();
    const s = v.payload?.summary;
    $('model-facts').textContent = v.payload?.read_only?`${s.triangles} triangles · ${s.curves} curves · Read-only`:s ? `${s.assembly ? `${s.assembly.parts.length} parts · ${(v.hidden_part_ids || []).length} hidden · ` : ''}${s.solid_count} solid${s.solid_count === 1 ? '' : 's'} · ${s.face_count} faces · ${s.edge_count} edges` : 'Ready when you are';
    const bounds=v.payload?.read_only?s?.bounds:s?.bounds_mm;
    $('dimensions').textContent=bounds?bounds.min.map((n,i)=>fmt(bounds.max[i]-n)).join(' × ')+' mm':'';
    for (const button of $('documents').querySelectorAll('button')) button.setAttribute('aria-current', String(button.dataset.document === v.document_id));
  }
  const state = new CadLiveState(bridge, update);
  let sequenceFrames=[],sequenceSourceKey=null;
  function sequenceControls(v){
    const p=v.payload,ready=v.status==='ready'&&!!p&&!p.read_only&&!v.read_only&&!sending&&!v.sequence_busy,source=p&&!p.read_only?[p.document_id,p.revision,p.feature_id].join('/') : null;
    if(sequenceSourceKey&&source&&source!==sequenceSourceKey)sequenceFrames=[];if(source)sequenceSourceKey=source;
    $('sequence-panel').hidden=!p||!!p.read_only||!!v.read_only;
    if(p?.read_only||v.read_only){sequenceFrames=[];sequenceSourceKey=null;return;}
    const select=$('sequence-list'),previous=select.value,key=JSON.stringify([v.sequences.map(s=>[s.name,s.source.revision]),source]);
    if(select.dataset.options!==key){select.dataset.options=key;select.replaceChildren();const placeholder=document.createElement('option');placeholder.value='';placeholder.textContent=v.sequences.length?'Choose a sequence…':'No saved sequences';select.append(placeholder);
      for(const s of v.sequences){const option=document.createElement('option');option.value=s.name;option.textContent=s.name+(s.source.revision!==p.revision?' · older revision':'');select.append(option);}select.value=v.sequences.some(s=>s.name===previous)?previous:v.playback?.name||v.sequences[0]?.name||'';}
    const sequence=v.sequences.find(s=>s.name===select.value),current=sequence&&sequence.source.document_id===p?.document_id&&sequence.source.revision===p?.revision&&sequence.source.feature_id===p?.feature_id,position=v.playback?.name===sequence?.name?v.playback:null,duration=sequence?.frames.at(-1).time_s||1;
    select.disabled=!ready||v.playing;$('sequence-play').disabled=!ready||!current||v.playing;$('sequence-pause').disabled=!v.playing;$('sequence-delete').disabled=!ready||!sequence;$('sequence-refresh').disabled=!ready;
    $('sequence-seek').disabled=!ready||!current;$('sequence-seek').max=duration;$('sequence-seek').value=position?.time_s||0;$('sequence-time').textContent=`${(position?.time_s||0).toFixed(2)} / ${duration.toFixed(2)} s`;
    $('sequence-speed').disabled=!ready||!current||v.playing;$('sequence-loop').disabled=!ready||!current||v.playing;if(position){if(document.activeElement!==$('sequence-speed'))$('sequence-speed').value=position.speed;$('sequence-loop').checked=position.loop;}
    $('sequence-capture').disabled=!ready||v.playing||sequenceFrames.length>=64;$('sequence-save').disabled=!ready||p?.draft||sequenceFrames.length<2||v.playing;$('sequence-clear').disabled=!sequenceFrames.length;
    $('sequence-frames').textContent=sequenceFrames.length?`${sequenceFrames.length} keyframes · ${sequenceFrames.map(f=>f.time_s+' s').join(', ')}`:'No keyframes captured.';
    $('sequence-status').textContent=v.sequence_error||(v.sequence_busy?'Admitting native sample…':v.playing?'Playing · native samples evaluate in order':v.status!=='ready'?'Evaluating current pose…':position?.state==='displayed'?`Paused at ${position.time_s.toFixed(2)} s · source r${position.source.revision}`:position?'Ready to seek or play this sequence.':sequence&&!current?'This sequence belongs to an older source revision.':'Seek or play to preview a sequence. Reopening keeps it paused.');
  }
  const sequenceName=()=>$('sequence-list').value;
  $('sequence-refresh').onclick=()=>state.sequence('list').catch(error=>status(error.message));
  $('sequence-play').onclick=async()=>{try{await state.sequence('options',{name:sequenceName(),speed:Number($('sequence-speed').value),loop:$('sequence-loop').checked});await state.playSequence(sequenceName());}catch(error){status(error.message);}};
  $('sequence-pause').onclick=()=>state.pausePlayback();
  $('sequence-delete').onclick=()=>state.sequence('delete',{name:sequenceName()}).catch(error=>status(error.message));
  $('sequence-seek').onchange=()=>state.sequence('seek',{name:sequenceName(),time_s:Number($('sequence-seek').value)}).catch(error=>status(error.message));
  for(const id of ['sequence-speed','sequence-loop'])$(id).onchange=()=>state.sequence('options',{name:sequenceName(),speed:Number($('sequence-speed').value),loop:$('sequence-loop').checked}).catch(error=>status(error.message));
  $('sequence-capture').onclick=()=>{try{const frame=state.captureSequenceFrame(Number($('sequence-keytime').value));if(sequenceFrames.length&&frame.time_s<=sequenceFrames.at(-1).time_s)throw Error('Choose a time after the previous keyframe.');if(!sequenceFrames.length&&frame.time_s!==0)throw Error('Start the sequence at time zero.');sequenceFrames.push(frame);$('sequence-keytime').value=frame.time_s+1;sequenceControls(state.value);}catch(error){status(error.message);}};
  $('sequence-clear').onclick=()=>{sequenceFrames=[];$('sequence-keytime').value=0;sequenceControls(state.value);};
  $('sequence-save').onclick=async()=>{try{await state.sequence('save',{sequence:{name:$('sequence-name').value,frames:sequenceFrames}});sequenceControls(state.value);}catch(error){status(error.message);}};
  const measurementText=n=>Number.isFinite(n)?n.toLocaleString(undefined,{maximumSignificantDigits:8}):'—';
  function sectionControls(v) {
    const p = v.payload, current = v.status === 'ready' && !!p && !p.draft && !p.read_only, ready = current && !sending && !v.preset_busy;
    const result = v.section_status, restoring = !!v.section && !result, pending = restoring || ['queued', 'running', 'cancelling'].includes(result?.state);
    const report = current && result?.state === 'succeeded' ? result.result?.report : null;
    $('section-panel').hidden = !p||!!p.read_only;
    $('section-calculate').disabled = !ready || !v.presentation?.clip || !!v.sectioning || pending;
    $('section-clear').disabled = !ready || !!v.sectioning || !v.section;
    let message = 'Enable a clipping plane, then calculate its exact section.';
    if (v.section_error) message = v.section_error;
    else if (p?.draft) message = 'Save or reset this pose before calculating a section.';
    else if (v.status !== 'ready') message = 'Waiting for the current geometry…';
    else if (v.sectioning) message = 'Updating section…';
    else if (restoring) message = 'Restoring the current section…';
    else if (result?.state === 'cancelling') message = 'Cancelling section…';
    else if (pending) message = 'Calculating the section… You can clear it to cancel.';
    else if (result?.error) message = result.error.message;
    else if (report?.status === 'area') message = 'Section ready. Filled surfaces show the material cut by this plane.';
    else if (report?.status === 'tangent') message = report.curves.length ? 'The plane touches the model along curves; there is no filled surface.' : 'The plane touches the model at points; there is no filled surface.';
    else if (report?.status === 'empty') message = 'The plane does not intersect this model.';
    else if (['cancelled', 'interrupted'].includes(result?.state)) message = 'Section stopped. Calculate it again to retry.';
    else if (result?.state === 'failed') message = 'The section could not be completed. Calculate it again to retry.';
    else if (v.presentation?.clip) message = 'Calculate this plane to show filled section surfaces and exact dimensions.';
    $('section-state').textContent = message;
    const rows = [];
    if (report) {
      const count = report.sections.length;
      const scope = report.coverage === 'all_assembly_leaves' ? `All ${count} parts` : report.coverage === 'explicit_leaf_subset' ? `${count} selected part${count === 1 ? '' : 's'}` : 'Displayed feature';
      const holes = report.regions.reduce((sum, region) => sum + Math.max(0, region.wire_count - 1), 0);
      rows.push(['Total area', `${measurementText(report.area_mm2)} mm²`], ['Boundary length', `${measurementText(report.boundary_length_mm)} mm`], ['Scope', scope], ['Filled regions', report.regions.length], ['Holes', holes]);
      if (report.status === 'tangent') rows.push(['Contact curves', report.curves.length], ['Contact points', report.sections.reduce((sum, item) => sum + item.contact_points.length, 0)]);
    }
    facts($('section-result'), rows);
    $('section-coverage').hidden = !report;
    $('section-coverage').textContent = report ? `${report.coverage === 'explicit_leaf_subset' ? 'Only the selected parts are included. ' : ''}${report.coverage === 'feature_solids' ? '' : 'Hidden parts remain included. '}Overlapping solids are counted separately. Section surfaces are for review; select an original face or edge to edit the model.` : '';
  }
  $('section-calculate').onclick = () => {
    const p = state.value.presentation;
    if (!p?.clip) return;
    const query = { action: 'section', plane: { normal: [...p.clip.normal], offset_mm: p.clip.offset_mm }, explode: JSON.parse(JSON.stringify(p.explode)) };
    state.section(query).catch(error => status(error.message));
  };
  $('section-clear').onclick = () => state.section(null).catch(error => status(error.message));
  const targetText=t=>t?.kind==='part'?t.part_id:t?`${t.kind==='face'?'Face':'Edge'} ${t.entity_id}`:'Choose a part or use a pick';
  function measurementControls(v){
    const p=v.payload,ready=v.status==='ready'&&!!p&&!p.read_only&&!p.draft&&!v.measuring&&!sending;
    $('measurement-panel').hidden=!p||!!p.read_only;
    for(const slot of ['a','b']){
      const select=$('measurement-'+slot),selected=v.measurement_targets[slot],value=selected?JSON.stringify(selected):'';
      const key=JSON.stringify([p?.evaluation_id,value]);if(select.dataset.options!==key){
        select.dataset.options=key;select.replaceChildren();const option=(text,value)=>{const node=document.createElement('option');node.textContent=text;node.value=value;select.append(node);};
        option('Choose a part or use a pick','');if(selected?.kind!=='part'&&selected)option(targetText(selected),value);
        for(const part of p?.summary.assembly?.parts||[])option(part.id,JSON.stringify({kind:'part',part_id:part.id}));select.value=value;
      }
      select.disabled=!ready;$('measurement-pick-'+slot).disabled=!ready||!v.selection;
    }
    const targets=v.measurement_targets,assembly=p?.summary.assembly?.parts;
    $('measurement-pair').disabled=!ready||!targets.a||!targets.b||JSON.stringify(targets.a)===JSON.stringify(targets.b);
    $('measurement-assembly').disabled=!ready||!assembly||assembly.length<2||assembly.length>23;
    $('measurement-threshold').disabled=!ready;
    $('measurement-clear').disabled=!ready||!v.measurement;
    const m=v.measurement_status,report=ready&&m?.state==='succeeded'?m.result.report:null;
    let message='Distances use the saved source pose, including hidden parts.';
    if(v.measurement_error)message=v.measurement_error;
    else if(p?.draft)message='Save or reset this pose before measuring.';
    else if(v.status!=='ready')message='Waiting for the current geometry…';
    else if(v.measuring)message='Starting measurement…';
    else if(m&&['queued','running','cancelling'].includes(m.state))message='Measuring source geometry…';
    else if(m?.error)message=m.error.message;
    else if(report)message=report.status==='fail'?(report.interference_count?'Interference detected.':'Clearance is below your limit.'):report.status==='pass'?'Clearance meets your limit.':'Source geometry measured.';
    $('measurement-state').textContent=message;
    const rows=[];if(report){
      const closest=report.pairs.reduce((best,item)=>item.distance_mm<best.distance_mm?item:best,report.pairs[0]);
      rows.push(['Minimum distance',`${measurementText(report.minimum_distance_mm)} mm`],['Scope',report.coverage==='all_assembly_leaves'?'All assembly leaves':report.coverage==='explicit_leaf_subset'?'Explicit leaf subset':'Selected pair'],['Pairs checked',report.pairs.length]);
      if(report.interference_count)rows.push(['Interfering pairs',report.interference_count]);
      rows.push(['Closest pair',closest.targets.map(targetText).join(' / ')],['Closest A',closest.witnesses[0].a_mm.map(measurementText).join(', ')+' mm'],['Closest B',closest.witnesses[0].b_mm.map(measurementText).join(', ')+' mm']);
      if(closest.witnesses_truncated)rows.push(['Witnesses retained',`${closest.witnesses.length} of ${closest.solution_count} native candidates`]);
      if(closest.rejected_witness_count)rows.push(['Inconsistent candidates',closest.rejected_witness_count]);
      if(report.pairs.length===1){if(Number.isFinite(closest.angle_deg))rows.push(['Unoriented angle',`${measurementText(closest.angle_deg)}°`]);if(Number.isFinite(closest.intersection_volume_mm3))rows.push(['Common material',`${measurementText(closest.intersection_volume_mm3)} mm³`]);}
    }
    facts($('measurement-result'),rows);
  }
  function measurementQuery(action){
    const input=$('measurement-threshold').value.trim(),query={action};
    if(input){const n=Number(input);if(!Number.isFinite(n)||n<0||n>1e6)throw Error('Enter a clearance from 0 to 1,000,000 mm.');query.minimum_clearance_mm=n;}
    if(action==='pair')query.targets=[state.value.measurement_targets.a,state.value.measurement_targets.b];return query;
  }
  for(const slot of ['a','b']){
    $('measurement-'+slot).onchange=()=>{try{const value=$('measurement-'+slot).value;state.setMeasurementTarget(slot,value?JSON.parse(value):null);}catch(error){status(error.message);}};
    $('measurement-pick-'+slot).onclick=()=>{try{const ref=state.value.selection?.reference;if(ref)state.setMeasurementTarget(slot,{kind:ref.kind,entity_id:ref.entity_id});}catch(error){status(error.message);}};
  }
  for(const [id,action]of [['measurement-pair','pair'],['measurement-assembly','clearance']])$(id).onclick=()=>{try{state.measure(measurementQuery(action)).catch(error=>status(error.message));}catch(error){status(error.message);}};
  $('measurement-clear').onclick=()=>state.measure(null).catch(error=>status(error.message));
  const axisVector=axis=>['x','y','z'].map(name=>name===axis?1:0);
  function displayedBounds(v) {
    const model=renderer?.model;
    return model?.bounds?{min:model.bounds.min.map((n,i)=>n*model.span+model.center[i]),max:model.bounds.max.map((n,i)=>n*model.span+model.center[i])}:(v.payload?.read_only?v.payload.summary.bounds:v.payload?.summary.bounds_mm);
  }
  function planeRange(normal,v) {
    const b=displayedBounds(v);let min=0,max=0;
    for(let i=0;i<3;i++){min+=normal[i]*(normal[i]<0?b.max[i]:b.min[i]);max+=normal[i]*(normal[i]<0?b.min[i]:b.max[i]);}
    return [min,max];
  }
  function presentationControls(v) {
    const ready=v.status==='ready'&&!!v.payload&&!sending&&!v.preset_busy,p=v.presentation;
    $('presentation-panel').hidden=!v.payload||!displayedBounds(v);
    for(const id of ['clip-enabled','clip-axis','clip-offset','clip-flip','explode-distance','presentation-reset'])$(id).disabled=!ready;
    if(!v.payload||!displayedBounds(v))return;
    $('clip-enabled').checked=!!p.clip;$('clip-controls').hidden=!p.clip;
    $('explode-controls').hidden=!v.payload.summary.assembly;
    const b=displayedBounds(v),span=Math.max(1e-6,...b.max.map((n,i)=>n-b.min[i]));
    $('explode-distance').max=Math.min(1e6,Math.max(span,p.explode.distance_mm));$('explode-distance').step=Math.max(1e-6,span/1000);
    $('explode-distance').value=p.explode.distance_mm;$('explode-position').textContent=`${fmt(p.explode.distance_mm)} mm`;
    if(p.clip) {
      const axis=['x','y','z'].find(name=>JSON.stringify(axisVector(name))===JSON.stringify(p.clip.normal));
      $('clip-axis').value=axis||'custom';$('clip-axis').querySelector('[value="custom"]').hidden=!!axis;
      const range=planeRange(p.clip.normal,v),lo=Math.min(range[0],p.clip.offset_mm),hi=Math.max(range[1],p.clip.offset_mm,lo+1e-6);
      $('clip-offset').min=lo;$('clip-offset').max=hi;$('clip-offset').step=Math.max(1e-9,(hi-lo)/1000);$('clip-offset').value=p.clip.offset_mm;
      $('clip-position').textContent=`${fmt(p.clip.offset_mm)} mm`;$('clip-flip').checked=p.clip.keep==='negative';
    }
    const section = ready && !v.payload.draft && v.section_status?.state === 'succeeded' && v.section_status.result?.report;
    const help = v.payload.read_only?'Clipping and colors affect the captured review display. Cut surfaces remain open.':!p.clip ? 'Visual offsets change this view. Measurements and exports use the saved source geometry.' : section?.status === 'area' ? 'Exact section surfaces fill the cut. Moving the plane or exploding parts clears them; calculate again.' : 'Clipping opens the model. Calculate an exact section below to fill the cut and measure its area.';
    $('presentation-status').textContent = `${v.presentation_unsaved ? 'Saving view… ' : ''}${help}`;
  }
  function present(change) {
    const value=JSON.parse(JSON.stringify(state.value.presentation));change(value);
    try{state.setPresentation(value).catch(error=>status(error.message));}catch(error){status(error.message);}
  }
  $('clip-enabled').onchange=()=>{const enabled=$('clip-enabled').checked;present(p=>{
    const normal=axisVector($('clip-axis').value==='custom'?'z':$('clip-axis').value),range=planeRange(normal,state.value);
    p.clip=enabled?{normal,offset_mm:(range[0]+range[1])/2,keep:'positive'}:null;
  });};
  $('clip-axis').onchange=()=>present(p=>{const normal=axisVector($('clip-axis').value),range=planeRange(normal,state.value);p.clip={normal,offset_mm:(range[0]+range[1])/2,keep:p.clip.keep};});
  $('clip-offset').oninput=()=>{const offset=Number($('clip-offset').value);present(p=>{p.clip.offset_mm=offset;});};
  $('clip-flip').onchange=()=>{const negative=$('clip-flip').checked;present(p=>{p.clip.keep=negative?'negative':'positive';});};
  $('explode-distance').oninput=()=>{const distance=Number($('explode-distance').value);present(p=>{p.explode.distance_mm=distance;});};
  $('presentation-reset').onclick=()=>state.setPresentation(CadRenderer.math.defaultPresentation()).catch(error=>status(error.message));
  const colorHex=color=>'#'+color.map(value=>Math.round(value*255).toString(16).padStart(2,'0')).join('');
  const colorRgb=value=>[1,3,5].map(index=>Number.parseInt(value.slice(index,index+2),16)/255);
  let annotationKey='',annotationDocument=null;
  function selectNote(id){const note=state.value.annotations.find(note=>note.id===id);$('annotation-list').value=id;$('annotation-text').value=note?.text||'';annotationKey='';annotationControls(state.value);shell?.openToolById('annotations-panel');}
  function annotationControls(v){
    const ready=v.status==='ready'&&!!v.payload&&v.read_only!==true&&v.payload.read_only!==true&&!sending&&!v.annotating,committed=ready&&!v.payload.draft,notes=v.annotations||[];
    if(annotationDocument!==v.payload?.document_id){annotationDocument=v.payload?.document_id;$('annotation-text').value='';$('annotation-list').value='';}
    $('annotations-panel').hidden=!v.payload||v.read_only===true||v.payload?.read_only===true;
    const chosen=$('annotation-list').value,anchor=$('annotation-anchor').value,key=JSON.stringify([v.payload?.evaluation_id,notes,ready,v.selection?.reference,chosen,anchor,$('annotation-text').value,v.annotation_error]);
    if(key===annotationKey)return;annotationKey=key;
    $('annotation-list').replaceChildren();const empty=document.createElement('option');empty.value='';empty.textContent='Choose a note…';$('annotation-list').append(empty);
    notes.forEach((note,i)=>{const option=document.createElement('option');option.value=note.id;option.textContent=`${i+1}. ${note.text.replace(/\s+/g,' ').slice(0,40)}${note.status==='retired'?` · retired r${note.revision}`:''}`;$('annotation-list').append(option);});
    $('annotation-list').value=notes.some(note=>note.id===chosen)?chosen:'';
    $('annotation-anchor').replaceChildren();
    for(const [value,label]of [['model','Model bounds center'],...(v.selection?[['entity',`Selected ${v.selection.reference.kind} inspection center`]]:[]),...(v.payload?.summary?.assembly?.parts||[]).map(part=>['part:'+part.id,part.id+' bounds center'])]){const option=document.createElement('option');option.value=value;option.textContent=label;$('annotation-anchor').append(option);}
    $('annotation-anchor').value=[...$('annotation-anchor').options].some(option=>option.value===anchor)?anchor:'model';
    const text=$('annotation-text').value,validText=!!text.trim()&&new TextEncoder().encode(text).length<=512&&!/[\u0000-\u0008\u000b-\u001f\u007f]/.test(text),selected=!!$('annotation-list').value;
    $('annotation-add').disabled=!committed||!validText||notes.length>=32;$('annotation-update').disabled=!ready||!validText||!selected;$('annotation-delete').disabled=!ready||!selected;$('annotation-clear').disabled=!ready||!notes.length;$('annotation-refresh').disabled=!ready;$('annotation-list').disabled=!ready||!notes.length;$('annotation-anchor').disabled=!committed;$('annotation-text').disabled=!ready;
    const retired=notes.filter(note=>note.status==='retired').length;
    $('annotation-status').textContent=v.annotation_error||(v.annotating?'Saving review note…':`${notes.length}/32 notes · ${retired} retired. Pins mark bounds or resolved inspection centers, which may lie inside material. Retired anchors remain at their original revision. Text allows 512 UTF-8 bytes.`);
    $('annotation-notes').replaceChildren();notes.forEach((note,i)=>{const li=document.createElement('li'),button=document.createElement('button'),meta=document.createElement('small');li.value=i+1;li.className=note.status==='retired'?'annotation-retired':'';button.textContent=note.text;button.onclick=()=>selectNote(note.id);meta.textContent=`${note.status} · ${note.document_id} r${note.revision} · ${note.anchor.part_id||note.feature_id} · ${note.anchor.position_semantics.replaceAll('_',' ')}`;li.append(button,meta);$('annotation-notes').append(li);});
  }
  $('annotation-list').onchange=()=>selectNote($('annotation-list').value);
  for(const id of ['annotation-text','annotation-anchor'])$(id).addEventListener(id==='annotation-text'?'input':'change',()=>{annotationKey='';annotationControls(state.value);});
  $('annotation-add').onclick=()=>{const value=$('annotation-anchor').value,anchor=value==='entity'?{kind:'entity',reference:state.value.selection?.reference}:value.startsWith('part:')?{kind:'part',part_id:value.slice(5)}:{kind:'model'};state.annotation('add',{text:$('annotation-text').value,anchor}).then(()=>{$('annotation-text').value='';annotationKey='';annotationControls(state.value);}).catch(error=>status(error.message));};
  $('annotation-update').onclick=()=>state.annotation('update',{annotation_id:$('annotation-list').value,text:$('annotation-text').value}).catch(error=>status(error.message));
  $('annotation-delete').onclick=()=>state.annotation('delete',{annotation_id:$('annotation-list').value}).then(()=>selectNote('')).catch(error=>status(error.message));
  $('annotation-clear').onclick=()=>state.annotation('clear').then(()=>selectNote('')).catch(error=>status(error.message));
  $('annotation-refresh').onclick=()=>state.annotation('list').catch(error=>status(error.message));
  function renderPins(pins){$('annotation-overlay').replaceChildren();for(const pin of pins){const button=document.createElement('button');button.className='annotation-pin';button.textContent=pin.number;button.title=pin.text;button.style.left=`${pin.x}px`;button.style.top=`${pin.y}px`;button.setAttribute('aria-label',`Review note ${pin.number}: ${pin.text}`);button.onclick=()=>selectNote(pin.id);$('annotation-overlay').append(button);}}
  let appearanceKey='',presetKey='',presetDocument=null;
  function appearanceControls(v){
    const ready=v.status==='ready'&&!!v.payload&&!sending&&!v.preset_busy,appearance=v.appearance,parts=v.payload?.summary?.assembly?.parts||[];
    $('appearance-panel').hidden=!v.payload;
    const key=JSON.stringify([v.payload?.evaluation_id,appearance,ready,v.appearance_unsaved,$('appearance-part').value]);
    if(key===appearanceKey)return;appearanceKey=key;
    const selected=parts.some(part=>part.id===$('appearance-part').value)?$('appearance-part').value:parts[0]?.id||'';
    $('appearance-part').replaceChildren();
    for(const part of parts){const option=document.createElement('option');option.value=part.id;option.textContent=part.id;$('appearance-part').append(option);}
    $('appearance-part').value=selected;$('appearance-part-controls').hidden=!parts.length;
    $('appearance-default').value=colorHex(appearance.default_color);
    const override=appearance.parts.find(part=>part.part_id===selected);
    $('appearance-color').value=colorHex(override?.color||appearance.default_color);
    for(const id of ['appearance-default','appearance-default-apply','appearance-reset'])$(id).disabled=!ready;
    for(const id of ['appearance-part','appearance-color','appearance-part-apply'])$(id).disabled=!ready||!parts.length;
    $('appearance-part-reset').disabled=!ready||!override;
    $('appearance-status').textContent=v.appearance_unsaved?'Saving colors…':`${appearance.parts.length} part color${appearance.parts.length===1?'':'s'} set. Colors affect this view and saved images.`;
  }
  function recolor(change){const value=JSON.parse(JSON.stringify(state.value.appearance));change(value);status('');try{state.setAppearance(value).catch(error=>status(error.message));}catch(error){status(error.message);}}
  $('appearance-part').onchange=()=>{appearanceKey='';appearanceControls(state.value);};
  $('appearance-default-apply').onclick=()=>recolor(value=>{value.default_color=colorRgb($('appearance-default').value);});
  $('appearance-part-apply').onclick=()=>{const part_id=$('appearance-part').value,color=colorRgb($('appearance-color').value);recolor(value=>{value.parts=value.parts.filter(part=>part.part_id!==part_id);value.parts.push({part_id,color});});};
  $('appearance-part-reset').onclick=()=>recolor(value=>{value.parts=value.parts.filter(part=>part.part_id!==$('appearance-part').value);});
  $('appearance-reset').onclick=()=>state.setAppearance(CadRenderer.math.defaultAppearance()).catch(error=>status(error.message));
  function presetControls(v){
    if(presetDocument!==v.payload?.document_id){presetDocument=v.payload?.document_id;$('preset-name').value='';$('preset-list').value='';}
    const ready=v.status==='ready'&&!!v.payload&&!sending&&!v.preset_busy,committed=ready&&!v.payload.draft&&!v.payload.read_only,presets=v.presets||[],chosen=$('preset-list').value;
    $('presets-panel').hidden=!v.payload||!!v.payload.read_only;
    const key=JSON.stringify([v.payload?.document_id,presets,ready,v.payload?.draft,v.preset_busy,v.preset_error,v.preset_message,chosen,$('preset-name').value]);
    if(key===presetKey)return;presetKey=key;
    $('preset-list').replaceChildren();const empty=document.createElement('option');empty.value='';empty.textContent=presets.length?'Choose a saved view…':'No saved views yet';$('preset-list').append(empty);
    for(const preset of presets){const option=document.createElement('option');option.value=preset.name;option.textContent=preset.name;$('preset-list').append(option);}
    $('preset-list').value=presets.some(preset=>preset.name===chosen)?chosen:'';
    const selected=$('preset-list').value,name=$('preset-name').value.trim(),validName=name&&new TextEncoder().encode(name).length<=64&&!/[\u0000-\u001f\u007f]/.test(name);
    $('preset-list').disabled=!ready||!presets.length;$('preset-name').disabled=!committed;
    $('preset-save').disabled=!committed||!validName||(presets.length===16&&!presets.some(preset=>preset.name===name));
    $('preset-apply').disabled=!committed||!selected;$('preset-delete').disabled=!ready||!selected;$('preset-refresh').disabled=!ready;
    $('preset-status').textContent=v.preset_error||(v.preset_busy?'Updating saved views…':v.payload?.draft?'Save or reset this pose to save or apply a view.':v.preset_message||`${presets.length} of 16 views saved for this model.`);
  }
  $('preset-list').onchange=()=>{presetKey='';presetControls(state.value);};
  $('preset-name').oninput=()=>{presetKey='';presetControls(state.value);};
  $('preset-save').onclick=()=>state.preset('save',$('preset-name').value.trim()).catch(error=>status(error.message));
  for(const operation of ['apply','delete'])$('preset-'+operation).onclick=()=>state.preset(operation,$('preset-list').value).catch(error=>status(error.message));
  $('preset-refresh').onclick=()=>state.preset('list').catch(error=>status(error.message));
  $('motion-mechanism').onchange = () => { motionDefinition = $('motion-mechanism').value; $('motion-name').value = ''; motionControls(state.value); };
  $('motion-poses').onchange = () => { if ($('motion-poses').value) state.previewPose($('motion-poses').value, motionDefinition).catch(error => { $('motion-status').textContent = error.message; }); };
  $('motion-reset').onclick = () => state.resetMotion().catch(error => { $('motion-status').textContent = error.message; });
  $('motion-save').onclick = () => state.saveMotion($('motion-name').value.trim(), motionDefinition).then(() => { $('motion-name').value = ''; }).catch(error => { $('motion-status').textContent = error.message; });
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
        if (state.value.status !== 'ready' || state.value.payload?.draft || state.value.preset_busy) { renderer?.setSelection(null); return; }
        if(selection&&state.value.payload?.read_only){if(!detail?.artifact||selection.reference.review_sha256!==state.value.payload.artifact.review_sha256){renderer?.setSelection(null);status('That artifact label belongs to a previous review.');return;}}
        if (selection && !state.value.payload?.read_only && ['document_id', 'revision', 'evaluation_id', 'feature_id'].some(key => selection.reference[key] !== state.value.payload?.[key])) {
          renderer?.setSelection(null); status('That selection belongs to a previous model view. Wait for the current view.'); return;
        }
        state.update({ selection }); saveSoon();
        if (detail?.section) status(detail.message || 'This is a section surface. Select an original face or edge to edit the model.');
        else if (detail?.ambiguous) status(detail.message || 'This geometry overlaps. Rotate the model and select again.');
        else status('');
      },
      onAnnotations(pins){renderPins(pins);},
      onView(camera) { shell?.syncView(camera); },
      onCamera(camera) { state.setCamera(camera); if (state.value.status === 'ready') saveSoon(); },
      onError(error) { rendererError = error.message; drawnEvaluation = null; $('viewport').style.visibility = 'hidden'; fail(error); },
      onReady(identity) { if (identity.evaluation_id === state.value.payload?.evaluation_id) { rendererError = null; drawnEvaluation = identity.evaluation_id; update(state.value); } }
    });
  } catch (error) { fail(error); }
  shell = CadShell.mount({ $, bridge, renderer });
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
    const value=state.value,payload=value.payload;
    if (value.status !== 'ready' || !payload || payload.draft || value.read_only === true || payload.read_only === true || exporting) return;
    const { document_id, revision, evaluation_id } = payload, format = $('export-format').value;
    const generation=state.epoch,view_id=value.view_id;
    // A saved export can finish after this view opens another source. Keep its
    // output in the workspace, but never relabel the new view with old results.
    const current=()=>!disposed&&state.epoch===generation&&state.value.view_id===view_id&&state.value.status==='ready'&&state.value.read_only!==true&&state.value.payload?.read_only!==true&&state.value.payload?.document_id===document_id&&state.value.payload?.revision===revision&&state.value.payload?.evaluation_id===evaluation_id;
    exporting = true; update(state.value); status(`Preparing ${format.toUpperCase()} for revision ${revision}…`);
    try {
      if (bridge.desktop) {
        const result = await bridge.desktop.export({ document_id, revision, format });
        if(!current())return;
        status(result.cancelled ? 'Save cancelled. The export remains in your workspace.' : `Saved: ${result.paths.join(', ')}`);
      } else {
        const drawing = ['pdf', 'svg', 'dxf'].includes(format);
        const args = { document_id, revision, ...(drawing ? { drawing: { formats: [format] } } : { format }) };
        let job = await bridge.tool('cad_job', { action: 'submit', request_id: `export_${crypto.randomUUID()}`, tool: drawing ? 'cad_drawing' : 'cad_export', arguments: args, budget: { timeout_ms: 300000, memory_mb: 2048 } });
        const deadline = Date.now() + 310000;
        while (['queued', 'running', 'cancelling'].includes(job.state) && current() && Date.now() < deadline) {
          await new Promise(resolve => setTimeout(resolve, 500));
          if(!current())return;
          job = await bridge.tool('cad_job', { action: 'get', job_id: job.job_id });
        }
        if (!current()) return;
        if (job.state !== 'succeeded') throw Error(job.error?.message || `Export ${job.state}. Job: ${job.job_id}`);
        const paths = drawing ? job.result.artifacts.map(item => item.path) : [job.result.path];
        $('copy-fallback').hidden = false; $('copy-fallback').open = true; $('copy-text').value = paths.join('\n');
        status('Export saved in the CAD workspace. File paths are below.');
      }
    } catch (error) { if(current())status(error.message); }
    finally { exporting = false; if (!disposed) update(state.value); }
  };
  $('fit').onclick = () => renderer?.reset();
  $('save-image').onclick=()=>{
    try{
      if(state.value.status!=='ready'||!renderer)return;
      const image=renderer.capture();if(typeof image!=='string'||!/^data:image\/png;base64,[A-Za-z0-9+/=]+$/.test(image)||image.length>8*1024*1024)throw Error('The saved image exceeds the 8 MiB limit.');
      const link=document.createElement('a');link.href=image;link.download=state.value.payload.read_only?`artifact-${state.value.payload.artifact.review_sha256.slice(0,12)}.png`:`${state.value.payload.document_id}-r${state.value.payload.revision}.png`;document.body.append(link);link.click();link.remove();status('PNG download requested. Your host may ask where to save it.');
    }catch(error){status(error.message);}
  };
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
    state.dispose(); shell?.dispose(); renderer?.destroy(); bridge.dispose();
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
      $('edit-help').textContent = `Selections are shared with agents using this workspace and view “${state.value.view_id}”. Ask in chat to use the selected face or edge, or paste a copied request. Saved edits appear automatically.`;
    }
    hostLayout();
    if (typeof ResizeObserver !== 'undefined') { sizeObserver = new ResizeObserver(hostLayout); sizeObserver.observe(document.body); }
    if (!bridge.capabilities.message) status('This host supports Copy request. Paste the request into your chat.');
    update(state.value); await refreshLibrary();
    libraryTimer = setInterval(refreshLibrary, 8000);
    state.start();
  }).catch(error => { state.update({ status: 'error', error: error.message }); });
})();
