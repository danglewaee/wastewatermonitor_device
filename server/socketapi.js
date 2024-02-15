
const option = {
    allowEIO3: true,
    cors: {
        origin: "*",
        methods: ["GET", "POST"],
        transports: ["websocket", "polling"],
        credentials: true,
    },
}

const io = require("socket.io")(option);

const socketapi = {
    io: io
}

io.on("connection", (socket) => {
    console.log("[INFO] new connection: [" + socket.id + "]");
    socket.on("message", (data) => {
        console.log(`message from ${data.clientID} via socket id: ${socket.id} on topic message`);
        socket.broadcast.emit("message", data);
    });

    socket.on('/esp/measure', (data) => {
        console.log(`message from ${data.clientID} via socket id: ${socket.id} on topic /esp/measure`);
        socket.broadcast.emit('/web/measure', data);
    })

    socket.on("/web/control", (data) => {
        console.log(`message from ${data.clientID} via socket id: ${socket.id} on topic /web/control`);
        console.log(data);
        socket.broadcast.emit("/esp/control", data);
    });

    socket.on("esp/other", (data) => {
        console.log(`message from ${data.clientID} via socket id: ${socket.id} on topic esp/other`);
        socket.broadcast.emit("web/other", data);
        console.log(data);
    });
    /**************************** */
    //xu ly chung
    socket.on("reconnect", function () {
        console.log("[" + socket.id + "] reconnect.");
    });
    socket.on("disconnect", () => {
        console.log("[" + socket.id + "] disconnect.");
    });
    socket.on("connect_error", (err) => {
        console.log(err.stack);
    });
})

module.exports = socketapi;