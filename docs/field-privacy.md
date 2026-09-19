# Field privacy

Owner types use `private:` sections inside their struct declarations. Fields
outside a visibility section remain public; `public:` can make that intent explicit.

```elisa
module Counter:
    struct Counter:
        private:
            value: mutable i64

    def Counter() -> Counter:
        Counter{value: 0}

    def value(counter: Counter&) -> i64:
        counter.value
```

The declaring module and its descendants can access private fields. External
modules can hold and pass the public type, call its constructor, and call public
functions. They cannot read or assign its private fields or bypass construction
with a brace literal or `zeroed` initialization (including nested stored values). References, aliases, block results, and generic return
values retain the type's privacy boundary. Destructuring and record updates are
field accesses too.

These are compile-time restrictions. The struct keeps its normal size, alignment,
field order, and calling convention. Privacy does not add an allocation or a
runtime access check. It is a module boundary, not protection against explicitly
unsafe memory operations.

The engine applies this boundary to `EntityIdAllocator`, `World`, `SceneRecorder`,
`FakeBridge::Bridge`, and `Lifetime::Queue`. Rollback belongs to `World::Rollback`.
Tests that deliberately construct invalid internal state live in child modules;
external rejection fixtures live under `test/negative`.

`scripts/check.elisascript` checks that outside field reads, writes, and allocator
construction fail with privacy diagnostics. The module hygiene script checks
module naming and constructor conventions; field access is enforced by the
compiler.
