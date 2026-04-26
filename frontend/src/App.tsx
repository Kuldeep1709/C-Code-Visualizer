import { useState, useEffect, useCallback } from 'react';
import CodeEditor from './components/CodeEditor';
import ControlPanel from './components/ControlPanel';
import VariablePanel from './components/VariablePanel';
import StackPanel from './components/StackPanel';
import ConsolePanel from './components/ConsolePanel';
import { useDebugSession } from './hooks/useDebugSession';
import './App.css';

const DEFAULT_CODE = `#include <stdio.h>

int add(int a, int b) {
    int result = a + b;
    return result;
}

int main() {
    int x = 5;
    int y = 10;
    int sum = add(x, y);

    printf("Sum of %d and %d is %d\\n", x, y, sum);

    for (int i = 0; i < 3; i++) {
        printf("i = %d\\n", i);
    }

    return 0;
}
`;

function App() {
  const [code, setCode] = useState(DEFAULT_CODE);
  const {
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
  } = useDebugSession();

  const handleCompile = useCallback(() => {
    if (canCompile && code.trim()) {
      compile(code);
    }
  }, [canCompile, code, compile]);

  const handleReset = useCallback(() => {
    reset();
  }, [reset]);

  // Keyboard shortcuts
  useEffect(() => {
    const handleKeyDown = (e: KeyboardEvent) => {
      if (e.key === 'F5') {
        e.preventDefault();
        handleCompile();
      } else if (e.key === 'F9') {
        e.preventDefault();
        if (canStepBack) stepBack();
      } else if (e.key === 'F10') {
        e.preventDefault();
        if (canStep) stepNext();
      } else if (e.key === 'F11') {
        e.preventDefault();
        if (canStep) stepInto();
      } else if (e.key === 'F8') {
        e.preventDefault();
        if (canStep) continueExec();
      }
    };

    window.addEventListener('keydown', handleKeyDown);
    return () => window.removeEventListener('keydown', handleKeyDown);
  }, [handleCompile, canStep, canStepBack, stepBack, stepNext, stepInto, continueExec]);

  return (
    <div className="app">
      {/* Header */}
      <header className="header">
        <div className="header__logo">
          <div className="header__icon">C</div>
          <span className="header__title">C Code Visualizer</span>
        </div>
        <div className="header__status">
          <span style={{ fontSize: '11px', color: 'var(--text-muted)' }}>
            {state.stepCount > 0 && `Step ${state.stepCount}`}
          </span>
        </div>
      </header>

      {/* Control Panel */}
      <ControlPanel
        onCompile={handleCompile}
        onStepNext={stepNext}
        onStepInto={stepInto}
        onStepBack={stepBack}
        onContinue={continueExec}
        onStop={stop}
        onReset={handleReset}
        canCompile={canCompile}
        canStep={canStep}
        canStepBack={canStepBack}
        isDebugging={isDebugging}
        status={state.status}
      />

      {/* Main Content */}
      <div className="main-content">
        {/* Left: Code Editor */}
        <div className="editor-panel">
          <div className="panel-header">
            <span className="panel-header__title">main.c</span>
            <span className="panel-header__badge">
              {state.currentLine > 0 ? `Line ${state.currentLine}` : 'Editor'}
            </span>
          </div>
          <div className="editor-container">
            <CodeEditor
              value={code}
              onChange={setCode}
              currentLine={state.currentLine}
              readOnly={isDebugging}
            />
          </div>
        </div>

        {/* Right: Sidebar */}
        <div className="sidebar">
          <VariablePanel
            variables={state.variables}
            previousVariables={state.previousVariables}
            status={state.status}
          />
          <StackPanel
            stack={state.stack}
            status={state.status}
          />
          <ConsolePanel
            output={state.output}
            compileError={state.compileError}
            status={state.status}
          />
        </div>
      </div>
    </div>
  );
}

export default App;
