#include "generated_structs.h"
#include "reflection/type_traits.h"

#include <fstream>
#include <iostream>

bool operator==(iobuf const&, iobuf const&) { return true; }
bool operator!=(iobuf const&, iobuf const&) { return true; }
bool operator<=(iobuf const&, iobuf const&) { return true; }
bool operator>=(iobuf const&, iobuf const&) { return true; }
bool operator<(iobuf const&, iobuf const&) { return true; }
bool operator>(iobuf const&, iobuf const&) { return true; }

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
void init(T& t, data_gen& gen) {
    if constexpr (serde::is_envelope_v<T>) {
        serde::envelope_for_each_field(t, [&](auto& f) { init(f, gen); });
    } else if constexpr (reflection::is_std_optional_v<T>) {
        if (gen.get<std::uint8_t>() > 128) {
            t = std::make_optional<typename std::decay_t<T>::value_type>();
            init(*t, gen);
        } else {
            t = std::nullopt;
        }
    } else if constexpr (reflection::is_std_vector_v<T>) {
        t.resize(gen.get<uint8_t>());
        for (auto& v : t) {
            init(v, gen);
        }
    } else if constexpr (std::is_same_v<ss::sstring, std::decay_t<T>>) {
        t.resize(gen.get<uint8_t>());
        for (auto& v : t) {
            v = gen.get<char>();
        }
    } else if constexpr (std::is_same_v<iobuf, std::decay_t<T>>) {
        // TODO fill iobuf like string
    } else {
        t = gen.get<T>();
    }
}

template<typename... T, std::size_t... I>
std::tuple<T...> init(data_gen& gen, std::index_sequence<I...>) {
    auto structs = std::tuple<T...>{};
    (init(std::get<I>(structs), gen), ...);
    return structs;
}

template<typename T>
void serialize(iobuf& iob, T const& t) {
    iob = serde::to_iobuf(t);
}

template<typename... T, std::size_t... I>
std::array<iobuf, sizeof...(T)>
serialize(std::tuple<T...> const& structs, std::index_sequence<I...>) {
    auto target = std::array<iobuf, sizeof...(T)>{};
    (serialize(target[I], std::get<I>(structs)), ...);
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
struct type_list {};

template<typename... T>
bool test_success(type_list<T...>, data_gen& gen) {
    constexpr auto const idx_seq = std::index_sequence_for<T...>();
    auto const structs = init<T...>(gen, idx_seq);
    return test<T...>(structs, serialize(structs, idx_seq), idx_seq);
}

template<typename... T1, typename... T2>
bool test_failure(type_list<T1...>, type_list<T2...>, data_gen& gen) {
    constexpr auto const idx_seq = std::index_sequence_for<T1...>();
    auto const structs = init<T1...>(gen, idx_seq);
    return test<T2...>(structs, serialize(structs, idx_seq), idx_seq);
}

#if defined(MAIN)
int main(int argc, char** argv) {
    if (argc != 2) {
        std::cout << "usage: " << argv[0] << " INPUT\n";
        return 1;
    }

    auto in = std::ifstream{argv[1], std::ios_base::binary};
    auto str = std::string{};

    in.seekg(0, std::ios::end);
    str.reserve(in.tellg());
    in.seekg(0, std::ios::beg);

    str.assign(
      (std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    try {
        using types = type_list<
          my_struct,
          my_struct_1,
          my_struct_2,
          my_struct_3,
          my_struct_4,
          my_struct_5,
          my_struct_6,
          my_struct_7,
          my_struct_8,
          my_struct_9>;
        auto generator = data_gen{
          reinterpret_cast<std::uint8_t const*>(str.data()), str.size()};
        test_success(types{}, generator);
        test_failure(types{}, types{}, generator);
    } catch (...) {
    }
};
#else
extern "C" int LLVMFuzzerTestOneInput(uint8_t const* data, size_t size) {
    if (size == 0) {
        return 0;
    }
    try {
        using types = type_list<
          my_struct,
          my_struct_1,
          my_struct_2,
          my_struct_3,
          my_struct_4,
          my_struct_5,
          my_struct_6,
          my_struct_7,
          my_struct_8,
          my_struct_9>;
        auto generator = data_gen{data, size};
        test_success(types{}, generator);
        test_failure(types{}, types{}, generator);
    } catch (...) {
    }
    return 0;
}
#endif
