/* Shared floating viewer chrome, card state and camera coordination. */
(() => {
  'use strict';
  const resolveTheme = (hostContext, prefersDark) => {
    const theme = hostContext && hostContext.theme;
    if (theme === 'light' || theme === 'dark') return theme;
    return prefersDark ? 'dark' : 'light';
  };
  const layoutMode = width => {
    if (!Number.isFinite(width) || width < 560) return { scene: 'chip', dock: 'menu', compact: true };
    if (width < 900) return { scene: 'chip', dock: 'icons', compact: false };
    return { scene: 'open', dock: 'icons', compact: false };
  };
  // Orientation cube. Each face is described as seen straight on from its
  // standard view: `right` and `up` are the world axes along screen-right and
  // screen-up there, so labels read upright and border clicks pick the right
  // neighbouring edge or corner.
  const CUBE_FACES = Object.freeze([
    { name: 'front', normal: [0, -1, 0], right: [1, 0, 0], up: [0, 0, 1] },
    { name: 'back', normal: [0, 1, 0], right: [-1, 0, 0], up: [0, 0, 1] },
    { name: 'right', normal: [1, 0, 0], right: [0, 1, 0], up: [0, 0, 1] },
    { name: 'left', normal: [-1, 0, 0], right: [0, -1, 0], up: [0, 0, 1] },
    { name: 'top', normal: [0, 0, 1], right: [1, 0, 0], up: [0, 1, 0] },
    { name: 'bottom', normal: [0, 0, -1], right: [1, 0, 0], up: [0, -1, 0] }
  ]);
  const cubeFace = name => {
    const found = CUBE_FACES.find(face => face.name === name);
    if (!found) throw new Error('Unknown cube face.');
    return found;
  };
  const cubeZone = value => (value < -.56 ? -1 : value > .56 ? 1 : 0);
  // (u,v) is the pointer position inside the face, 0..1, with v=0 at its top.
  // The centre looks straight at the face; its border adds the neighbouring axes
  // for 45-degree edge views and isometric corner views.
  const cubeDirection = (name, u, v) => {
    const face = cubeFace(name), right = cubeZone(2 * u - 1), up = cubeZone(1 - 2 * v);
    return face.normal.map((n, i) => n + right * face.right[i] + up * face.up[i]);
  };
  // CSS matrix3d is column-major. The cube's local frame is world-aligned, so the
  // container transform maps world to CSS (x right, y down, z toward the viewer).
  const cubeMatrix = camera => {
    const b = CadRenderer.math.basis(camera);
    return [b[0][0], -b[1][0], -b[2][0], 0, b[0][1], -b[1][1], -b[2][1], 0, b[0][2], -b[1][2], -b[2][2], 0, 0, 0, 0, 1];
  };
  // A face square (x right, y down, z out of the face), placed `half` along its normal.
  const cubeFaceMatrix = (name, half) => {
    const face = cubeFace(name), down = face.up.map(v => -v);
    return [...face.right, 0, ...down, 0, ...face.normal, 0, ...face.normal.map(n => n * half), 1];
  };
  const clamp01 = value => Math.max(0, Math.min(1, Number.isFinite(value) ? value : .5));
  const titleCase = name => name[0].toUpperCase() + name.slice(1);
  // Documents provide values, not design limits. These are local slider windows;
  // the number field accepts the full scalar range and the kernel validates intent.
  const parameterRange = value => {
    const span = Math.max(Math.abs(value), 1);
    return { min: Math.max(-1000000, value > 0 ? 0 : value - span), max: Math.min(1000000, value + span), step: Math.pow(10, Math.floor(Math.log10(span)) - 3) };
  };

  // Connects the floating chrome to the existing section elements. Panels keep
  // their IDs and are only moved; app.js still owns their content and `hidden`
  // state, and the shell reads that state on every refresh().
  function mount({ $, bridge, renderer }) {
    const doc = document, root = doc.documentElement, app = $('app');
    const cleanups = [];
    const on = (target, type, handler, options) => {
      target.addEventListener(type, handler, options);
      cleanups.push(() => target.removeEventListener(type, handler, options));
    };

    // Theme: host context first, then the OS preference.
    const media = typeof matchMedia === 'function' ? matchMedia('(prefers-color-scheme: dark)') : null;
    let theme = null;
    const applyTheme = () => {
      const next = resolveTheme(bridge.hostContext, !!media?.matches);
      if (next === theme) return;
      theme = next;
      root.dataset.theme = next;
      renderer?.setTheme(next === 'dark' ? CadRenderer.math.darkTheme() : CadRenderer.math.defaultTheme());
    };
    media?.addEventListener?.('change', applyTheme);
    cleanups.push(() => media?.removeEventListener?.('change', applyTheme));
    cleanups.push(bridge.on('ui/notifications/host-context-changed', applyTheme));

    // Popovers: one open at a time, Esc or an outside press closes, focus returns.
    const popovers = {
      models: { el: $('models-popover'), trigger: $('project-pill') },
      export: { el: $('export-menu'), trigger: $('export-menu-button') },
      tool: { el: $('tool-popover'), trigger: null }
    };
    const dock = $('tool-dock'), dockToggle = $('dock-toggle');
    const dockButtons = () => [...dock.querySelectorAll('.dock-button')];
    let openKey = null, activeTool = null, returnTo = null, dockOpen = false, promptOpen = false;
    const parkToolPanel = () => { const panel = activeTool && $(activeTool); if (panel) $('tool-panels').append(panel); };
    function close(restoreFocus = true) {
      if (!openKey) return;
      const entry = popovers[openKey], target = returnTo;
      entry.el.hidden = true;
      entry.trigger?.setAttribute('aria-expanded', 'false');
      if (openKey === 'tool') { parkToolPanel(); for (const button of dockButtons()) button.setAttribute('aria-expanded', 'false'); }
      openKey = null; activeTool = null; returnTo = null;
      if (restoreFocus && target && doc.contains(target) && !target.hidden) target.focus();
    }
    function open(key, trigger) {
      if (openKey) close(false);
      openKey = key; returnTo = key === 'export' ? $('project-pill') : trigger || doc.activeElement;
      const entry = popovers[key];
      entry.el.hidden = false;
      entry.trigger?.setAttribute('aria-expanded', 'true');
      entry.el.querySelector('input:not([type=checkbox]),select,textarea,button:not(.icon-button)')?.focus({ preventScroll: true });
    }
    const toggle = key => (openKey === key ? close() : open(key, popovers[key].trigger));
    on(popovers.models.trigger, 'click', () => toggle('models'));
    on(popovers.export.trigger, 'click', () => toggle('export'));
    on($('ask-agent'), 'click', () => { close(false); promptOpen = true; refreshBar(); $('prompt').focus(); });
    on($('show-cube'), 'click', () => { const visible = $('cube-area').hidden; $('cube-area').hidden = !visible; $('show-cube').setAttribute('aria-pressed', String(visible)); });
    on($('tool-popover-close'), 'click', () => close());
    on($('documents'), 'click', event => { if (event.target.closest('button')) close(false); });
    on($('export-menu'), 'click', event => { if (event.target.closest('#export-model,#save-image')) close(false); });
    on(doc, 'pointerdown', event => {
      if (dockOpen && !dock.contains(event.target) && !dockToggle.contains(event.target) && !$('tool-popover').contains(event.target)) { dockOpen = false; dockToggle.setAttribute('aria-expanded', 'false'); refreshDock(); }
      if (!openKey) return;
      const entry = popovers[openKey], target = event.target;
      if (entry.el.contains(target) || (openKey === 'tool' ? dock.contains(target) : entry.trigger.contains(target))) return;
      close(false);
    }, true);
    on(doc, 'keydown', event => { if (event.key === 'Escape') { if(openKey) close(); dockOpen = false; dockToggle.setAttribute('aria-expanded', 'false'); refreshDock(); } });
    function openTool(button) {
      const id = button.dataset.tool;
      if (activeTool === id) { close(); return; }
      open('tool', button);
      activeTool = id;
      button.setAttribute('aria-expanded', 'true');
      $('tool-popover-title').textContent = button.getAttribute('aria-label');
      $('tool-popover-body').replaceChildren($(id));
    }
    for (const button of dockButtons()) on(button, 'click', () => openTool(button));
    on(dock, 'keydown', event => {
      if (event.key !== 'ArrowDown' && event.key !== 'ArrowUp') return;
      const visible = dockButtons().filter(button => !button.hidden), at = visible.indexOf(doc.activeElement);
      if (at < 0) return;
      event.preventDefault();
      visible[(at + (event.key === 'ArrowDown' ? 1 : visible.length - 1)) % visible.length].focus();
    });
    on(dockToggle, 'click', () => { dockOpen = !dockOpen; dockToggle.setAttribute('aria-expanded', String(dockOpen)); refreshDock(); });

    // Responsive layout from the viewer's own width.
    let mode = null, userScene = null, userParameters = null, parameterDocument = null, cameraLayout = null;
    const sceneCard = $('scene-card'), sceneChip = $('scene-chip');
    function applyLayout() {
      const next = layoutMode(app.clientWidth);
      if (mode && mode.scene !== next.scene) userScene = null;
      mode = next;
      app.dataset.scene = next.scene; app.dataset.dock = next.dock; app.dataset.compact = String(next.compact);
      const visible = userScene ?? next.scene === 'open';
      sceneCard.hidden = !visible; sceneChip.hidden = visible; sceneChip.setAttribute('aria-expanded', String(visible));
      // Fit and framing target what the floating chrome leaves uncovered. A Scene
      // card opened over the model from the chip is transient and not counted.
      refreshParameters();
      refreshDock();
    }
    on($('scene-collapse'), 'click', () => { const content = $('scene-content'); content.hidden = !content.hidden; $('scene-collapse').setAttribute('aria-expanded', String(!content.hidden)); });
    on(sceneChip, 'click', () => { userScene = true; if(mode?.compact) userParameters = false; applyLayout(); });
    const observer = typeof ResizeObserver === 'function' ? new ResizeObserver(applyLayout) : null;
    observer?.observe(app);

    function refreshParameters() {
      const key = $('document-title').textContent;
      if (key !== parameterDocument) { parameterDocument = key; userParameters = null; }
      const available = $('parameters').children.length > 0 && !$('parameters').dataset.readonly;
      const visible = available && (userParameters ?? mode?.scene === 'open');
      $('parameters-panel').hidden = !visible; $('parameters-chip').hidden = !available || visible;
      $('parameters-chip').setAttribute('aria-expanded', String(visible)); $('show-parameters').disabled = !available;
      const insets = { top: 48, right: visible && mode?.scene === 'open' ? $('parameters-panel').offsetWidth + 20 : 12, bottom: 56, left: 12 };
      const size = Math.max(1, Math.min(app.clientWidth, app.clientHeight - 36));
      // Keep the model's position relative to the uncovered viewport when cards
      // open/close or a host resizes its embedded app. Preserve orbit and zoom.
      if(renderer?.model && cameraLayout) {
        const old = cameraLayout.insets, oldSize = cameraLayout.size, camera = renderer.getCamera();
        const dx = (insets.left-insets.right)/(2*size) - (old.left-old.right)/(2*oldSize);
        const dy = (insets.top-insets.bottom)/(2*size) - (old.top-old.bottom)/(2*oldSize);
        if(Math.abs(dx)+Math.abs(dy)>1e-6) renderer.setCamera({...camera,pan:[camera.pan[0]+dx,camera.pan[1]+dy]});
      }
      cameraLayout = { insets, size }; renderer?.setInsets(insets);
    }
    const showParameters = () => { userParameters = true; if(mode?.compact) userScene = false; close(false); applyLayout(); };
    on($('parameters-chip'), 'click', showParameters); on($('show-parameters'), 'click', showParameters);
    on($('parameters-close'), 'click', () => { userParameters = false; applyLayout(); });
    on($('parameters-collapse'), 'click', () => { const content = $('parameter-content'); content.hidden = !content.hidden; $('parameters-collapse').setAttribute('aria-expanded', String(!content.hidden)); });

    function refreshDock() {
      let visible = 0;
      for (const button of dockButtons()) {
        const panel = $(button.dataset.tool);
        button.hidden = !panel || panel.hidden;
        if (!button.hidden) visible++;
      }
      if (activeTool && $(activeTool).hidden) close(false);
      dockToggle.hidden = !visible;
      dock.hidden = !visible || !dockOpen;
    }

    // Scene card tabs. The Parts tab exists only while app.js shows the parts panel.
    const tabs = [...doc.querySelectorAll('#scene-tabs .tab')];
    function selectPane(id) {
      for (const tab of tabs) {
        const selected = tab.dataset.pane === id;
        tab.setAttribute('aria-selected', String(selected));
        $(tab.dataset.pane).hidden = !selected;
      }
    }
    for (const tab of tabs) on(tab, 'click', () => selectPane(tab.dataset.pane));
    function refreshScene() {
      const parts = !$('parts-panel').hidden;
      $('tab-parts').hidden = !parts;
      if (!parts && $('tab-parts').getAttribute('aria-selected') === 'true') selectPane('features-pane');
    }

    // Selection bar: idle prompt pill, expanded summary when something is selected.
    const bar = $('selection-bar'), prompt = $('prompt');
    const autosize = () => { prompt.style.height = 'auto'; prompt.style.height = Math.min(prompt.scrollHeight, 104) + 'px'; };
    on(prompt, 'input', autosize);
    on($('details-toggle'), 'click', () => {
      const panel = $('details-panel'), expanded = panel.hidden;
      panel.hidden = !expanded;
      $('details-toggle').setAttribute('aria-expanded', String(expanded));
      $('details-toggle').setAttribute('aria-label', expanded ? 'Hide details' : 'Show details');
    });
    function refreshBar() {
      const selected = !$('clear-selection').hidden;
      bar.dataset.state = selected ? 'selected' : 'idle';
      let key = '';
      if (selected) {
        const labels = [...$('measurements').querySelectorAll('dt')], values = [...$('measurements').querySelectorAll('dd')];
        const at = labels.findIndex(dt => /^(Length|Area|Radius)$/.test(dt.textContent));
        if (at >= 0) key = `${labels[at].textContent} ${values[at].textContent}`;
      }
      $('selection-key').textContent = key;
      bar.dataset.prompt = String(promptOpen);
      $('pick-hint').hidden = selected || promptOpen || !$('empty-state').hidden || !$('all-hidden').hidden;
      $('menu-title').textContent = $('document-title').textContent;
      $('menu-revision').textContent = $('revision').textContent;
      autosize();
    }
    on(doc, 'keydown', event => { if (event.key === 'Escape') { promptOpen = false; refreshBar(); } });

    for (const button of $('standard-views').querySelectorAll('[data-view]')) on(button, 'click', () => renderer?.setView(button.dataset.view, { animate: true }));
    for (const [id, key] of [['toggle-grid','grid'], ['toggle-axes','axes'], ['toggle-edges','edges']]) on($(id), 'click', () => {
      const enabled = $(id).getAttribute('aria-pressed') !== 'true';
      $(id).setAttribute('aria-pressed', String(enabled)); renderer?.setDisplay({ [key]: enabled });
    });
    on($('open-measure'), 'click', () => { const button = dockButtons().find(item => item.dataset.tool === 'measurement-panel'); if (button && !button.hidden) openTool(button); });

    // Orientation cube: six real buttons in a world-aligned CSS 3D scene.
    const cube = $('cube'), cubeScene = doc.createElement('div');
    cubeScene.className = 'cube-scene';
    for (const face of CUBE_FACES) {
      const button = doc.createElement('button'), label = doc.createElement('span');
      button.type = 'button'; button.className = 'cube-face'; button.dataset.face = face.name;
      button.setAttribute('aria-label', `${titleCase(face.name)} view`);
      button.style.transform = `matrix3d(${cubeFaceMatrix(face.name, 24).join(',')})`;
      label.textContent = face.name; button.append(label); cubeScene.append(button);
    }
    cube.append(cubeScene);
    const syncView = camera => {
      cubeScene.style.transform = `matrix3d(${cubeMatrix(camera).join(',')})`;
      for (const button of $('standard-views').querySelectorAll('[data-view]')) {
        const [yaw,pitch] = CadRenderer.math.STANDARD_VIEWS[button.dataset.view];
        const selected = Math.abs(Math.atan2(Math.sin(camera.yaw-yaw),Math.cos(camera.yaw-yaw))) < .01 && Math.abs(camera.pitch-pitch) < .01;
        button.setAttribute('aria-pressed', String(selected));
      }
    };
    const look = direction => {
      if (!renderer) return;
      const view = CadRenderer.math.viewFromDirection(direction);
      renderer.animateTo({ ...renderer.getCamera(), yaw: view.yaw, pitch: view.pitch });
    };
    let cubeDrag = null, suppressClick = false;
    on(cubeScene, 'click', event => {
      const button = event.target.closest?.('.cube-face');
      if (!button || suppressClick) return;
      // A keyboard activation has no pointer position: look straight at the face.
      const keyboard = event.detail === 0;
      look(cubeDirection(button.dataset.face, keyboard ? .5 : clamp01(event.offsetX / (button.offsetWidth || 48)), keyboard ? .5 : clamp01(event.offsetY / (button.offsetHeight || 48))));
    });
    on(cube, 'dblclick', () => {
      if (!renderer) return;
      const [yaw, pitch] = CadRenderer.math.STANDARD_VIEWS.iso;
      renderer.animateTo({ ...renderer.getCamera(), yaw, pitch });
    });
    on(cube, 'pointerdown', event => {
      if (event.button !== 0 || !renderer) return;
      cubeDrag = { id: event.pointerId, x: event.clientX, y: event.clientY, moved: false, camera: renderer.getCamera() };
    });
    on(cube, 'pointermove', event => {
      if (!cubeDrag || cubeDrag.id !== event.pointerId) return;
      const dx = event.clientX - cubeDrag.x, dy = event.clientY - cubeDrag.y;
      if (!cubeDrag.moved && Math.hypot(dx, dy) <= 3) return;
      if (!cubeDrag.moved) { cubeDrag.moved = true; cube.setPointerCapture?.(event.pointerId); }
      renderer.setCamera({ ...cubeDrag.camera, yaw: cubeDrag.camera.yaw + dx * .01, pitch: cubeDrag.camera.pitch + dy * .01 });
    });
    const endCubeDrag = () => {
      if (cubeDrag?.moved) { suppressClick = true; setTimeout(() => { suppressClick = false; }, 0); }
      cubeDrag = null;
    };
    on(cube, 'pointerup', endCubeDrag);
    on(cube, 'pointercancel', endCubeDrag);

    applyTheme();
    applyLayout();
    if (renderer) syncView(renderer.getCamera());
    refreshScene(); refreshBar();
    return {
      refresh() { applyTheme(); refreshDock(); refreshScene(); refreshParameters(); refreshBar(); },
      syncView,
      openToolById(id) {
        const button = dockButtons().find(item => item.dataset.tool === id);
        if (button && !button.hidden && activeTool !== id) openTool(button);
      },
      closePopover: close,
      dispose() {
        close(false);
        observer?.disconnect();
        for (const cleanup of cleanups.splice(0)) cleanup();
      }
    };
  }
  globalThis.CadShell = Object.freeze({ resolveTheme, layoutMode, parameterRange, CUBE_FACES, cubeDirection, cubeMatrix, cubeFaceMatrix, mount });
})();
