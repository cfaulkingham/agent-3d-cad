/* Native CAD's MCP Apps transport. No network or host globals required. */
(() => {
  'use strict';
  class CadBridge {
    constructor(self = window) {
      this.self = self; this.peer = self.parent; this.nextId = 1;
      this.pending = new Map(); this.listeners = new Map(); this.closed = false;
      this.origin = null; this.capabilities = {}; this.hostContext = {}; this.lastResult = null;
      this.receive = this.receive.bind(this);
      self.addEventListener('message', this.receive);
    }
    on(method, listener) {
      if (!this.listeners.has(method)) this.listeners.set(method, new Set());
      this.listeners.get(method).add(listener);
      if (method === 'ui/notifications/tool-result' && this.lastResult) listener(this.lastResult);
      return () => this.listeners.get(method)?.delete(listener);
    }
    emit(method, value) { for (const fn of this.listeners.get(method) || []) fn(value); }
    post(value) {
      if (this.closed) throw Error('The CAD connection has closed.');
      this.peer.postMessage({ jsonrpc: '2.0', ...value }, this.origin && this.origin !== 'null' ? this.origin : '*');
    }
    request(method, params = {}, timeoutMs = 15000) {
      if (this.closed) return Promise.reject(Error('The CAD connection has closed.'));
      const id = `cad-${this.nextId++}`;
      return new Promise((resolve, reject) => {
        const timer = setTimeout(() => {
          this.pending.delete(id);
          reject(Error(method === 'ui/message' ? 'The host did not confirm delivery. Check the chat before sending again.' : 'The CAD host did not respond.'));
        }, timeoutMs);
        this.pending.set(id, { resolve, reject, timer });
        try { this.post({ id, method, params }); }
        catch (error) { clearTimeout(timer); this.pending.delete(id); reject(error); }
      });
    }
    notify(method, params = {}) { if (!this.desktop) this.post({ method, params }); }
    reportSize(width, height) {
      if (!Number.isFinite(width) || !Number.isFinite(height) || width < 1 || height < 1) return;
      const size = { width: Math.min(10000, Math.ceil(width)), height: Math.min(10000, Math.ceil(height)) };
      if (this.lastSize?.width === size.width && this.lastSize?.height === size.height) return;
      this.lastSize = size; this.notify('ui/notifications/size-changed', size);
    }
    receive(event) {
      // Opaque sandbox origins require '*', but the sender must still be our parent.
      if (event.source !== this.peer || (this.origin !== null && event.origin !== this.origin)) return;
      const m = event.data;
      if (!m || typeof m !== 'object' || m.jsonrpc !== '2.0') return;
      if (!m.method) {
        const item = this.pending.get(m.id);
        if (!item || (!Object.hasOwn(m, 'result') && !m.error)) return;
        if (this.origin === null) this.origin = event.origin;
        this.pending.delete(m.id); clearTimeout(item.timer);
        if (m.error) { const error = Error(m.error.message || 'The host refused the request.'); error.code = m.error.code; item.reject(error); }
        else item.resolve(m.result);
      } else if (Object.hasOwn(m, 'id')) {
        if (m.method === 'ping') this.post({ id: m.id, result: {} });
        else if (m.method === 'ui/resource-teardown') {
          this.post({ id: m.id, result: {} }); this.emit(m.method, m.params || {}); this.dispose();
        } else this.post({ id: m.id, error: { code: -32601, message: 'Unsupported UI method' } });
      } else {
        if (m.method === 'ui/notifications/tool-result') this.lastResult = m.params;
        if (m.method === 'ui/notifications/host-context-changed') this.hostContext = { ...this.hostContext, ...m.params };
        this.emit(m.method, m.params || {});
      }
    }
    async initialize() {
      if (this.self.__TAURI__) {
        const invoke = async (command, args) => {
          try { return await this.self.__TAURI__.core.invoke(command, args); }
          catch (error) { throw error instanceof Error ? error : Error(String(error)); }
        };
        this.self.cadDesktop = {
          initialize: () => invoke('initialize'),
          call: (name, args) => invoke('call_tool', { name, args }),
          openWorkspace: () => invoke('open_workspace'),
          recentWorkspaces: () => invoke('recent_workspaces'),
          openRecent: index => invoke('open_recent', { index }),
          export: request => invoke('export_model', { request })
        };
      }
      if (this.self.cadDesktop) {
        this.desktop = this.self.cadDesktop;
        const opened = await this.desktop.initialize();
        this.capabilities = { desktop: true }; this.hostContext = { workspace: opened.workspace };
        this.lastResult = { structuredContent: opened };
        this.emit('ui/notifications/tool-result', this.lastResult);
        return {};
      }
      if (this.peer === this.self) throw Error('Open this viewer through cad_open in an MCP Apps host. For offline review, use cad_view.');
      const result = await this.request('ui/initialize', {
        protocolVersion: '2026-01-26', appInfo: { name: 'agent-3d-cad', version: '0.1.0' },
        appCapabilities: { availableDisplayModes: ['inline', 'fullscreen'] }
      });
      if (result?.protocolVersion !== '2026-01-26') throw Error('The host uses an unsupported MCP Apps version.');
      this.capabilities = result.hostCapabilities || {}; this.hostContext = result.hostContext || {};
      this.notify('ui/notifications/initialized');
      return result;
    }
    static value(result) {
      let value = result?.structuredContent;
      if (!value) {
        const text = result?.content?.find(item => item.type === 'text')?.text;
        if (text) { try { value = JSON.parse(text); } catch { /* Report below. */ } }
      }
      // Tool failure is isError. A successful payload may describe an error
      // state (sync `state: "error"` with `error`) and must reach the caller.
      // A bare {error} envelope is also a failure if a host dropped isError.
      const envelope = value && typeof value === 'object' && !Array.isArray(value) && Object.keys(value).length === 1 && value.error;
      if (result?.isError || envelope) {
        const problem = value?.error;
        const error = Error(problem?.message || 'The CAD service could not complete the request.');
        error.code = problem?.code; error.details = problem?.details; throw error;
      }
      if (!value || typeof value !== 'object') throw Error('The CAD service returned an invalid response.');
      return value;
    }
    async tool(name, args = {}) { return CadBridge.value(this.desktop ? await this.desktop.call(name, args) : await this.request('tools/call', { name, arguments: args })); }
    async download(contents) {
      if (!this.capabilities.downloadFile) throw Error('This host does not support file downloads.');
      if (!Array.isArray(contents) || !contents.length || contents.length > 128) throw Error('No bounded export download set is available.');
      return this.request('ui/download-file', { contents }, 120000);
    }
    dispose() {
      if (this.closed) return;
      this.closed = true; this.self.removeEventListener('message', this.receive);
      for (const item of this.pending.values()) { clearTimeout(item.timer); item.reject(Error('The CAD view closed.')); }
      this.pending.clear(); this.listeners.clear();
    }
  }
  globalThis.CadBridge = CadBridge;
})();
