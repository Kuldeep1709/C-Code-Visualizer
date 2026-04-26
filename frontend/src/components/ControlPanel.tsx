interface ControlPanelProps {
  onCompile: () => void;
  onStepNext: () => void;
  onStepInto: () => void;
  onStepBack: () => void;
  onContinue: () => void;
  onStop: () => void;
  onReset: () => void;
  canCompile: boolean;
  canStep: boolean;
  canStepBack: boolean;
  isDebugging: boolean;
  status: string;
}

export default function ControlPanel({
  onCompile,
  onStepNext,
  onStepInto,
  onStepBack,
  onContinue,
  onStop,
  onReset,
  canCompile,
  canStep,
  canStepBack,
  isDebugging,
  status,
}: ControlPanelProps) {
  return (
    <div className="control-panel" id="control-panel">
      {/* Compile / Run */}
      <button
        id="btn-compile"
        className="control-btn control-btn--success"
        onClick={onCompile}
        disabled={!canCompile}
        title="Compile & Debug (F5)"
      >
        <span className="control-btn__icon">▶</span>
        Compile & Debug
        <span className="control-btn__kbd">F5</span>
      </button>

      <div className="control-divider" />

      {/* Step controls */}
      <button
        id="btn-step-back"
        className="control-btn control-btn--secondary"
        onClick={onStepBack}
        disabled={!canStepBack}
        title="Step Back (F9)"
      >
        <span className="control-btn__icon">↩</span>
        Back
        <span className="control-btn__kbd">F9</span>
      </button>

      <button
        id="btn-step-next"
        className="control-btn control-btn--primary"
        onClick={onStepNext}
        disabled={!canStep}
        title="Step Over (F10)"
      >
        <span className="control-btn__icon">⤵</span>
        Next
        <span className="control-btn__kbd">F10</span>
      </button>

      <button
        id="btn-step-into"
        className="control-btn control-btn--primary"
        onClick={onStepInto}
        disabled={!canStep}
        title="Step Into (F11)"
      >
        <span className="control-btn__icon">↓</span>
        Step Into
        <span className="control-btn__kbd">F11</span>
      </button>

      <button
        id="btn-continue"
        className="control-btn"
        onClick={onContinue}
        disabled={!canStep}
        title="Run to finish (F8)"
      >
        Finish
        <span className="control-btn__kbd">F8</span>
      </button>

      <div className="control-divider" />

      {/* Stop / Reset */}
      <button
        id="btn-stop"
        className="control-btn control-btn--danger"
        onClick={onStop}
        disabled={!isDebugging}
        title="Stop debugging"
      >
        <span className="control-btn__icon">⏹</span>
        Stop
      </button>

      <button
        id="btn-reset"
        className="control-btn"
        onClick={onReset}
        title="Reset everything"
      >
        <span className="control-btn__icon">↺</span>
        Reset
      </button>

      {/* Status indicator on the right */}
      <div style={{ marginLeft: 'auto' }}>
        <div className="header__status">
          <span
            className={`status-dot ${status === 'paused' ? 'status-dot--active' :
                status === 'stepping' || status === 'compiling' ? 'status-dot--running' :
                  status === 'error' ? 'status-dot--error' : ''
              }`}
          />
          <span>
            {status === 'idle' && 'Ready'}
            {status === 'compiling' && 'Compiling...'}
            {status === 'ready' && 'Starting...'}
            {status === 'paused' && 'Paused'}
            {status === 'stepping' && 'Running...'}
            {status === 'finished' && 'Program Exited'}
            {status === 'error' && 'Error'}
          </span>
        </div>
      </div>
    </div>
  );
}
