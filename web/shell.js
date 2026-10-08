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
  globalThis.CadShell = Object.freeze({ resolveTheme, layoutMode, CUBE_FACES, cubeDirection, cubeMatrix, cubeFaceMatrix });
})();
