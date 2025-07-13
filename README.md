# leveldb-repl

**LevelDB REPL (Read-Eval-Print Loop)** is a command-line interface that allows you to interactively open, query, and manipulate a [LevelDB](https://github.com/google/leveldb) database.

This project is built in **C++23** and serves as an interactive shell to experiment with or debug LevelDB databases easily.

---

## Features

- Open (Automatically tries to create if not present)
- Reading from database
- Writing values to database
- Delete values from database
- Printing whole database
- Double or single quote keys or values for json and other stuff

Example:
```bash
write hello_world "This will 'be written'"
write "hello world" 'This will "also be written"'
```

---

## Requirements

- **C++23** compatible compiler (e.g., `g++-13`, `clang++-17`)

---

## Building

```bash
cmake -Bbuild -DCMAKE_BUILD_TYPE=Release -H.
cmake --build build -j$(nproc)
```

## Running

```bash 
./build/leveldb-repl
```


## TODO

- Tests
- Precompiled binaries with each release aarch64, x86_64 with musl