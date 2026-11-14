// Initial procedural samples. Approved full-field wood artwork is maintained separately.
const fs = require('node:fs');
const path = require('node:path');
const zlib = require('node:zlib');
const size = 512;
const output = path.resolve(__dirname, '../SourceArt/Decoration');
fs.mkdirSync(output, { recursive: true });
const clamp = x => Math.max(0, Math.min(1, x));
const hash = (x, y, seed = 0) => {
  let n = Math.imul(x + seed * 71, 374761393) ^ Math.imul(y + seed * 13, 668265263);
  n = Math.imul(n ^ (n >>> 13), 1274126177);
  return ((n ^ (n >>> 16)) >>> 0) / 4294967296;
};
function noise(x, y, seed = 0) {
  const ix = Math.floor(x), iy = Math.floor(y);
  const fx = x - ix, fy = y - iy;
  const a = fx * fx * (3 - 2 * fx), b = fy * fy * (3 - 2 * fy);
  return (hash(ix, iy, seed) * (1 - a) + hash(ix + 1, iy, seed) * a) * (1 - b)
    + (hash(ix, iy + 1, seed) * (1 - a) + hash(ix + 1, iy + 1, seed) * a) * b;
}
function crc32(buffer) {
  let crc = 0xffffffff;
  for (const value of buffer) {
    crc ^= value;
    for (let bit = 0; bit < 8; ++bit) crc = (crc >>> 1) ^ ((crc & 1) ? 0xedb88320 : 0);
  }
  return (crc ^ 0xffffffff) >>> 0;
}
function chunk(type, data) {
  const name = Buffer.from(type), result = Buffer.alloc(data.length + 12);
  result.writeUInt32BE(data.length); name.copy(result, 4); data.copy(result, 8);
  result.writeUInt32BE(crc32(Buffer.concat([name, data])), data.length + 8);
  return result;
}
function png(name, pixels) {
  const header = Buffer.alloc(13);
  header.writeUInt32BE(size); header.writeUInt32BE(size, 4); header[8] = 8; header[9] = 6;
  const rows = Buffer.alloc((size * 4 + 1) * size);
  for (let y = 0; y < size; ++y) pixels.copy(rows, y * (size * 4 + 1) + 1, y * size * 4, (y + 1) * size * 4);
  fs.writeFileSync(path.join(output, name + '.png'), Buffer.concat([
    Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', header),
    chunk('IDAT', zlib.deflateSync(rows)), chunk('IEND', Buffer.alloc(0)),
  ]));
}
const fibers = Array.from({ length: 24 }, (_, i) => ({
  x: (i + hash(i, 2, 9051)) / 24, drift: (hash(i, 3, 9051) - .5) * .13,
  end: .36 + hash(i, 4, 9051) * .78, width: .004 + hash(i, 5, 9051) * .021,
  phase: hash(i, 6, 9051) * Math.PI * 2,
}));
for (const kind of ['Concrete', 'Wood']) {
  if (kind === 'Wood' && fs.existsSync(path.join(output, 'WoodFibers_Approved.png'))) {
    console.log('Wood: preserving approved artwork; use prepare_wood_fiber_maps.ps1 to rebuild its maps.');
    continue;
  }
  const color = Buffer.alloc(size * size * 4), masks = Buffer.alloc(size * size * 4);
  const normal = Buffer.alloc(size * size * 4), height = new Float64Array(size * size);
  let open = 0;
  for (let y = 0; y < size; ++y) {
    for (let x = 0; x < size; ++x) {
      const u = x / (size - 1), v = y / (size - 1), p = y * size + x, i = p * 4;
      const fine = hash(x, y, 42), coarse = noise(x / 27, y / 27, 42), medium = noise(x / 5, y / 5, 17);
      let rgb, roughness, ao, solid;
      if (kind === 'Concrete') {
        const pit = fine < .025 ? .20 : 0;
        const shade = .72 + coarse * .27 + (medium - .5) * .13 + (fine - .5) * .11 - pit;
        rgb = [174 * shade, 171 * shade, 161 * shade];
        height[p] = .5 + (medium - .5) * .18 + (fine - .5) * .11 - pit;
        roughness = .83 + coarse * .15; ao = 1 - pit;
        const edge = .65 + noise(u * 22, 2, 37) * .32 + noise(u * 57, 7, 37) * .04;
        solid = v < edge && !(v > .42 && noise(u * 73, v * 47, 84) < .12);
      } else {
        const drift = Math.sin(v * 13) * .012 + (noise(u * 3, v * 6, 3) - .5) * .028;
        const grain = Math.sin((u + drift) * 620 + noise(u * 22, v * 5, 14) * 3);
        const bands = noise((u + drift) * 90, v * 3, 7);
        const shade = .64 + bands * .4 + grain * .075 + (fine - .5) * .075;
        rgb = [166 * shade, 111 * shade, 58 * shade];
        height[p] = .5 + grain * .045 + (bands - .5) * .12 + (fine - .5) * .035;
        roughness = .70 + bands * .23; ao = .88 + bands * .12;
        solid = v < .13 + Math.sin(u * 18) * .015;
        for (const fiber of fibers) {
          const center = fiber.x + fiber.drift * v + Math.sin(v * 24 + fiber.phase) * .006;
          if (v < fiber.end && Math.abs(u - center) < fiber.width * (1 - .7 * v / fiber.end)) solid = true;
        }
        for (let split = 0; split < 7 && v > .16; ++split) {
          const start = .18 + hash(split, 8, 9051) * .32, t = (v - start) / .32;
          const center = .06 + hash(split, 9, 9051) * .88 + t * .025;
          if (t > 0 && t < 1 && Math.abs(u - center) < Math.sin(t * Math.PI) * .009) solid = false;
        }
      }
      if (!solid) ++open;
      for (let c = 0; c < 3; ++c) color[i + c] = Math.round(Math.max(0, Math.min(255, rgb[c])));
      color[i + 3] = 255;
      masks[i] = Math.round(clamp(roughness) * 255); masks[i + 1] = Math.round(clamp(ao) * 255);
      masks[i + 2] = solid ? 255 : 0; masks[i + 3] = 255;
    }
  }
  for (let y = 0; y < size; ++y) {
    for (let x = 0; x < size; ++x) {
      const left = height[y * size + Math.max(0, x - 1)], right = height[y * size + Math.min(size - 1, x + 1)];
      const top = height[Math.max(0, y - 1) * size + x], bottom = height[Math.min(size - 1, y + 1) * size + x];
      const nx = (left - right) * 1.8, ny = (top - bottom) * 1.8, length = Math.hypot(nx, ny, 1), i = (y * size + x) * 4;
      normal[i] = Math.round((nx / length * .5 + .5) * 255);
      normal[i + 1] = Math.round((ny / length * .5 + .5) * 255);
      normal[i + 2] = Math.round((1 / length * .5 + .5) * 255); normal[i + 3] = 255;
    }
  }
  png(`T_Deco_${kind}_BaseColor`, color); png(`T_Deco_${kind}_Normal`, normal); png(`T_Deco_${kind}_Masks`, masks);
  console.log(`${kind}: 512x512, mask empty ${(open / size ** 2 * 100).toFixed(1)}%, solid root`);
}
