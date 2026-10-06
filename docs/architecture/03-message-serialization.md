# Message Serialization — duplication & consolidation

> 14 message types under `types/messages/loramesher/` (~2,177 L of `.cpp`) plus ~6 header
> pairs repeat the same serialization skeleton. The serialization *utilities* are fine; the
> duplication is in the per-type *calling pattern*. Drives WS-3.

## What is NOT the problem

`utils/byte_operations.h` (263 L) provides `ByteSerializer` / `ByteDeserializer` with
`WriteUintN` / `ReadUintN`. They are clean, well-factored, and used consistently. Do **not**
touch them.

## What IS the problem: the repeated skeleton

Each message type re-implements the same five shapes by hand:

1. **`Create(dest, src, …fields, payload)`** — size-validate, build header, construct.
2. **`CreateFromSerialized(vector<uint8_t>)`** — size guard → `Header::Deserialize` → extract
   payload → construct, with a `LOG_ERROR; return nullopt` after each step.
3. **`CreateFromBaseMessage(const BaseMessage&)`** — type check → payload size check →
   deserialize fields → construct.
4. **`ToBaseMessage()`** — allocate a 255-byte `std::array` on the stack, serialize fields +
   payload into it, wrap in a `BaseMessage`.
5. **`Serialize()`** — allocate `vector`, serialize header + payload.

Plus, in every header's `Deserialize`, the read-then-null-check idiom repeats per field:

```cpp
auto ttl = d.ReadUint8();
if (!ttl) { LOG_ERROR("Failed to read ttl"); return std::nullopt; }
```

Per-file `.cpp` sizes (verified): `routing_table_message` 293, `sync_beacon_message` 258,
`data_message` 216, `join_response_message` 191, `broadcast_message`/`join_request_message`
185, `group_message` 137, headers 88–152. Structural similarity 80–90%.

## Consolidation design

Two reusable bases + one helper. Keep them inline-light and use out-of-line common bodies /
`extern template` so flash size does not grow (templates instantiate per type otherwise).

### 1. `RequireField` helper (smallest, do first)
```cpp
// types/messages/serialization_helpers.hpp
template <typename T>
std::optional<T> RequireField(std::optional<T> v, const char* name) {
    if (!v) { LOG_ERROR("Failed to deserialize field: %s", name); return std::nullopt; }
    return v;
}
```
Collapses the per-field read-null-check-log idiom (80+ sites).

### 2. `HeaderSerializerMixin<Derived, TYPE, FIELDS_SIZE>` (CRTP)
Provides `Serialize` (calls `BaseHeader::Serialize` then `Derived::SerializeFields`),
`Deserialize` (calls `BaseHeader::Deserialize`, checks `GetType() == TYPE`, then
`Derived::DeserializeFields`), and `GetSize`. Each header keeps only `SerializeFields` /
`DeserializeFields` + getters.

### 3. `MessageContainer<HeaderType>` base
Provides the generic `Create` / `CreateFromSerialized` / `CreateFromBaseMessage` /
`ToBaseMessage` / `Serialize`, parameterized by the header type and its `MessageType`. Each
message keeps only its custom field accessors + any non-generic construction.

## Estimated effect
~2,177 L of message `.cpp` + ~700 L of header `.cpp` → ~1,200–1,400 L total, with the
boilerplate centralized and the wire format unchanged (behavior-preserving).

## Migration & risk
- Each message type has dedicated tests under `test/types/test_messages/` — migrate a few
  types per phase (≤5 files), run those tests each time.
- **Wire format must not change** — `*FieldsSize()` values and byte order are part of the
  protocol. Add a round-trip test (`Serialize` → `CreateFromSerialized` equals original) for
  every migrated type before refactoring it.
- Flash-size guard: after the CRTP/template introduction, compare `pio run -e esp32` firmware
  size before/after; if templates bloat, push common logic out-of-line.

## Validation
- File counts/sizes from `wc -l src/types/messages/loramesher/*.cpp` (14 files, 2,177 L).
- `ByteSerializer`/`ByteDeserializer` usage confirmed consistent across the types.
