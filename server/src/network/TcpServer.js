const net = require('net');
const nconf = require('nconf');
const Player = require('../game/Player');
const Universe = require('../game/Universe');
const UniverseStore = require('../game/UniverseStore');

nconf.argv().env().file({ file: __dirname + '/../config/default.json' });

// Protocol: [uint16 BE type][uint32 BE length][payload UTF-8 JSON]
const MSG = {
    // C -> S
    HELLO: 1,
    INPUT: 2,
    CHAT: 3,
    PLACE_BLOCK: 4,
    BREAK_BLOCK: 5,
    JOIN_UNIVERSE: 6,
    LEAVE_UNIVERSE: 7,
    CREATE_UNIVERSE: 8,
    UPDATE_UNIVERSE: 9,
    DELETE_UNIVERSE: 10,
    UPLOAD_TEXTURE: 11,
    UI_EVENT: 12,

    // S -> C
    WELCOME: 100,
    WORLD_STATE: 101,
    WORLD_UPDATE: 102,
    PLAYER_JOIN: 103,
    PLAYER_LEAVE: 104,
    ENTITY_CREATE: 105,
    ENTITY_REMOVE: 106,
    CHAT_BROADCAST: 107,
    UNIVERSE_LIST: 108,
    UNIVERSE_JOINED: 109,
    SERVER_LIST: 110,
    SERVER_INFO: 111,
    UNIVERSE_CREATED: 112,
    UNIVERSE_UPDATED: 113,
    UNIVERSE_DELETED: 114,
    TEXTURE_ACK: 115,
    UI_DEFINITION: 116,
    TEXTURE_DATA: 117,
    CUSTOM_EVENT: 118,
};

class TcpGameServer {
    constructor() {
        this.port = nconf.get('server:port') || 8765;
        this.maxPlayers = nconf.get('server:maxPlayers') || 64;
        this.tickRate = nconf.get('server:tickRate') || 30;

        this.clients = new Map();
        this.nextClientId = 1;

        this.universes = new Map();
        this.nextUniverseId = 1;

        // Player-created universes (persisted)
        this.store = new UniverseStore();

        this.running = false;

        this.createDefaultUniverses();
        this.loadPlayerUniverses();
    }

    createDefaultUniverses() {
        const defaults = [
            {
                name: 'Spawn Island',
                description: 'Um mundo aberto com terreno verde e blocos coloridos. O lugar perfeito para comecar.',
                maxPlayers: 32,
                seed: 1337,
                bgColor: { r: 0.15, g: 0.25, b: 0.35 }
            },
            {
                name: 'Desert Valley',
                description: 'Terreno arenoso com dunas e formacoes rochosas. Cuidado com a gravidade.',
                maxPlayers: 16,
                seed: 42069,
                bgColor: { r: 0.4, g: 0.3, b: 0.15 }
            },
            {
                name: 'Arctic Peaks',
                description: 'Montanhas nevadas e vales profundos. O terreno mais desafiador.',
                maxPlayers: 24,
                seed: 77777,
                bgColor: { r: 0.3, g: 0.35, b: 0.5 }
            },
            {
                name: 'Mega Build',
                description: 'Mapa grande para construcoes. Ideal para building com amigos.',
                maxPlayers: 48,
                seed: 99999,
                bgColor: { r: 0.2, g: 0.15, b: 0.3 }
            }
        ];

        for (const cfg of defaults) {
            const id = this.nextUniverseId++;
            const u = new Universe(id, cfg);
            u.setServer(this);
            this.universes.set(id, u);
        }
    }

    loadPlayerUniverses() {
        for (const doc of this.store.getAll()) {
            // avoid id collisions with defaults
            if (doc.id >= this.nextUniverseId) this.nextUniverseId = doc.id + 1;
            const u = new Universe(doc.id, doc);
            u.setServer(this);
            this.universes.set(doc.id, u);
        }
        // continue the store's id counter past defaults + loaded player universes
        this.store.nextId = this.nextUniverseId;
    }

    start() {
        this.server = net.createServer((socket) => {
            this.handleConnection(socket);
        });

        this.server.listen(this.port, () => {
            console.log(`Zaviro TCP Server running on port ${this.port}`);
            console.log(`Universes: ${this.universes.size}`);
            console.log(`Tick rate: ${this.tickRate} Hz`);
            console.log(`Max players: ${this.maxPlayers}`);
        });

        this.running = true;
        this.gameLoop();
    }

    handleConnection(socket) {
        if (this.clients.size >= this.maxPlayers) {
            socket.end();
            return;
        }

        const clientId = this.nextClientId++;
        const client = {
            id: clientId,
            socket,
            player: new Player(clientId, `Player${clientId}`),
            lastInput: { forward: 0, right: 0, up: 0, jump: 0, mouseX: 0, mouseY: 0 },
            buffer: Buffer.alloc(0),
            currentUniverse: null
        };

        this.clients.set(clientId, client);
        console.log(`Client ${client.player.name} connected (ID: ${clientId})`);

        // Send welcome
        this.send(client, MSG.WELCOME, {
            clientId: clientId,
            playerName: client.player.name,
            server: this.getServerInfo()
        });

        // Send universe list
        this.sendUniverseList(client);

        // Send federation server list (multi-server support)
        this.sendServerList(client);

        socket.on('data', (data) => {
            this.handleData(client, data);
        });

        socket.on('error', () => this.handleDisconnect(clientId));
        socket.on('close', () => this.handleDisconnect(clientId));
    }

    handleData(client, data) {
        client.buffer = Buffer.concat([client.buffer, data]);

        while (client.buffer.length >= 6) {
            const type = client.buffer.readUInt16BE(0);
            const length = client.buffer.readUInt32BE(2);

            if (client.buffer.length < 6 + length) break;

            const payload = client.buffer.subarray(6, 6 + length).toString('utf8');
            client.buffer = client.buffer.subarray(6 + length);

            this.handleMessage(client.id, type, payload);
        }

        if (client.buffer.length > 10 * 1024 * 1024) {
            client.socket.end();
        }
    }

    handleMessage(clientId, type, payload) {
        const client = this.clients.get(clientId);
        if (!client) return;

        let data = {};
        if (payload) {
            try { data = JSON.parse(payload); } catch (e) { return; }
        }

        switch (type) {
            case MSG.HELLO:
                client.player.name = data.name || client.player.name;
                break;

            case MSG.INPUT:
                client.lastInput = {
                    forward: data.forward || 0,
                    right: data.right || 0,
                    jump: data.jump || 0,
                    mouseX: data.mouseX || 0,
                    mouseY: data.mouseY || 0
                };
                // Apply mouse rotation
                client.player.yaw += (data.mouseX || 0) * client.player.sensitivity;
                client.player.pitch -= (data.mouseY || 0) * client.player.sensitivity;
                client.player.pitch = Math.max(-89, Math.min(89, client.player.pitch));
                break;

            case MSG.CHAT:
                if (client.currentUniverse) {
                    client.currentUniverse.broadcastToAll(this, {
                        type: MSG.CHAT_BROADCAST,
                        data: {
                            clientId,
                            playerName: client.player.name,
                            message: data.message || ''
                        }
                    }, clientId);
                    client.currentUniverse.fire('onChat', {
                        id: clientId,
                        name: client.player.name,
                        message: data.message || ''
                    });
                }
                break;

            case MSG.PLACE_BLOCK:
                if (client.currentUniverse && data && data.position && data.color) {
                    const entity = client.currentUniverse.world.createEntity(
                        `Block_${Date.now()}_${clientId}`,
                        data.position,
                        { x: 1, y: 1, z: 1 },
                        data.color,
                        false
                    );
                    client.currentUniverse.broadcastToAll(this, {
                        type: MSG.ENTITY_CREATE,
                        data: entity
                    });
                    client.currentUniverse.fire('onPlaceBlock', {
                        id: clientId,
                        name: client.player.name,
                        entity: entity
                    });
                }
                break;

            case MSG.BREAK_BLOCK:
                if (client.currentUniverse && data && data.entityId !== undefined) {
                    if (client.currentUniverse.world.removeEntity(data.entityId)) {
                        client.currentUniverse.broadcastToAll(this, {
                            type: MSG.ENTITY_REMOVE,
                            data: { entityId: data.entityId }
                        });
                        client.currentUniverse.fire('onBreakBlock', {
                            id: clientId,
                            name: client.player.name,
                            entityId: data.entityId
                        });
                    }
                }
                break;

            case MSG.JOIN_UNIVERSE: {
                const universeId = data.universeId;
                const universe = this.universes.get(universeId);
                if (!universe) {
                    this.send(client, MSG.UNIVERSE_JOINED, { success: false, error: 'Universe not found' });
                    break;
                }
                if (universe.world.players.size >= universe.maxPlayers) {
                    this.send(client, MSG.UNIVERSE_JOINED, { success: false, error: 'Universe full' });
                    break;
                }

                // Leave current universe if any
                if (client.currentUniverse) {
                    this.leaveUniverse(client);
                }

                // Join new universe
                client.currentUniverse = universe;
                universe.addPlayer(clientId, client.player);

                // Send confirmation with universe info
                this.send(client, MSG.UNIVERSE_JOINED, {
                    success: true,
                    universe: universe.getInfo()
                });

                // Send world state
                this.send(client, MSG.WORLD_STATE, {
                    world: {
                        tick: universe.world.tick,
                        entities: Array.from(universe.world.entities.values()),
                        players: Array.from(universe.world.players.values()).map(p => p.getState())
                    }
                });

                // Send UI definition + textures for scripted universes
                if (universe.scriptClient && universe.scriptClient.ui) {
                    this.send(client, MSG.UI_DEFINITION, {
                        universeId: universe.id,
                        ui: universe.scriptClient.ui,
                        hasServerScript: !!universe.scriptServer
                    });
                }
                if (universe.textures && Object.keys(universe.textures).length > 0) {
                    for (const [name, tex] of Object.entries(universe.textures)) {
                        this.send(client, MSG.TEXTURE_DATA, { name, mime: tex.mime, data: tex.data });
                    }
                }

                // Notify others in universe
                universe.broadcastToAll(this, {
                    type: MSG.PLAYER_JOIN,
                    data: {
                        clientId,
                        playerName: client.player.name,
                        position: client.player.transform.position
                    }
                }, clientId);

                // Update universe list for all clients
                this.broadcastUniverseList();

                console.log(`${client.player.name} joined universe "${universe.name}"`);
                break;
            }

            case MSG.LEAVE_UNIVERSE: {
                if (client.currentUniverse) {
                    this.leaveUniverse(client);
                    this.sendUniverseList(client);
                    this.broadcastUniverseList();
                }
                break;
            }

            case MSG.CREATE_UNIVERSE: {
                const doc = this.store.create({
                    name: data.name || 'Novo Universo',
                    description: data.description || '',
                    maxPlayers: data.maxPlayers || 16,
                    seed: data.seed || Math.floor(Math.random() * 100000),
                    bgColor: data.bgColor || { r: 0.15, g: 0.15, b: 0.2 },
                    ownerId: clientId,
                    ownerName: client.player.name,
                    scriptServer: data.scriptServer || '',
                    scriptClient: (data.scriptClient && data.scriptClient.ui)
                        ? data.scriptClient
                        : { ui: [] },
                    textures: data.textures || {}
                });
                const u = new Universe(doc.id, doc);
                u.setServer(this);
                this.universes.set(u.id, u);
                this.send(client, MSG.UNIVERSE_CREATED, { success: true, universe: u.getInfo() });
                this.broadcastUniverseList();
                console.log(`${client.player.name} criou universo "${u.name}" (#${u.id})`);
                break;
            }

            case MSG.UPDATE_UNIVERSE: {
                const universe = this.universes.get(data.universeId);
                if (!universe) { this.send(client, MSG.UNIVERSE_UPDATED, { success: false, error: 'not found' }); break; }
                if (universe.ownerId !== clientId) { this.send(client, MSG.UNIVERSE_UPDATED, { success: false, error: 'not owner' }); break; }
                const patch = {};
                if (data.name !== undefined) patch.name = data.name;
                if (data.description !== undefined) patch.description = data.description;
                if (data.maxPlayers !== undefined) patch.maxPlayers = data.maxPlayers;
                if (data.seed !== undefined) patch.seed = data.seed;
                if (data.bgColor !== undefined) patch.bgColor = data.bgColor;
                if (data.scriptServer !== undefined) patch.scriptServer = data.scriptServer;
                if (data.scriptClient !== undefined) patch.scriptClient = data.scriptClient;
                if (data.textures !== undefined) patch.textures = data.textures;
                this.store.update(universe.id, patch);
                // rebuild universe in place
                const fresh = new Universe(universe.id, this.store.get(universe.id));
                fresh.setServer(this);
                // preserve currently-connected players
                for (const [pid, player] of universe.world.players) {
                    fresh.world.players.set(pid, player);
                }
                this.universes.set(universe.id, fresh);
                this.send(client, MSG.UNIVERSE_UPDATED, { success: true, universe: fresh.getInfo() });
                this.broadcastUniverseList();
                console.log(`${client.player.name} atualizou universo #${universe.id}`);
                break;
            }

            case MSG.DELETE_UNIVERSE: {
                const universe = this.universes.get(data.universeId);
                if (!universe) { this.send(client, MSG.UNIVERSE_DELETED, { success: false, error: 'not found' }); break; }
                if (universe.ownerId !== clientId) { this.send(client, MSG.UNIVERSE_DELETED, { success: false, error: 'not owner' }); break; }
                this.store.remove(universe.id);
                this.universes.delete(universe.id);
                // kick players out
                for (const [cid] of universe.world.players) {
                    const c = this.clients.get(cid);
                    if (c && c.currentUniverse === universe) {
                        this.leaveUniverse(c);
                        this.sendUniverseList(c);
                    }
                }
                this.send(client, MSG.UNIVERSE_DELETED, { success: true, universeId: data.universeId });
                this.broadcastUniverseList();
                console.log(`${client.player.name} excluiu universo #${data.universeId}`);
                break;
            }

            case MSG.UPLOAD_TEXTURE: {
                if (!client.currentUniverse) break;
                const univ = client.currentUniverse;
                const name = (data.name || 'tex' + Date.now()).toString();
                const tex = {
                    mime: data.mime || 'image/png',
                    data: data.data || ''
                };
                univ.textures[name] = tex;
                if (univ.ownerId === clientId) {
                    const stored = this.store.get(univ.id);
                    if (stored) {
                        stored.textures = stored.textures || {};
                        stored.textures[name] = tex;
                        this.store.update(univ.id, { textures: stored.textures });
                    }
                } else {
                    // non-owner uploads runtime texture; keep in memory only
                    console.log(`${client.player.name} enviou textura ${name} (runtime)`);
                }
                this.send(client, MSG.TEXTURE_ACK, { success: true, name });
                // send to everyone in universe for runtime textures
                if (univ.ownerId !== clientId && univ.world.players.size > 0) {
                    univ.broadcastToAll(this, { type: MSG.TEXTURE_DATA, data: { name, mime: tex.mime, data: tex.data } });
                }
                break;
            }

            case MSG.UI_EVENT: {
                if (!client.currentUniverse) break;
                client.currentUniverse.fire('onUIEvent', {
                    id: clientId,
                    name: client.player.name,
                    elementId: data.elementId || '',
                    value: data.value
                });
                break;
            }
        }
    }

    leaveUniverse(client) {
        const universe = client.currentUniverse;
        if (!universe) return;

        universe.removePlayer(client.id);

        universe.broadcastToAll(this, {
            type: MSG.PLAYER_LEAVE,
            data: { clientId: client.id }
        }, client.id);

        client.currentUniverse = null;

        console.log(`${client.player.name} left universe "${universe.name}"`);
    }

    handleDisconnect(clientId) {
        const client = this.clients.get(clientId);
        if (!client) return;

        console.log(`${client.player.name} disconnected`);

        if (client.currentUniverse) {
            this.leaveUniverse(client);
        }

        this.clients.delete(clientId);
        this.broadcastUniverseList();
    }

    sendUniverseList(client) {
        const list = [];
        for (const [id, universe] of this.universes) {
            list.push(universe.getInfo());
        }
        this.send(client, MSG.UNIVERSE_LIST, { universes: list });
    }

    broadcastUniverseList() {
        const list = [];
        for (const [id, universe] of this.universes) {
            list.push(universe.getInfo());
        }
        for (const [clientId, client] of this.clients) {
            this.send(client, MSG.UNIVERSE_LIST, { universes: list });
        }
    }

    gameLoop() {
        if (!this.running) return;

        setInterval(() => {
            // Dispatch onTick to every universe's script (runs even when empty)
            for (const [id, universe] of this.universes) {
                universe.fire('onTick', 1 / this.tickRate);
            }

            // Process inputs and send updates for all active universes
            for (const [id, universe] of this.universes) {
                if (universe.world.players.size === 0) continue;

                // Process inputs for players in this universe
                for (const [clientId, player] of universe.world.players) {
                    const client = this.clients.get(clientId);
                    if (!client) continue;
                    player.updateFromInput(client.lastInput, 1 / this.tickRate);
                }

                // Run physics (gravity + ground collision)
                universe.world.physicsStep(1 / this.tickRate);

                // Send world updates to all players in this universe
                const players = {};
                for (const [pid, player] of universe.world.players) {
                    players[pid] = player.getState();
                }

                for (const [pid] of universe.world.players) {
                    const client = this.clients.get(pid);
                    if (!client) continue;
                    const player = universe.world.players.get(pid);
                    if (!player) continue;

                    this.send(client, MSG.WORLD_UPDATE, {
                        tick: universe.world.tick,
                        players,
                        self: player.getState()
                    });
                }
            }
        }, 1000 / this.tickRate);
    }

    send(client, type, data) {
        if (!client.socket || client.socket.destroyed) return;
        const payload = Buffer.from(JSON.stringify(data), 'utf8');
        const header = Buffer.alloc(6);
        header.writeUInt16BE(type, 0);
        header.writeUInt32BE(payload.length, 2);
        client.socket.write(Buffer.concat([header, payload]));
    }

    broadcast(msg, excludeId = null) {
        for (const [clientId, client] of this.clients) {
            if (clientId !== excludeId) {
                this.send(client, msg.type, msg.data);
            }
        }
    }

    getServerInfo() {
        return {
            host: this.onionAddress || ('127.0.0.1:' + this.port),
            port: this.port,
            onion: this.onionAddress || null,
            name: this.serverName || 'Zaviro Server',
            players: this.clients.size,
            maxPlayers: this.maxPlayers,
            pubKey: this.node ? this.node.identity.pubHex : null
        };
    }

    sendServerList(client) {
        const list = (this.peerServers || []).map((p) => ({
            onion: p.onion,
            name: p.name || 'unknown',
            region: p.region || '',
            pubKey: p.pubKey,
            icon: p.icon || null
        }));
        this.send(client, MSG.SERVER_LIST, { servers: list });
    }

    shutdown() {
        this.running = false;
        if (this.server) this.server.close();
        for (const [id, client] of this.clients) {
            client.socket.destroy();
        }
    }
}

module.exports = TcpGameServer;
