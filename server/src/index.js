const TcpServer = require('./network/TcpServer');

const server = new TcpServer();

process.on('SIGINT', () => {
    console.log('\nShutting down Zaviro server...');
    server.shutdown();
    process.exit(0);
});

server.start();
