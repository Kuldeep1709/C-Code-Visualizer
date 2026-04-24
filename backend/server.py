"""
C Code Visualizer — Thin Web Server

A minimal Python FastAPI server that bridges WebSocket connections
from the browser to the C++ visualizer engine via subprocess pipes.

The C++ engine handles ALL core logic (GDB control, MI parsing, state management).
This server only forwards JSON messages between the browser and the engine.
"""

import asyncio
import json
import os
import shutil
import uuid

from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel

app = FastAPI(title="C Code Visualizer", version="1.0.0")

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# Path to the compiled C++ engine binary
ENGINE_PATH = os.path.join(os.path.dirname(__file__), "engine", "build", "visualizer")
TEMP_DIR = os.path.join(os.path.dirname(__file__), "temp")
os.makedirs(TEMP_DIR, exist_ok=True)

# Active sessions: session_id -> {process, work_dir}
sessions: dict[str, dict] = {}


class CompileRequest(BaseModel):
    source_code: str


@app.post("/api/compile")
async def compile_code(req: CompileRequest):
    """Compile C source code using the C++ engine."""
    session_id = str(uuid.uuid4())[:8]
    work_dir = os.path.join(TEMP_DIR, session_id)
    os.makedirs(work_dir, exist_ok=True)

    try:
        # Spawn the C++ engine process
        process = await asyncio.create_subprocess_exec(
            ENGINE_PATH, work_dir,
            stdin=asyncio.subprocess.PIPE,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE,
        )

        # Send compile command
        cmd = json.dumps({"cmd": "compile", "source": req.source_code}) + "\n"
        process.stdin.write(cmd.encode())
        await process.stdin.drain()

        # Read response
        response_line = await asyncio.wait_for(process.stdout.readline(), timeout=15)
        response = json.loads(response_line.decode())

        if response.get("success"):
            sessions[session_id] = {
                "process": process,
                "work_dir": work_dir,
                "source_code": req.source_code,
            }
            response["session_id"] = session_id
            return response
        else:
            # Compilation failed — clean up
            process.stdin.write(b'{"cmd":"quit"}\n')
            await process.stdin.drain()
            process.kill()
            shutil.rmtree(work_dir, ignore_errors=True)
            return response

    except Exception as e:
        shutil.rmtree(work_dir, ignore_errors=True)
        return {"success": False, "error": str(e)}


@app.websocket("/ws/{session_id}")
async def debug_session(websocket: WebSocket, session_id: str):
    """WebSocket endpoint — forwards commands to the C++ engine."""
    await websocket.accept()

    session = sessions.get(session_id)
    if not session:
        await websocket.send_json({"type": "error", "message": "Invalid session ID"})
        await websocket.close()
        return

    process: asyncio.subprocess.Process = session["process"]

    try:
        # Send start command to engine
        process.stdin.write(b'{"cmd":"start"}\n')
        await process.stdin.drain()

        # Read initial state
        response_line = await asyncio.wait_for(process.stdout.readline(), timeout=10)
        state = json.loads(response_line.decode())
        state["sourceCode"] = session["source_code"]
        await websocket.send_json({"type": "state", "data": state})

        # Main command loop
        while True:
            data = await websocket.receive_json()
            command = data.get("command", "")

            if command in ("next", "step", "continue", "stop"):
                cmd = json.dumps({"cmd": command}) + "\n"
                process.stdin.write(cmd.encode())
                await process.stdin.drain()

                response_line = await asyncio.wait_for(
                    process.stdout.readline(), timeout=30
                )
                state = json.loads(response_line.decode())
                await websocket.send_json({"type": "state", "data": state})

                if state.get("status") in ("finished", "stopped"):
                    break
            else:
                await websocket.send_json({
                    "type": "error",
                    "message": f"Unknown command: {command}"
                })

    except WebSocketDisconnect:
        pass
    except asyncio.TimeoutError:
        try:
            await websocket.send_json({
                "type": "error",
                "message": "Engine timeout — the program may be in an infinite loop"
            })
        except Exception:
            pass
    except Exception as e:
        try:
            await websocket.send_json({"type": "error", "message": str(e)})
        except Exception:
            pass
    finally:
        await cleanup_session_internal(session_id)


async def cleanup_session_internal(session_id: str):
    """Clean up a session's engine process and temp files."""
    session = sessions.pop(session_id, None)
    if session:
        process = session.get("process")
        if process:
            try:
                process.stdin.write(b'{"cmd":"quit"}\n')
                await process.stdin.drain()
                await asyncio.wait_for(process.wait(), timeout=2)
            except Exception:
                try:
                    process.kill()
                except Exception:
                    pass
        work_dir = session.get("work_dir")
        if work_dir and os.path.exists(work_dir):
            shutil.rmtree(work_dir, ignore_errors=True)


@app.delete("/api/session/{session_id}")
async def cleanup_session(session_id: str):
    """Clean up a debug session via REST."""
    await cleanup_session_internal(session_id)
    return {"success": True}


@app.get("/api/health")
async def health():
    return {"status": "ok", "engine_exists": os.path.exists(ENGINE_PATH)}


if __name__ == "__main__":
    import uvicorn
    uvicorn.run(app, host="0.0.0.0", port=8000)
