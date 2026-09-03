'use strict';
/**
 * ScriptHost: sandboxed execution of a universe's server-side game script.
 *
 * The developer writes JavaScript in `vm`-safe code. The host injects a
 * controlled `world`/`host` API and dispatches lifecycle events. All access to
 * the outside system goes through the injected host object — the script cannot
 * require(), use the network, or touch the filesystem.
 *
 * Available globals/functions in the script:
 *   events.onInit()                 when server loads the universe
 *   events.onJoin(player)           player joined
 *   events.onLeave(player)          player left
 *   events.onTick(dt)               every tick (seconds)
 *   events.onChat(player, message)  chat message from player
 *   events.onPlaceBlock(player, data)
 *   events.onBreakBlock(player, data)
 *   events.onUIEvent(player, event)
 *   host: world.createBlock(...), world.removeBlock(id),
 *         world.broadcast(type, data), world.broadcastTo(playerId, type, data),
 *         state (persistent per-run), log(...)
 */

const vm = require('vm');

class ScriptHost {
    constructor({ source, logger = console }) {
        this.source = source || '';
        this.log = logger;
        this.state = {};
        this._handlers = {};
        this._world = null; // set by caller
        this._ready = false;
    }

    /**
     * Compile + set up the context. Must be called after constructing.
     * @param {object} deps { createBlock, removeBlock, broadcast, broadcastTo }
     */
    load(deps) {
        this._world = deps;
        const sandbox = {
            state: this.state,
            events: {},
            world: {
                createBlock: (opts) => this._callHost('createBlock', opts),
                removeBlock: (id) => this._callHost('removeBlock', id),
                broadcast: (type, data) => this._callHost('broadcast', type, data),
                broadcastTo: (playerId, type, data) => this._callHost('broadcastTo', playerId, type, data),
                players: () => this._callHost('players'),
                getState: () => this._callHost('getState')
            },
            log: (...a) => { try { console.log('[UniverseScript]', ...a); } catch {} },
            setTimeout: () => { throw new Error('setTimeout is disabled in universe scripts'); },
            setInterval: () => { throw new Error('setInterval is disabled in universe scripts'); },
            require: () => { throw new Error('require is disabled in universe scripts'); },
            process: null,
            global: null,
            Buffer: null
        };

        // Capture any `events.xxx = fn` the script assigns.
        const handlerProxy = new Proxy({}, {
            set: (target, prop, value) => {
                if (typeof value === 'function') target[prop] = value;
                return true;
            }
        });
        sandbox.events = handlerProxy;
        this._handlerProxy = handlerProxy;

        try {
            // Trampoline: the script may attach to `events` either declaratively
            // (events.onTick = ...) via the proxy, which we then use.
            vm.createContext(sandbox);
            this._context = sandbox;
            vm.runInContext(this.source, sandbox, { filename: 'universe-script.js', timeout: 1000 });
            // Copy captured handlers
            this._handlers = { ...this._handlerProxy };
            // Also allow the script to define `host.onInit()` style if it used
            // a plain object named `events`.
            this._ready = true;
        } catch (e) {
            this.log.error('[ScriptHost] load error:', e.message);
            this._handlers = {};
            this._ready = false;
        }
        return this;
    }

    get ready() { return this._ready; }

    _callHost(name, ...args) {
        if (!this._world || typeof this._world[name] !== 'function') {
            throw new Error('host function not available: ' + name);
        }
        return this._world[name](...args);
    }

    async emit(event, ...args) {
        if (!this._ready) return;
        const fn = this._handlers[event];
        if (typeof fn !== 'function') return;
        try {
            await fn.call(this._context, ...args);
        } catch (e) {
            this.log.error('[ScriptHost] error in on' + event + ':', e.message);
        }
    }

    destroy() {
        this._ready = false;
        this._handlers = {};
        this._context = null;
    }
}

module.exports = ScriptHost;
