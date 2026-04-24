import type { Variable } from '../hooks/useDebugSession';

interface VariablePanelProps {
  variables: Variable[];
  previousVariables: Variable[];
  status: string;
}

export default function VariablePanel({ variables, previousVariables, status }: VariablePanelProps) {
  // Track which values changed since last step
  const getChangeClass = (variable: Variable): string => {
    const prev = previousVariables.find(v => v.name === variable.name);
    if (prev && prev.value !== variable.value) {
      return 'var-value--changed';
    }
    return '';
  };

  if (status === 'idle') {
    return (
      <div className="variable-panel">
        <div className="panel-header">
          <span className="panel-header__title">Variables</span>
        </div>
        <div className="empty-state">
          <span className="empty-state__icon">{ }</span>
          <span>Variables will appear here<br/>during debugging</span>
        </div>
      </div>
    );
  }

  return (
    <div className="variable-panel" id="variable-panel">
      <div className="panel-header">
        <span className="panel-header__title">Variables</span>
        {variables.length > 0 && (
          <span className="panel-header__badge">{variables.length}</span>
        )}
      </div>

      <div className="variable-table">
        {variables.length > 0 ? (
          <>
            <div className="variable-row variable-row--header">
              <span>Name</span>
              <span>Type</span>
              <span>Value</span>
            </div>
            {variables.map((v, i) => (
              <div className="variable-row" key={`${v.name}-${i}`}>
                <span className="var-name">{v.name}</span>
                <span className="var-type">{v.type}</span>
                <span className={`var-value ${getChangeClass(v)}`}>{v.value}</span>
              </div>
            ))}
          </>
        ) : (
          <div className="empty-state">
            <span className="empty-state__icon">∅</span>
            <span>No local variables<br/>in current scope</span>
          </div>
        )}
      </div>
    </div>
  );
}
