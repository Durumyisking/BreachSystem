import test from 'node:test';
import assert from 'node:assert/strict';
import { generateDeco, presets, wall } from './geometry.mjs';
import { fitDistance } from './camera.mjs';

test('same parameters and seed reproduce every vertex and triangle', () => {
  assert.deepEqual(generateDeco(presets.concrete), generateDeco(presets.concrete));
  assert.notDeepEqual(generateDeco(presets.concrete), generateDeco({ ...presets.concrete, seed: 43 }));
});

test('material selection does not participate in geometry generation', () => {
  assert.deepEqual(generateDeco({ ...presets.concrete, material: 'concrete' }), generateDeco({ ...presets.concrete, material: 'wood' }));
});

test('placement contains tight groups and large gaps instead of a regular comb', () => {
  const pieces = generateDeco(presets.wood);
  const gaps = [];
  for (let edge = 0; edge < wall.hole.length; ++edge) {
    const onEdge = pieces.filter(piece => piece.edge === edge);
    for (let i = 1; i < onEdge.length; ++i) {
      gaps.push(Math.hypot(onEdge[i].anchor[0] - onEdge[i - 1].anchor[0], onEdge[i].anchor[1] - onEdge[i - 1].anchor[1]));
    }
  }
  assert.ok(Math.min(...gaps) < presets.wood.spacing * 0.6, 'at least one tight group');
  assert.ok(Math.max(...gaps) > presets.wood.spacing * 1.8, 'at least one broad empty gap');
});

test('every generated plate is a closed, nondegenerate solid attached to the boundary', () => {
  const pieces = generateDeco(presets.concrete);
  assert.ok(pieces.length > 12);
  for (const { positions, indices, profile, embed, anchor, edge } of pieces) {
    assert.ok(positions.every(Number.isFinite));
    assert.ok(embed > 0);
    assert.ok(profile.every(p => p[1] > 0));
    const [a, b] = [wall.hole[edge], wall.hole[(edge + 1) % wall.hole.length]];
    const cross = (anchor[0] - a[0]) * (b[1] - a[1]) - (anchor[1] - a[1]) * (b[0] - a[0]);
    assert.ok(Math.abs(cross) < 1e-8);
    const edges = new Map();
    for (let i = 0; i < indices.length; i += 3) {
      const ids = indices.slice(i, i + 3);
      const p = ids.map(id => positions.slice(id * 3, id * 3 + 3));
      const ab = p[1].map((v, j) => v - p[0][j]);
      const ac = p[2].map((v, j) => v - p[0][j]);
      const area2 = Math.hypot(ab[1] * ac[2] - ab[2] * ac[1], ab[2] * ac[0] - ab[0] * ac[2], ab[0] * ac[1] - ab[1] * ac[0]);
      assert.ok(area2 > 1e-8);
      for (let j = 0; j < 3; ++j) {
        const k = [ids[j], ids[(j + 1) % 3]].sort((a, b) => a - b).join(':');
        edges.set(k, (edges.get(k) ?? 0) + 1);
      }
    }
    assert.ok([...edges.values()].every(count => count === 2));
  }
});

test('wood preset makes narrower and longer plates using the same generator', () => {
  const concrete = generateDeco(presets.concrete);
  const wood = generateDeco(presets.wood);
  const ratio = pieces => pieces.reduce((sum, p) => sum + p.length / p.width, 0) / pieces.length;
  assert.ok(ratio(wood) > ratio(concrete) * 4);
  assert.ok(wood.length > concrete.length);
});

test('thickness controls actual vertex separation instead of surface shading', () => {
  const a = generateDeco({ ...presets.concrete, thickness: 0.3 });
  const b = generateDeco({ ...presets.concrete, thickness: 3 });
  const rootDepth = p => Math.abs(p.positions[2] - p.positions[p.profile.length * 2 * 3 + 2]);
  assert.ok(Math.abs(rootDepth(a[0]) - 0.3) < 1e-8);
  assert.ok(Math.abs(rootDepth(b[0]) - 3) < 1e-8);
});

test('invalid dimensions and seed are rejected', () => {
  for (const patch of [{ width: 0 }, { spacing: 0 }, { thickness: -1 }, { length: NaN }, { seed: Infinity }]) {
    assert.throws(() => generateDeco({ ...presets.concrete, ...patch }), RangeError);
  }
});

test('camera fits the wall in both narrow and wide viewports', () => {
  for (const [width, height] of [[294, 430], [483, 640], [1000, 660]]) {
    const distance = fitDistance(width, height);
    const visibleHeight = 2 * distance * Math.tan(35 * Math.PI / 360);
    assert.ok(visibleHeight > wall.height);
    assert.ok(visibleHeight * width / height > wall.width);
  }
});

test('oblique and side views keep even the nearest wall corners in frame', () => {
  for (const [width, height] of [[294, 430], [1000, 660]]) {
    for (const azimuth of [0.38, Math.PI * 0.42, Math.PI]) {
      const elevation = 0.13;
      const distance = fitDistance(width, height, 35, azimuth, elevation);
      for (const x of [-135, 135]) {
        for (const y of [-105, 105]) {
          for (const z of [-6, 6]) {
            const depth = distance - (x * Math.sin(azimuth) * Math.cos(elevation) + y * Math.sin(elevation) + z * Math.cos(azimuth) * Math.cos(elevation));
            const horizontal = x * Math.cos(azimuth) - z * Math.sin(azimuth);
            const vertical = -x * Math.sin(azimuth) * Math.sin(elevation) + y * Math.cos(elevation) - z * Math.cos(azimuth) * Math.sin(elevation);
            assert.ok(Math.abs(horizontal / depth) < Math.tan(35 * Math.PI / 360) * width / height);
            assert.ok(Math.abs(vertical / depth) < Math.tan(35 * Math.PI / 360));
          }
        }
      }
    }
  }
});
