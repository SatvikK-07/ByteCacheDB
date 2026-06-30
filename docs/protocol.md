# ByteCacheDB Protocol

ByteCacheDB uses a simple human-readable, newline-delimited text protocol. Each command is one line ending in `\n` or `\r\n`.

```text
SET name Satvik
GET name
```

Responses use Redis-style prefixes for familiarity:

| Prefix | Meaning | Example |
| --- | --- | --- |
| `+` | Simple string | `+OK` |
| `-ERR` | Error | `-ERR invalid command` |
| `:` | Integer | `:1` |
| `$N` | Bulk string with length | `$6\r\nSatvik` |
| `$-1` | Missing bulk string | `$-1` |
| `*N` | Array | `*2 ...` |

## Commands

### PING

```text
PING
```

Returns `+PONG`.

### SET

```text
SET key value
```

Stores `value` at `key` and clears any existing TTL.

### GET

```text
GET key
```

Returns a bulk string or `$-1` if the key is missing or expired.

### DEL

```text
DEL key
```

Returns `:1` when a key was removed and `:0` otherwise.

### EXISTS

```text
EXISTS key
```

Returns `:1` when a live key exists and `:0` otherwise.

### EXPIRE

```text
EXPIRE key seconds
```

Sets a TTL in seconds. Negative values are rejected. Returns `:1` if the key was updated and `:0` if the key does not exist.

### TTL

```text
TTL key
```

Returns:

- `>= 0`: remaining TTL in seconds
- `-1`: key exists without expiration
- `-2`: key does not exist

### PERSIST

```text
PERSIST key
```

Removes a TTL and returns `:1` if one was removed.

### KEYS

```text
KEYS
```

Returns an array of all live keys sorted lexicographically.

### FLUSH

```text
FLUSH
```

Clears all keys and returns `+OK`.

### INFO

```text
INFO
```

Returns a bulk string containing one metric per line.

### Extended Commands

```text
MSET key1 value1 key2 value2
MGET key1 key2
INCR counter
DECR counter
APPEND key suffix
STRLEN key
```

Values are whitespace-delimited. Arbitrary binary strings and quoted strings are not supported in this protocol version.

## Pipelining

Clients may send multiple commands without waiting for each response:

```text
SET a 1
GET a
DEL a
GET a
```

Responses are sent in the same order.
