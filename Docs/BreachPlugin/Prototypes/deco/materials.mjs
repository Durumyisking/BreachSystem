import * as THREE from './vendor/three.module.min.js';
import { randomStream } from './geometry.mjs';
import { generateWoodMask } from './wood-mask.mjs';

function texture(kind, height = false) {
  const canvas = document.createElement('canvas');
  canvas.width = canvas.height = 512;
  const context = canvas.getContext('2d');
  const image = context.createImageData(512, 512);
  const random = randomStream(2026);
  for (let y = 0; y < 512; ++y) {
    for (let x = 0; x < 512; ++x) {
      const noise = random();
      const i = (y * 512 + x) * 4;
      let channels;
      if (kind === 'wood') {
        const warp = Math.sin(y * 0.028 + Math.sin(y * 0.007) * 1.8) * 3.5;
        const grain = Math.sin((x + warp) * 1.1) * 0.25 + Math.sin((x + warp) * 0.26) * 0.25;
        const fine = noise * 0.25;
        const shade = 0.7 + grain * 0.5 + fine;
        channels = height ? [140 + grain * 80 + noise * 25] : [119 * shade, 88 * shade, 53 * shade];
      } else {
        const mottling = Math.sin(x * 0.055 + Math.cos(y * 0.04)) * 4 + Math.sin(y * 0.033) * 3;
        const pore = noise < 0.025 ? -32 : 0;
        const grain = (noise - 0.5) * 24 + mottling + pore;
        channels = height ? [140 + grain * 2] : [171 + grain, 168 + grain, 155 + grain];
      }
      for (let c = 0; c < 3; ++c) {
        image.data[i + c] = channels[c] ?? channels[0];
      }
      image.data[i + 3] = 255;
    }
  }
  context.putImageData(image, 0, 0);
  const result = new THREE.CanvasTexture(canvas);
  result.flipY = false;
  result.wrapS = result.wrapT = THREE.RepeatWrapping;
  result.anisotropy = 8;
  if (!height) {
    result.colorSpace = THREE.SRGBColorSpace;
  }
  return result;
}

export function createMaterials() {
  const mask = generateWoodMask();
  const canvas = document.createElement('canvas');
  canvas.width = mask.width;
  canvas.height = mask.height;
  canvas.getContext('2d').putImageData(new ImageData(mask.data, mask.width, mask.height), 0, 0);
  const alphaMap = new THREE.CanvasTexture(canvas);
  alphaMap.flipY = false;
  alphaMap.anisotropy = 8;
  const materials = Object.fromEntries(['concrete', 'wood'].map(kind => [kind, new THREE.MeshStandardMaterial({
    color: kind === 'wood' ? '#ffffff' : '#bdb9b0',
    map: texture(kind), bumpMap: texture(kind, true), bumpScale: kind === 'wood' ? 0.08 : 0.12,
    alphaMap: kind === 'wood' ? alphaMap : null,
    alphaTest: kind === 'wood' ? 0.5 : 0,
    alphaToCoverage: kind === 'wood',
    roughness: kind === 'wood' ? 0.95 : 1, metalness: 0, flatShading: true,
  })]));
  return materials;
}
