const TcpServer = require('./network/TcpServer');
const ServerNode = require('./federation/ServerNode');
const nconf = require('nconf');

nconf.argv().env().file({ file: __dirname + '/../config/default.json' });

const server = new TcpServer();

(async () => {
    server.start();

    const node = new ServerNode({
        server: server,
        servicePort: server.port
    });

    try {
        const directoryOnion = nconf.get('federation:directory:onion');
        const torEnabledVal = nconf.get('tor:enabled');
        const torEnabled = torEnabledVal !== false && torEnabledVal !== 'false' && torEnabledVal !== '0';
        await node.init({
            tor: torEnabled,
            socksPort: nconf.get('tor:socksPort') || 19050,
            directory: directoryOnion ? {
                onion: directoryOnion,
                socksPort: nconf.get('federation:directory:socksPort') || 19050
            } : null,
            announceIntervalSec: nconf.get('federation:announceIntervalSec') || 120
        });
        server.node = node;
        console.log('ServerNode ready.');
    } catch (e) {
        console.warn('[ServerNode] init failed (continuing in local/clearnet mode):', e.message);
    }

    process.on('SIGINT', () => {
        console.log('\nShutting down Zaviro server...');
        if (server.node) server.node.shutdown();
        server.shutdown();
        process.exit(0);
    });
})();
