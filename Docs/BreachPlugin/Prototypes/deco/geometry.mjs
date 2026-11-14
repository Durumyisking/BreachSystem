// Coordinates and dimensions are in centimetres; the wall lies in XY.
export const wall = {
  width: 270, height: 210, depth: 12,
  hole: [[-55, -64], [51, -64], [66, -49], [66, 44], [49, 64], [-48, 64], [-66, 43], [-66, -47]],
};

export const presets = {
  concrete: { width: 22, length: 10, thickness: 1.4, spacing: 19, roughness: 0.7, seed: 42 },
  wood: { width: 3, length: 16, thickness: 0.4, spacing: 4.2, roughness: 0.8, seed: 42 },
};

export const maskGrid = 4;

export function randomStream(seed) {
  let state = seed >>> 0;
  return () => {
    state += 0x6D2B79F5;
    let t = Math.imul(state ^ state >>> 15, 1 | state);
    t ^= t + Math.imul(t ^ t >>> 7, 61 | t);
    return ((t ^ t >>> 14) >>> 0) / 4294967296;
  };
}

function plate(anchor, tangent, inward, width, length, options, random, edge) {
  const n = 7;
  const embed = Math.min(1, width * 0.18);
  const profile = [];
  const thicknesses = [];
  const bend = (random() - 0.5) * options.thickness * 2;
  const twist = (random() - 0.5) * options.thickness * 1.5;
  const tile = Math.floor(random() * maskGrid * maskGrid);
  for (let i = 0; i < n; ++i) {
    const t = i / (n - 1);
    const u = (t - 0.5) * width;
    const shoulder = 0.3 + 0.7 * Math.pow(Math.sin(Math.PI * t), 0.45);
    const variation = 1 - options.roughness * (0.15 + 0.7 * random());
    profile.push([u, length * shoulder * variation]);
    thicknesses.push(options.thickness * (0.18 + random() * 0.3));
  }
  const positions = [];
  const uvs = [];
  const indices = [];
  const front = wall.depth / 2 + 0.04 + random() * 0.1;
  // Four vertex rows: front roots, front tips, back roots, back tips.
  for (let row = 0; row < 4; ++row) {
    for (let i = 0; i < n; ++i) {
      const [u, v] = profile[i];
      const tip = row % 2 === 1;
      const back = row >= 2;
      const distance = tip ? v : -embed;
      const raised = tip ? bend * v / length + twist * u / width : 0;
      const thickness = tip ? thicknesses[i] : options.thickness;
      positions.push(anchor[0] + tangent[0] * u + inward[0] * distance,
        anchor[1] + tangent[1] * u + inward[1] * distance,
        front + raised - (back ? thickness : 0));
      // Each shard samples one atlas tile, with its opaque root at V=0.
      uvs.push((tile % maskGrid + 0.01 + (u / width + 0.5) * 0.98) / maskGrid,
        (Math.floor(tile / maskGrid) + 0.01 + (distance + embed) / (length + embed) * 0.98) / maskGrid);
    }
  }
  const quad = (a, b, c, d) => indices.push(a, b, c, a, c, d);
  for (let i = 0; i < n - 1; ++i) {
    quad(i, i + 1, n + i + 1, n + i);
    quad(2 * n + i, 3 * n + i, 3 * n + i + 1, 2 * n + i + 1);
    quad(i, 2 * n + i, 2 * n + i + 1, i + 1);
    quad(n + i, n + i + 1, 3 * n + i + 1, 3 * n + i);
  }
  quad(0, n, 3 * n, 2 * n);
  quad(n - 1, 3 * n - 1, 4 * n - 1, 2 * n - 1);
  return { positions, indices, uvs, profile, embed, anchor, edge, width, length };
}

export function generateDeco(options) {
  for (const key of ['width', 'length', 'thickness', 'spacing']) {
    if (!Number.isFinite(options[key]) || options[key] <= 0) {
      throw new RangeError(`${key} must be a positive finite number.`);
    }
  }
  if (!Number.isFinite(options.seed) || !Number.isFinite(options.roughness) || options.roughness < 0 || options.roughness > 1) {
    throw new RangeError('Seed must be finite and roughness must be between zero and one.');
  }
  const random = randomStream(options.seed);
  const pieces = [];
  wall.hole.forEach((a, edge) => {
    const b = wall.hole[(edge + 1) % wall.hole.length];
    const length = Math.hypot(b[0] - a[0], b[1] - a[1]);
    const tangent = [(b[0] - a[0]) / length, (b[1] - a[1]) / length];
    const inward = [-tangent[1], tangent[0]];
    let distance = options.spacing * (0.15 + random() * 1.2);
    let groupRemaining = 2 + Math.floor(random() * 4);
    let groupSize = 0.5 + random();
    while (distance < length - 0.5) {
      const width = Math.min(options.width * groupSize * (0.45 + random()), 2 * distance - 0.4, 2 * (length - distance) - 0.4);
      const protrusion = options.length * (0.3 + random() * 1.15);
      if (width > 0.3) {
        const anchor = [a[0] + tangent[0] * distance, a[1] + tangent[1] * distance];
        pieces.push(plate(anchor, tangent, inward, width, protrusion, options, random, edge));
      }
      if (--groupRemaining <= 0) {
        distance += options.spacing * (1.6 + random() * 2.2);
        groupRemaining = 2 + Math.floor(random() * 5);
        groupSize = 0.5 + random();
      } else {
        distance += options.spacing * (0.2 + random() * 0.55);
      }
    }
  });
  return pieces;
}

export function geometryFingerprint(pieces) {
  let hash = 2166136261;
  for (const piece of pieces) {
    for (const value of piece.positions) {
      hash = Math.imul(hash ^ Math.round(value * 100000), 16777619);
    }
  }
  return (hash >>> 0).toString(16).padStart(8, '0');
}
