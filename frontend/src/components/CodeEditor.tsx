import { useRef, useEffect, useCallback } from 'react';
import Editor from '@monaco-editor/react';

interface CodeEditorProps {
  value: string;
  onChange: (value: string) => void;
  currentLine: number;
  readOnly: boolean;
}

export default function CodeEditor({ value, onChange, currentLine, readOnly }: CodeEditorProps) {
  const editorRef = useRef<any>(null);
  const decorationsRef = useRef<any>(null);

  const handleMount = useCallback((editor: any) => {
    editorRef.current = editor;
    decorationsRef.current = editor.createDecorationsCollection([]);

    // Set focus
    editor.focus();
  }, []);

  // Update line highlighting when currentLine changes
  useEffect(() => {
    const editor = editorRef.current;
    const decorations = decorationsRef.current;
    if (!editor || !decorations) return;

    if (currentLine > 0) {
      decorations.set([
        {
          range: {
            startLineNumber: currentLine,
            startColumn: 1,
            endLineNumber: currentLine,
            endColumn: 1,
          },
          options: {
            isWholeLine: true,
            className: 'current-line-decoration',
            glyphMarginClassName: 'current-line-glyph',
          },
        },
      ]);

      // Scroll to the current line
      editor.revealLineInCenter(currentLine);
    } else {
      decorations.clear();
    }
  }, [currentLine]);

  return (
    <Editor
      height="100%"
      defaultLanguage="c"
      theme="vs-dark"
      value={value}
      onChange={(v) => onChange(v || '')}
      onMount={handleMount}
      options={{
        fontSize: 14,
        fontFamily: "'JetBrains Mono', 'Fira Code', monospace",
        fontLigatures: true,
        minimap: { enabled: false },
        lineNumbers: 'on',
        glyphMargin: true,
        folding: true,
        scrollBeyondLastLine: false,
        automaticLayout: true,
        readOnly,
        padding: { top: 12 },
        renderLineHighlight: 'none',
        smoothScrolling: true,
        cursorSmoothCaretAnimation: 'on',
        cursorBlinking: 'smooth',
        bracketPairColorization: { enabled: true },
        tabSize: 4,
      }}
    />
  );
}
