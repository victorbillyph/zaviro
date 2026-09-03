const GameWorld = require('./GameWorld');
const Terrain = require('./Terrain');

class Universe {
    constructor(id, config) {
        this.id = id;
        this.name = config.name || `Universe ${id}`;
        this.description = config.description || '';
        this.maxPlayers = config.maxPlayers || 32;
        this.seed = config.seed || Math.floor(Math.random() * 100000);
        this.bgColor = config.bgColor || { r: 0.15, g: 0.15, b: 0.2 };

        this.world = new GameWorld();

        this.loadContent();
    }

    loadContent() {
        // Each universe can have different blocks/items
        this.world.loadDefaultWorld();
    }

    addPlayer(clientId, player) {
        player.heightAt = Terrain.terrainHeightAt;
        player.transform.position = {
            x: 0,
            y: Terrain.terrainHeightAt(0, 0) + 5.0,
            z: 8
        };
        player.yaw = -90;
        player.pitch = 0;
        player.grounded = false;
        player.velocity = { x: 0, y: 0, z: 0 };
        this.world.players.set(clientId, player);
    }

    removePlayer(clientId) {
        this.world.players.delete(clientId);
    }

    tick() {
        this.world.physicsStep(1 / this.tickRate);
    }

    getInfo() {
        return {
            id: this.id,
            name: this.name,
            description: this.description,
            players: this.world.players.size,
            maxPlayers: this.maxPlayers,
            bgColor: this.bgColor,
            seed: this.seed
        };
    }

    broadcastToAll(server, msg, excludeId = null) {
        for (const [clientId] of this.world.players) {
            if (clientId === excludeId) continue;
            const client = server.clients.get(clientId);
            if (client) {
                server.send(client, msg.type, msg.data);
            }
        }
    }
}

module.exports = Universe;
