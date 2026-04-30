import type { Variable } from '../hooks/useDebugSession';

interface VariablePanelProps {
  variables: Variable[];
  previousVariables: Variable[];
  status: string;
}

/**
 * Normalises verbose GDB type strings into short, readable C type labels.
 * The raw type is always exposed via a `title` tooltip.
 */
function formatType(raw: string): string {
  let t = raw.trim();
  t = t.replace(/\s+/g, ' ');

  const map: [RegExp, string][] = [
    [/\bunsigned long long int\b/g, 'ullong'],
    [/\bunsigned long long\b/g, 'ullong'],
    [/\blong long unsigned int\b/g, 'ullong'],
    [/\blong long int\b/g, 'llong'],
    [/\blong long\b/g, 'llong'],
    [/\bunsigned long int\b/g, 'ulong'],
    [/\bunsigned long\b/g, 'ulong'],
    [/\blong unsigned int\b/g, 'ulong'],
    [/\blong int\b/g, 'long'],
    [/\bunsigned short int\b/g, 'ushort'],
    [/\bshort unsigned int\b/g, 'ushort'],
    [/\bunsigned short\b/g, 'ushort'],
    [/\bshort int\b/g, 'short'],
    [/\bunsigned int\b/g, 'uint'],
    [/\bunsigned char\b/g, 'uchar'],
    [/\bsigned char\b/g, 'char'],
    [/\bsigned int\b/g, 'int'],
    [/\bsigned\b/g, 'int'],
  ];

  for (const [re, replacement] of map) {
    t = t.replace(re, replacement);
  }

  t = t.replace(/\s+\*/g, '*');
  t = t.replace(/\s+\[/g, '[');
  return t;
}

export default function VariablePanel({ variables, previousVariables, status }: VariablePanelProps) {
  const getChangeClass = (variable: Variable): string => {
    const prev = previousVariables.find(v => v.name === variable.name);
    if (prev && prev.value !== variable.value) {
      return 'var-value--changed';
    }
    return '';
  };

  const renderVariableRow = (v: Variable, index: number) => {
    const changeClass = getChangeClass(v);

    if (v.isArray && v.elements && v.elements.length > 0) {
      return (
        <div key={`${v.name}-${index}`} className="variable-group">
          <div className="variable-row variable-row--expandable">
            <span className="var-name">
              <span className="array-toggle">v</span>
              {v.name}
            </span>
            <span className="var-type" title={v.type}>{formatType(v.type)}</span>
            <span className={`var-value ${changeClass}`}>
              {v.address && <span className="var-address" title={v.address}>{v.address}</span>}
              [{v.elements.length}]
            </span>
          </div>
          <div className="array-elements">
            <div className="array-header">
              <span>Index</span>
              <span>Address</span>
              <span>Value</span>
            </div>
            {v.elements.map((elem, idx) => (
              <div key={idx} className="array-element-row">
                <span className="array-index">{elem.name}</span>
                <span className="array-address">{elem.address || '-'}</span>
                <span className="array-value">{elem.value}</span>
              </div>
            ))}
          </div>
        </div>
      );
    }

    if (v.isString && v.elements && v.elements.length > 0) {
      return (
        <div key={`${v.name}-${index}`} className="variable-group">
          <div className="variable-row variable-row--expandable">
            <span className="var-name">
              <span className="array-toggle">v</span>
              {v.name}
            </span>
            <span className="var-type" title={v.type}>{formatType(v.type)}</span>
            <span className={`var-value ${changeClass}`}>
              {v.address && <span className="var-address" title={v.address}>{v.address}</span>}
              "{v.elements.map(e => e.value).join('')}"
            </span>
          </div>
          <div className="array-elements">
            <div className="array-header">
              <span>Index</span>
              <span>Address</span>
              <span>Char</span>
            </div>
            {v.elements.map((elem, idx) => (
              <div key={idx} className="array-element-row">
                <span className="array-index">{elem.name}</span>
                <span className="array-address">{elem.address || '-'}</span>
                <span className="array-value string-char">{elem.value}</span>
              </div>
            ))}
          </div>
        </div>
      );
    }

    if (v.isPointer) {
      const dereferenced = v.elements?.[0];

      return (
        <div key={`${v.name}-${index}`} className="variable-group pointer-group">
          <div className="variable-row pointer-row">
            <span className="var-name">{v.name}</span>
            <span className="var-type" title={v.type}>{formatType(v.type)}</span>
            <span className={`var-value ${changeClass}`}>{v.value}</span>
          </div>
          <div className="pointer-visual" title={`${v.name} points to ${v.value}`}>
            <div className="pointer-node pointer-node--source">
              <span className="pointer-node__label">{v.name}</span>
              <span className="pointer-node__address">{v.address || 'local'}</span>
            </div>
            <div className="pointer-arrow" aria-hidden="true">
              <span className="pointer-arrow__line" />
              <span className="pointer-arrow__head">-&gt;</span>
            </div>
            <div className="pointer-node pointer-node--target">
              <span className="pointer-node__label">{dereferenced?.name || `*${v.name}`}</span>
              <span className="pointer-node__value">{dereferenced?.value || 'unavailable'}</span>
            </div>
          </div>
        </div>
      );
    }

    return (
      <div className="variable-row" key={`${v.name}-${index}`}>
        <span className="var-name">{v.name}</span>
        <span className="var-type" title={v.type}>{formatType(v.type)}</span>
        <span className={`var-value ${changeClass}`}>
          {v.address && <span className="var-address" title={v.address}>{v.address}</span>}
          {v.value}
        </span>
      </div>
    );
  };

  if (status === 'idle') {
    return (
      <div className="variable-panel">
        <div className="panel-header">
          <span className="panel-header__title">Variables</span>
        </div>
        <div className="empty-state">
          <span className="empty-state__icon">{}</span>
          <span>Variables will appear here<br />during debugging</span>
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
              <span>Value {variables.some(v => v.address) && <span className="var-address">(Addr)</span>}</span>
            </div>
            {variables.map((v, i) => renderVariableRow(v, i))}
          </>
        ) : (
          <div className="empty-state">
            <span className="empty-state__icon">empty</span>
            <span>No local variables<br />in current scope</span>
          </div>
        )}
      </div>
    </div>
  );
}
