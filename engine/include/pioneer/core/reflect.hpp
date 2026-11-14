#pragma once
// Pioneer Engine - a little compile-time reflection for plain structs.
//
// Plain structs are written to saves, replays and network messages as raw
// bytes. The gaps the compiler leaves between their fields (padding) hold
// whatever was in memory, so two machines in exactly the same state could
// write different bytes. `podMask<T>()` says which bytes of a T are real
// data; the byte writer blanks the rest. Saved state is then canonical: equal
// states give equal bytes, and a checksum of the bytes means something.
//
// `forEachField(obj, fn)` visits the fields of an aggregate (up to 24), in
// order. Structs with a C array member can't be counted this way: give such a
// type no padding, or write it field by field.

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace pioneer::reflect {

namespace detail {
struct Any {
  template <class T>
  operator T() const;  // never defined: only used in unevaluated contexts
};
template <class T, size_t... I>
constexpr bool braceInit(std::index_sequence<I...>) {
  return requires { T{(static_cast<void>(I), Any{})...}; };
}
template <class T, size_t N>
constexpr bool takes = braceInit<T>(std::make_index_sequence<N>{});

template <class T, size_t N = 0>
constexpr size_t countFields() {
  if constexpr (N >= 24) return N;
  else if constexpr (takes<T, N + 1>) return countFields<T, N + 1>();
  else return N;
}

template <class T>
struct IsStdArray : std::false_type {};
template <class T, size_t N>
struct IsStdArray<std::array<T, N>> : std::true_type {};
}  // namespace detail

// Number of fields of an aggregate.
template <class T>
constexpr size_t fieldCount = detail::countFields<std::remove_cvref_t<T>>();

// Calls fn(field) for every field of the aggregate `t`, in declaration order.
template <class T, class Fn>
constexpr void forEachField(T&& t, Fn&& fn) {
  using U = std::remove_cvref_t<T>;
  static_assert(std::is_aggregate_v<U>, "forEachField() needs an aggregate (a plain struct)");
  constexpr size_t n = fieldCount<U>;
  static_assert(n <= 24, "forEachField(): more than 24 fields");
#define PNR_RF(...)                \
  {                                \
    auto& [__VA_ARGS__] = t;       \
    visit(fn, __VA_ARGS__);        \
  }
  auto visit = [](auto& f, auto&... fields) { (f(fields), ...); };
  if constexpr (n == 0) {
  } else if constexpr (n == 1) PNR_RF(a)
  else if constexpr (n == 2) PNR_RF(a, b)
  else if constexpr (n == 3) PNR_RF(a, b, c)
  else if constexpr (n == 4) PNR_RF(a, b, c, d)
  else if constexpr (n == 5) PNR_RF(a, b, c, d, e)
  else if constexpr (n == 6) PNR_RF(a, b, c, d, e, f)
  else if constexpr (n == 7) PNR_RF(a, b, c, d, e, f, g)
  else if constexpr (n == 8) PNR_RF(a, b, c, d, e, f, g, h)
  else if constexpr (n == 9) PNR_RF(a, b, c, d, e, f, g, h, i)
  else if constexpr (n == 10) PNR_RF(a, b, c, d, e, f, g, h, i, j)
  else if constexpr (n == 11) PNR_RF(a, b, c, d, e, f, g, h, i, j, k)
  else if constexpr (n == 12) PNR_RF(a, b, c, d, e, f, g, h, i, j, k, l)
  else if constexpr (n == 13) PNR_RF(a, b, c, d, e, f, g, h, i, j, k, l, m)
  else if constexpr (n == 14) PNR_RF(a, b, c, d, e, f, g, h, i, j, k, l, m, o)
  else if constexpr (n == 15) PNR_RF(a, b, c, d, e, f, g, h, i, j, k, l, m, o, p)
  else if constexpr (n == 16) PNR_RF(a, b, c, d, e, f, g, h, i, j, k, l, m, o, p, q)
  else if constexpr (n == 17) PNR_RF(a, b, c, d, e, f, g, h, i, j, k, l, m, o, p, q, r)
  else if constexpr (n == 18) PNR_RF(a, b, c, d, e, f, g, h, i, j, k, l, m, o, p, q, r, s)
  else if constexpr (n == 19) PNR_RF(a, b, c, d, e, f, g, h, i, j, k, l, m, o, p, q, r, s, u)
  else if constexpr (n == 20) PNR_RF(a, b, c, d, e, f, g, h, i, j, k, l, m, o, p, q, r, s, u, v)
  else if constexpr (n == 21) PNR_RF(a, b, c, d, e, f, g, h, i, j, k, l, m, o, p, q, r, s, u, v, w)
  else if constexpr (n == 22) PNR_RF(a, b, c, d, e, f, g, h, i, j, k, l, m, o, p, q, r, s, u, v, w, x)
  else if constexpr (n == 23) PNR_RF(a, b, c, d, e, f, g, h, i, j, k, l, m, o, p, q, r, s, u, v, w, x, y)
  else PNR_RF(a, b, c, d, e, f, g, h, i, j, k, l, m, o, p, q, r, s, u, v, w, x, y, z)
#undef PNR_RF
}

// True when every byte of a T is data (no padding): it can be written as is.
template <class T>
constexpr bool isPacked = std::has_unique_object_representations_v<T> || !std::is_class_v<T>;

namespace detail {
template <class T>
void markBytes(const T& obj, const uint8_t* base, uint8_t* mask) {
  if constexpr (isPacked<T>) {
    const size_t at = static_cast<size_t>(reinterpret_cast<const uint8_t*>(&obj) - base);
    for (size_t i = 0; i < sizeof(T); ++i) mask[at + i] = 0xFF;
  } else if constexpr (IsStdArray<T>::value) {
    for (const auto& item : obj) markBytes(item, base, mask);
  } else {
    forEachField(obj, [&](const auto& field) { markBytes(field, base, mask); });
  }
}
}  // namespace detail

// One byte per byte of T: 0xFF where a field lives, 0 where there is padding.
template <class T>
const std::array<uint8_t, sizeof(T)>& podMask() {
  static const std::array<uint8_t, sizeof(T)> mask = [] {
    std::array<uint8_t, sizeof(T)> m{};
    const T obj{};
    detail::markBytes(obj, reinterpret_cast<const uint8_t*>(&obj), m.data());
    return m;
  }();
  return mask;
}

}  // namespace pioneer::reflect
