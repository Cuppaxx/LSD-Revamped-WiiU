# Lua 5.2.3 (vendored)

Unmodified source files of Lua 5.2.3 (from the official mirror https://github.com/lua/lua, tag `v5.2.3`), MIT licensed
(see [LICENSE](LICENSE); the notice is also at the end of `lua.h`).

`app/build.py` compiles every `*.c` here except `lua.c`, `luac.c`, `liolib.c`, `loslib.c`, `ldblib.c` and `linit.c`:
the game opens only the base, package, table, string, math, coroutine and bit32 libraries (`app/src/script.c`).
LSD: Revamped's scripts were written for MoonSharp (a Lua 5.2 interpreter for .NET), which is why 5.2 is used.

Set `LUA_DIR` to build against another copy.
