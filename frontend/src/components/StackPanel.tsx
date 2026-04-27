import type { StackFrame } from '../hooks/useDebugSession';

interface StackPanelProps {
  stack: StackFrame[];
  status: string;
}

export default function StackPanel({ stack, status }: StackPanelProps) {
  if (status === 'idle') {
    return (
      <div className="stack-panel">
        <div className="panel-header">
          <span className="panel-header__title">Call Stack</span>
        </div>
        <div className="empty-state">
          <span>Stack frames appear here</span>
        </div>
      </div>
    );
  }

  return (
    <div className="stack-panel" id="stack-panel">
      <div className="panel-header">
        <span className="panel-header__title">Call Stack</span>
        {stack.length > 0 && (
          <span className="panel-header__badge">{stack.length}</span>
        )}
      </div>

      <div className="stack-list">
        {stack.length > 0 ? (
          stack.map((frame, i) => (
            <div className="stack-frame" key={`${frame.func}-${i}`}>
              <span className="stack-frame__level">#{frame.level}</span>
              <span className="stack-frame__func">{frame.func}()</span>
              <span className="stack-frame__location">
                {frame.file ? frame.file.split('/').pop() : '??'}:{frame.line}
              </span>
            </div>
          ))
        ) : (
          <div className="empty-state">
            <span>No stack frames</span>
          </div>
        )}
      </div>
    </div>
  );
}
