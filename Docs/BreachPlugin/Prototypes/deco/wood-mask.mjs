import { randomStream, maskGrid } from './geometry.mjs';

// White keeps the surface, black discards it. Every tile retains a solid root.
export function generateWoodMask(size = 512, seed = 9051) {
  if (!Number.isInteger(size) || size < maskGrid * 8 || size % maskGrid !== 0) {
    throw new RangeError('Mask size must be a multiple of the atlas grid.');
  }
  const data = new Uint8ClampedArray(size * size * 4);
  const random = randomStream(seed);
  const tileSize = size / maskGrid;
  for (let tileY = 0; tileY < maskGrid; ++tileY) {
    for (let tileX = 0; tileX < maskGrid; ++tileX) {
      const fibers = Array.from({ length: 13 }, (_, i) => ({
        x: (i + random()) / 13,
        drift: (random() - 0.5) * 0.2,
        end: 0.35 + random() * 0.65,
        halfWidth: 0.014 + random() * 0.035,
        phase: random() * Math.PI * 2,
      }));
      const slots = Array.from({ length: 5 }, () => ({
        x: 0.08 + random() * 0.84,
        start: 0.18 + random() * 0.3,
        length: 0.15 + random() * 0.35,
        halfWidth: 0.01 + random() * 0.025,
        drift: (random() - 0.5) * 0.12,
      }));
      for (let y = 0; y < tileSize; ++y) {
        const v = y / (tileSize - 1);
        for (let x = 0; x < tileSize; ++x) {
          const u = x / (tileSize - 1);
          let solid = v < 0.14 + 0.045 * Math.sin(u * 19 + tileX + tileY);
          if (!solid) {
            solid = fibers.some(fiber => {
              if (v > fiber.end) {
                return false;
              }
              const center = fiber.x + fiber.drift * v + Math.sin(v * 24 + fiber.phase) * 0.01;
              const halfWidth = fiber.halfWidth * (1 - 0.68 * v / fiber.end) * (0.85 + 0.15 * Math.sin(v * 90 + fiber.phase));
              return Math.abs(u - center) < halfWidth;
            });
          }
          if (solid && v > 0.15) {
            for (const slot of slots) {
              const t = (v - slot.start) / slot.length;
              if (t > 0 && t < 1) {
                const center = slot.x + t * slot.drift;
                const halfWidth = slot.halfWidth * Math.sin(Math.PI * t);
                if (Math.abs(u - center) < halfWidth) {
                  solid = false;
                  break;
                }
              }
            }
          }
          const index = ((tileY * tileSize + y) * size + tileX * tileSize + x) * 4;
          const value = solid ? 255 : 0;
          data[index] = data[index + 1] = data[index + 2] = value;
          data[index + 3] = 255;
        }
      }
    }
  }
  return { width: size, height: size, data };
}
