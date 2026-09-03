'use strict';
/**
 * Tor integration.
 *
 * Two responsibilities:
 *  - Outbound: connect to `.onion` addresses through a Tor SOCKS5 proxy
 *    (SocksClient handles RFC 1928; the proxy is what in Tor Browser).
 *  - Inbound: run (or reuse) a tor daemon that exposes an Onion Service
 *    (v3). We write the `torrc` and read the generated hostname from
 *    the HiddenServiceDir.
 *
 * The daemon must be available on `tor` on PATH (Arch: pacman -S tor).
 */

const { spawn, spawnSync } = require('child_process');
const fs = require('fs');
const path = require('path');
const os = require('os');

let Socks = null;
try {
    Socks = require('socks');
} catch {
    // optional
}

function tmpdir(suffix = '') {
    return fs.mkdtempSync(path.join(os.tmpdir(), 'zaviro-tor-' + suffix + '-'));
}

/**
 * Connect to a host:port via the Tor SOCKS proxy.
 * host may be a `.onion` (v3) address.
 * @returns {Promise<net.Socket>} connected socket
 */
function socksConnect({ proxyHost = '127.0.0.1', proxyPort = 9050, host, port }) {
    if (!Socks) {
        return Promise.reject(new Error('socks library not available'));
    }
    return new Promise((resolve, reject) => {
        Socks.SocksClient.createConnection({
            proxy: { host: proxyHost, port: proxyPort, type: 5 },
            command: 'connect',
            destination: { host, port },
            timeout: 30_000
        }, (err, info) => {
            if (err) return reject(err);
            resolve(info.socket);
        });
    });
}

/**
 * Ensure a tor daemon is running in a dedicated DataDirectory with an
 * Onion Service configured.
 *
 * @param {object} opts
 * @param {number} opts.servicePort local port the onion service forwards to
 * @param {string} [opts.socksPort] SOCKS port for outbound (default 19050)
 * @param {string} [opts.dataDir] where tor keeps state + hs keys (persistent!)
 * @returns {Promise<{hostname: string, dataDir: string, socksPort: number}>}
 */
async function startTor({ servicePort, socksPort = 19050, dataDir, torPath }) {
    const torBin = torPath || 'tor';
    const dir = dataDir || tmpdir('svc');
    fs.mkdirSync(path.join(dir, 'services'), { recursive: true });
    fs.chmodSync(dir, 0o700);

    const hsDir = path.join(dir, 'services', 'zaviro');
    fs.mkdirSync(hsDir, { recursive: true });
    fs.chmodSync(hsDir, 0o700);

    const torrc = path.join(dir, 'torrc');
    const socks = socksPort;
    const control = 19060;

    const torrcContent = [
        'DataDirectory ' + dir,
        'SocksPort ' + socks,
        'ControlPort ' + control,
        'HiddenServiceDir ' + hsDir,
        'HiddenServicePort 80 127.0.0.1:' + servicePort,
        'Log notice stderr',
        // v3 onion (default in modern tor)
        'HiddenServiceVersion 3'
    ].join('\n');

    fs.writeFileSync(torrc, torrcContent);

    // Check tor binary — try bundled path, then PATH
    let resolvedTor = torBin;
    if (!fs.existsSync(resolvedTor)) {
        resolvedTor = 'tor';
    }
    if (spawnSync(resolvedTor, ['--version'], { encoding: 'utf8' }).error) {
        throw new Error(
            'Tor binary not found. Install it (Arch: sudo pacman -S tor; Debian: sudo apt install tor).'
        );
    }

    const proc = spawn(resolvedTor, ['-f', torrc], {
        stdio: ['ignore', 'ignore', 'pipe']
    });

    // Wait for hostname file to appear (means the onion service is published)
    const hostFile = path.join(hsDir, 'hostname');
    const deadline = Date.now() + 240_000; // cold Tor bootstrap can be slow
    let hostname = null;
    let lastLog = 0;

    const procExit = new Promise((_, reject) => {
        proc.on('error', reject);
        proc.on('exit', (code) => {
            if (!hostname && code && Date.now() < deadline) {
                reject(new Error('tor exited early (code ' + code + ')'));
            }
        });
    });

    try {
        const start = Date.now();
        while (Date.now() < deadline) {
            if (fs.existsSync(hostFile)) {
                hostname = fs.readFileSync(hostFile, 'utf8').trim();
                break;
            }
            if (Date.now() - lastLog > 30_000) {
                lastLog = Date.now();
                console.log(`[Tor] bootstrapping onion service... ${Math.round((Date.now() - start) / 1000)}s elapsed`);
            }
            await Promise.race([procExit.then(() => { throw new Error('tor process ended'); }), new Promise(r => setTimeout(r, 500))]);
        }
    } catch (e) {
        proc.kill();
        throw new Error('Tor failed to publish onion service: ' + e.message);
    }

    if (!hostname) {
        let errMsg = 'timed out';
        try { errMsg = 'tor exited: ' + (proc.stderr.read() || '') || errMsg; } catch {}
        proc.kill();
        throw new Error('Tor failed to publish onion service: ' + errMsg.slice(0, 300));
    }

    return { hostname, dataDir: dir, socksPort: socks, process: proc };
}

module.exports = { startTor, socksConnect, tmpdir };
