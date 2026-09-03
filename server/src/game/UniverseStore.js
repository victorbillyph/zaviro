'use strict';
/**
 * UniverseStore: persistence + CRUD for player-created universes.
 *
 * Player universes are stored as JSON files on disk so they survive restarts.
 * Built-in/default universes are NOT stored here (they come from
 * createDefaultUniverses in TcpServer).
 *
 * Storage layout: data/universes/<id>.json
 */

const fs = require('fs');
const path = require('path');

class UniverseStore {
    constructor({ dir = path.join(__dirname, '../../data/universes') } = {}) {
        this.dir = dir;
        fs.mkdirSync(this.dir, { recursive: true });
        this.byId = new Map(); // id -> universe document (raw JSON)
        this.nextId = 1;
        this.loadAll();
    }

    loadAll() {
        if (!fs.existsSync(this.dir)) return;
        for (const f of fs.readdirSync(this.dir)) {
            if (!f.endsWith('.json')) continue;
            try {
                const doc = JSON.parse(fs.readFileSync(path.join(this.dir, f), 'utf8'));
                this.byId.set(doc.id, doc);
                if (doc.id >= this.nextId) this.nextId = doc.id + 1;
            } catch (e) {
                console.warn('[UniverseStore] skip bad file', f, e.message);
            }
        }
    }

    idForFile(id) {
        return path.join(this.dir, id + '.json');
    }

    create(doc) {
        const id = this.nextId++;
        const now = Date.now();
        const full = Object.assign({
            id,
            name: 'Novo Universo',
            description: '',
            maxPlayers: 16,
            seed: Math.floor(Math.random() * 100000),
            bgColor: { r: 0.15, g: 0.15, b: 0.2 },
            ownerId: null,
            ownerName: '',
            scriptServer: '',
            scriptClient: { ui: [] },
            textures: {},
            createdAt: now,
            updatedAt: now
        }, doc, { id, createdAt: now, updatedAt: now });
        this.byId.set(id, full);
        this.persist(id);
        return full;
    }

    update(id, patch) {
        const doc = this.byId.get(id);
        if (!doc) return null;
        Object.assign(doc, patch, { id, updatedAt: Date.now() });
        this.persist(id);
        return doc;
    }

    remove(id) {
        this.byId.delete(id);
        try { fs.unlinkSync(this.idForFile(id)); } catch {}
    }

    get(id) {
        return this.byId.get(id) || null;
    }

    getAll() {
        return Array.from(this.byId.values());
    }

    persist(id) {
        const doc = this.byId.get(id);
        if (!doc) return;
        fs.writeFileSync(this.idForFile(id), JSON.stringify(doc, null, 2));
    }
}

module.exports = UniverseStore;
