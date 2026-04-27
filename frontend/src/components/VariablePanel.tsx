import type { Variable } from '../hooks/useDebugSession';

interface VariablePanelProps {
  variables: Variable[];
  previousVariables: Variable[];
  status: string;
}

/**
 * Normalises verbose GDB type strings into short, readable C type labels.
 * The raw type is always exposed via a `title` tooltip.
 *
 * Examples:
 *   "unsigned int"    → "uint"
 *   "long long int"   → "llong"
 *   "unsigned char"   → "uchar"
 *   "short int"       → "short"
 *   "long int"        → "long"
 *   "int [5]"         → "int[5]"
 *   "char *"          → "char*"
 *   "char [100]"      → "char[100]"
 */
function formatType(raw: string): string {
  let t = raw.trim();

  // Collapse whitespace
  t = t.replace(/\s+/g, ' ');

  // Normalise common verbose GDB forms  ─────────────────────────────────────
  const map: [RegExp, string][] = [
    [/\bunsigned long long int\b/g,  'ullong'],
    [/\bunsigned long long\b/g,      'ullong'],
    [/\blong long unsigned int\b/g,  'ullong'],
    [/\blong long int\b/g,           'llong'],
    [/\blong long\b/g,               'llong'],
    [/\bunsigned long int\b/g,       'ulong'],
    [/\bunsigned long\b/g,           'ulong'],
    [/\blong unsigned int\b/g,       'ulong'],
    [/\blong int\b/g,                'long'],
    [/\bunsigned short int\b/g,      'ushort'],
    [/\bshort unsigned int\b/g,      'ushort'],
    [/\bunsigned short\b/g,          'ushort'],
    [/\bshort int\b/g,               'short'],
    [/\bunsigned int\b/g,            'uint'],
    [/\bunsigned char\b/g,           'uchar'],
    [/\bsigned char\b/g,             'char'],
    [/\bsigned int\b/g,              'int'],
    [/\bsigned\b/g,                  'int'],
  ];
  for (const [re, replacement] of map) {
    t = t.replace(re, replacement);
  }

  // Strip spaces before `*`  (e.g. "char *" → "char*")
  t = t.replace(/\s+\*/g, '*');

  // Strip spaces inside array brackets  (e.g. "int [5]" → "int[5]")
  t = t.replace(/\s+\[/g, '[');

  return t;
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

  // Render a single variable row
  const renderVariableRow = (v: Variable, index: number) => {
    const changeClass = getChangeClass(v);

    // Array rendering
    if (v.isArray && v.elements && v.elements.length > 0) {
      return (
        <div key={`${v.name}-${index}`} className="variable-group">
          <div className="variable-row variable-row--expandable">
            <span className="var-name">
              <span className="array-toggle">▾</span>
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
                <span className="array-address">{elem.address || '—'}</span>
                <span className="array-value">{elem.value}</span>
              </div>
            ))}
          </div>
        </div>
      );
    }

    // String rendering (char pointer)
    if (v.isString && v.elements && v.elements.length > 0) {
      return (
        <div key={`${v.name}-${index}`} className="variable-group">
          <div className="variable-row variable-row--expandable">
            <span className="var-name">
              <span className="array-toggle">▾</span>
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
                <span className="array-address">{elem.address || '—'}</span>
                <span className="array-value string-char">{elem.value}</span>
              </div>
            ))}
          </div>
        </div>
      );
    }

    // Pointer rendering
    if (v.isPointer && v.elements && v.elements.length > 0) {
      return (
        <div key={`${v.name}-${index}`} className="variable-group">
          <div className="variable-row variable-row--expandable">
            <span className="var-name">
              <span className="array-toggle">▾</span>
              {v.name}
            </span>
            <span className="var-type" title={v.type}>{formatType(v.type)}</span>
            <span className={`var-value ${changeClass}`}>
              {v.address && <span className="var-address" title={v.address}>{v.address}</span>}
              {v.value}
            </span>
          </div>
          <div className="array-elements">
            <div className="array-header">
              <span>Deref</span>
              <span>Value</span>
            </div>
            {v.elements.map((elem, idx) => (
              <div key={idx} className="array-element-row">
                <span className="array-index">{elem.name}</span>
                <span className="array-value" style={{ gridColumn: 'span 2' }}>{elem.value}</span>
              </div>
            ))}
          </div>
        </div>
      );
    }

    // Simple variable
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
              <span>Value {variables.some(v => v.address) && <span className="var-address">(Addr)</span>}</span>
            </div>
            {variables.map((v, i) => renderVariableRow(v, i))}
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
