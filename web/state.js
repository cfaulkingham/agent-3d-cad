/* View coordination is separate from DOM/WebGL so revision races can be tested. */
(() => {
  'use strict';
  const copy = value => value == null ? value : JSON.parse(JSON.stringify(value));
  const retryablePoll = code => ['workspace_busy', 'queue_full', 'stale_selection'].includes(code);
  class CadLiveState {
    constructor(bridge, changed = () => {}) {
      this.bridge = bridge; this.changed = changed; this.epoch = 0; this.busy = false; this.closed = false;
      this.contextQueue = Promise.resolve(); this.deliveryQueue = Promise.resolve(); this.contextVersion = 0; this.timer = null;
      this.value = { view_id: null, status: 'connecting', payload: null, selection: null, camera: null, model: null, error: null };
    }
    update(values) { Object.assign(this.value, values); this.changed(this.value); }
    attach(view_id) {
      if (typeof view_id !== 'string' || !/^[A-Za-z][A-Za-z0-9_-]{0,63}$/.test(view_id)) throw Error('Invalid CAD view identity.');
      if (this.value.view_id === view_id) return;
      this.epoch++; this.update({ view_id, status: 'loading', payload: null, selection: null, camera: null, model: null, error: null });
    }
    invalidate() { this.epoch++; this.contextVersion++; this.update({ status: 'loading', selection: null, error: null }); }
    async pollOnce() {
      if (this.busy || this.closed || !this.value.view_id) return;
      this.busy = true;
      const epoch = this.epoch, view = this.value.view_id;
      const current = () => !this.closed && epoch === this.epoch;
      try {
        const state = await this.bridge.tool('cad_viewer', { action: 'sync', view_id: view,
          ...(this.value.payload ? { known_evaluation_id: this.value.payload.evaluation_id } : {}) });
        if (!current()) return;
        if (state.state !== 'ready') {
          // Keep the last solid visible while building, but disable interaction with its old references.
          this.update({ status: state.state, selection: null, error: state.state === 'loading' && retryablePoll(state.error?.code) ? null : state.error?.message || null,
            document_id: state.document_id, revision: state.revision,
            ...(state.state === 'empty' || (this.value.payload && this.value.payload.document_id !== state.document_id) ? { payload: null, model: null, camera: null } : {}) });
          return;
        }
        if (state.evaluation_id === this.value.payload?.evaluation_id) {
          this.update({ status: 'ready', error: null }); return;
        }
        this.update({ status: 'loading', selection: null, error: null, document_id: state.document_id, revision: state.revision,
          ...(this.value.payload && this.value.payload.document_id !== state.document_id ? { payload: null, model: null, camera: null } : {}) });
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
        let saved = null, selection = null;
        if (!sameDocument) {
          saved = await this.bridge.tool('cad_context', { view_id: view });
          if (!current()) return;
          if (saved.document_id !== payload.document_id || (saved.head_revision !== undefined && saved.head_revision !== payload.revision)) return;
          if (saved.stale || saved.document_id !== payload.document_id || saved.evaluation_id !== payload.evaluation_id) saved = null;
          if (saved?.selection) {
            const ref = saved.selection, geometry = payload.topology[ref.kind === 'face' ? 'faces' : 'edges']?.find(item => item.id === ref.entity_id);
            if (geometry && ref.document_id === payload.document_id && ref.revision === payload.revision && ref.evaluation_id === payload.evaluation_id && ref.feature_id === payload.feature_id)
              selection = { reference: ref, geometry };
          }
        }
        this.update({ status: 'ready', payload, selection, model: state.model, error: null, context_error: null,
          document_id: payload.document_id, revision: payload.revision,
          camera: sameDocument ? this.value.camera : (saved?.camera || null) });
        // Host context is optional and may acknowledge slowly. It must not hold up HEAD polling.
        this.publishContext().catch(error => { if (current()) this.update({ context_error: error.message }); });
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
      return copy({ view_id: v.view_id, document_id: p.document_id, revision: p.revision,
        evaluation_id: p.evaluation_id, feature_id: p.feature_id, selection: v.selection?.reference || null,
        ...(v.camera ? { camera: v.camera } : {}), prompt });
    }
    matches(snapshot) { return !this.closed && this.value.status === 'ready' && this.value.view_id === snapshot.view_id && this.value.payload?.evaluation_id === snapshot.evaluation_id; }
    saveContext(snapshot) {
      const action = this.contextQueue.then(async () => {
        if (!this.matches(snapshot)) throw Error('The model changed. Review the new revision before sending.');
        const { view_id, evaluation_id, selection, camera, prompt } = snapshot;
        const result = await this.bridge.tool('cad_viewer', { action: 'context', view_id, evaluation_id, selection,
          ...(camera ? { camera } : {}), prompt });
        if (!this.matches(snapshot)) throw Error('The model changed. Review the new revision before sending.');
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
      const deliver = this.deliveryQueue.then(async () => {
        if (version !== this.contextVersion || !this.matches(snapshot)) return;
        await this.saveContext(snapshot);
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
