# Lesson 3 — Bytes and numbers

🚀 **Explorer** · 🔧 Builder · about 20 minutes

---

## ❓ The question

You want to tell a spacecraft the number **250**. You cannot send the word
"250" — a radio carries only ones and zeros. So how do you write a number down
in a way that a completely different computer, built by a different company in
a different country, reads back as the *same* number?

This sounds trivial. It is not, and getting it wrong has broken real missions.

## 💡 The idea

Big numbers do not fit in one byte. A byte holds 0 to 255. So a number like
305,419,896 needs four bytes — and now you have a choice nobody warned you
about: **which end goes first?**

```
  The number 305,419,896  (hex 0x12345678)

  BIG endian     12 34 56 78     most important part first
  LITTLE endian  78 56 34 12     backwards
```

Both are correct. Both are used. Your laptop almost certainly uses little
endian internally. And if one side writes big and the other reads little:

```
      sent:   305,419,896
      read: 2,018,915,346
```

Same four bytes. Completely different number. If that was a battery voltage,
mission control would think the spacecraft was at two billion volts.

## 👀 See it

```bash
python3 learn/toolbox/byte_order.py
```

That program shows you the same number both ways, the disaster when the two
disagree, and then something stranger — how negative numbers and decimals are
stored.

## 💡 The agreement

Space engineers solved this the only way it can be solved: everybody agreed on
one, and wrote it down.

> **CCSDS says: everything on the wire is BIG endian.**

Every spacecraft. Every ground station. Every agency. No negotiation.

It is sometimes called "network byte order" because the internet made the same
choice, for the same reason.

## 🔍 In the code

Because there is one rule, there can be one place that implements it. In this
repository that place is [`fsw/core/bytes.hpp`](../../fsw/core/bytes.hpp):

```cpp
bool write_uint32(uint32_t v) {
    const uint8_t b[4] = { static_cast<uint8_t>(v >> 24),   // most significant
                           static_cast<uint8_t>(v >> 16),
                           static_cast<uint8_t>(v >> 8),
                           static_cast<uint8_t>(v) };       // least significant
    return raw(b, 4);
}
```

`v >> 24` means "shift the bits right 24 places", which brings the top byte
down where it can be stored. Then 16, then 8, then the bottom byte. Most
significant first — big endian, by construction.

**Everything else in the flight software calls `write_uint32()` and never
thinks about byte order again.** One place to get right. One place to test.

## 🧪 Try it — the poisoned writer

Open [`fsw/core/bytes.hpp`](../../fsw/core/bytes.hpp) and look for this comment:

> Once any write overflows the buffer the writer becomes "poisoned": every
> later write is a no-op and `ok()` stays false.

Why would anyone design it that way? Consider writing 20 fields into a packet.
Without poisoning:

```cpp
if (!w.write_uint32(a)) return false;
if (!w.write_uint32(b)) return false;
if (!w.write_uint32(c)) return false;
... seventeen more times ...
```

With poisoning:

```cpp
w.write_uint32(a);
w.write_uint32(b);
w.write_uint32(c);
...
if (!w.ok()) { /* something went wrong somewhere */ }
```

One check instead of twenty. Fewer places to forget one. In flight software,
"fewer places to make a mistake" is worth a great deal.

**Experiment:** run the tests that prove it works:

```bash
./build/tests/fsw_tests 2>&1 | grep -A2 poisoned
```

## 🎓 Go deeper — three ways numbers surprise you

**Negative numbers.** `-1` stored as a 32-bit integer is `FF FF FF FF` — every
bit set. That is *two's complement*, and it exists so that the same addition
circuit works for positive and negative numbers. Counting down past zero wraps
around to the top.

**Decimals are not exact.** Try this:

```bash
python3 -c "print(0.1 + 0.2)"
```

You get `0.30000000000000004`. Floating point stores numbers as
sign × fraction × 2^exponent, and 0.1 has no exact binary representation, just
as ⅓ has no exact decimal one. This is why comparing decimals with `==` is a
habit that will eventually bite you, and why the tests in this repository use
`CHECK_NEAR` with a tolerance instead.

**Silent truncation.** Put 70,000 into a 16-bit field and you get 4,464 — the
top bits are simply discarded, quietly. This is why the build in this
repository uses `-Wconversion -Werror`: the compiler refuses to compile code
that might silently narrow a value. It is annoying about a dozen times and then
saves you once, spectacularly.

## ✅ Check yourself

1. What are the four bytes of 1,000 in big endian? (Hint: 1000 = 0x000003E8.)
2. Why did CCSDS pick big endian rather than letting each mission choose?
3. Your telemetry field is 16 bits and your counter reaches 70,000. What does
   the ground see?
4. Why does `ByteWriter` keep failing after the first failure instead of
   recovering?

---

**Next:** [Lesson 4 — Checksums](../04-checksums/) — how the spacecraft knows a
message wasn't damaged on the way.

<details>
<summary>✅ Answers</summary>

1. `00 00 03 E8`.
2. Because interoperability is the whole point. A ground station must be able
   to talk to a spacecraft it has never seen, built decades apart. "Each mission
   chooses" means every pairing needs negotiation and every pairing can get it
   wrong.
3. 70,000 − 65,536 = **4,464**. The value wraps, silently, and looks perfectly
   plausible — which is far worse than an obvious error.
4. So the caller can write many fields and check once. If it recovered, a
   later successful write would clear the error and hide the fact that data
   was lost in the middle.

</details>
