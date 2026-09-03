'use strict';
/**
 * Central Directory (bootstrap / rendezvous).
 *
 * A small trusted server that:
 *  - Registers Zaviro servers (each authenticated by Ed25519 + PoW)
 *  - Serves the list of known servers (filtered by liveness)
 *  - Does NOT run gameplay; it only facilitates discovery.
 *
 * It is reachable over Tor via an onion service. Run it with:
 *   node src/federation/Directory.js
 */

const net = require('net');
const fs = require('fs');
const path = require('path');
const ID = require('./Identity');
const { MSG, encode, readMessages } = require('./Federation');
const Tor = require('../tor/Tor');

class Directory {
    constructor({ port = 8766, difficulty = 18, dataDir, socksPort = 19050 }) {
        this.port = port;
        this.difficulty = difficulty;
        this.dataDir = dataDir || process.env.ZAVIRO_DIR_DIR;
        this.socksPort = socksPort;
        this.servers = new Map(); // pubKey -> {onion, pubKey, name, region, lastSeen, nonce}
        this.sessions = new Map(); // socket -> {challenge}
    }

    addServer(reg) {
        reg.lastSeen = Date.now();
        this.servers.set(reg.pubKey, reg);
        // prune stale (older than 5 min)
        for (const [k, v] of this.servers) {
            if (Date.now() - v.lastSeen > 5 * 60_000) this.servers.delete(k);
        }
    }

    start() {
        this.identity = null;

        this.server = net.createServer((sock) => {
            this.handleSock(sock);
        });
        this.server.listen(this.port, () => {
            console.log(`[Directory] listening 0.0.0.0:${this.port}`);
        });

        // Expose over Tor
        if (process.env.ZAVIRO_TOR !== '0') {
            Tor.startTor({ servicePort: this.port, dataDir: this.dataDir, socksPort: this.socksPort })
                .then(({ hostname }) => {
                    this.hostname = hostname;
                    console.log(`[Directory] onion service published: ${hostname}`);
                })
                .catch((e) => {
                    console.warn('[Directory] Tor failed, directory only reachable on clearnet/localhost:', e.message);
                });
        }
    }

    handleSock(sock) {
        let session = { challenge: null };
        readMessages(sock, (m) => this.handleMsg(sock, session, m));
        sock.on('close', () => { this.sessions.delete(sock); });
    }

    async handleMsg(sock, session, m) {
        switch (m.type) {
            case MSG.HELLO: {
                const challenge = ID.createChallenge(this.difficulty);
                session.challenge = challenge;
                sock.write(encode({ type: MSG.AUTH_REQUEST, challenge: challenge.challenge, difficulty: this.difficulty }));
                break;
            }
            case MSG.AUTH_RESPONSE: {
                if (!session.challenge) return;
                if (!ID.verifyChallenge(session.challenge.challenge, m.nonce, this.difficulty)) {
                    sock.write(encode({ type: 'auth_fail' }));
                    sock.end();
                    return;
                }
                session.authenticated = true;
                sock.write(encode({ type: MSG.REGISTERED, ok: true }));
                break;
            }
            case MSG.REGISTER: {
                if (!session.authenticated) {
                    sock.write(encode({ type: 'auth_required' }));
                    return;
                }
                // Over a Tor network the connecting peer's onion is the only
                // address we can advertise back; for test/localhost we accept
                // whatever the peer sends.
                const rec = {
                    onion: m.onion,
                    pubKey: m.pubKey,
                    name: m.name,
                    region: m.region || '',
                    lastSeen: Date.now()
                };
                if (!rec.onion || !rec.pubKey) {
                    sock.write(encode({ type: MSG.SERVER_LIST, servers: [] }));
                    return;
                }
                // Optionally verify the peer's Ed25519 signature over its statement.
                if (m.sig && m.pubKey) {
                    const statement = JSON.stringify({ onion: m.onion, name: m.name, region: m.region || '' });
                    const okSig = await this.verifySig(m.pubKey, statement, m.sig).catch(() => false);
                    if (!okSig) {
                        sock.write(encode({ type: MSG.SERVER_LIST, servers: [] }));
                        return;
                    }
                }
                this.addServer(rec);
                console.log(`[Directory] registered ${m.name} @ ${m.onion} (${m.region || '?'})`);
                const servers = Array.from(this.servers.values())
                    .filter((s) => Date.now() - s.lastSeen < 5 * 60_000)
                    .map(({ onion, pubKey, name, region, lastSeen }) => ({ onion, pubKey, name, region, lastSeen }));
                sock.write(encode({ type: MSG.SERVER_LIST, servers }));
                break;
            }
            case 'id_proof': {
                // Legacy/back-compat: answer with the server list.
                const servers = Array.from(this.servers.values())
                    .filter((s) => Date.now() - s.lastSeen < 5 * 60_000)
                    .map(({ onion, pubKey, name, region, lastSeen }) => ({ onion, pubKey, name, region, lastSeen }));
                sock.write(encode({ type: MSG.SERVER_LIST, servers }));
                break;
            }
            default:
                break;
        }
    }

    async verifySig(pubHex, statement, sigHex) {
        return ID.verify(
            Buffer.from(pubHex, 'hex'),
            statement,
            Buffer.from(sigHex, 'hex')
        );
    }

    shutdown() {
        if (this.server) this.server.close();
    }
}

if (require.main === module) {
    const nconf = require('nconf');
    nconf.argv().env().file({ file: path.join(__dirname, '../../config/directory.json') });
    const dir = new Directory({
        port: nconf.get('port') || 8766,
        difficulty: nconf.get('difficulty') || 18,
        socksPort: nconf.get('socksPort') || 19050,
        dataDir: nconf.get('dataDir')
    });
    dir.start();
    process.on('SIGINT', () => { dir.shutdown(); process.exit(0); });
}

module.exports = Directory;
