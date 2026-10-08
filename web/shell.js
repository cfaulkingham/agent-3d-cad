/* Viewer shell: pure UI decisions first, DOM glue added by later tasks. */
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
  globalThis.CadShell = Object.freeze({ resolveTheme, layoutMode });
})();
