/* OhEzTouch MQTT Visualizer -- the space view.
 *
 * The third picture of the same facts: the house as a laser projection,
 * imported from a SweetHome3D file, and the network inside it. The
 * broker, the access points and the panels are placed by hand -- a drag
 * snaps them onto the wall or the floor under the pointer -- and the
 * beacons find their own places from the distances the panels report,
 * in metres, which the house finally gives a meaning to.
 *
 * app.js stays the page; it hands this module what it knows through
 * window.mqttvizHost and calls MqttvizSpace.update() after every poll.
 * Everything here is drawn by three.js, vendored as one bundle.
 */
import * as THREE from "/vendor/three.bundle.js";

const { OrbitControls, CSS2DRenderer, CSS2DObject, EffectComposer,
        RenderPass, UnrealBloomPass, OutputPass } = THREE;

const host = window.mqttvizHost;
const P = host.PALETTE;

const HOLO = new THREE.Color("#5fe0ff");     // the projection's own colour
const BEACON_HEIGHT = 1.2;    // m above the floor a beacon is carried at
const WALL_OFFSET = 0.025;    // m a plate stands off the wall it sits on
const WALL_SNAP = 0.35;       // m from a wall at which a drag sticks to it
// m above the floor each kind goes onto a wall at, until it is lifted
const DEFAULT_HEIGHT = { panel: 1.4, ap: 2.1, broker: 1.6 };
const DOCK_GAP = 0.7;         // m between the unplaced, waiting in a row
const TRAIL_LEN = 48;

/* ------------------------------------------------------------- the stage */

let ready = false;
let active = false;
let renderer, labels, scene, camera, controls, composer, bloom;
let root, houseGroup, objGroup, linkLines, beaconLines, pulsePoints;
let house = null;
let houseLevels = [];        // [{id, name, elevation, height, group, ...}]
let levelFilter = null;      // a level id, or null for the whole house
let placeTargets = [];       // the meshes a drag may snap onto
let bounds = { min: [-3, -3], max: [3, 3] };
let camAdopted = false;
let houseLoaded = false;     // the camera frames the house, once it is in
let lastTime = performance.now();

const objs = new Map();      // key -> one object of the network
const pulses = [];           // activity travelling along the links
const holoMaterials = [];    // every material that follows the opacity

const stageEl = document.getElementById("stage");
const ui = {};

function settings() { return host.state.settings || {}; }
function placed() { return settings().positions3d || {}; }
function opacity() { return (Number(settings().holo_opacity) || 50) / 100; }
function showBroker() { return settings().show_broker !== false; }
function showBeacons() { return settings().show_beacons !== false; }

/* A soft radial glow, the 3D cousin of the canvas's halo sprites. */
const glowTextures = {};
function glowTexture(hex) {
  if (glowTextures[hex]) return glowTextures[hex];
  const c = document.createElement("canvas");
  c.width = c.height = 64;
  const g = c.getContext("2d");
  const col = new THREE.Color(hex);
  const rgb = [col.r, col.g, col.b].map((v) => Math.round(v * 255)).join(",");
  const grad = g.createRadialGradient(32, 32, 0, 32, 32, 32);
  grad.addColorStop(0, "rgba(" + rgb + ",1)");
  grad.addColorStop(0.25, "rgba(" + rgb + ",0.45)");
  grad.addColorStop(1, "rgba(" + rgb + ",0)");
  g.fillStyle = grad;
  g.fillRect(0, 0, 64, 64);
  const texture = new THREE.CanvasTexture(c);
  texture.colorSpace = THREE.SRGBColorSpace;
  glowTextures[hex] = texture;
  return texture;
}

function glowSprite(hex, size, alpha) {
  const sprite = new THREE.Sprite(new THREE.SpriteMaterial({
    map: glowTexture(hex), color: 0xffffff, transparent: true,
    opacity: alpha, depthWrite: false, blending: THREE.AdditiveBlending,
  }));
  sprite.scale.set(size, size, 1);
  sprite.raycast = () => {};            // a glow is never what was clicked
  return sprite;
}

/* ------------------------------------------------------------ hologram */

/* The walls: light, not matter. Brighter where they are seen edge-on,
 * striped with faint scanlines, swept by a band of light rising from
 * the floor, and never quite steady. Additive, so walls behind walls
 * add up the way a projection in fog would. */
const HOLO_VERTEX = `
  varying vec3 vWorld;
  varying vec3 vNormal;
  void main() {
    vec4 world = modelMatrix * vec4(position, 1.0);
    vWorld = world.xyz;
    vNormal = normalize(mat3(modelMatrix) * normal);
    gl_Position = projectionMatrix * viewMatrix * world;
  }
`;

const HOLO_FRAGMENT = `
  uniform vec3 uColor;
  uniform float uTime;
  uniform float uOpacity;
  uniform float uBase;
  uniform float uTop;
  varying vec3 vWorld;
  varying vec3 vNormal;
  void main() {
    vec3 n = normalize(vNormal);
    vec3 v = normalize(cameraPosition - vWorld);
    float rim = pow(1.0 - abs(dot(n, v)), 2.0);
    float h = clamp((vWorld.y - uBase) / max(0.01, uTop - uBase), 0.0, 1.0);
    // the tops and feet of the walls take no scanlines and no band:
    // a stripe on a level face would only be a wash of light
    float upright = 1.0 - abs(n.y);
    float lines = upright
                  * smoothstep(0.55, 1.0, 0.5 + 0.5 * sin(vWorld.y * 140.0));
    float scan = fract(uTime * 0.12);
    float band = upright * exp(-pow((h - scan) * 14.0, 2.0));
    float flicker = 0.93 + 0.07 * sin(uTime * 41.0 + vWorld.x * 2.7 + vWorld.z);
    float fade = mix(1.0, 0.45, h);
    float a = uOpacity * (0.06 + rim * 0.35 + lines * 0.05 + band * 0.4)
              * flicker * fade;
    gl_FragColor = vec4(uColor * (1.0 + band * 0.8), a);
  }
`;

function holoMaterial(base, top) {
  const material = new THREE.ShaderMaterial({
    uniforms: {
      uColor: { value: HOLO.clone() },
      uTime: { value: 0 },
      uOpacity: { value: 0.5 },
      uBase: { value: base },
      uTop: { value: top },
    },
    vertexShader: HOLO_VERTEX,
    fragmentShader: HOLO_FRAGMENT,
    transparent: true,
    depthWrite: false,
    side: THREE.DoubleSide,
    blending: THREE.AdditiveBlending,
  });
  material.userData.weight = 0.5;
  holoMaterials.push(material);
  return material;
}

function lineMaterial(hex, weight) {
  const material = new THREE.LineBasicMaterial({
    color: hex, transparent: true, depthWrite: false,
    blending: THREE.AdditiveBlending,
  });
  material.userData.weight = weight;
  holoMaterials.push(material);
  return material;
}

function fillMaterial(hex, weight) {
  const material = new THREE.MeshBasicMaterial({
    color: hex, transparent: true, depthWrite: false,
    side: THREE.DoubleSide, blending: THREE.AdditiveBlending,
  });
  material.userData.weight = weight;
  holoMaterials.push(material);
  return material;
}

/* How bright the house is: the slider times each material's own share,
 * dimmed for the levels below the one looked at. */
function applyOpacity() {
  const base = opacity();
  for (const level of houseLevels) {
    const dim = level.dim ? 0.3 : 1;
    for (const material of level.materials) {
      const value = base * material.userData.weight * dim;
      if (material.uniforms) material.uniforms.uOpacity.value = value;
      else material.opacity = Math.min(1, value);
    }
  }
}

/* ---------------------------------------------------------- the house */

/* All the walls of one level as one geometry: a prism per wall, its top
 * sloped where the file says the wall is higher at one end. Flat
 * shaded, so a raycast's face normal is the wall's own. */
function wallGeometry(walls, y0) {
  const pos = [];
  const quad = (a, b, c, d) => pos.push(...a, ...b, ...c, ...a, ...c, ...d);

  for (const wall of walls) {
    const [ax, az] = wall.a, [bx, bz] = wall.b;
    const len = Math.hypot(bx - ax, bz - az);
    if (len < 1e-3) continue;
    const nx = -(bz - az) / len * wall.thickness / 2;
    const nz = (bx - ax) / len * wall.thickness / 2;
    const ya = y0 + wall.height, yb = y0 + wall.height_end;

    const a0 = [ax + nx, y0, az + nz], a1 = [ax - nx, y0, az - nz];
    const b0 = [bx + nx, y0, bz + nz], b1 = [bx - nx, y0, bz - nz];
    const A0 = [ax + nx, ya, az + nz], A1 = [ax - nx, ya, az - nz];
    const B0 = [bx + nx, yb, bz + nz], B1 = [bx - nx, yb, bz - nz];

    quad(a0, b0, B0, A0);      // one face
    quad(b1, a1, A1, B1);      // the other
    quad(A0, B0, B1, A1);      // the top
    quad(a1, b1, b0, a0);      // the foot
    const caps = wall.caps || [true, true];
    if (caps[0]) quad(a1, a0, A0, A1);      // the start
    if (caps[1]) quad(b0, b1, B1, B0);      // the end
  }

  const geometry = new THREE.BufferGeometry();
  geometry.setAttribute("position", new THREE.Float32BufferAttribute(pos, 3));
  geometry.computeVertexNormals();
  return geometry;
}

function roomShape(points) {
  const shape = new THREE.Shape();
  points.forEach(([x, z], i) => {
    if (i === 0) shape.moveTo(x, -z);
    else shape.lineTo(x, -z);
  });
  shape.closePath();
  return shape;
}

function clearGroup(group) {
  for (const child of [...group.children]) {
    group.remove(child);
    child.traverse((node) => {
      if (node.geometry) node.geometry.dispose();
      if (node.isCSS2DObject && node.element) node.element.remove();
    });
  }
}

function buildHouse() {
  clearGroup(houseGroup);
  holoMaterials.length = 0;
  houseLevels = [];
  placeTargets = [];

  const levels = house && house.levels && house.levels.length
    ? house.levels
    : [{ id: "_", name: "", elevation: 0, height: 2.5 }];

  if (house && house.bounds) bounds = house.bounds;
  else bounds = { min: [-3, -3], max: [3, 3] };

  const cx = (bounds.min[0] + bounds.max[0]) / 2;
  const cz = (bounds.min[1] + bounds.max[1]) / 2;
  const span = Math.max(4, bounds.max[0] - bounds.min[0],
                        bounds.max[1] - bounds.min[1]);

  /* The projector's floor: a grid under the whole house, and a ring of
   * light around it -- the plate the picture is cast from. */
  const grid = new THREE.GridHelper(Math.ceil(span + 4), Math.ceil(span + 4),
                                    0x1d5566, 0x0f2a36);
  grid.position.set(cx, -0.002, cz);
  grid.material.transparent = true;
  grid.material.opacity = 0.55;
  grid.material.depthWrite = false;
  houseGroup.add(grid);

  const ring = new THREE.Mesh(
    new THREE.RingGeometry(span * 0.72, span * 0.74, 96),
    new THREE.MeshBasicMaterial({ color: HOLO, transparent: true,
                                  opacity: 0.25, depthWrite: false,
                                  side: THREE.DoubleSide,
                                  blending: THREE.AdditiveBlending }));
  ring.rotation.x = -Math.PI / 2;
  ring.position.set(cx, 0, cz);
  ring.raycast = () => {};
  houseGroup.add(ring);

  for (const level of levels) {
    const group = new THREE.Group();
    const y0 = level.elevation;
    const entry = { ...level, group, materials: [], dim: false, walls: [] };
    houseLevels.push(entry);
    houseGroup.add(group);

    const walls = (house && house.walls || [])
      .filter((w) => w.level === level.id);
    entry.walls = walls;
    const top = y0 + Math.max(level.height,
                              ...walls.map((w) => Math.max(w.height,
                                                           w.height_end)));

    if (walls.length) {
      const geometry = wallGeometry(walls, y0);
      const material = holoMaterial(y0, top);
      const mesh = new THREE.Mesh(geometry, material);
      mesh.userData = { level: level.id, surface: "wall" };
      mesh.renderOrder = 2;
      group.add(mesh);
      placeTargets.push(mesh);
      entry.materials.push(material);

      const edgeMat = lineMaterial(HOLO, 0.8);
      const edges = new THREE.LineSegments(
        new THREE.EdgesGeometry(geometry, 25), edgeMat);
      edges.raycast = () => {};
      edges.renderOrder = 3;
      group.add(edges);
      entry.materials.push(edgeMat);
    }

    /* The floors: a breath of light inside every room, its outline
     * drawn sharp, its name floating just above it. */
    const floorMat = fillMaterial(HOLO, 0.05);
    const outlineMat = lineMaterial(HOLO, 0.5);
    entry.materials.push(floorMat, outlineMat);
    for (const room of (house && house.rooms || [])
           .filter((r) => r.level === level.id)) {
      const geometry = new THREE.ShapeGeometry(roomShape(room.points));
      geometry.rotateX(-Math.PI / 2);
      const floor = new THREE.Mesh(geometry, floorMat);
      floor.position.y = y0 + 0.003;
      floor.userData = { level: level.id, surface: "floor" };
      group.add(floor);
      placeTargets.push(floor);

      const outline = new THREE.LineLoop(
        new THREE.BufferGeometry().setFromPoints(
          room.points.map(([x, z]) => new THREE.Vector3(x, y0 + 0.006, z))),
        outlineMat);
      outline.raycast = () => {};
      group.add(outline);

      if (room.name) {
        let sx = 0, sz = 0;
        for (const [x, z] of room.points) { sx += x; sz += z; }
        const el = document.createElement("div");
        el.className = "space-room";
        el.textContent = room.name;
        const label = new CSS2DObject(el);
        label.position.set(sx / room.points.length, y0 + 0.05,
                           sz / room.points.length);
        group.add(label);
      }
    }

    /* Something to stand on where no room was drawn: an invisible
     * plane over the whole plan, so a drag always finds a floor. */
    const ground = new THREE.Mesh(
      new THREE.PlaneGeometry(span + 6, span + 6),
      new THREE.MeshBasicMaterial({ visible: false, side: THREE.DoubleSide }));
    ground.rotation.x = -Math.PI / 2;
    ground.position.set(cx, y0, cz);
    ground.userData = { level: level.id, surface: "floor", ground: true };
    group.add(ground);
    placeTargets.push(ground);

    /* The doors and windows: frames of brighter light in the walls. */
    const frameMat = lineMaterial(P.orange, 0.7);
    entry.materials.push(frameMat);
    for (const op of (house && house.openings || [])
           .filter((o) => o.level === level.id)) {
      const dx = Math.cos(op.angle) * op.w / 2;
      const dz = Math.sin(op.angle) * op.w / 2;
      const yb = y0 + op.elev, yt = yb + op.h;
      const frame = new THREE.LineLoop(
        new THREE.BufferGeometry().setFromPoints([
          new THREE.Vector3(op.x - dx, yb, op.z - dz),
          new THREE.Vector3(op.x + dx, yb, op.z + dz),
          new THREE.Vector3(op.x + dx, yt, op.z + dz),
          new THREE.Vector3(op.x - dx, yt, op.z - dz),
        ]), frameMat);
      frame.raycast = () => {};
      group.add(frame);
    }
  }

  applyLevelFilter();
  renderToolbar();
}

function levelById(id) {
  return houseLevels.find((l) => l.id === id) || houseLevels[0];
}

/* The level looked at is drawn whole; the ones below it dimmed, the
 * ones above it not at all -- the roof lifted off. */
function applyLevelFilter() {
  const chosen = levelFilter ? levelById(levelFilter) : null;
  for (const level of houseLevels) {
    const above = chosen && level.elevation > chosen.elevation + 1e-3;
    const below = chosen && level.elevation < chosen.elevation - 1e-3;
    level.group.visible = !above;
    level.dim = !!below;
  }
  applyOpacity();
}

/* Whether a level is drawn at all -- what sits on a hidden one is
 * hidden with it. */
function levelShown(id) {
  if (!id) return true;
  const level = houseLevels.find((l) => l.id === id);
  return !level || level.group.visible;
}

function visiblePlaceTargets() {
  const chosen = levelFilter ? levelById(levelFilter) : null;
  return placeTargets.filter((mesh) => {
    const level = levelById(mesh.userData.level);
    if (!level.group.visible) return false;
    // below the chosen level, only the chosen level's own floors count
    return !chosen || level === chosen;
  });
}

/* --------------------------------------------------------- the objects */

function makePanel() {
  const group = new THREE.Group();
  const body = new THREE.Mesh(
    new THREE.BoxGeometry(0.2, 0.13, 0.025),
    new THREE.MeshBasicMaterial({ color: P.orange, transparent: true,
                                  opacity: 0.35, depthWrite: false,
                                  blending: THREE.AdditiveBlending }));
  body.position.z = 0.0125;
  const edges = new THREE.LineSegments(
    new THREE.EdgesGeometry(body.geometry),
    new THREE.LineBasicMaterial({ color: P.orange }));
  edges.position.copy(body.position);
  edges.raycast = () => {};
  group.add(body, edges, glowSprite(P.orange, 0.7, 0.5));
  return { group, tint: [body.material, edges.material], hit: body };
}

function makeAp() {
  const group = new THREE.Group();
  const ring = new THREE.Mesh(
    new THREE.TorusGeometry(0.11, 0.012, 8, 40),
    new THREE.MeshBasicMaterial({ color: P.peri }));
  ring.rotation.x = Math.PI / 2;
  const dot = new THREE.Mesh(
    new THREE.SphereGeometry(0.04, 16, 12),
    new THREE.MeshBasicMaterial({ color: P.peri }));
  const hit = new THREE.Mesh(
    new THREE.SphereGeometry(0.16, 8, 6),
    new THREE.MeshBasicMaterial({ visible: false }));
  group.add(ring, dot, hit, glowSprite(P.peri, 0.8, 0.55));
  return { group, tint: [ring.material, dot.material], hit, spin: ring };
}

function makeBroker() {
  const group = new THREE.Group();
  const shell = new THREE.Mesh(
    new THREE.IcosahedronGeometry(0.17, 1),
    new THREE.MeshBasicMaterial({ color: P.orange, wireframe: true }));
  const core = new THREE.Mesh(
    new THREE.IcosahedronGeometry(0.07, 0),
    new THREE.MeshBasicMaterial({ color: "#ffd38a" }));
  group.add(shell, core, glowSprite(P.orange, 1.1, 0.6));
  return { group, tint: [shell.material], hit: shell, spin: shell };
}

function makeBeacon() {
  const group = new THREE.Group();
  const dot = new THREE.Mesh(
    new THREE.SphereGeometry(0.05, 16, 12),
    new THREE.MeshBasicMaterial({ color: P.teal }));
  const hit = new THREE.Mesh(
    new THREE.SphereGeometry(0.15, 8, 6),
    new THREE.MeshBasicMaterial({ visible: false }));
  group.add(dot, hit, glowSprite(P.teal, 0.6, 0.6));

  const trailGeometry = new THREE.BufferGeometry();
  trailGeometry.setAttribute("position", new THREE.BufferAttribute(
    new Float32Array(TRAIL_LEN * 3), 3));
  trailGeometry.setDrawRange(0, 0);
  const trail = new THREE.Line(
    trailGeometry,
    new THREE.LineBasicMaterial({ color: P.teal, transparent: true,
                                  opacity: 0.45, depthWrite: false,
                                  blending: THREE.AdditiveBlending }));
  trail.frustumCulled = false;
  trail.raycast = () => {};
  objGroup.add(trail);
  return { group, tint: [dot.material], hit, trail };
}

const MAKERS = { panel: makePanel, ap: makeAp, broker: makeBroker,
                 beacon: makeBeacon };

function ensureObj(key, kind) {
  let obj = objs.get(key);
  if (obj) return obj;

  const made = MAKERS[kind]();
  const el = document.createElement("div");
  el.className = "space-label space-" + kind;
  const label = new CSS2DObject(el);
  label.position.set(0, kind === "panel" ? 0.16 : 0.24, 0);
  made.group.add(label);
  made.group.userData.key = key;
  objGroup.add(made.group);

  obj = { key, kind, ...made, label: el, seen: true,
          pos: new THREE.Vector3(), target: new THREE.Vector3(),
          normal: new THREE.Vector3(0, 1, 0), level: null, placed: false,
          fresh: true, flash: 0, msgcount: null, history: [],
          lastTrail: 0, orbit: Math.random() * Math.PI * 2 };
  objs.set(key, obj);
  return obj;
}

function dropObj(obj) {
  objGroup.remove(obj.group);
  obj.label.remove();
  if (obj.trail) objGroup.remove(obj.trail);
  objs.delete(obj.key);
}

/* Which way an object faces: a plate's back against its wall, upright
 * on a floor, facing the way the floor's placement looked. */
function orient(obj) {
  const n = obj.normal;
  if (obj.kind !== "panel") {
    obj.group.quaternion.identity();
    return;
  }
  const facing = Math.abs(n.y) > 0.7
    ? new THREE.Vector3(Math.sin(obj.yaw || 0), 0, Math.cos(obj.yaw || 0))
    : new THREE.Vector3(n.x, 0, n.z).normalize();
  obj.group.quaternion.setFromUnitVectors(new THREE.Vector3(0, 0, 1), facing);
}

/* The dock: where everything the house has no place for yet waits, in
 * a row along the front of the plan, to be dragged in. */
function dockSpot(index) {
  const x = bounds.min[0] + index * DOCK_GAP;
  const z = bounds.max[1] + 1.2;
  return new THREE.Vector3(x, 0.25, z);
}

/* --------------------------------------------------------- the facts */

function apKey(bssid) { return "ap:" + bssid; }

/* What the network is right now, as objects in the house: one per
 * panel, one per access point the panels report, the broker, and one
 * per beacon -- each where it was placed, or in the dock. */
function syncObjects() {
  const state = host.state;
  const live = new Set();
  const stored = placed();
  let dockIndex = 0;

  const place = (obj, label, online) => {
    live.add(obj.key);
    obj.label.textContent = label;
    obj.label.classList.toggle("offline", online === false);
    for (const material of obj.tint) {
      material.color.set(online === false ? P.dim
                         : obj.kind === "ap" ? P.peri
                         : obj.kind === "beacon" ? P.teal : P.orange);
    }

    const pos = stored[obj.key];
    if (pos && !obj.dragging) {
      obj.placed = true;
      obj.target.set(pos.x, pos.y, pos.z);
      obj.normal.set(pos.nx, pos.ny, pos.nz);
      obj.yaw = pos.yaw || 0;
      obj.level = pos.level || null;
      if (Math.abs(pos.ny) < 0.7) {
        obj.height = pos.y - (levelById(obj.level) || { elevation: 0 })
          .elevation;
      }
    } else if (!pos && !obj.dragging) {
      obj.placed = false;
      obj.target.copy(dockSpot(dockIndex));
      obj.height = null;
      obj.normal.set(0, 0, 1);
      obj.yaw = 0;
      obj.level = null;
    }
    // every object keeps its own spot in the dock, placed or not, so
    // taking one into the house never shuffles the others under the hand
    dockIndex++;
    if (obj.fresh) { obj.pos.copy(obj.target); obj.fresh = false; }
    orient(obj);
  };

  const broker = ensureObj("broker", "broker");
  broker.group.visible = showBroker();
  place(broker, state.broker && state.broker.host
        ? state.broker.host : host.t("word.broker"),
        state.broker ? state.broker.connected : false);

  const aps = new Map();
  for (const dev of state.devices) {
    const bssid = host.deviceBssid(dev);
    if (bssid && !aps.has(bssid)) aps.set(bssid, host.deviceSsid(dev));
  }
  for (const [bssid, ssid] of [...aps].sort()) {
    const obj = ensureObj(apKey(bssid), "ap");
    obj.bssid = bssid;
    place(obj, ssid || bssid, true);
  }

  for (const dev of state.devices) {
    const obj = ensureObj("panel:" + dev.host, "panel");
    obj.host = dev.host;
    obj.bssid = host.deviceBssid(dev);
    if (obj.msgcount != null && dev.msgcount > obj.msgcount) {
      obj.flash = 1;
      spawnPulse(obj);
    }
    obj.msgcount = dev.msgcount;
    place(obj, dev.host, !!dev.online);
  }

  for (const beacon of state.beacons) {
    const key = "beacon:" + beacon.addr;
    live.add(key);
    const obj = ensureObj(key, "beacon");
    obj.addr = beacon.addr;
    obj.dev = beacon;
    obj.label.textContent = beacon.name || beacon.addr;
    obj.label.classList.toggle("offline", !beacon.active);
  }

  for (const obj of [...objs.values()]) {
    if (!live.has(obj.key)) dropObj(obj);
  }
}

/* -------------------------------------------------------------- beacons */

/* Where a beacon is, from what the placed panels say of it: the point
 * whose distances to them come closest to the ones they report -- a
 * few steps of gradient descent from where it was, in the plane of
 * the floor, at hand height. One panel alone can only say how far: the
 * beacon circles it at that distance. None, and it is not drawn. */
function solveBeacon(obj, dt) {
  const beacon = obj.dev;
  const timeout = Number((settings().phys_beacons || {}).line_timeout) || 90;
  const hearers = [];
  for (const seen of beacon.devices || []) {
    const panel = objs.get("panel:" + seen.host);
    const d = Number(seen.distance);
    if (!panel || !panel.placed || !(d > 0) || seen.age > timeout) continue;
    hearers.push({ p: panel.target, d: Math.min(30, d), panel, seen });
  }
  obj.hearers = hearers;
  if (!hearers.length) return false;

  const floorY = (levelById(hearers[0].panel.level) || { elevation: 0 })
    .elevation + BEACON_HEIGHT;

  if (hearers.length === 1) {
    obj.orbit += dt * 0.25;
    const h = hearers[0];
    obj.target.set(h.p.x + Math.cos(obj.orbit) * h.d, floorY,
                   h.p.z + Math.sin(obj.orbit) * h.d);
    return true;
  }

  let x = obj.solved ? obj.target.x : 0, z = obj.solved ? obj.target.z : 0;
  if (!obj.solved) {
    for (const h of hearers) { x += h.p.x; z += h.p.z; }
    x /= hearers.length; z /= hearers.length;
    x += 0.05; z += 0.05;                 // never exactly on a panel
  }
  for (let i = 0; i < 40; i++) {
    let gx = 0, gz = 0;
    for (const h of hearers) {
      const dx = x - h.p.x, dz = z - h.p.z;
      const r = Math.max(0.05, Math.hypot(dx, dz));
      const err = r - h.d;
      gx += err * dx / r;
      gz += err * dz / r;
    }
    x -= 0.4 * gx / hearers.length;
    z -= 0.4 * gz / hearers.length;
  }
  obj.solved = true;
  obj.target.set(x, floorY, z);
  return true;
}

/* ---------------------------------------------------------- the links */

const linkColor = new THREE.Color(P.peri);
const beaconColor = new THREE.Color(P.teal);

/* Where an object's line goes: a panel to its access point, an access
 * point to the broker -- and past whatever is hidden, to the next one
 * that is drawn, or nowhere. */
function uplinkOf(obj) {
  const shown = (o) => (o && o.group.visible ? o : null);
  const broker = () => shown(objs.get("broker"));
  if (obj.kind === "panel") {
    return shown(obj.bssid && objs.get(apKey(obj.bssid))) || broker();
  }
  if (obj.kind === "ap") return broker();
  return null;
}

/* A message is a spark running up the panel's line: to its access
 * point, and on to the broker. */
function spawnPulse(panel) {
  if (!active) return;
  if (pulses.length > 120) pulses.shift();
  pulses.push({ from: panel, t0: performance.now(), dur: 900 });
}

function updateLinks(time) {
  const pos = [], col = [];
  for (const obj of objs.values()) {
    const up = uplinkOf(obj);
    if (!up || !obj.group.visible || !up.group.visible) continue;
    pos.push(obj.pos.x, obj.pos.y, obj.pos.z, up.pos.x, up.pos.y, up.pos.z);
    const k = 0.35 + (obj.flash || 0) * 0.65;
    col.push(linkColor.r * k, linkColor.g * k, linkColor.b * k,
             linkColor.r * 0.35, linkColor.g * 0.35, linkColor.b * 0.35);
  }
  setLineBuffer(linkLines, pos, col);

  const bpos = [], bcol = [];
  for (const obj of objs.values()) {
    if (obj.kind !== "beacon" || !obj.group.visible) continue;
    for (const h of obj.hearers || []) {
      const fade = Math.max(0.15, 1 - h.seen.age / 60);
      bpos.push(obj.pos.x, obj.pos.y, obj.pos.z,
                h.panel.pos.x, h.panel.pos.y, h.panel.pos.z);
      bcol.push(beaconColor.r * fade, beaconColor.g * fade,
                beaconColor.b * fade, beaconColor.r * fade * 0.4,
                beaconColor.g * fade * 0.4, beaconColor.b * fade * 0.4);
    }
  }
  setLineBuffer(beaconLines, bpos, bcol);

  const ppos = [];
  for (let i = pulses.length - 1; i >= 0; i--) {
    const pulse = pulses[i];
    const t = (time - pulse.t0) / pulse.dur;
    const first = uplinkOf(pulse.from);
    if (t >= 1 || !first) { pulses.splice(i, 1); continue; }
    const second = uplinkOf(first);
    const legs = second ? 2 : 1;
    const leg = Math.min(legs - 1, Math.floor(t * legs));
    const lt = t * legs - leg;
    const a = leg === 0 ? pulse.from.pos : first.pos;
    const b = leg === 0 ? first.pos : second.pos;
    ppos.push(a.x + (b.x - a.x) * lt, a.y + (b.y - a.y) * lt,
              a.z + (b.z - a.z) * lt);
  }
  pulsePoints.geometry.setAttribute(
    "position", new THREE.Float32BufferAttribute(ppos, 3));
}

function setLineBuffer(lines, pos, col) {
  lines.geometry.setAttribute("position",
                              new THREE.Float32BufferAttribute(pos, 3));
  lines.geometry.setAttribute("color",
                              new THREE.Float32BufferAttribute(col, 3));
}

/* ------------------------------------------------------------- the loop */

function frame(time) {
  if (!active) return;
  requestAnimationFrame(frame);
  const dt = Math.min(0.1, (time - lastTime) / 1000);
  lastTime = time;

  for (const material of holoMaterials) {
    if (material.uniforms) material.uniforms.uTime.value = time / 1000;
  }

  const ease = 1 - Math.exp(-dt * 6);
  for (const obj of objs.values()) {
    if (obj.kind === "beacon") {
      const ok = showBeacons() && solveBeacon(obj, dt)
                 && levelShown(obj.hearers[0].panel.level);
      obj.group.visible = ok;
      if (obj.trail) obj.trail.visible = ok;
      if (!ok) { obj.fresh = true; continue; }
      if (obj.fresh) { obj.pos.copy(obj.target); obj.fresh = false; }
      obj.pos.lerp(obj.target, 1 - Math.exp(-dt * 1.5));
      if (time - obj.lastTrail > 250) {
        obj.lastTrail = time;
        obj.history.push(obj.pos.clone());
        if (obj.history.length > TRAIL_LEN) obj.history.shift();
        const attr = obj.trail.geometry.attributes.position;
        obj.history.forEach((p, i) => attr.setXYZ(i, p.x, p.y, p.z));
        attr.needsUpdate = true;
        obj.trail.geometry.setDrawRange(0, obj.history.length);
      }
    } else {
      if (!obj.dragging) obj.pos.lerp(obj.target, ease);
      obj.group.visible = (obj.kind !== "broker" || showBroker())
                          && (!obj.placed || levelShown(obj.level));
    }
    obj.group.position.copy(obj.pos);
    obj.flash = Math.max(0, (obj.flash || 0) - dt * 1.5);
    if (obj.spin) obj.spin.rotation.z += dt * (obj.kind === "ap" ? 0.8 : 0.3);
    const lift = obj === hovered ? 1.25 : 1;
    obj.group.scale.setScalar(lift + (obj.flash || 0) * 0.25);
  }

  updateLinks(time);
  controls.update();

  if (host.fxOn()) composer.render();
  else renderer.render(scene, camera);
  labels.render(scene, camera);
}

/* ------------------------------------------------------------ the camera */

/* Where the camera starts: where the last session left it, or high at
 * a corner of the house looking at its middle. */
function frameHouse() {
  const cx = (bounds.min[0] + bounds.max[0]) / 2;
  const cz = (bounds.min[1] + bounds.max[1]) / 2;
  const span = Math.max(4, bounds.max[0] - bounds.min[0],
                        bounds.max[1] - bounds.min[1]);
  controls.target.set(cx, 0.8, cz);
  camera.position.set(cx + span * 0.55, span * 0.85, cz + span * 1.0);
  controls.update();
}

function adoptCamera() {
  if (camAdopted || !houseLoaded) return;
  camAdopted = true;
  const c = settings().cam3d;
  if (c) {
    camera.position.set(c.px, c.py, c.pz);
    controls.target.set(c.tx, c.ty, c.tz);
    controls.update();
  } else {
    frameHouse();
  }
}

let camSaveTimer = null;
function saveCamera() {
  clearTimeout(camSaveTimer);
  camSaveTimer = setTimeout(() => {
    const p = camera.position, t = controls.target;
    const cam3d = { px: p.x, py: p.y, pz: p.z, tx: t.x, ty: t.y, tz: t.z };
    settings().cam3d = cam3d;
    host.api("/api/settings", { cam3d }).catch(() => {});
  }, 800);
}

/* ------------------------------------------------------------ the hands */

const raycaster = new THREE.Raycaster();
const pointer = new THREE.Vector2();
let drag = null;
let hovered = null;

function setPointer(event) {
  const rect = renderer.domElement.getBoundingClientRect();
  pointer.x = ((event.clientX - rect.left) / rect.width) * 2 - 1;
  pointer.y = -((event.clientY - rect.top) / rect.height) * 2 + 1;
  raycaster.setFromCamera(pointer, camera);
}

function objectAt(event) {
  setPointer(event);
  const hits = [...objs.values()]
    .filter((o) => o.group.visible)
    .map((o) => o.hit);
  const hit = raycaster.intersectObjects(hits, false)[0];
  if (!hit) return null;
  let node = hit.object;
  while (node && !node.userData.key) node = node.parent;
  return node ? objs.get(node.userData.key) : null;
}

/* Where a drag puts an object: the pointer moves over the plan of the
 * level in view -- through the walls, which are light, not matter --
 * and near a wall the object sticks to it, on the side the pointer is
 * on, at the height it carries. Away from the walls it stands on the
 * floor. With every level shown, the highest floor under the pointer
 * is the one; the level selector reaches the ones below. */
function placementAt(event, obj) {
  setPointer(event);
  const grounds = visiblePlaceTargets().filter((m) => m.userData.ground);
  const hit = raycaster.intersectObjects(grounds, false)[0];
  if (!hit) return null;
  const level = levelById(hit.object.userData.level);
  const px = hit.point.x, pz = hit.point.z;

  let best = null;
  for (const wall of level.walls) {
    const [ax, az] = wall.a, [bx, bz] = wall.b;
    const lx = bx - ax, lz = bz - az;
    const len2 = lx * lx + lz * lz;
    if (len2 < 1e-6) continue;
    const t = Math.max(0, Math.min(1, ((px - ax) * lx + (pz - az) * lz) / len2));
    const cx = ax + lx * t, cz = az + lz * t;
    const d = Math.hypot(px - cx, pz - cz);
    const reach = wall.thickness / 2 + WALL_SNAP;
    if (d > reach || (best && d >= best.d)) continue;
    const len = Math.sqrt(len2);
    let nx = -lz / len, nz = lx / len;
    if ((px - cx) * nx + (pz - cz) * nz < 0) { nx = -nx; nz = -nz; }
    const top = wall.height + (wall.height_end - wall.height) * t;
    best = { d, cx, cz, nx, nz, half: wall.thickness / 2, top };
  }

  const height = obj.height != null ? obj.height : DEFAULT_HEIGHT[obj.kind];
  if (best) {
    const off = best.half + WALL_OFFSET;
    const y = level.elevation + Math.max(0.05, Math.min(best.top - 0.05,
                                                         height));
    return { point: new THREE.Vector3(best.cx + best.nx * off, y,
                                      best.cz + best.nz * off),
             normal: new THREE.Vector3(best.nx, 0, best.nz),
             level: level.id, surface: "wall" };
  }
  const lift = obj.kind === "panel" ? 0.08 : 0.2;
  return { point: new THREE.Vector3(px, level.elevation + lift, pz),
           normal: new THREE.Vector3(0, 1, 0),
           level: level.id, surface: "floor" };
}

function onPointerDown(event) {
  if (event.button !== 0) return;
  const obj = objectAt(event);
  if (!obj) return;
  drag = { obj, x: event.clientX, y: event.clientY, moved: false,
           id: event.pointerId };
  /* The object is the hand's now, not the camera's: the orbit never
   * hears of this press. */
  controls.enabled = false;
  renderer.domElement.setPointerCapture(event.pointerId);
  event.preventDefault();
  event.stopImmediatePropagation();
}

function onPointerMove(event) {
  if (!drag) {
    const obj = objectAt(event);
    hovered = obj;
    renderer.domElement.style.cursor = obj ? "pointer" : "";
    return;
  }

  const dx = event.clientX - drag.x, dy = event.clientY - drag.y;
  if (!drag.moved && Math.hypot(dx, dy) < 5) return;
  const obj = drag.obj;
  if (obj.kind === "beacon") return;
  drag.moved = true;
  obj.dragging = true;

  /* Shift lifts and lowers: the object keeps its place on the wall and
   * only its height follows the hand. */
  if (event.shiftKey && obj.placed) {
    const level = levelById(obj.level);
    const low = level ? level.elevation + 0.05 : 0.05;
    const high = level ? level.elevation + level.height - 0.05 : 3;
    const step = -(event.clientY - (drag.lastY || drag.y)) * 0.01;
    obj.target.y = Math.max(low, Math.min(high, obj.target.y + step));
    obj.height = obj.target.y - (level ? level.elevation : 0);
    obj.pos.copy(obj.target);
    drag.lastY = event.clientY;
    drag.changed = true;
    return;
  }
  drag.lastY = event.clientY;

  const hit = placementAt(event, obj);
  if (!hit) return;
  obj.target.copy(hit.point);
  obj.pos.copy(obj.target);
  obj.normal.copy(hit.normal);
  if (hit.surface === "floor") {
    const toEye = camera.position.clone().sub(hit.point);
    obj.yaw = Math.atan2(toEye.x, toEye.z);
  }
  obj.level = hit.level;
  obj.placed = true;
  drag.changed = true;
  orient(obj);
}

function onPointerUp(event) {
  if (!drag) return;
  const { obj, moved, changed } = drag;
  drag = null;
  controls.enabled = true;
  obj.dragging = false;
  try { renderer.domElement.releasePointerCapture(event.pointerId); }
  catch (error) { /* never captured */ }

  if (!moved) return openFor(obj);
  if (changed) savePlace(obj);
}

function openFor(obj) {
  if (obj.kind === "panel") host.openDetail(obj.host);
  else if (obj.kind === "ap") host.openApDetail(obj.bssid);
  else if (obj.kind === "beacon") host.openBeaconDetail(obj.addr);
}

function savePlace(obj) {
  const n = obj.normal;
  const pos = { x: obj.target.x, y: obj.target.y, z: obj.target.z,
                nx: n.x, ny: n.y, nz: n.z, yaw: obj.yaw || 0,
                level: obj.level || "" };
  const s = settings();
  s.positions3d = { ...(s.positions3d || {}), [obj.key]: pos };
  host.api("/api/position3d", { key: obj.key, ...pos })
    .then((doc) => { settings().positions3d = doc.positions3d; })
    .catch((error) => host.toast(error.message, "err"));
}

function unplace(key) {
  const s = settings();
  const rest = { ...(s.positions3d || {}) };
  delete rest[key];
  s.positions3d = rest;
  syncObjects();
  return host.api("/api/position3d/delete", { key })
    .then((doc) => { settings().positions3d = doc.positions3d; })
    .catch((error) => host.toast(error.message, "err"));
}

/* Shift and the wheel over a placed object: its height, without
 * picking it up. Everything else the wheel does is the camera's. */
let wheelSave = null;
function onWheel(event) {
  if (!event.shiftKey) return;
  const obj = objectAt(event);
  if (!obj || !obj.placed || obj.kind === "beacon") return;
  event.preventDefault();
  event.stopImmediatePropagation();
  const level = levelById(obj.level);
  const low = level ? level.elevation + 0.05 : 0.05;
  const high = level ? level.elevation + level.height - 0.05 : 3;
  const delta = (event.deltaY || event.deltaX) > 0 ? -0.05 : 0.05;
  obj.target.y = Math.max(low, Math.min(high, obj.target.y + delta));
  obj.height = obj.target.y - (level ? level.elevation : 0);
  clearTimeout(wheelSave);
  wheelSave = setTimeout(() => savePlace(obj), 400);
}

/* ------------------------------------------------------------ the chrome */

function el(tag, cls, text) {
  const node = document.createElement(tag);
  if (cls) node.className = cls;
  if (text != null) node.textContent = text;
  return node;
}

/* The space view's own toolbar: the levels, the brightness of the
 * house, and the house itself -- imported, replaced, removed. */
function buildChrome() {
  ui.bar = el("div", "space-bar");
  ui.levels = el("div", "segment space-levels");
  ui.importBtn = el("label", "lcars-btn small");
  ui.file = el("input");
  ui.file.type = "file";
  ui.file.accept = ".sh3d";
  ui.file.hidden = true;
  ui.file.id = "space-file";
  ui.importBtn.htmlFor = "space-file";
  ui.removeBtn = el("button", "lcars-btn small danger");
  ui.homeBtn = el("button", "lcars-btn small");
  ui.opacity = el("input", "space-opacity");
  ui.opacity.type = "range";
  ui.opacity.min = 5;
  ui.opacity.max = 100;
  ui.name = el("span", "space-name");
  ui.hint = el("span", "space-hint");
  ui.bar.append(ui.levels, ui.name, ui.opacity, ui.homeBtn, ui.importBtn,
                ui.file, ui.removeBtn);

  ui.empty = el("div", "space-empty");
  ui.emptyTitle = el("div", "space-empty-title");
  ui.emptyText = el("p", "hint");
  ui.emptyBtn = el("label", "lcars-btn");
  ui.emptyBtn.htmlFor = "space-file";
  ui.empty.append(ui.emptyTitle, ui.emptyText, ui.emptyBtn);

  ui.help = el("div", "space-help");

  stageEl.append(ui.bar, ui.empty, ui.help);

  ui.file.addEventListener("change", () => {
    const file = ui.file.files[0];
    ui.file.value = "";
    if (file) importHouse(file);
  });
  ui.removeBtn.addEventListener("click", removeHouse);
  ui.homeBtn.addEventListener("click", () => { frameHouse(); saveCamera(); });

  let opacitySave = null;
  ui.opacity.addEventListener("input", () => {
    settings().holo_opacity = Number(ui.opacity.value);
    applyOpacity();
    clearTimeout(opacitySave);
    opacitySave = setTimeout(() => host.api("/api/settings", {
      holo_opacity: Number(ui.opacity.value) }).catch(() => {}), 500);
  });
}

function renderToolbar() {
  if (!ui.bar) return;
  const t = host.t;
  ui.importBtn.textContent = house ? t("space.replace") : t("space.import");
  ui.emptyBtn.textContent = t("space.import");
  ui.removeBtn.textContent = t("space.remove");
  ui.homeBtn.textContent = "⌂";
  ui.homeBtn.title = t("space.home");
  ui.opacity.title = t("space.opacity");
  ui.removeBtn.classList.toggle("hidden", !house);
  if (document.activeElement !== ui.opacity)
    ui.opacity.value = settings().holo_opacity || 50;
  ui.name.textContent = house ? house.name : "";
  ui.empty.classList.toggle("hidden", !!house || !active);
  ui.emptyTitle.textContent = t("space.emptyTitle");
  ui.emptyText.textContent = t("space.emptyText");
  ui.help.textContent = t("space.help");

  ui.levels.replaceChildren();
  ui.levels.classList.toggle("hidden", houseLevels.length < 2);
  const button = (label, id) => {
    const b = el("button", "seg-btn" + (levelFilter === id ? " active" : ""),
                 label);
    b.addEventListener("click", () => {
      levelFilter = id;
      applyLevelFilter();
      renderToolbar();
    });
    ui.levels.append(b);
  };
  button(t("space.allLevels"), null);
  for (const level of houseLevels) button(level.name || level.id, level.id);
}

async function loadHouse() {
  try {
    const doc = await host.api("/api/house");
    house = doc && doc.walls ? doc : null;
  } catch (error) {
    house = null;
  }
  houseLoaded = true;
  buildHouse();
  if (active) adoptCamera();
}

async function importHouse(file) {
  try {
    const response = await fetch("/api/house/import", {
      method: "POST",
      headers: { "Content-Type": "application/octet-stream",
                 "X-Filename": encodeURIComponent(file.name) },
      body: file,
    });
    const doc = await response.json().catch(() => ({}));
    if (!response.ok) throw new Error(doc.error || "HTTP " + response.status);
    house = doc;
    levelFilter = null;
    buildHouse();
    frameHouse();
    saveCamera();
    syncObjects();
    host.toast(host.tf("space.imported", {
      name: doc.name, walls: doc.walls.length, rooms: doc.rooms.length }));
  } catch (error) {
    host.toast(host.t("space.importFailed") + error.message, "err");
  }
}

async function removeHouse() {
  try {
    await host.api("/api/house/delete", {});
    house = null;
    levelFilter = null;
    buildHouse();
    syncObjects();
  } catch (error) {
    host.toast(error.message, "err");
  }
}

/* ------------------------------------------------------------ the setup */

function init() {
  if (ready) return;
  ready = true;

  renderer = new THREE.WebGLRenderer({ antialias: true });
  renderer.setPixelRatio(Math.min(2, window.devicePixelRatio || 1));
  renderer.setClearColor(P.bg, 1);
  renderer.domElement.id = "space-canvas";
  document.getElementById("canvas").after(renderer.domElement);

  labels = new CSS2DRenderer();
  labels.domElement.id = "space-labels";
  renderer.domElement.after(labels.domElement);

  scene = new THREE.Scene();
  // the background as the scene's, not the clear colour: the bloom's
  // render targets take a clear colour unconverted, and the dark of
  // the page would come out grey
  scene.background = new THREE.Color(P.bg);
  scene.fog = new THREE.FogExp2(P.bg, 0.02);
  camera = new THREE.PerspectiveCamera(50, 1, 0.05, 500);

  controls = new OrbitControls(camera, renderer.domElement);
  controls.enableDamping = true;
  controls.dampingFactor = 0.08;
  controls.maxPolarAngle = Math.PI * 0.495;
  controls.addEventListener("end", saveCamera);

  root = new THREE.Group();
  houseGroup = new THREE.Group();
  objGroup = new THREE.Group();
  root.add(houseGroup, objGroup);
  scene.add(root);

  const vertexLines = () => new THREE.LineSegments(
    new THREE.BufferGeometry(),
    new THREE.LineBasicMaterial({ vertexColors: true, transparent: true,
                                  depthWrite: false,
                                  blending: THREE.AdditiveBlending }));
  linkLines = vertexLines();
  beaconLines = vertexLines();
  pulsePoints = new THREE.Points(
    new THREE.BufferGeometry(),
    new THREE.PointsMaterial({ map: glowTexture("#ffd38a"), size: 0.18,
                               transparent: true, depthWrite: false,
                               blending: THREE.AdditiveBlending }));
  for (const node of [linkLines, beaconLines, pulsePoints]) {
    node.frustumCulled = false;
    node.raycast = () => {};
    scene.add(node);
  }

  composer = new EffectComposer(renderer);
  composer.addPass(new RenderPass(scene, camera));
  bloom = new UnrealBloomPass(new THREE.Vector2(256, 256), 0.55, 0.25, 0.4);
  composer.addPass(bloom);
  composer.addPass(new OutputPass());

  const dom = renderer.domElement;
  dom.addEventListener("pointerdown", onPointerDown, { capture: true });
  dom.addEventListener("pointermove", onPointerMove);
  dom.addEventListener("pointerup", onPointerUp);
  dom.addEventListener("pointercancel", onPointerUp);
  dom.addEventListener("wheel", onWheel, { capture: true, passive: false });

  new ResizeObserver(resize).observe(stageEl);
  buildChrome();
  resize();
  loadHouse();
}

function resize() {
  if (!renderer) return;
  const w = stageEl.clientWidth, h = stageEl.clientHeight;
  if (!w || !h) return;
  renderer.setSize(w, h, false);
  labels.setSize(w, h);
  composer.setSize(w, h);
  camera.aspect = w / h;
  camera.updateProjectionMatrix();
}

function setVisible(on) {
  for (const node of [renderer.domElement, labels.domElement, ui.bar,
                      ui.help]) {
    node.classList.toggle("hidden", !on);
  }
  ui.empty.classList.toggle("hidden", !on || !!house);
}

function enter() {
  init();
  if (active) return;
  active = true;
  setVisible(true);
  resize();
  adoptCamera();
  syncObjects();
  renderToolbar();
  lastTime = performance.now();
  requestAnimationFrame(frame);
}

function leave() {
  if (!active) return;
  active = false;
  setVisible(false);
  hovered = null;
}

/* Called by app.js after every poll and every switch of the picture:
 * this view follows the setting on its own. */
function update() {
  const want = settings().view_mode === "space";
  if (want) enter();
  else leave();
  if (active) {
    syncObjects();
    renderToolbar();
    applyOpacity();
  }
}

/* Debug hook, as in app.js: with #debug in the URL, every object's place
 * and where it lands on the screen are written into the page, where a
 * headless browser can read them and aim its pointer. */
if (location.hash === "#debug") {
  const out = document.createElement("div");
  out.id = "debug-space";
  out.hidden = true;
  document.body.append(out);
  window.__space = { bloom, composer, renderer, camera, controls };
  window.__spaceProject = (x, y, z) => {
    const rect = renderer.domElement.getBoundingClientRect();
    const v = new THREE.Vector3(x, y, z).project(camera);
    return [Math.round(rect.left + (v.x + 1) / 2 * rect.width),
            Math.round(rect.top + (1 - v.y) / 2 * rect.height)];
  };
  setInterval(() => {
    if (!active) return;
    const rect = renderer.domElement.getBoundingClientRect();
    out.textContent = JSON.stringify([...objs.values()].map((o) => {
      const v = o.pos.clone().project(camera);
      return { key: o.key, placed: o.placed, visible: o.group.visible,
               level: o.level,
               pos: [o.pos.x, o.pos.y, o.pos.z].map((n) => +n.toFixed(2)),
               sx: Math.round(rect.left + (v.x + 1) / 2 * rect.width),
               sy: Math.round(rect.top + (1 - v.y) / 2 * rect.height) };
    }));
  }, 300);
}

document.addEventListener("langchange", () => { if (ready) renderToolbar(); });

window.MqttvizSpace = {
  update,
  isPlaced: (key) => !!placed()[key],
  unplace,
  relabel: renderToolbar,
};

update();
