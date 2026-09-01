# ZRay regression tests

Run the focused regression suite from the repository root:

```sh
make test
```

The suite uses LLVM 15. It checks:

- counter separation for control-equivalent blocks in different loop nests;
- full-scan region closure on every function exit; and
- the runtime diagnostic for an unbalanced region.

Set `LLVM_BIN` if LLVM 15 is installed somewhere other than
`/usr/lib/llvm-15/bin`. Each test uses a temporary directory and does not need
the benchmark datasets.
