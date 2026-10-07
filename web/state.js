/* View coordination is separate from DOM/WebGL so revision races can be tested. */
(() => {
  'use strict';
  const copy = value => value == null ? value : JSON.parse(JSON.stringify(value));
  const retryablePoll = code => ['workspace_busy', 'queue_full', 'stale_selection'].includes(code);
  class CadLiveState {
    constructor(bridge, changed = () => {}) {
      this.bridge = bridge; this.changed = changed; this.epoch = 0; this.busy = false; this.closed = false;
      this.contextQueue = Promise.resolve(); this.deliveryQueue = Promise.resolve(); this.contextVersion = 0; this.timer = null;
      this.visibilityVersion = 0; this.visibilitySaved = 0; this.snapshotVersions = new WeakMap();
      this.value = { view_id: null, status: 'connecting', payload: null, selection: null, camera: null, model: null, error: null, hidden_part_ids: [], visibility_unsaved: false };
    }
    update(values) { Object.assign(this.value, values); this.changed(this.value); }
    attach(view_id) {
      if (typeof view_id !== 'string' || !/^[A-Za-z][A-Za-z0-9_-]{0,63}$/.test(view_id)) throw Error('Invalid CAD view identity.');
      if (this.value.view_id === view_id) return;
      this.epoch++; this.clearVisibility(); this.update({ view_id, status: 'loading', payload: null, selection: null, camera: null, model: null, error: null, hidden_part_ids: [] });
    }
    clearVisibility() { this.visibilitySaved = ++this.visibilityVersion; this.value.visibility_unsaved = false; }
    partIds(payload = this.value.payload) { return (payload?.summary?.assembly?.parts || []).map(part => part.id); }
    visibleSelection(selection, hidden) { return selection && hidden.includes(selection.geometry?.part_id) ? null : selection; }
    setHiddenParts(ids) {
      if (this.value.status !== 'ready' || !this.value.payload) throw Error('Wait for the current revision to finish loading.');
      const parts = this.partIds();
      if (!Array.isArray(ids) || ids.length > 64 || new Set(ids).size !== ids.length || ids.some(id => !parts.includes(id))) throw Error('Visibility must name unique current assembly parts.');
      const hidden = parts.filter(id => ids.includes(id));
      if (JSON.stringify(hidden) === JSON.stringify(this.value.hidden_part_ids) && this.visibilitySaved === this.visibilityVersion) return Promise.resolve();
      const version = ++this.visibilityVersion;
      this.update({ hidden_part_ids: hidden, visibility_unsaved: true, selection: this.visibleSelection(this.value.selection, hidden), context_error: null });
      return this.publishContext().catch(error => {
        if (version !== this.visibilityVersion) return; // A newer visibility action owns feedback now.
        this.update({ context_error: error.message }); throw error;
      });
    }
    setPartVisible(id, visible) {
      if (!this.partIds().includes(id) || typeof visible !== 'boolean') throw Error('Choose a current assembly part.');
      return this.setHiddenParts(visible ? this.value.hidden_part_ids.filter(part => part !== id) : [...new Set([...this.value.hidden_part_ids, id])]);
    }
    isolatePart(id) {
      if (!this.partIds().includes(id)) throw Error('Choose a current assembly part.');
      return this.setHiddenParts(this.partIds().filter(part => part !== id));
    }
    showAll() { return this.setHiddenParts([]); }
    invalidate() { this.epoch++; this.contextVersion++; this.update({ status: 'loading', selection: null, error: null }); }
    async pollOnce() {
      if (this.busy || this.closed || !this.value.view_id) return;
      this.busy = true;
      const epoch = this.epoch, view = this.value.view_id, visibilityVersion = this.visibilityVersion;
      const pendingVisibility = this.visibilitySaved !== visibilityVersion;
      const current = () => !this.closed && epoch === this.epoch;
      const mayUseSavedVisibility = () => !pendingVisibility && visibilityVersion === this.visibilityVersion && this.visibilitySaved === this.visibilityVersion;
      try {
        const state = await this.bridge.tool('cad_viewer', { action: 'sync', view_id: view,
          ...(this.value.payload ? { known_evaluation_id: this.value.payload.evaluation_id } : {}) });
        if (!current()) return;
        if (state.state !== 'ready') {
          const retarget = state.state === 'empty' || (this.value.payload && this.value.payload.document_id !== state.document_id);
          if (retarget) this.clearVisibility();
          // Keep the last solid visible while building, but disable interaction with its old references.
          this.update({ status: state.state, selection: null, error: state.state === 'loading' && retryablePoll(state.error?.code) ? null : state.error?.message || null,
            document_id: state.document_id, revision: state.revision,
            ...(retarget ? { payload: null, model: null, camera: null, hidden_part_ids: [] } : {}) });
          return;
        }
        if (state.evaluation_id === this.value.payload?.evaluation_id) {
          const hidden = mayUseSavedVisibility() ? this.partIds().filter(id => (state.hidden_part_ids || []).includes(id)) : this.value.hidden_part_ids;
          if (JSON.stringify(hidden) !== JSON.stringify(this.value.hidden_part_ids)) this.clearVisibility();
          this.update({ status: 'ready', error: null, hidden_part_ids: hidden, selection: this.visibleSelection(this.value.selection, hidden) }); return;
        }
        const retarget = this.value.payload && this.value.payload.document_id !== state.document_id;
        if (retarget) this.clearVisibility();
        this.update({ status: 'loading', selection: null, error: null, document_id: state.document_id, revision: state.revision,
          ...(retarget ? { payload: null, model: null, camera: null, hidden_part_ids: [] } : {}) });
        let offset = 0, total = null; const chunks = [];
        while (true) {
          const part = await this.bridge.tool('cad_viewer', { action: 'mesh', view_id: view, evaluation_id: state.evaluation_id, offset });
          if (!current()) return;
          if (typeof part.data !== 'string' || part.offset !== offset || !Number.isSafeInteger(part.total_bytes) ||
              part.total_bytes < 1 || part.total_bytes > 64 * 1024 * 1024 || (total !== null && total !== part.total_bytes) ||
              part.data.length < 1 || part.data.length > 128 * 1024 || /[^\x00-\x7f]/.test(part.data)) throw Error('Invalid CAD mesh transfer.');
          total = part.total_bytes; offset += part.data.length; chunks.push(part.data);
          if (offset > total) throw Error('CAD mesh transfer exceeded its declared size.');
          if (part.next_offset === null) { if (offset !== total) throw Error('Incomplete CAD mesh transfer.'); break; }
          if (part.next_offset !== offset || offset >= total) throw Error('Invalid CAD mesh continuation.');
        }
        const payload = JSON.parse(chunks.join(''));
        if (payload.evaluation_id !== state.evaluation_id || payload.document_id !== state.document_id ||
            payload.revision !== state.revision || payload.draft) throw Error('CAD mesh identity does not match the displayed revision.');
        // A revision may commit during transfer. Do not briefly expose those outdated picks.
        const confirmed = await this.bridge.tool('cad_viewer', { action: 'sync', view_id: view, known_evaluation_id: state.evaluation_id });
        if (!current()) return;
        if (confirmed.state !== 'ready' || confirmed.evaluation_id !== payload.evaluation_id) return;
        const sameDocument = this.value.payload?.document_id === payload.document_id;
        let saved = null, savedHidden = null, selection = null;
        if (!sameDocument) {
          saved = await this.bridge.tool('cad_context', { view_id: view });
          if (!current()) return;
          if (saved.document_id !== payload.document_id || (saved.head_revision !== undefined && saved.head_revision !== payload.revision)) return;
          // This read is newer than the confirming sync. Native visibility
          // describes the current display even when the saved pick is stale.
          savedHidden = saved.hidden_part_ids || null;
          if (saved.stale || saved.document_id !== payload.document_id || saved.evaluation_id !== payload.evaluation_id) saved = null;
          if (saved?.selection) {
            const ref = saved.selection, geometry = payload.topology[ref.kind === 'face' ? 'faces' : 'edges']?.find(item => item.id === ref.entity_id);
            if (geometry && ref.document_id === payload.document_id && ref.revision === payload.revision && ref.evaluation_id === payload.evaluation_id && ref.feature_id === payload.feature_id)
              selection = { reference: ref, geometry };
          }
        }
        const hidden = this.partIds(payload).filter(id => (sameDocument && !mayUseSavedVisibility() ? this.value.hidden_part_ids : (savedHidden || confirmed.hidden_part_ids || [])).includes(id));
        this.update({ status: 'ready', payload, selection: this.visibleSelection(selection, hidden), model: state.model, error: null, context_error: null, hidden_part_ids: hidden,
          document_id: payload.document_id, revision: payload.revision,
          camera: sameDocument ? this.value.camera : (saved?.camera || null) });
        // Host context is optional and may acknowledge slowly. It must not hold up HEAD polling.
        const publicationVersion = this.contextVersion + 1;
        this.publishContext().catch(error => { if (current() && publicationVersion === this.contextVersion) this.update({ context_error: error.message }); });
      } catch (error) {
        // Polling is read-only: a brief lock conflict or an evaluation superseded
        // during transfer can be retried by the next bounded poll. Old picks
        // stay disabled while the last successfully rendered solid is retained.
        // This does not retry context publication, mutations, or ui/message.
        if (current()) this.update({ status: retryablePoll(error.code) ? 'loading' : 'error', selection: null,
          error: retryablePoll(error.code) ? null : error.message });
      } finally { this.busy = false; }
    }
    async start() {
      if (this.closed) return;
      await this.pollOnce();
      if (!this.closed) this.timer = setTimeout(() => this.start(), 1000);
    }
    snapshot(prompt = '') {
      const v = this.value, p = v.payload;
      if (v.status !== 'ready' || !p) throw Error('Wait for the current revision to finish loading.');
      const snapshot = copy({ view_id: v.view_id, document_id: p.document_id, revision: p.revision,
        evaluation_id: p.evaluation_id, feature_id: p.feature_id, selection: v.selection?.reference || null,
        hidden_part_ids: v.hidden_part_ids, ...(v.camera ? { camera: v.camera } : {}), prompt });
      this.snapshotVersions.set(snapshot, this.visibilityVersion); return snapshot;
    }
    matches(snapshot) { return !this.closed && this.value.status === 'ready' && this.value.view_id === snapshot.view_id && this.value.payload?.evaluation_id === snapshot.evaluation_id && (!this.snapshotVersions.has(snapshot) || this.snapshotVersions.get(snapshot) === this.visibilityVersion); }
    saveContext(snapshot) {
      const action = this.contextQueue.then(async () => {
        if (!this.matches(snapshot)) throw Error('The model changed. Review the new revision before sending.');
        const { view_id, evaluation_id, selection, camera, prompt, hidden_part_ids } = snapshot;
        const result = await this.bridge.tool('cad_viewer', { action: 'context', view_id, evaluation_id, selection,
          hidden_part_ids, ...(camera ? { camera } : {}), prompt });
        if (!this.matches(snapshot)) throw Error('The model changed. Review the new revision before sending.');
        this.visibilitySaved = this.visibilityVersion;
        if (this.value.visibility_unsaved) this.update({ visibility_unsaved: false });
        return result;
      });
      this.contextQueue = action.catch(() => {}); return action;
    }
    static promptText(snapshot) {
      const { prompt, ...context } = snapshot;
      return `${prompt || 'Inspect the selected CAD geometry.'}\n\nCAD view context (millimeters):\n${JSON.stringify(context, null, 2)}\nRead this document before editing. Resolve any selection with cad_resolve_selection; use expected_revision for edits. The open viewer follows committed changes.`;
    }
    async publishContext() {
      const snapshot = this.snapshot(), version = ++this.contextVersion;
      const priorDelivery = this.deliveryQueue;
      // Native visibility writes must not wait for an optional host context
      // acknowledgment. Native writes themselves remain ordered by contextQueue.
      const saved = Promise.resolve().then(async () => {
        if (version !== this.contextVersion || !this.matches(snapshot)) return false;
        await this.saveContext(snapshot);
        return true;
      });
      const deliver = saved.then(async persisted => {
        if (!persisted) return;
        await priorDelivery;
        if (this.bridge.capabilities.updateModelContext && this.matches(snapshot) && version === this.contextVersion) {
          await this.bridge.request('ui/update-model-context', { content: [{ type: 'text', text: CadLiveState.promptText(snapshot) }], structuredContent: snapshot });
        }
      });
      this.deliveryQueue = deliver.catch(() => {}); return deliver;
    }
    async sendPrompt(prompt, image) {
      if (!prompt.trim() || new TextEncoder().encode(prompt).length > 8000) throw Error('Write a request of up to 8,000 UTF-8 bytes.');
      if (!this.bridge.capabilities.message) throw Error('This host cannot send messages. Use Copy request.');
      const snapshot = this.snapshot(prompt.trim()); this.contextVersion++;
      await this.saveContext(snapshot);
      const content = [{ type: 'text', text: CadLiveState.promptText(snapshot) }];
      if (image) {
        if (!this.bridge.capabilities.message.image) throw Error('This host does not accept images in messages.');
        if (!/^data:image\/png;base64,[A-Za-z0-9+/=]+$/.test(image) || image.length > 2 * 1024 * 1024) throw Error('The viewport image exceeds the 2 MiB limit.');
        content.push({ type: 'image', mimeType: 'image/png', data: image.slice(image.indexOf(',') + 1) });
      }
      // Deliberately never retry: a lost acknowledgment can still mean delivery succeeded.
      await this.bridge.request('ui/message', { role: 'user', content }, 30000);
    }
    dispose() { this.closed = true; this.epoch++; clearTimeout(this.timer); }
  }
  globalThis.CadLiveState = CadLiveState;
})();
