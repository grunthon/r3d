# r3d - Guidelines

## Style

### Indentation Style - Allman

Braces are placed on their own line, aligned with the enclosing statement.
See the [Allman style entry on Wikipedia](https://en.wikipedia.org/wiki/Indent_style#Allman_style) for a detailed explanation with examples.

```c
if (x > 0)
{
    DoSomething();
}
```

### Control Flow Blocks (`if` / `for` / `while`)

- A single-statement body must stay on **one line**, with no braces.
- If the line becomes too long to read comfortably, use **full braces** (Allman style) instead.
- Never leave a body on its own line without braces.

```c
// OK: single line, no braces
if (x > 0) DoSomething();

// OK: too long for one line, use braces
if (someVeryLongConditionThatNeedsWrapping(x, y, z))
{
    DoSomethingElseWithMultipleArguments(x, y, z);
}

// FORBIDDEN: body on its own line without braces
if (x > 0)
    DoSomething();
```

### Indentation Width

- 4 spaces per indentation level.
- No tabs.

### Spacing Around Operators

- Operators are surrounded by spaces.
- Exception: multiplication/division may omit spaces when it improves the readability of a documented mathematical formula.

```c
int result = a + b * c;

// Exception: compact form for a documented formula
// discriminant = b^2 - 4ac
float delta = b*b - 4*a*c;
```

### Pointers

- The `*` is attached to the type, not the variable: `Type* v`.
- Multiple declarations on a single line are forbidden.

```c
Type* v;    // OK
Type* a, b; // FORBIDDEN: split into separate declarations
```

This choice keeps the type easy to read at a glance and avoids the classic
`Type* a, b;` pitfall (`b` is a `Type`, not a `Type*`).

### Vertical Alignment

Vertical alignment of declarations, assignments, or parameters is allowed
when it improves the readability of a small group of closely related lines.
It must not be applied systematically.

```c
int   width  = 800;
int   height = 600;
float scale  = 1.0f;
```

### Naming Conventions

| Kind                           | Convention           | Example              |
|--------------------------------|----------------------|----------------------|
| Public function                | `R3D_PascalCase()`   | `R3D_LoadTexture()`  |
| Internal function (non-static) | `r3d_snake_case()`   | `r3d_upload_mesh()`  |
| Internal function (static)     | `snake_case()`       | `compute_bounds()`   |
| Variable                       | `camelCase`          | `frameCount`         |
