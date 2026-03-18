module.exports = function(RED) {
    "use strict";
    var http = require("http");

    function ESP32SseNode(config) {
        RED.nodes.createNode(this, config);
        var node = this;

        // Config from editor
        var host = config.host || "10.1.32.20";
        var port = parseInt(config.port) || 1800;
        var path = config.path || "/api/events?subscribe=registers&hr=0-31&coils=0-255";
        var username = node.credentials.username || "";
        var password = node.credentials.password || "";
        var autoConnect = config.autoConnect !== false;
        var reconnectInterval = parseInt(config.reconnectInterval) || 5000;
        var filterEvent = config.filterEvent || "all";

        // State
        var currentReq = null;
        var currentRes = null;
        var reconnectTimer = null;
        var closing = false;

        // Type mapping
        var typeMap = {
            "hr": "holding",
            "ir": "input",
            "coil": "coils",
            "di": "dis input"
        };

        function buildAuth() {
            if (username && password) {
                return "Basic " + Buffer.from(username + ":" + password).toString("base64");
            }
            return "";
        }

        function isConnected() {
            return currentReq && !currentReq.destroyed;
        }

        function disconnect() {
            if (reconnectTimer) {
                clearTimeout(reconnectTimer);
                reconnectTimer = null;
            }
            if (currentReq) {
                currentReq.destroy();
                currentReq = null;
            }
            currentRes = null;
        }

        function scheduleReconnect() {
            if (closing) return;
            if (reconnectTimer) return;
            reconnectTimer = setTimeout(function() {
                reconnectTimer = null;
                if (!closing && !isConnected()) {
                    connect();
                }
            }, reconnectInterval);
            node.status({fill:"yellow", shape:"ring", text:"reconnecting in " + (reconnectInterval/1000) + "s"});
        }

        function connect() {
            if (isConnected()) {
                node.status({fill:"green", shape:"dot", text:"already connected"});
                return;
            }

            disconnect();

            var options = {
                hostname: host,
                port: port,
                path: path,
                method: "GET",
                headers: {}
            };

            var auth = buildAuth();
            if (auth) {
                options.headers["Authorization"] = auth;
            }

            node.status({fill:"yellow", shape:"dot", text:"connecting..."});

            var req = http.request(options, function(res) {
                // Handle non-200 responses
                if (res.statusCode !== 200) {
                    var body = "";
                    res.on("data", function(chunk) { body += chunk.toString(); });
                    res.on("end", function() {
                        var errMsg = "HTTP " + res.statusCode;
                        try {
                            var err = JSON.parse(body);
                            errMsg = err.error || errMsg;
                        } catch(e) {}
                        currentReq = null;
                        currentRes = null;
                        node.status({fill:"red", shape:"ring", text:errMsg});
                        node.error("ESP32 SSE: " + errMsg);
                        scheduleReconnect();
                    });
                    return;
                }

                currentRes = res;
                node.status({fill:"green", shape:"dot", text:"connected"});

                var buffer = "";
                var currentEvent = "";

                res.on("data", function(chunk) {
                    buffer += chunk.toString();
                    var parts = buffer.split("\n\n");
                    buffer = parts.pop();

                    for (var p = 0; p < parts.length; p++) {
                        var lines = parts[p].split("\n");
                        var evtType = "";
                        var evtData = "";

                        for (var i = 0; i < lines.length; i++) {
                            var line = lines[i].trim();
                            if (line.indexOf("event: ") === 0) {
                                evtType = line.substring(7);
                            } else if (line.indexOf("data: ") === 0) {
                                evtData = line.substring(6);
                            }
                        }

                        if (!evtType || !evtData) continue;

                        // Filter events
                        if (filterEvent !== "all" && evtType !== filterEvent) continue;

                        try {
                            var data = JSON.parse(evtData);

                            if (evtType === "register") {
                                node.send([{
                                    payload: {
                                        register: data.addr,
                                        value: data.value,
                                        type: typeMap[data.type] || data.type
                                    },
                                    topic: "register/" + (data.type || "unknown") + "/" + data.addr,
                                    event: evtType
                                }, null]);
                            } else if (evtType === "counter") {
                                node.send([{
                                    payload: {
                                        id: data.id,
                                        value: data.value,
                                        enabled: data.enabled,
                                        running: data.running
                                    },
                                    topic: "counter/" + data.id,
                                    event: evtType
                                }, null]);
                            } else if (evtType === "timer") {
                                node.send([{
                                    payload: {
                                        id: data.id,
                                        enabled: data.enabled,
                                        mode: data.mode,
                                        output: data.output
                                    },
                                    topic: "timer/" + data.id,
                                    event: evtType
                                }, null]);
                            } else if (evtType === "heartbeat") {
                                node.status({fill:"green", shape:"dot",
                                    text:"connected | heap:" + data.heap_free + " clients:" + data.sse_clients});
                                // Heartbeat on output 2
                                node.send([null, {
                                    payload: data,
                                    topic: "heartbeat",
                                    event: evtType
                                }]);
                            } else if (evtType === "connected") {
                                node.send([null, {
                                    payload: data,
                                    topic: "connected",
                                    event: evtType
                                }]);
                            }
                        } catch(e) {
                            node.warn("SSE parse error: " + e.message);
                        }
                    }
                });

                res.on("end", function() {
                    currentReq = null;
                    currentRes = null;
                    node.status({fill:"red", shape:"ring", text:"disconnected"});
                    scheduleReconnect();
                });

                res.on("error", function(e) {
                    currentReq = null;
                    currentRes = null;
                    node.status({fill:"red", shape:"ring", text:e.message});
                    scheduleReconnect();
                });
            });

            req.on("error", function(e) {
                currentReq = null;
                currentRes = null;
                node.status({fill:"red", shape:"ring", text:e.message});
                node.error("ESP32 SSE: " + e.message);
                scheduleReconnect();
            });

            req.setTimeout(0); // No timeout for SSE stream
            currentReq = req;
            req.end();
        }

        // Handle input messages
        node.on("input", function(msg) {
            if (msg.payload === "disconnect" || msg.payload === "stop") {
                disconnect();
                node.status({fill:"grey", shape:"ring", text:"stopped"});
            } else if (msg.payload === "reconnect" || msg.payload === "connect" || msg.payload === "start") {
                connect();
            } else {
                // Default: connect if not connected
                if (!isConnected()) {
                    connect();
                }
            }
        });

        // Cleanup on close
        node.on("close", function(done) {
            closing = true;
            disconnect();
            done();
        });

        // Auto-connect on deploy
        if (autoConnect) {
            connect();
        }
    }

    RED.nodes.registerType("esp32-sse", ESP32SseNode, {
        credentials: {
            username: {type: "text"},
            password: {type: "password"}
        }
    });
};
