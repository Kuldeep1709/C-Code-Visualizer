import { useRef, useEffect } from 'react';

interface ConsolePanelProps {
  output: string;
  compileError: string;
  status: string;
}

export default function ConsolePanel({ output, compileError, status }: ConsolePanelProps) {
  const outputRef = useRef<HTMLDivElement>(null);

  // Auto-scroll to bottom on new output
  useEffect(() => {
    if (outputRef.current) {
      outputRef.current.scrollTop = outputRef.current.scrollHeight;
    }
  }, [output, compileError]);

  const hasError = compileError && status === 'error';
  const hasOutput = output.length > 0;

  return (
    <div className="console-panel" id="console-panel">
      <div className="panel-header">
        <span className="panel-header__title">Console</span>
        {status === 'finished' && (
          <span className="panel-header__badge" style={{ background: 'rgba(52,211,153,0.15)', color: '#34d399' }}>
            Exited
          </span>
        )}
      </div>

      <div className="console-output" ref={outputRef}>
        {hasError && (
          <div className="error-overlay">
            <div className="error-overlay__title">
              ✕ Compilation Error
            </div>
            <div className="error-overlay__message">{compileError}</div>
          </div>
        )}

        {!hasError && !hasOutput && status === 'idle' && (
          <div className="welcome-state">
            <span className="welcome-state__icon">{'</>'}</span>
            <span className="welcome-state__title">C Code Visualizer</span>
            <span className="welcome-state__desc">
              Write your C code in the editor, then click <strong>Compile & Debug</strong> to step through it line by line.
            </span>
          </div>
        )}

        {!hasError && hasOutput && (
          <>
            <span className={status === 'finished' ? 'console-output--success' : ''}>
              {output}
            </span>
            {status !== 'finished' && <span className="console-cursor" />}
          </>
        )}

        {!hasError && !hasOutput && status !== 'idle' && status !== 'finished' && (
          <span style={{ color: 'var(--text-muted)', fontStyle: 'italic' }}>
            No output yet...
            <span className="console-cursor" />
          </span>
        )}

        {!hasError && !hasOutput && status === 'finished' && (
          <span className="console-output--success">
            Program finished with no output.
          </span>
        )}
      </div>
    </div>
  );
}
