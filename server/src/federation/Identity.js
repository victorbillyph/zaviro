'use strict';
/**
 * Identity & Proof-of-Work module.
 *
 * Every Zaviro server has a persistent Ed25519 identity key. It is used to:
 *  - Sign protocol statements (advertisements, reputation claims, mesh joins)
 *  - Authenticate to the central directory (the "trusted" gatekeeper)
 *
 * Before a server (or client connecting through the federation) is accepted
 * into the mesh it must solve a Proof-of-Work challenge. This makes it
 * computationally expensive to spin up thousands of fake/malicious nodes,
 * providing Sybil resistance on top of identity.
 *
 * NOTE: server-side crypto uses libsodium (libsodium-wrappers). Null-crypto
 * fallbacks are provided so the federation can still run in tests/localhost.
 */

const sodiumPromise = require('libsodium-wrappers');
let sodium = null;
let initPromise = null;

async function init() {
    if (sodium) return sodium;
    if (!initPromise) {
        initPromise = sodiumPromise.ready.then(() => {
            sodium = sodiumPromise;
            return sodium;
        });
    }
    return initPromise;
}

function hex(buf) {
    return Buffer.from(buf).toString('hex');
}

/**
 * Deterministic-ish random nonce for challenges.
 */
function randomHex(bytes = 16) {
    const b = require('crypto').randomBytes(bytes);
    return b.toString('hex');
}

/**
 * Generate a persistent Ed25519 identity.
 * @returns {{publicKey: Buffer, privateKey: Buffer, pubHex: string}}
 */
async function generateIdentity() {
    const s = await init();
    const kp = s.crypto_sign_keypair();
    return {
        publicKey: Buffer.from(kp.publicKey),
        privateKey: Buffer.from(kp.privateKey),
        pubHex: hex(kp.publicKey)
    };
}

/**
 * Sign a message (arbitrary string/buffer) with the identity private key.
 * @returns {Buffer} signature
 */
async function sign(privateKey, message) {
    const s = await init();
    if (!sodium) await init();
    const sig = s.crypto_sign_detached(
        typeof message === 'string' ? Buffer.from(message, 'utf8') : message,
        privateKey
    );
    return Buffer.from(sig);
}

/**
 * Verify a detached signature against a public key.
 */
async function verify(publicKey, message, signature) {
    const s = await init();
    try {
        return s.crypto_sign_verify_detached(
            signature,
            typeof message === 'string' ? Buffer.from(message, 'utf8') : message,
            publicKey
        );
    } catch {
        return false;
    }
}

/**
 * Create a PoW challenge.
 * @returns {{challenge: string, difficulty: number, expiresAt: number}}
 */
function createChallenge(difficulty) {
    return {
        challenge: randomHex(16),
        difficulty: difficulty || 16,
        expiresAt: Date.now() + 60_000
    };
}

/**
 * Solve a PoW challenge. Finds a nonce such that
 *   sha256(challenge + nonce) has `difficulty` leading zero *bits*.
 * @returns {Promise<{nonce: string, hash: string}>}
 */
async function solveChallenge(challenge, difficulty) {
    const crypto = require('crypto');
    const targetBits = difficulty;
    const target = BigInt(1) << BigInt(256 - targetBits);
    let nonce = 0n;
    while (true) {
        const nonceStr = nonce.toString(16).padStart(16, '0');
        const hash = crypto.createHash('sha256')
            .update(challenge + nonceStr)
            .digest('hex');
        const h = BigInt('0x' + hash);
        if (h < target) {
            return { nonce: nonceStr, hash, attempts: (nonce + 1n).toString() };
        }
        nonce++;
    }
}

/**
 * Verify a PoW solution.
 * @returns {boolean}
 */
function verifyChallenge(challenge, nonce, difficulty) {
    const crypto = require('crypto');
    const targetBits = difficulty;
    if (targetBits < 0 || targetBits > 256) return false;
    const target = BigInt(1) << BigInt(256 - targetBits);
    const hash = crypto.createHash('sha256').update(challenge + nonce).digest('hex');
    const h = BigInt('0x' + hash);
    return h < target;
}

/**
 * Derive a stable "server id" from a public key (first 8 bytes hex).
 */
function serverIdFromPub(pubHex) {
    return (pubHex || '').substring(0, 16);
}

module.exports = {
    init,
    generateIdentity,
    sign,
    verify,
    createChallenge,
    solveChallenge,
    verifyChallenge,
    serverIdFromPub,
    randomHex,
    hex
};
