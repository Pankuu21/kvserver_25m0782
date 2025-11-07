# KV Store — Phase 1 

**Author:** Pankaj Ramesh Badgujar  
**Project:** Multithreaded HTTP key–value store (C++) with an in-memory LRU cache and PostgreSQL persistence.

---

## Overview

This project implements a  HTTP key–value store with:

- A  HTTP server (bare POSIX sockets).
- Multithreading via a ThreadPool to handle concurrent clients.
- An in-memory LRU cache (`LRUCache`) for fast reads.
- PostgreSQL persistence (`Database` using libpq).
- A minimal CLI client (`SimpleClient`) that performs `PUT`, `GET`, and `DELETE` over HTTP.

The server supports the following endpoints:
- `PUT /kv/<key>` — store the request body as the value for `<key>`.
- `GET /kv/<key>` — retrieve the value for `<key>`.
- `DELETE /kv/<key>` — delete the key.

Responses are simple text bodies, with `200 OK` on success and `404 Not Found` when a key is missing.

---

## Files 

- `simple_client.cpp` — `SimpleClient` implementation (POSIX sockets).
- `server/`:
  - `server.cpp` — `main()` that constructs `HTTPServer` and starts it.
  - `server.h`, `server.cpp` — HTTPServer implementation (accept loop, request handling).
  - `cache.h`, `cache.cpp` — `LRUCache` implementation .
  - `database.h`, `database.cpp` — `Database` wrapper around libpq for PostgreSQL.
  - `threadpool.h`, `threadpool.cpp` — simple thread pool.


---

##  Run (Linux)

### Dependencies
- C++17 compiler (g++ / clang++)
- `libpq` (Postgres client library) and headers (`libpq-dev` on Debian/Ubuntu)


### Compile
```bash
g++ -std=c++17 -O2 -g -pthread     -Iinclude     -I/usr/include/postgresql     -L/usr/lib/x86_64-linux-gnu     src/main.cpp src/server.cpp src/cache.cpp src/database.cpp src/threadpool.cpp     -o build/kv_server     -lpq

g++ -std=c++17 -O2 -g client/simple_client.cpp -o build/simple_client

```

### Run
Start PostgreSQL and create a database (`kv_db`) and user matching the connection string in `server.cpp` :

```bash

sudo -u postgres psql -c "CREATE DATABASE kv_db;"
sudo -u postgres psql -c "CREATE USER kv_user WITH PASSWORD 'password';"
sudo -u postgres psql -c "GRANT ALL PRIVILEGES ON DATABASE kv_db TO kv_user;"
```

Run the server:
```bash
./kv_server 8080 4 100

```

Run the client:
```bash
./kv_client put mykey "my value"
./kv_client get mykey
./kv_client delete mykey
```

---




## GitHub repository

`https://github.com/Pankuu21/kvserver_25m0782`  


