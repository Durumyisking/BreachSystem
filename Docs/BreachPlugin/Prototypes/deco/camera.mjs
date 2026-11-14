import { wall } from './geometry.mjs';

export function fitDistance(width, height, fov = 35, azimuth = 0, elevation = 0) {
  const halfFov = fov * Math.PI / 360;
  const aspect = width / height;
  const direction = [Math.sin(azimuth) * Math.cos(elevation), Math.sin(elevation), Math.cos(azimuth) * Math.cos(elevation)];
  const right = [Math.cos(azimuth), 0, -Math.sin(azimuth)];
  const up = [-Math.sin(azimuth) * Math.sin(elevation), Math.cos(elevation), -Math.cos(azimuth) * Math.sin(elevation)];
  const dot = (a, b) => a.reduce((sum, v, i) => sum + v * b[i], 0);
  let distance = 0;
  for (const x of [-wall.width / 2, wall.width / 2]) {
    for (const y of [-wall.height / 2, wall.height / 2]) {
      for (const z of [-wall.depth / 2, wall.depth / 2]) {
        const corner = [x, y, z];
        const projected = Math.max(Math.abs(dot(corner, right)) / (Math.tan(halfFov) * aspect), Math.abs(dot(corner, up)) / Math.tan(halfFov));
        distance = Math.max(distance, dot(corner, direction) + projected);
      }
    }
  }
  return distance * 1.12;
}
