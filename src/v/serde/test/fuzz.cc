#include "generated_structs.h"
#include "reflection/type_traits.h"

#if defined(MAIN)
#include <fstream>
#endif
#include <iostream>

constexpr auto const max_depth = 3;

template<typename... T1, typename... T2, std::size_t... I>
bool eq(
  std::tuple<T1...> const& a,
  std::tuple<T2...> const& b,
  std::index_sequence<I...>) {
    return ((std::get<I>(a) == std::get<I>(b)) && ...);
}

template<
  typename T1,
  typename T2,
  typename std::enable_if_t<
    serde::is_envelope_v<T1> && serde::is_envelope_v<T2>,
    void*> = nullptr>
bool operator==(T1 const& a, T2 const& b) {
    return eq(
      envelope_to_tuple(a),
      envelope_to_tuple(b),
      std::make_index_sequence<std::min(
        reflection::arity<T1>() - 1, reflection::arity<T2>() - 1)>());
}

struct data_gen {
    data_gen(std::uint8_t const* data, std::size_t const size)
      : _data{data}
      , _size{size} {}

    template<
      typename T,
      std::enable_if_t<std::is_trivially_copyable_v<T>>* = nullptr>
    T get() {
        auto val = T{};
        for (auto i = 0U; i != sizeof(val); ++i) {
            auto const byte = get_byte();
            std::memcpy(reinterpret_cast<std::uint8_t*>(&val) + i, &byte, 1);
        }
        return val;
    }

    std::uint8_t get_byte() {
        auto const d = _data[_i];
        ++_i;
        if (_i == _size) {
            _i = 0U;
        }
        return d;
    }

    std::uint8_t const* _data{};
    std::size_t _size{};
    std::size_t _i{};
};

template<typename T>
void init(T& t, data_gen& gen, int depth = 0) {
    if constexpr (serde::is_envelope_v<T>) {
        serde::envelope_for_each_field(
          t, [&](auto& f) { init(f, gen, depth + 1); });
    } else if constexpr (reflection::is_std_optional_v<T>) {
        if (
          depth != max_depth
          && gen.get<std::uint8_t>()
               > std::numeric_limits<std::uint8_t>::max() / 2) {
            t = std::make_optional<typename std::decay_t<T>::value_type>();
            init(*t, gen, depth + 1);
        } else {
            t = std::nullopt;
        }
    } else if constexpr (reflection::is_std_vector_v<T>) {
        if (depth != max_depth) {
            t.resize(gen.get<uint8_t>());
            for (auto& v : t) {
                init(v, gen, depth + 1);
            }
        }
    } else if constexpr (std::is_same_v<ss::sstring, std::decay_t<T>>) {
        t.resize(gen.get<uint8_t>());
        for (auto& v : t) {
            v = (gen.get<char>() & std::numeric_limits<char>::max());
        }
    } else if constexpr (std::is_same_v<iobuf, std::decay_t<T>>) {
        auto s = ss::sstring{};
        init(s, gen, depth + 1);
        t.append(std::move(s).release());
    } else {
        t = gen.get<T>();
    }
}

template<typename... T, std::size_t... I>
std::tuple<T...> init(data_gen gen, std::index_sequence<I...>) {
    auto structs = std::tuple<T...>{};
    (init(std::get<I>(structs), gen), ...);
    return structs;
}

template<typename T>
void serialize(iobuf& iob, T&& t) {
    iob = serde::to_iobuf(std::forward<T>(t));
}

template<typename... T, std::size_t... I>
std::array<iobuf, sizeof...(T)>
serialize(std::tuple<T...>&& structs, std::index_sequence<I...>) {
    auto target = std::array<iobuf, sizeof...(T)>{};
    (serialize(target[I], std::move(std::get<I>(structs))), ...);
    return target;
}

template<typename T>
bool test(T const& orig, iobuf&& serialized) {
    return serde::from_iobuf<T>(std::move(serialized)) == orig;
}

template<typename... T, std::size_t... I>
bool test(
  std::tuple<T...> const& original,
  std::array<iobuf, sizeof...(T)>&& serialized,
  std::index_sequence<I...>) {
    return (test(std::get<I>(original), std::move(serialized[I])) && ...);
}

template<typename... T>
bool test_success(type_list<T...>, data_gen gen) {
    constexpr auto const idx_seq = std::index_sequence_for<T...>();
    return test(
      init<T...>(gen, idx_seq),
      serialize(init<T...>(gen, idx_seq), idx_seq),
      idx_seq);
}

template<typename... T1, typename... T2>
bool test_failure(type_list<T1...>, type_list<T2...>, data_gen gen) {
    constexpr auto const idx_seq = std::index_sequence_for<T1...>();
    return test(
      init<T1...>(gen, idx_seq),
      serialize(init<T2...>(gen, idx_seq), idx_seq),
      idx_seq);
}

template<typename... T1, typename... T2>
bool test_version_upgrade(type_list<T1...>, type_list<T2...>, data_gen gen) {}

#if defined(MAIN)
int main(int argc, char** argv) {
    if (argc != 2) {
        std::cout << "usage: " << argv[0] << " INPUT\n";
        return 1;
    }

    auto in = std::ifstream{};
    in.exceptions(std::ios::failbit | std::ios::badbit);
    in.open(argv[1], std::ios_base::binary);
    auto str = std::string{};

    in.seekg(0, std::ios::end);
    str.reserve(in.tellg());
    in.seekg(0, std::ios::beg);

    str.assign(
      (std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto const data = reinterpret_cast<std::uint8_t const*>(str.data());
    auto const size = str.size();

    try {
        test_success(types_2{}, {data, size});
    } catch (...) {
        __builtin_trap();
    }

    auto failed = false;
    try {
        test_failure(types_2{}, types_3{}, {data, size});
    } catch (...) {
        failed = true;
    }
    if (!failed) {
        __builtin_trap();
    }
};
#else
extern "C" int LLVMFuzzerTestOneInput(uint8_t const* data, size_t size) {
    if (size == 0) {
        return 0;
    }

    try {
        test_success(types_21{}, {data, size});
        test_success(types_31{}, {data, size});
    } catch (...) {
        __builtin_trap();
    }

    auto failed = false;
    try {
        test_failure(types_21{}, types_31{}, {data, size});
    } catch (...) {
        failed = true;
    }
    if (!failed) {
        __builtin_trap();
    }

    return 0;
}
#endif
