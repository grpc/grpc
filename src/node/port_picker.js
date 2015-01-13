var net = require('net');

/**
 * Finds a free port that a server can bind to, in the format
 * "address:port"
 * @param {function(string)} cb The callback that should execute when the port
 *     is available
 */
function nextAvailablePort(cb) {
  var server = net.createServer();
  server.listen(function() {
    var address = server.address();
    server.close(function() {
      cb(address.address + ':' + address.port.toString());
    });
  });
}

exports.nextAvailablePort = nextAvailablePort;
