// Replicates the client's procedural terrain height function so that
// server-authoritative collision matches the rendered terrain exactly.

const SEED = 1337;
const SIZE = 100;

function hash12(x, y) {
    return ((Math.sin(x * 12.9898 + y * 78.233) * 43758.5453) % 1 + 1) % 1;
}

function smoothstepf(t) {
    return t * t * (3 - 2 * t);
}

function valueNoise2D(x, y, seed) {
    const ix = Math.floor(x);
    const iy = Math.floor(y);
    const fx = x - ix;
    const fy = y - iy;

    const s = hash12(ix, iy);
    const t = hash12(ix + 1, iy);
    const u = hash12(ix, iy + 1);
    const v = hash12(ix + 1, iy + 1);

    const sx = smoothstepf(fx);
    const sy = smoothstepf(fy);

    return s + (t - s) * sx + ((u + (v - u) * sx) - (s + (t - s) * sx)) * sy;
}

function fbm2D(x, y, seed, octaves) {
    let total = 0;
    let amplitude = 0.5;
    let frequency = 1;
    let maxValue = 0;

    for (let i = 0; i < octaves; i++) {
        total += valueNoise2D(x * frequency + seed * 0.01, y * frequency + seed * 0.013, seed + i) * amplitude;
        maxValue += amplitude;
        amplitude *= 0.5;
        frequency *= 2;
    }

    return total / maxValue;
}

function noise2D(x, z, seed) {
    return fbm2D(x, z, seed, 4);
}

function terrainHeightAt(x, z) {
    const nx = (x + SIZE * 0.5) / SIZE;
    const nz = (z + SIZE * 0.5) / SIZE;

    let h = noise2D(nx * 4, nz * 4, SEED) * 12;
    h += noise2D(nx * 8, nz * 8, SEED + 1) * 4;
    h -= 4;

    return Math.max(h, 0);
}

module.exports = { terrainHeightAt };
