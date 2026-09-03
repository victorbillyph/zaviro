const GameWorld = require('./GameWorld');
const Terrain = require('./Terrain');
const ScriptHost = require('./ScriptHost');

class Universe {
    /**
     * @param {number} id
     * @param {object} config
     *   { name, description, maxPlayers, seed, bgColor, ownerId, ownerName,
     *     scriptServer, scriptClient (UI def), textures, server }
     */
    constructor(id, config) {
        this.id = id;
        this.name = config.name || `Universe ${id}`;
        this.description = config.description || '';
        this.maxPlayers = config.maxPlayers || 32;
        this.seed = config.seed || Math.floor(Math.random() * 100000);
        this.bgColor = config.bgColor || { r: 0.15, g: 0.15, b: 0.2 };
        this.ownerId = config.ownerId || null;
        this.ownerName = config.ownerName || '';
        this.scriptServer = config.scriptServer || '';
        this.scriptClient = config.scriptClient || null; // declarative UI + client mini-script
        this.textures = config.textures || {}; // {name: {mime, data(base64)}}
        this.createdAt = config.createdAt || Date.now();
        this.updatedAt = config.updatedAt || Date.now();

        this.server = config.server || null;
        this.world = new GameWorld();
        this.script = null;

        this.loadContent();
    }

    setServer(server) {
        this.server = server;
        this.setupScript(); // loads + fires onInit now that server is available
    }

    loadContent() {
        if (this.scriptServer) return; // scripted universes start empty
        this.world.loadDefaultWorld();
    }

    setupScript() {
        if (!this.scriptServer) return;
        const hostFns = {
            createBlock: (opts) => this.hostCreateBlock(opts),
            removeBlock: (id) => this.hostRemoveBlock(id),
            broadcast: (type, data) => this.hostBroadcast(type, data),
            broadcastTo: (playerId, type, data) => this.hostBroadcastTo(playerId, type, data),
            players: () => this.hostGetPlayers(),
            getState: () => this.world.getState()
        };
        this.script = new ScriptHost({ source: this.scriptServer, logger: console });
        this.script.load(hostFns);
        if (this.script.ready) {
            this.fire('onInit');
        }
    }

    // ---- host functions exposed to the script ----
    hostCreateBlock(opts) {
        if (!this.server) return null;
        opts = opts || {};
        const entity = this.world.createEntity(
            opts.name || ('Block_' + Date.now()),
            opts.position || { x: 0, y: 3, z: 0 },
            opts.scale || { x: 1, y: 1, z: 1 },
            opts.color || { r: 0.8, g: 0.8, b: 0.8 },
            opts.static !== false
        );
        this.broadcastToAll(this.server, { type: 105, data: entity }); // ENTITY_CREATE
        return entity;
    }

    hostRemoveBlock(id) {
        if (!this.server) return false;
        if (!this.world.removeEntity(id)) return false;
        this.broadcastToAll(this.server, { type: 106, data: { entityId: id } }); // ENTITY_REMOVE
        return true;
    }

    hostBroadcast(type, data) {
        if (!this.server) return;
        this.broadcastToAll(this.server, { type, data });
    }

    hostBroadcastTo(playerId, type, data) {
        if (!this.server) return;
        const client = this.server.clients.get(playerId);
        if (client) this.server.send(client, type, data);
    }

    hostGetPlayers() {
        if (!this.server) return [];
        const arr = [];
        for (const [cid, player] of this.world.players) {
            arr.push({ id: cid, name: player.name, position: player.transform.position });
        }
        return arr;
    }

    // ---- lifecycle events ----
    async fire(event, ...args) {
        if (this.script && this.script.ready) {
            await this.script.emit(event, ...args);
        }
    }

    addPlayer(clientId, player) {
        player.heightAt = Terrain.terrainHeightAt;
        const y = Terrain.terrainHeightAt(0, 0) + 5.0;
        player.transform.position = { x: 0, y, z: 8 };
        player.yaw = -90;
        player.pitch = 0;
        player.grounded = false;
        player.velocity = { x: 0, y: 0, z: 0 };
        this.world.players.set(clientId, player);
        this.fire('onJoin', { id: clientId, name: player.name, position: player.transform.position });
    }

    removePlayer(clientId) {
        this.world.players.delete(clientId);
        this.fire('onLeave', { id: clientId });
    }

    tick(dt) {
        this.world.physicsStep(dt);
        this.fire('onTick', dt);
    }

    getInfo() {
        return {
            id: this.id,
            name: this.name,
            description: this.description,
            players: this.world.players.size,
            maxPlayers: this.maxPlayers,
            bgColor: this.bgColor,
            seed: this.seed,
            ownerId: this.ownerId,
            ownerName: this.ownerName,
            scripted: !!this.scriptServer,
            hasUI: !!(this.scriptClient && this.scriptClient.ui)
        };
    }

    // Full definition sent to owner/editor (includes script, UI, textures).
    getDefinition() {
        return {
            id: this.id,
            name: this.name,
            description: this.description,
            maxPlayers: this.maxPlayers,
            seed: this.seed,
            bgColor: this.bgColor,
            ownerId: this.ownerId,
            ownerName: this.ownerName,
            scriptServer: this.scriptServer,
            scriptClient: this.scriptClient,
            textures: Object.fromEntries(Object.entries(this.textures).filter(([k]) => k)) // names only (not data) for listing
        };
    }

    broadcastToAll(server, msg, excludeId = null) {
        for (const [clientId] of this.world.players) {
            if (clientId === excludeId) continue;
            const client = server.clients.get(clientId);
            if (client) server.send(client, msg.type, msg.data);
        }
    }
}

module.exports = Universe;
