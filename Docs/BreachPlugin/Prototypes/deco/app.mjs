import * as THREE from './vendor/three.module.min.js';
import { generateDeco, geometryFingerprint, presets, wall } from './geometry.mjs';
import { createMaterials } from './materials.mjs';
import { fitDistance } from './camera.mjs';

const viewport = document.getElementById('viewport');
const error = document.getElementById('error');
const modelInfo = document.getElementById('model-info');
const sliders = ['width', 'length', 'thickness', 'spacing', 'roughness'];
let parameters = { ...presets.concrete };
let activeMaterial = 'wood';
let pieces = [];
let renderer;
let scene;
let camera;
let decoGroup;
let materials;
let geometryRevision = 0;
let fittedDistance = 430;
const orbit = { azimuth: 0.38, elevation: 0.13, distance: 430 };
const drag = { active: false, x: 0, y: 0, pointers: new Map(), pinch: 0 };

function updateCamera() {
  const fitted = fitDistance(viewport.clientWidth, viewport.clientHeight, camera.fov, orbit.azimuth, orbit.elevation);
  orbit.distance *= fitted / fittedDistance;
  fittedDistance = fitted;
  const cos = Math.cos(orbit.elevation);
  camera.position.set(Math.sin(orbit.azimuth) * cos * orbit.distance,
    Math.sin(orbit.elevation) * orbit.distance, Math.cos(orbit.azimuth) * cos * orbit.distance);
  camera.lookAt(0, 0, 0);
  render();
}

function render() {
  if (renderer) {
    renderer.render(scene, camera);
  }
}

function resize() {
  const { width, height } = viewport.getBoundingClientRect();
  renderer.setSize(width, height);
  camera.aspect = width / height;
  camera.updateProjectionMatrix();
  updateCamera();
}

function makeWall() {
  const outer = new THREE.Shape();
  outer.moveTo(-wall.width / 2, -wall.height / 2);
  outer.lineTo(wall.width / 2, -wall.height / 2);
  outer.lineTo(wall.width / 2, wall.height / 2);
  outer.lineTo(-wall.width / 2, wall.height / 2);
  outer.closePath();
  const hole = new THREE.Path();
  [...wall.hole].reverse().forEach(([x, y], i) => i === 0 ? hole.moveTo(x, y) : hole.lineTo(x, y));
  hole.closePath();
  outer.holes.push(hole);
  const geometry = new THREE.ExtrudeGeometry(outer, { depth: wall.depth, bevelEnabled: false, steps: 1 });
  geometry.translate(0, 0, -wall.depth / 2);
  const mesh = new THREE.Mesh(geometry, new THREE.MeshStandardMaterial({ color: '#cbd0bd', roughness: 1 }));
  mesh.castShadow = true;
  mesh.receiveShadow = true;
  scene.add(mesh);
}

function updateStatus(message) {
  modelInfo.textContent = `${pieces.length}개 조각 · 시드 ${parameters.seed}`;
  modelInfo.dataset.geometryHash = geometryFingerprint(pieces);
  modelInfo.dataset.geometryRevision = geometryRevision;
  modelInfo.dataset.maskEnabled = activeMaterial === 'wood' && materials.wood.alphaTest > 0;
  modelInfo.title = message;
}

function regenerate() {
  try {
    parameters.seed = readSeed();
    const generated = generateDeco(parameters);
    while (decoGroup.children.length) {
      const mesh = decoGroup.children[0];
      decoGroup.remove(mesh);
      mesh.geometry.dispose();
    }
    pieces = generated;
    for (const piece of pieces) {
      const geometry = new THREE.BufferGeometry();
      geometry.setAttribute('position', new THREE.Float32BufferAttribute(piece.positions, 3));
      geometry.setAttribute('uv', new THREE.Float32BufferAttribute(piece.uvs, 2));
      geometry.setIndex(piece.indices);
      const flat = geometry.toNonIndexed();
      flat.computeVertexNormals();
      geometry.dispose();
      const mesh = new THREE.Mesh(flat, materials[activeMaterial]);
      mesh.castShadow = true;
      mesh.receiveShadow = true;
      decoGroup.add(mesh);
    }
    geometryRevision += 1;
    error.hidden = true;
    updateStatus('형상을 생성했습니다.');
    render();
  } catch (reason) {
    error.textContent = `형상을 생성하지 못했어요. ${reason.message}`;
    error.hidden = false;
  }
}

function readSeed() {
  const input = document.getElementById('seed');
  if (input.value.trim() === '' || !input.checkValidity()) {
    throw new RangeError('시드는 0~4294967295 사이의 정수를 입력해 주세요.');
  }
  return Number(input.value);
}

function syncControls() {
  for (const name of sliders) {
    document.getElementById(name).value = parameters[name];
    document.getElementById(`${name}-value`).value = name === 'roughness'
      ? `${Math.round(parameters[name] * 100)}%` : `${Number(parameters[name].toFixed(1))} cm`;
  }
}

function setMaterial(kind) {
  activeMaterial = kind;
  materials[kind].wireframe = document.getElementById('wireframe').checked;
  decoGroup.children.forEach(mesh => { mesh.material = materials[kind]; });
  for (const candidate of ['concrete', 'wood']) {
    const button = document.getElementById(`material-${candidate}`);
    button.classList.toggle('active', candidate === kind);
    button.setAttribute('aria-pressed', candidate === kind);
  }
  document.getElementById('material-label').textContent = kind.toUpperCase();
  document.getElementById('mask-control').hidden = kind !== 'wood';
  updateMask();
  updateStatus('색과 마스크만 변경했습니다. 조각의 정점과 위치는 그대로입니다.');
  render();
}

function updateMask() {
  const enabled = document.getElementById('wood-mask').checked && !document.getElementById('wireframe').checked;
  materials.wood.alphaTest = enabled ? 0.5 : 0;
  materials.wood.alphaToCoverage = enabled;
  materials.wood.needsUpdate = true;
}

function preset(kind) {
  // Shape presets deliberately do not change the selected material or seed.
  parameters = { ...presets[kind], seed: parameters.seed };
  syncControls();
  regenerate();
}

function bindControls() {
  for (const name of sliders) {
    document.getElementById(name).addEventListener('input', event => {
      parameters[name] = Number(event.target.value);
      syncControls();
      regenerate();
    });
  }
  for (const kind of ['concrete', 'wood']) {
    document.getElementById(`material-${kind}`).addEventListener('click', () => setMaterial(kind));
    document.getElementById(`shape-${kind}`).addEventListener('click', () => preset(kind));
  }
  document.getElementById('regenerate').addEventListener('click', regenerate);
  document.getElementById('seed').addEventListener('change', regenerate);
  document.getElementById('wireframe').addEventListener('change', event => {
    Object.values(materials).forEach(material => { material.wireframe = event.target.checked; });
    updateMask();
    updateStatus('와이어프레임에서는 마스크 적용 전의 실제 기하를 표시합니다.');
    render();
  });
  document.getElementById('wood-mask').addEventListener('change', () => {
    updateMask();
    updateStatus('섬유 마스크만 변경했습니다. 정점과 배치는 그대로입니다.');
    render();
  });
  document.getElementById('reset-camera').addEventListener('click', () => {
    Object.assign(orbit, { azimuth: 0.38, elevation: 0.13, distance: fittedDistance });
    updateCamera();
  });
  for (const [name, azimuth] of [['front', 0], ['side', Math.PI * 0.42], ['back', Math.PI]]) {
    document.getElementById(`view-${name}`).addEventListener('click', () => {
      Object.assign(orbit, { azimuth, elevation: name === 'side' ? 0.12 : 0, distance: fittedDistance });
      updateCamera();
    });
  }
  viewport.addEventListener('pointerdown', event => {
    viewport.setPointerCapture(event.pointerId);
    drag.pointers.set(event.pointerId, [event.clientX, event.clientY]);
    drag.active = true;
    drag.x = event.clientX;
    drag.y = event.clientY;
    drag.pinch = 0;
  });
  viewport.addEventListener('pointermove', event => {
    if (!drag.active || !drag.pointers.has(event.pointerId)) {
      return;
    }
    drag.pointers.set(event.pointerId, [event.clientX, event.clientY]);
    if (drag.pointers.size === 2) {
      const [a, b] = [...drag.pointers.values()];
      const distance = Math.hypot(a[0] - b[0], a[1] - b[1]);
      if (drag.pinch > 0) {
        orbit.distance = THREE.MathUtils.clamp(orbit.distance * drag.pinch / Math.max(distance, 1), fittedDistance * 0.35, fittedDistance * 2.5);
      }
      drag.pinch = distance;
    } else {
      orbit.azimuth -= (event.clientX - drag.x) * 0.006;
      orbit.elevation = THREE.MathUtils.clamp(orbit.elevation + (event.clientY - drag.y) * 0.005, -1.3, 1.3);
    }
    drag.x = event.clientX;
    drag.y = event.clientY;
    updateCamera();
  });
  const release = event => {
    drag.pointers.delete(event.pointerId);
    drag.active = drag.pointers.size > 0;
    drag.pinch = 0;
    const remaining = [...drag.pointers.values()][0];
    if (remaining) {
      [drag.x, drag.y] = remaining;
    }
  };
  viewport.addEventListener('pointerup', release);
  viewport.addEventListener('pointercancel', release);
  viewport.addEventListener('wheel', event => {
    event.preventDefault();
    orbit.distance = THREE.MathUtils.clamp(orbit.distance * Math.exp(event.deltaY * 0.001), fittedDistance * 0.35, fittedDistance * 2.5);
    updateCamera();
  }, { passive: false });
  viewport.addEventListener('keydown', event => {
    const action = {
      ArrowLeft: () => { orbit.azimuth -= 0.1; }, ArrowRight: () => { orbit.azimuth += 0.1; },
      ArrowUp: () => { orbit.elevation = Math.min(1.3, orbit.elevation + 0.1); },
      ArrowDown: () => { orbit.elevation = Math.max(-1.3, orbit.elevation - 0.1); },
      '+': () => { orbit.distance = Math.max(fittedDistance * 0.35, orbit.distance * 0.9); },
      '=': () => { orbit.distance = Math.max(fittedDistance * 0.35, orbit.distance * 0.9); },
      '-': () => { orbit.distance = Math.min(fittedDistance * 2.5, orbit.distance * 1.1); },
    }[event.key];
    if (action) {
      event.preventDefault();
      action();
      updateCamera();
    }
  });
}

try {
  renderer = new THREE.WebGLRenderer({ antialias: true, alpha: false });
  renderer.setPixelRatio(Math.min(devicePixelRatio, 2));
  renderer.outputColorSpace = THREE.SRGBColorSpace;
  renderer.toneMapping = THREE.ACESFilmicToneMapping;
  renderer.toneMappingExposure = 1;
  renderer.shadowMap.enabled = true;
  renderer.shadowMap.type = THREE.PCFSoftShadowMap;
  viewport.append(renderer.domElement);
  scene = new THREE.Scene();
  scene.background = new THREE.Color('#e7eae4');
  camera = new THREE.PerspectiveCamera(35, 1, 0.1, 2000);
  scene.add(new THREE.HemisphereLight('#fff9ec', '#8b9986', 1.2));
  const light = new THREE.DirectionalLight('#fff6df', 2.3);
  light.position.set(-150, 230, 250);
  light.castShadow = true;
  light.shadow.mapSize.set(2048, 2048);
  Object.assign(light.shadow.camera, { left: -220, right: 220, top: 220, bottom: -220, near: 1, far: 800 });
  light.shadow.normalBias = 0.12;
  scene.add(light);
  const fill = new THREE.DirectionalLight('#dce8ff', 0.65);
  fill.position.set(150, 70, -220);
  scene.add(fill);
  const floor = new THREE.Mesh(new THREE.PlaneGeometry(2000, 2000), new THREE.MeshStandardMaterial({ color: '#dce1d4', roughness: 1 }));
  floor.rotation.x = -Math.PI / 2;
  floor.position.y = -wall.height / 2 - 0.1;
  floor.receiveShadow = true;
  scene.add(floor);
  makeWall();
  materials = createMaterials();
  decoGroup = new THREE.Group();
  scene.add(decoGroup);
  syncControls();
  bindControls();
  regenerate();
  new ResizeObserver(resize).observe(viewport);
  updateCamera();
  resize();
} catch (reason) {
  error.textContent = `3D 미리보기를 시작하지 못했어요. WebGL을 지원하는 브라우저에서 열어 주세요. (${reason.message})`;
  error.hidden = false;
}
