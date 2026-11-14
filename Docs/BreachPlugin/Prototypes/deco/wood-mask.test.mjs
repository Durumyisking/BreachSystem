import test from 'node:test';
import assert from 'node:assert/strict';
import { generateWoodMask } from './wood-mask.mjs';
import { maskGrid, generateDeco, presets } from './geometry.mjs';

test('wood mask has actual empty areas and retains opaque attachment roots', () => {
  const mask = generateWoodMask(256);
  const tileSize = mask.width / maskGrid;
  let empty = 0;
  let solid = 0;
  for (let tileY = 0; tileY < maskGrid; ++tileY) {
    for (let tileX = 0; tileX < maskGrid; ++tileX) {
      for (let x = 0; x < tileSize; ++x) {
        const index = (tileY * tileSize * mask.width + tileX * tileSize + x) * 4;
        assert.equal(mask.data[index + 1], 255, 'root remains opaque');
      }
    }
  }
  for (let i = 1; i < mask.data.length; i += 4) {
    if (mask.data[i] === 0) {
      empty++;
    } else {
      solid++;
    }
  }
  assert.ok(empty > solid * 0.5, 'enough gaps to visibly perforate the material');
  assert.ok(solid > empty * 0.15, 'wood fibers remain visible');
});

test('mask atlas is deterministic and contains varied fiber patterns', () => {
  const mask = generateWoodMask(128, 17);
  assert.deepEqual(mask, generateWoodMask(128, 17));
  assert.notDeepEqual(mask, generateWoodMask(128, 18));
  const tile = mask.width / maskGrid;
  const left = [];
  const right = [];
  for (let y = 0; y < tile; ++y) {
    for (let x = 0; x < tile; ++x) {
      left.push(mask.data[(y * mask.width + x) * 4]);
      right.push(mask.data[(y * mask.width + x + tile) * 4]);
    }
  }
  assert.notDeepEqual(left, right);
});

test('generated UVs keep roots in opaque atlas bands on both sides', () => {
  const mask = generateWoodMask();
  for (const piece of generateDeco(presets.concrete)) {
    const n = piece.profile.length;
    for (const row of [0, 2]) {
      for (let i = 0; i < n; ++i) {
        const index = (row * n + i) * 2;
        const x = Math.floor(piece.uvs[index] * mask.width);
        const y = Math.floor(piece.uvs[index + 1] * mask.height);
        assert.equal(mask.data[(y * mask.width + x) * 4 + 1], 255);
      }
    }
  }
});
