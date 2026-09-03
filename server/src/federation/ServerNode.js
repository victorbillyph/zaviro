'use strict';
/**
 * ServerNode: federation + Tor for a Zaviro *gameplay* server.
 *
 * Responsibilities:
 *  - Own a persistent Ed25519 identity on disk.
 *  - Publish a Tor onion service that fronts the gameplay TCP port so
 *    remote clients (and peers) can reach us over the internet without
 *    port forwarding.
 *  - Announce our presence to a central directory (authenticated via PoW).
 *  - Maintain a list of peer servers and expose it to the client lobby.
 */

const fs = require('fs');
const path = require('path');
const ID = require('./Identity');
const { FederationPeer } = require('./Federation');
const Tor = require('../tor/Tor');

class ServerNode {
    constructor({ server, servicePort }) {
        this.server = server;
        this.servicePort = servicePort;
        this.idPath = path.join(__dirname, '../../data/identity.json');
        this.peers = new Map();
        this.announcePeer = new FederationPeer({ identity: null });
    }

    async init(opts = {}) {
        await this.ensureIdentity();
        this.socksPort = opts.socksPort || 19050;

        if (opts.tor !== false) {
            await this.startTor();
        }

        if (opts.directory && opts.directory.onion) {
            this.directory = opts.directory;
            this.announceIntervalSec = opts.announceIntervalSec || 120;
            await this.announce();
            if (this.announceIntervalSec > 0) {
                this.announceTimer = setInterval(
                    () => this.announce(),
                    this.announceIntervalSec * 1000
                );
            }
        }

        return this;
    }

    loadIdentityFromDisk() {
        if (fs.existsSync(this.idPath)) {
            const raw = JSON.parse(fs.readFileSync(this.idPath, 'utf8'));
            return {
                publicKey: Buffer.from(raw.publicKey, 'hex'),
                privateKey: Buffer.from(raw.privateKey, 'hex'),
                pubHex: raw.publicKey
            };
        }
        return null;
    }

    async ensureIdentity() {
        if (this.identity) return this.identity;
        const existing = this.loadIdentityFromDisk();
        if (existing) {
            this.identity = existing;
        } else {
            this.identity = await ID.generateIdentity();
            fs.mkdirSync(path.dirname(this.idPath), { recursive: true });
            fs.writeFileSync(this.idPath, JSON.stringify({
                publicKey: this.identity.publicKey.toString('hex'),
                privateKey: this.identity.privateKey.toString('hex')
            }));
        }
        this.announcePeer.identity = this.identity;
        return this.identity;
    }

    async startTor() {
        const TorOpts = await Tor.startTor({
            servicePort: this.servicePort,
            socksPort: this.socksPort,
            dataDir: process.env.ZAVIRO_TOR_DATA_DIR
        });
        this.onion = TorOpts.hostname;
        this.torProcess = TorOpts.process;
        this.server.onionAddress = this.onion;
        this.server.onionAvailable = true;
        console.log(`[ServerNode] onion service published: ${this.onion}`);
        return TorOpts;
    }

    async announce() {
        if (!this.directory || !this.onion) {
            console.log('[ServerNode] announce skipped: no directory or no onion yet.');
            return;
        }
        try {
            const self = {
                onion: this.onion,
                name: this.server.serverName || 'Zaviro Server',
                region: process.env.ZAVIRO_REGION || 'unknown',
                pubKey: this.identity.pubHex
            };
            const peers = await this.announcePeer.registerAndList({
                directoryOnion: this.directory.onion,
                socksPort: this.directory.socksPort || this.socksPort,
                self
            });
            // Drop self from the peer list.
            this.peers.clear();
            for (const s of peers) {
                if (s.pubKey === this.identity.pubHex) continue;
                this.peers.set(s.pubKey, s);
            }
            this.server.peerServers = Array.from(this.peers.values());
            console.log(`[ServerNode] announced; known peers: ${this.server.peerServers.length}`);
        } catch (e) {
            console.warn('[ServerNode] announce failed:', e.message);
        }
    }

    shutdown() {
        if (this.announceTimer) clearInterval(this.announceTimer);
        if (this.torProcess) this.torProcess.kill();
    }
}

module.exports = ServerNode;
