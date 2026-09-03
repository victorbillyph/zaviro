const nconf = require('nconf');
const Player = require('./Player');
const Terrain = require('./Terrain');

class GameWorld {
    constructor() {
        this.gravity = nconf.get('world:gravity') || -20.0;
        this.tick = 0;

        this.players = new Map();
        this.entities = new Map();
        this.nextEntityId = 1;

        this.loadDefaultWorld();
    }

    loadDefaultWorld() {
        // Some decorative blocks placed on top of the terrain
        const blocks = [
            { x: 2, y: 3, z: 2, c: { r: 0.9, g: 0.2, b: 0.2 } },
            { x: -2, y: 3, z: -2, c: { r: 0.2, g: 0.2, b: 0.9 } },
            { x: 0, y: 3, z: -3, c: { r: 0.9, g: 0.9, b: 0.2 } },
            { x: 3, y: 4, z: 0, c: { r: 0.2, g: 0.9, b: 0.9 } },
            { x: -3, y: 5, z: 3, c: { r: 0.9, g: 0.5, b: 0.9 } },
        ];
        blocks.forEach(b => {
            this.createEntity(
                `Block${b.x}${b.z}`,
                { x: b.x, y: b.y, z: b.z },
                { x: 1, y: 1, z: 1 },
                b.c,
                false
            );
        });
    }

    createPlayer(id, name) {
        const player = new Player(id, name);
        player.heightAt = Terrain.terrainHeightAt;
        player.transform.position.y = Terrain.terrainHeightAt(
            player.transform.position.x,
            player.transform.position.z
        ) + 5.0;
        this.players.set(id, player);
        return player;
    }

    removePlayer(id) {
        this.players.delete(id);
    }

    createEntity(name, position, scale, color, isStatic) {
        const id = this.nextEntityId++;
        const entity = {
            id,
            name,
            position,
            scale,
            color,
            isStatic: isStatic !== false
        };
        this.entities.set(id, entity);
        return entity;
    }

    removeEntity(id) {
        return this.entities.delete(id);
    }

    physicsStep(dt) {
        this.tick++;

        for (const [id, player] of this.players) {
            player.physicsStep(dt, this.gravity);
        }
    }

    getState() {
        const players = {};
        for (const [id, player] of this.players) {
            players[id] = player.getState();
        }

        const entities = {};
        for (const [id, entity] of this.entities) {
            entities[id] = entity;
        }

        return {
            tick: this.tick,
            players,
            entities
        };
    }
}

module.exports = GameWorld;
