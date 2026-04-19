// WebSocket连接
let ws = null;
let username = "";
let currentRoom = "Global";

// 初始化
function init() {
    // 连接WebSocket服务器
    ws = new WebSocket("ws://localhost:8080");
    
    ws.onopen = function() {
        console.log("WebSocket connected");
    };
    
    ws.onmessage = function(event) {
        handleMessage(event.data);
    };
    
    ws.onclose = function() {
        console.log("WebSocket disconnected");
    };
    
    ws.onerror = function(error) {
        console.error("WebSocket error:", error);
    };
    
    // 绑定事件
    document.getElementById("login-btn").addEventListener("click", login);
    document.getElementById("register-btn").addEventListener("click", register);
    document.getElementById("send-btn").addEventListener("click", sendMessage);
    document.getElementById("message-input").addEventListener("keypress", function(e) {
        if (e.key === "Enter") {
            sendMessage();
        }
    });
    document.getElementById("file-btn").addEventListener("click", function() {
        document.getElementById("file-input").click();
    });
    document.getElementById("file-input").addEventListener("change", sendFile);
    document.getElementById("create-room-btn").addEventListener("click", createRoom);
    document.getElementById("join-room-btn").addEventListener("click", joinRoom);
    document.getElementById("leave-room-btn").addEventListener("click", leaveRoom);
}

// 登录
function login() {
    const username = document.getElementById("username").value;
    const password = document.getElementById("password").value;
    
    if (username) {
        ws.send(`/login ${username} ${password}`);
        this.username = username;
    }
}

// 注册
function register() {
    const username = document.getElementById("username").value;
    const password = document.getElementById("password").value;
    
    if (username && password) {
        ws.send(`/register ${username} ${password}`);
        this.username = username;
    }
}

// 发送消息
function sendMessage() {
    const message = document.getElementById("message-input").value;
    
    if (message) {
        ws.send(message);
        document.getElementById("message-input").value = "";
    }
}

// 发送文件
function sendFile() {
    const fileInput = document.getElementById("file-input");
    const file = fileInput.files[0];
    
    if (file) {
        const reader = new FileReader();
        reader.onload = function(e) {
            const fileData = e.target.result;
            ws.send(`/file ${file.name} ${fileData}`);
        };
        reader.readAsDataURL(file);
        fileInput.value = "";
    }
}

// 创建聊天室
function createRoom() {
    const roomName = prompt("Enter room name:");
    const isPrivate = confirm("Make this room private?");
    let password = "";
    
    if (isPrivate) {
        password = prompt("Enter room password:");
    }
    
    if (roomName) {
        ws.send(`/createroom ${roomName} ${isPrivate ? "private" : ""} ${password}`);
    }
}

// 加入聊天室
function joinRoom() {
    const roomName = prompt("Enter room name:");
    const password = prompt("Enter room password (if any):");
    
    if (roomName) {
        ws.send(`/joinroom ${roomName} ${password}`);
        currentRoom = roomName;
        document.getElementById("current-room").textContent = roomName;
    }
}

// 离开聊天室
function leaveRoom() {
    ws.send("/leaveroom");
    currentRoom = "Global";
    document.getElementById("current-room").textContent = currentRoom;
}

// 处理接收到的消息
function handleMessage(data) {
    const messageList = document.getElementById("message-list");
    const messageElement = document.createElement("div");
    messageElement.className = "message";
    
    // 解析消息
    if (data.startsWith("MSG")) {
        // 聊天消息
        const parts = data.split("|");
        if (parts.length >= 5) {
            const sender = parts[1];
            const content = parts[4];
            
            messageElement.innerHTML = `
                <div class="sender">${sender}</div>
                <div class="content">${content}</div>
            `;
        }
    } else if (data.startsWith("SYS")) {
        // 系统消息
        const content = data.substring(4);
        messageElement.innerHTML = `
            <div class="sender">System</div>
            <div class="content">${content}</div>
        `;
        messageElement.style.backgroundColor = "#e3f2fd";
    } else if (data.startsWith("ROOM_LIST")) {
        // 聊天室列表
        const parts = data.split("|");
        if (parts.length >= 2) {
            const roomCount = parseInt(parts[1]);
            const roomList = document.getElementById("room-list");
            roomList.innerHTML = '<li class="room-item active">Global</li>';
            
            for (let i = 2; i < 2 + roomCount; i++) {
                const roomName = parts[i];
                const roomItem = document.createElement("li");
                roomItem.className = "room-item";
                roomItem.textContent = roomName;
                roomItem.addEventListener("click", function() {
                    joinRoom(roomName);
                });
                roomList.appendChild(roomItem);
            }
        }
    }
    
    messageList.appendChild(messageElement);
    messageList.scrollTop = messageList.scrollHeight;
}

// 页面加载完成后初始化
window.onload = init;
