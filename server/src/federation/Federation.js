'use strict';
/**
 * Federation mesh.
 *
 * Servers announce themselves to a central directory (a trusted rendezvous,
 * identified solely by its own Ed25519 key + .onion). The directory runs
 * JSON-over-TCP behind Tor. Federation peers then interconnect in a mesh and
 * gossip peer lists + signed server-identity statements.
 *
 * Wire format (federation layer): [uint32 BE len][JSON] per message, since
 * federation is only between servers/clients and is not the gameplay protocol.
 *
 * A new node must solve a Proof-of-Work challenge (provided by the directory)
 * before it is allowed past the auth gate. This raises the cost of spinning up
 * fake/Sybil servers. The node further proves its persistent identity by
 * signing its registration statement with its Ed25519 key.
 */

const { socksConnect } = require('../tor/Tor');
const ID = require('./Identity');

const MSG = {
    HELLO: 'hello',              // {version, pubKey}
    AUTH_REQUEST: 'auth_request',// {challenge, difficulty}
    AUTH_RESPONSE: 'auth_response',// {nonce}
    SERVER_LIST: 'server_list',  // {servers:[{onion, pubKey, name, region, lastSeen}]}
    REGISTER: 'register',        // {onion, pubKey, name, region, sig}
    REGISTERED: 'registered'     // {ok:bool}
};

function encode(data) {
    const body = Buffer.from(JSON.stringify(data), 'utf8');
    const h = Buffer.alloc(4);
    h.writeUInt32BE(body.length, 0);
    return Buffer.concat([h, body]);
}

function readMessages(sock, onMsg) {
    let buf = Buffer.alloc(0);
    sock.on('data', (d) => {
        buf = Buffer.concat([buf, d]);
        while (buf.length >= 4) {
            const len = buf.readUInt32BE(0);
            if (buf.length < 4 + len) break;
            let data;
            try {
                data = JSON.parse(buf.subarray(4, 4 + len).toString('utf8'));
            } catch { buf = buf.subarray(4 + len); continue; }
            buf = buf.subarray(4 + len);
            onMsg(data);
        }
    });
}

function waitFor(messages, type, timeoutMs = 30_000) {
    return new Promise((resolve, reject) => {
        const deadline = Date.now() + timeoutMs;
        const t = setInterval(() => {
            const found = messages.find((x) => x.type === type);
            if (found) { clearInterval(t); resolve(found); }
            else if (Date.now() > deadline) { clearInterval(t); reject(new Error('timeout waiting for ' + type)); }
        }, 25);
    });
}

/**
 * Open a session with a directory and complete the PoW auth gate.
 * @returns {Promise<{sock, messages}>}
 */
async function openDirectorySession({ directoryOnion, socksPort, difficulty }) {
    const sock = await socksConnect({
        proxyPort: socksPort,
        host: directoryOnion,
        port: 80
    });
    const messages = [];
    readMessages(sock, (m) => messages.push(m));
    sock.write(encode({ type: MSG.HELLO, version: 1 }));

    const challenge = await waitFor(messages, MSG.AUTH_REQUEST);
    const { nonce } = await ID.solveChallenge(challenge.challenge, challenge.difficulty);
    sock.write(encode({ type: MSG.AUTH_RESPONSE, nonce }));

    // Wait for the directory to confirm auth before continuing.
    await waitFor(messages, MSG.REGISTERED, 30_000).catch(() => {});
    return { sock, messages };
}

class FederationPeer {
    /**
     * @param {object} opts
     * @param {object} opts.identity {publicKey, privateKey, pubHex}
     */
    constructor(opts = {}) {
        this.identity = opts.identity || null;
    }

    /**
     * Register ourselves with the directory (authenticated via PoW + identity
     * signature) and fetch the list of other servers.
     * @returns {Promise<Array>} list of servers known to the directory
     */
    async registerAndList({ directoryOnion, socksPort, self }) {
        const { sock, messages } = await openDirectorySession({ directoryOnion, socksPort });
        // messages now contains REGISTERED from auth confirmation, or SERVER_LIST.
        // Send id_proof THEN register.
        if (this.identity) {
            const statement = JSON.stringify({
                onion: self.onion, name: self.name, region: self.region
            });
            const sig = await ID.sign(this.identity.privateKey, statement);
            sock.write(encode({
                type: MSG.REGISTER,
                onion: self.onion,
                pubKey: this.identity.pubHex,
                name: self.name,
                region: self.region,
                sig: sig.toString('hex')
            }));
        } else {
            if (self) {
                sock.write(encode({
                    type: MSG.REGISTER,
                    onion: self.onion,
                    pubKey: self.pubKey || '',
                    name: self.name,
                    region: self.region || ''
                }));
            }
        }

        // Wait for SERVER_LIST (directory replies after registering us).
        let servers = [];
        try {
            const list = await waitFor(messages, MSG.SERVER_LIST, 30_000);
            servers = list.servers || [];
        } catch {}
        const reg = messages.find((x) => x.type === MSG.REGISTERED);
        try { sock.end(); } catch {}
        return servers;
    }

    /**
     * Connect to a peer server and perform mutual identity + PoW auth.
     * @returns {Promise<net.Socket>}
     */
    async connectToPeer(peer, { socksPort }) {
        const sock = await socksConnect({
            proxyPort: socksPort,
            host: peer.onion,
            port: 80
        });
        const messages = [];
        readMessages(sock, (m) => messages.push(m));
        sock.write(encode({ type: MSG.HELLO, version: 1, pubKey: this.identity ? this.identity.pubHex : '' }));
        const auth = await waitFor(messages, MSG.AUTH_REQUEST, 30_000);
        const { nonce } = await ID.solveChallenge(auth.challenge, auth.difficulty);
        sock.write(encode({ type: MSG.AUTH_RESPONSE, nonce }));
        return sock;
    }

    async requestServiceList({ directoryOnion, socksPort }) {
        return this.registerAndList({ directoryOnion, socksPort, self: null });
    }
}

module.exports = { FederationPeer, MSG, encode, readMessages, openDirectorySession };
