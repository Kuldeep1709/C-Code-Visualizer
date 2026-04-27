import { useState, useRef, useCallback } from 'react';

export interface Variable {
  name: string;
  type: string;
  value: string;
  address?: string;
  elements?: Variable[];
  isArray?: boolean;
  isString?: boolean;
  isPointer?: boolean;
}

export interface StackFrame {
  level: number;
  func: string;
  file: string;
  line: number;
}

export interface DebugState {
  status: 'idle' | 'compiling' | 'ready' | 'paused' | 'stepping' | 'finished' | 'error';
  currentLine: number;
  variables: Variable[];
  previousVariables: Variable[];
  stack: StackFrame[];
  output: string;
  sourceCode: string;
  sourceLines: string[];
  compileError: string;
  stepCount: number;
}

// Environment variables for deployment. Default to localhost for local development.
const API_BASE = import.meta.env.VITE_API_BASE || 'http://localhost:8000';
const WS_BASE = import.meta.env.VITE_WS_BASE || 'ws://localhost:8000';

const initialState: DebugState = {
  status: 'idle',
  currentLine: -1,
  variables: [],
  previousVariables: [],
  stack: [],
  output: '',
  sourceCode: '',
  sourceLines: [],
  compileError: '',
  stepCount: 0,
};

const CONTROL_KEYWORDS = new Set([
  'if', 'for', 'while', 'switch', 'return', 'sizeof',
]);

function getUserDefinedFunctions(sourceCode: string): Set<string> {
  const functions = new Set<string>();
  const functionDefPattern = /^\s*[A-Za-z_]\w*(?:\s+[*\w]+)*\s+([A-Za-z_]\w*)\s*\([^;{}]*\)\s*\{/gm;
  let match: RegExpExecArray | null = functionDefPattern.exec(sourceCode);
  while (match) {
    functions.add(match[1]);
    match = functionDefPattern.exec(sourceCode);
  }
  return functions;
}

function inferPendingFunctionCall(
  sourceLine: string,
  existingFrames: StackFrame[],
  userDefinedFunctions: Set<string>,
): string | null {
  const withoutStrings = sourceLine
    .replace(/"([^"\\]|\\.)*"/g, '')
    .replace(/'([^'\\]|\\.)*'/g, '');

  const callMatches = Array.from(withoutStrings.matchAll(/([A-Za-z_]\w*)\s*\(/g))
    .map((m) => m[1])
    .filter((name) => !CONTROL_KEYWORDS.has(name) && userDefinedFunctions.has(name));
  if (callMatches.length === 0) return null;

  const realFrames = existingFrames.filter((frame) => frame.level >= 0);
  const topRealFrame = realFrames[0];

  for (const candidate of callMatches) {
    const alreadyInRealStack = realFrames.some((frame) => frame.func === candidate);
    const isRecursiveCallFromCurrentFrame = topRealFrame?.func === candidate;
    if (!alreadyInRealStack || isRecursiveCallFromCurrentFrame) {
      return candidate;
    }
  }

  return null;
}

export function useDebugSession() {
  const [state, setState] = useState<DebugState>(initialState);
  const wsRef = useRef<WebSocket | null>(null);
  const sessionIdRef = useRef<string | null>(null);
  // Ring-buffer of past snapshots (max 200 entries)
  const historyRef = useRef<DebugState[]>([]);

  // compile and connect

  const compile = useCallback(async (sourceCode: string) => {
    if (wsRef.current) {
      wsRef.current.close();
      wsRef.current = null;
    }
    historyRef.current = [];

    setState(prev => ({
      ...prev,
      status: 'compiling',
      compileError: '',
      output: '',
      variables: [],
      previousVariables: [],
      stack: [],
      currentLine: -1,
      stepCount: 0,
      sourceCode,
      sourceLines: sourceCode.split('\n'),
    }));

    try {
      const res = await fetch(`${API_BASE}/api/compile`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ source_code: sourceCode }),
      });

      const data = await res.json();

      if (!data.success) {
        setState(prev => ({
          ...prev,
          status: 'error',
          compileError: data.error || 'Compilation failed',
        }));
        return;
      }

      sessionIdRef.current = data.session_id;

      const ws = new WebSocket(`${WS_BASE}/ws/${data.session_id}`);
      wsRef.current = ws;

      ws.onmessage = (event) => {
        const message = JSON.parse(event.data);

        if (message.type === 'state') {
          const s = message.data;
          setState(prev => {
            const currentLine = s.line ?? s.currentLine ?? -1;
            const userDefinedFunctions = getUserDefinedFunctions(prev.sourceCode);
            const mappedStack: StackFrame[] = (s.stack ?? []).map((f: any) => ({
              level: f.level,
              func: f.func ?? f.function,
              file: f.file,
              line: f.line,
            }));

            const sourceLine = currentLine > 0 ? (prev.sourceLines[currentLine - 1] ?? '') : '';
            const inferredCall = inferPendingFunctionCall(sourceLine, mappedStack, userDefinedFunctions);
            const stackWithPendingCall = inferredCall
              ? [{ level: -1, func: inferredCall, file: 'main.c', line: currentLine }, ...mappedStack]
              : mappedStack;

            const next: DebugState = {
              ...prev,
              status: s.status === 'paused' ? 'paused' :
                s.status === 'finished' ? 'finished' :
                  s.status === 'stopped' ? 'idle' :
                    s.status === 'error' ? 'error' : prev.status,
              currentLine,
              previousVariables: prev.variables,
              variables: (s.vars ?? s.variables ?? []).map((v: any) => ({
                name: v.name,
                type: v.type,
                value: v.value,
              })),
              stack: stackWithPendingCall,
              output: s.output !== undefined ? s.output : prev.output,
              compileError: s.error && s.status === 'error' ? s.error : prev.compileError,
              stepCount: prev.stepCount + (s.status === 'paused' ? 1 : 0),
            };
            // Save current state to history whenever we advance to "paused"
            if (next.status === 'paused') {
              historyRef.current.push(prev);
              if (historyRef.current.length > 200) historyRef.current.shift();
            }
            return next;
          });
        } else if (message.type === 'error') {
          setState(prev => ({
            ...prev,
            status: 'error',
            compileError: message.message,
          }));
        }
      };

      ws.onerror = () => {
        setState(prev => ({
          ...prev,
          status: 'error',
          compileError: 'WebSocket connection error',
        }));
      };

      ws.onclose = () => {
        setState(prev => {
          if (prev.status === 'stepping') return { ...prev, status: 'finished' };
          return prev;
        });
      };

      ws.onopen = () => {
        setState(prev => ({ ...prev, status: 'ready' }));
      };

    } catch (err: any) {
      setState(prev => ({
        ...prev,
        status: 'error',
        compileError: err.message || 'Failed to connect to server',
      }));
    }
  }, []);

  // debug commands

  const sendCommand = useCallback((command: string) => {
    if (wsRef.current && wsRef.current.readyState === WebSocket.OPEN) {
      setState(prev => ({ ...prev, status: 'stepping' }));
      wsRef.current.send(JSON.stringify({ command }));
    }
  }, []);

  const stepNext = useCallback(() => {
    const sourceLine = state.currentLine > 0 ? (state.sourceLines[state.currentLine - 1] ?? '') : '';
    const userDefinedFunctions = getUserDefinedFunctions(state.sourceCode);
    const pendingCall = inferPendingFunctionCall(sourceLine, state.stack, userDefinedFunctions);
    sendCommand(pendingCall ? 'step' : 'next');
  }, [sendCommand, state.currentLine, state.sourceCode, state.sourceLines, state.stack]);
  const stepInto = useCallback(() => sendCommand('step'), [sendCommand]);
  const continueExec = useCallback(() => sendCommand('continue'), [sendCommand]);


  const stepBack = useCallback(() => {
    if (historyRef.current.length === 0) return;
    const snapshot = historyRef.current.pop()!;
    setState({ ...snapshot, status: 'paused' });
  }, []);

  const stop = useCallback(() => {
    sendCommand('stop');
    if (wsRef.current) {
      wsRef.current.close();
      wsRef.current = null;
    }
    historyRef.current = [];
    setState(prev => ({ ...prev, status: 'idle', currentLine: -1 }));
  }, [sendCommand]);

  const reset = useCallback(() => {
    if (wsRef.current) {
      wsRef.current.close();
      wsRef.current = null;
    }
    sessionIdRef.current = null;
    historyRef.current = [];
    setState(initialState);
  }, []);

  // derived flags

  const isDebugging = ['paused', 'stepping', 'ready'].includes(state.status);
  const canStep = state.status === 'paused';
  const canStepBack = state.status === 'paused' && historyRef.current.length > 0;
  const canCompile = ['idle', 'error', 'finished'].includes(state.status);

  return {
    state,
    compile,
    stepNext,
    stepInto,
    stepBack,
    continueExec,
    stop,
    reset,
    isDebugging,
    canStep,
    canStepBack,
    canCompile,
  };
}
