export interface CodeExample {
  id: string;
  label: string;
  code: string;
}

export const DEFAULT_CODE = `#include <stdio.h>

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

export const POINTER_CODE = `#include <stdio.h>

int main() {
    int value = 42;
    int *ptr = &value;

    value = *ptr + 1;

    printf("value = %d\\n", value);
    return 0;
}
`;

export const CODE_EXAMPLES: CodeExample[] = [
  {
    id: 'default',
    label: 'Function + Loop',
    code: DEFAULT_CODE,
  },
  {
    id: 'pointer',
    label: 'Pointer Visualization',
    code: POINTER_CODE,
  },
];
