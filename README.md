# C Code Visualizer

A web based, real time debugging and visualization tool for C programming. It provides an interactive environment to write C code, compile it, and step through the execution line by line while observing the call stack, local variables, and console output.

## What It Does
The C Code Visualizer bridges the gap between complex command line debuggers and user friendly web interfaces. It allows developers and students to
- Write C code directly in the browser with a modern IDE like interface.
- Compile and execute code.
- Perform step by step debugging (Step Over, Step Into, Continue, Stop).
- Visualize real time changes in local variables and the function call stack.
- View standard output (`printf`) integrated directly into a console panel.

## How We Built It
The architecture is divided into three highly decoupled components, communicating over HTTP and WebSockets for low latency performance

1. **Frontend (React, TypeScript & Vite):** 
   A responsive UI built with React. It utilizes a custom hook (`useDebugSession`) to manage WebSocket connections, parse incoming execution states, and maintain a history buffer for instantaneous "step back" functionality.

2. **Middleware Server (Python FastAPI):** 
   A thin, asynchronous web server. It exposes REST endpoints for code compilation and WebSocket endpoints for real time debugging. It securely spawns, manages, and cleans up isolated instances of the C++ execution engine for each user session.

3. **Execution Engine (C++ & GDB):** 
   The core backend component responsible for process management. It runs as a subprocess communicating with the Python server via I/O pipes. The engine wraps **GDB (GNU Debugger)** using the Machine Interface (MI) protocol. It parses GDB's raw outputs, extracts the current execution frame, call stack, and variables, and serializes this program state into structured JSON to be streamed back to the frontend.

## How to Run Locally

We need three terminal windows to run the different components of this project.

**1. Build the C++ Execution Engine**
```bash
cd backend/engine
mkdir -p build && cd build
cmake .. && make
```

**2. Start Backend Server**
```bash
cd backend
python3 -m venv venv
source venv/bin/activate
pip install -r requirements.txt
python3 server.py
```

**3. Start Frontend**
```bash
cd frontend
npm install
npm run dev
```
