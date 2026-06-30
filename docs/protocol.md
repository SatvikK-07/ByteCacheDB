# Protocol

ByteCacheDB accepts one text command per `\n` or `\r\n` terminated line. Responses use
Redis-style type prefixes but the protocol is not RESP-compatible.

| Prefix | Meaning |
| --- | --- |
| `+` | simple string |
| `-ERR` | error |
| `:` | integer |
| `$N` | bulk string with byte length |
| `$-1` | missing value |
| `*N` | array |

Arguments normally end at whitespace. Double quotes preserve spaces and empty values:

```text
SET greeting "hello systems world"
SET empty ""
```

Inside quotes, `\"`, `\\`, `\n`, `\r`, and `\t` are decoded. The protocol is text-only
and does not support arbitrary binary bytes.

## Commands

```text
PING
SET key value
GET key
DEL key
EXISTS key
EXPIRE key seconds
TTL key
PERSIST key
KEYS
FLUSH
INFO
MSET key value [key value ...]
MGET key [key ...]
INCR key
DECR key
APPEND key suffix
STRLEN key
SAVE
```

`TTL` returns `-1` for a live key without expiry and `-2` for a missing/expired key.
Integer commands reject non-integer values and overflow. `SAVE` atomically publishes the
configured checkpoint snapshot.

`EXPIREAT key epoch_millis` is accepted for AOF/snapshot recovery but is an internal
command rather than part of the public client surface.

## Pipelining

Clients may send multiple complete lines before reading. ByteCacheDB executes and returns
them in connection order:

```text
SET a 1
GET a
DEL a
GET a
```
