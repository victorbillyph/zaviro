const WebSocket = require('ws');
const http = require('http');
const GameWorld = require('../game/GameWorld');
const nconf = require('nconf');

nconf.argv().env().file({ file: __dirname + '/../config/default.json' });

class GameServer {
    constructor() {
        this.port = nconf.get('server:port') || 8765;
        this.maxPlayers = nconf.get('server:maxPlayers') || 64;
        this.tickRate = nconf.get('server:tickRate') || 30;

        this.world = new GameWorld();
        this.clients = new Map();
        this.nextClientId = 1;

        this.httpServer = http.createServer((req, res) => {
            res.writeHead(200, { 'Content-Type': 'application/json' });
            res.end(JSON.stringify({
                status: 'online',
                players: this.clients.size,
                maxPlayers: this.maxPlayers,
                uptime: process.uptime()
            }));
        });

        this.wss = new WebSocket.Server({ server: this.httpServer });

        this.wss.on('connection', (ws, req) => {
            this.handleConnection(ws, req);
        });

        this.running = false;
    }

    start() {
        this.httpServer.listen(this.port, () => {
            console.log(`Zaviro Server running on port ${this.port}`);
            console.log(`Tick rate: ${this.tickRate} Hz`);
            console.log(`Max players: ${this.maxPlayers}`);
        });

        this.running = true;
        this.gameLoop();
    }

    handleConnection(ws, req) {
        if (this.clients.size >= this.maxPlayers) {
            ws.close(1013, 'Server full');
            return;
        }

        const clientId = this.nextClientId++;
        const client = {
            id: clientId,
            ws: ws,
            player: this.world.createPlayer(clientId, `Player${clientId}`),
            lastInput: { forward: 0, right: 0, up: 0, mouseX: 0, mouseY: 0 }
        };

        this.clients.set(clientId, client);

        console.log(`Player ${client.player.name} connected (ID: ${clientId})`);

        // Send welcome message
        this.sendMessage(ws, {
            type: 'welcome',
            data: {
                clientId: clientId,
                playerName: client.player.name
            }
        });

        // Send current world state
        this.sendMessage(ws, {
            type: 'worldState',
            data: this.world.getState()
        });

        // Broadcast join to others
        this.broadcast({
            type: 'playerJoin',
            data: {
                clientId: clientId,
                playerName: client.player.name,
                position: client.player.transform.position
            }
        }, clientId);

        ws.on('message', (data) => {
            this.handleMessage(clientId, data);
        });

        ws.on('close', () => {
            this.handleDisconnect(clientId);
        });
    }

    handleMessage(clientId, rawData) {
        try {
            const msg = JSON.parse(rawData.toString());
            const client = this.clients.get(clientId);
            if (!client) return;

            switch (msg.type) {
                case 'input':
                    client.lastInput = msg.data;
                    break;

                case 'chat':
                    this.broadcast({
                        type: 'chat',
                        data: {
                            clientId: clientId,
                            playerName: client.player.name,
                            message: msg.data.message
                        }
                    });
                    break;

                case 'placeBlock':
                    this.handlePlaceBlock(clientId, msg.data);
                    break;

                case 'breakBlock':
                    this.handleBreakBlock(clientId, msg.data);
                    break;
            }
        } catch (e) {
            console.error(`Invalid message from ${clientId}:`, e.message);
        }
    }

    handlePlaceBlock(clientId, data) {
        if (!data || !data.position || !data.color) return;

        const entity = this.world.createEntity(
            `Block_${Date.now()}`,
            data.position,
            { x: 1, y: 1, z: 1 },
            data.color,
            false
        );

        this.broadcast({
            type: 'entityCreate',
            data: entity
        });
    }

    handleBreakBlock(clientId, data) {
        if (!data || !data.entityId) return;

        if (this.world.removeEntity(data.entityId)) {
            this.broadcast({
                type: 'entityRemove',
                data: { entityId: data.entityId }
            });
        }
    }

    handleDisconnect(clientId) {
        const client = this.clients.get(clientId);
        if (!client) return;

        console.log(`Player ${client.player.name} disconnected`);

        this.world.removePlayer(clientId);
        this.clients.delete(clientId);

        this.broadcast({
            type: 'playerLeave',
            data: { clientId: clientId }
        });
    }

    gameLoop() {
        if (!this.running) return;

        const tickInterval = 1000 / this.tickRate;

        setInterval(() => {
            // Process inputs
            for (const [clientId, client] of this.clients) {
                const input = client.lastInput;
                const player = client.player;

                player.updateFromInput(input, 1 / this.tickRate);
            }

            // Physics step
            this.world.physicsStep(1 / this.tickRate);

            // Sync state to all clients
            const state = this.world.getState();
            this.broadcast({
                type: 'worldUpdate',
                data: {
                    players: state.players,
                    tick: state.tick
                }
            });
        }, tickInterval);
    }

    sendMessage(ws, msg) {
        if (ws.readyState === WebSocket.OPEN) {
            ws.send(JSON.stringify(msg));
        }
    }

    broadcast(msg, excludeId = null) {
        const data = JSON.stringify(msg);
        for (const [clientId, client] of this.clients) {
            if (clientId !== excludeId && client.ws.readyState === WebSocket.OPEN) {
                client.ws.send(data);
            }
        }
    }

    shutdown() {
        this.running = false;
        this.wss.close();
        this.httpServer.close();
    }
}

module.exports = GameServer;
