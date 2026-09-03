const net = require('net');
const nconf = require('nconf');
const Player = require('../game/Player');
const Universe = require('../game/Universe');

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

        this.running = false;

        this.createDefaultUniverses();
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
            this.universes.set(id, new Universe(id, cfg));
        }
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
            playerName: client.player.name
        });

        // Send universe list
        this.sendUniverseList(client);

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
                }
                break;

            case MSG.BREAK_BLOCK:
                if (client.currentUniverse && data && data.entityId !== undefined) {
                    if (client.currentUniverse.world.removeEntity(data.entityId)) {
                        client.currentUniverse.broadcastToAll(this, {
                            type: MSG.ENTITY_REMOVE,
                            data: { entityId: data.entityId }
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
            // Process inputs and send updates for all active universes
            for (const [id, universe] of this.universes) {
                if (universe.world.players.size === 0) continue;

                // Process inputs for players in this universe
                for (const [clientId, player] of universe.world.players) {
                    const client = this.clients.get(clientId);
                    if (!client) continue;
                    player.updateFromInput(client.lastInput, 1 / this.tickRate);
                }

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

    shutdown() {
        this.running = false;
        if (this.server) this.server.close();
        for (const [id, client] of this.clients) {
            client.socket.destroy();
        }
    }
}

module.exports = TcpGameServer;
