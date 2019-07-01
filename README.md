## Introduction

`seqmalloc` is a naive, leaking malloc(3) implementation.

It ignores `free()` and attempts to do as little memory management as
possible while still being malloc(3) compliant and actually useful.

`seqmalloc` can be used to:

- speed-up short-lived applications where memory usage does not matter
- as a baseline comparison against other memory allocators

## Building

To compile, just download this repository and run:
```console
make
```

This will produce `seqmalloc.a` and `seqmalloc.so`.

## Usage

If successfully compiled, you can link with seqmalloc with your application
at compile time with:
```console
-lseqmalloc
```
or you can dynamically link it with your application by using LD_PRELOAD (if
your application was not statically linked with another memory allocator).
```console
LD_PRELOAD=seqmalloc.so ./your_application
```

## Copyright

License: MIT

Read file [COPYING](COPYING)

