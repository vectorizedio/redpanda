#pragma once
#include "bytes/details/io_allocation_size.h"
#include "bytes/details/io_byte_iterator.h"
#include "bytes/details/io_fragment.h"
#include "bytes/details/io_iterator_consumer.h"
#include "bytes/details/io_placeholder.h"
#include "bytes/details/out_of_range.h"
#include "likely.h"
#include "seastarx.h"
#include "utils/intrusive_list_helpers.h"

#include <seastar/core/iostream.hh>
#include <seastar/core/scattered_message.hh>
#include <seastar/core/shared_ptr.hh>
#include <seastar/core/smp.hh>
#include <seastar/core/temporary_buffer.hh>

#include <fmt/format.h>

#include <list>
#include <ostream>
#include <stdexcept>
#include <type_traits>

/// our iobuf is a fragmented buffer. modeled after
/// folly::iobufqueue.h - it supports prepend and append, but no
/// operations in the middle. It provides a forward iterator for
/// byte scanning and parsing. This is intended to be the workhorse
/// of our data path.
/// Noteworthy Operations:
/// Append/Prepend - O(1)
/// operator==, operator!=  - O(N)
///
class iobuf {
    // Not a lightweight object.
    // 16 bytes for std::list
    // 8  bytes for _alloc_sz
    // 8  bytes for _size_bytes
    // -----------------------
    //
    // 32 bytes total.

    // Each fragment:
    // 24  of ss::temporary_buffer<>
    // 16  for left,right pointers
    // 8   for consumed capacity
    // -----------------------
    //
    // 48 bytes total

public:
    using fragment = details::io_fragment;
    using container = intrusive_list<fragment, &fragment::hook>;
    using iterator = typename container::iterator;
    using reverse_iterator = typename container::reverse_iterator;
    using const_iterator = typename container::const_iterator;
    using iterator_consumer = details::io_iterator_consumer;
    using byte_iterator = details::io_byte_iterator;
    using placeholder = details::io_placeholder;

    // NOLINTNEXTLINE
    iobuf() noexcept {
        // nothing allocates memory, but boost intrusive list is not marked as
        // noexcept
    }
    ~iobuf() noexcept;
    iobuf(iobuf&& x) noexcept
      : _frags(std::move(x._frags))
      , _size(x._size)
      , _alloc_sz(x._alloc_sz) {
        x.clear();
    }
    iobuf& operator=(iobuf&& x) noexcept {
        if (this != &x) {
            this->~iobuf();
            new (this) iobuf(std::move(x));
        }
        return *this;
    }
    iobuf(const iobuf&) = delete;
    iobuf& operator=(const iobuf&) = delete;

    /// override to pass in any container of temp bufs
    template<
      typename Range,
      typename = std::enable_if<
        std::is_same_v<typename Range::value_type, ss::temporary_buffer<char>>>>
    explicit iobuf(Range&& r) {
        static_assert(
          std::is_rvalue_reference_v<decltype(r)>,
          "Must be an rvalue. Use std::move()");
        for (auto& buf : r) {
            append(std::move(buf));
        }
    }

    /// shares the underlying temporary buffers
    iobuf share(size_t pos, size_t len);
    /// makes a copy of the data
    iobuf copy() const;

    /// makes a reservation with the internal storage. adds a layer of
    /// indirection instead of raw byte pointer to allow the
    /// details::io_fragments to internally compact buffers as long as they
    /// don't violate the reservation size here
    placeholder reserve(size_t reservation);

    /// only ensures that a segment of at least reservation is avaible
    /// as an empty details::io_fragment
    void reserve_memory(size_t reservation);

    /// append src + len into storage
    void append(const char*, size_t);
    /// appends the contents of buffer; might pack values into existing space
    void append(ss::temporary_buffer<char>);
    /// appends the contents of buffer; might pack values into existing space
    void append(iobuf);
    /// prepends the _the buffer_ as iobuf::details::io_fragment::full{}
    void prepend(ss::temporary_buffer<char>);
    /// prepends the arg to this as iobuf::details::io_fragment::full{}
    void prepend(iobuf);
    /// used for iostreams
    void pop_front();
    void trim_front(size_t n);
    void clear();
    size_t size_bytes() const;
    bool empty() const;
    /// compares that the _content_ is the same;
    /// ignores allocation strategy, and number of details::io_fragments
    /// it is a byte-per-byte comparator
    bool operator==(const iobuf&) const;
    bool operator!=(const iobuf&) const;

    iterator begin();
    iterator end();
    reverse_iterator rbegin();
    reverse_iterator rend();
    const_iterator begin() const;
    const_iterator end() const;
    const_iterator cbegin() const;
    const_iterator cend() const;

private:
    size_t available_bytes() const;
    void create_new_fragment(size_t);

    container _frags;
    size_t _size{0};
    details::io_allocation_size _alloc_sz;

    friend std::ostream& operator<<(std::ostream&, const iobuf&);
};

inline void iobuf::clear() {
    while (!_frags.empty()) {
        _frags.pop_back_and_dispose([](fragment* f) {
            delete f; // NOLINT
        });
    }
    _size = 0;
    _alloc_sz.reset();
}
inline iobuf::~iobuf() noexcept { clear(); }
inline iobuf::iterator iobuf::begin() { return _frags.begin(); }
inline iobuf::iterator iobuf::end() { return _frags.end(); }
inline iobuf::reverse_iterator iobuf::rbegin() { return _frags.rbegin(); }
inline iobuf::reverse_iterator iobuf::rend() { return _frags.rend(); }
inline iobuf::const_iterator iobuf::begin() const { return _frags.cbegin(); }
inline iobuf::const_iterator iobuf::end() const { return _frags.cend(); }
inline iobuf::const_iterator iobuf::cbegin() const { return _frags.cbegin(); }
inline iobuf::const_iterator iobuf::cend() const { return _frags.cend(); }

inline bool iobuf::operator==(const iobuf& o) const {
    if (_size != o._size) {
        return false;
    }
    auto lhs_begin = byte_iterator(cbegin(), cend());
    auto lhs_end = byte_iterator(cend(), cend());
    auto rhs = byte_iterator(o.cbegin(), o.cend());
    while (lhs_begin != lhs_end) {
        char l = *lhs_begin;
        char r = *rhs;
        if (l != r) {
            return false;
        }
        ++lhs_begin;
        ++rhs;
    }
    return true;
}
inline bool iobuf::operator!=(const iobuf& o) const { return !(*this == o); }
inline bool iobuf::empty() const { return _frags.empty(); }
inline size_t iobuf::size_bytes() const { return _size; }

inline size_t iobuf::available_bytes() const {
    if (_frags.empty()) {
        return 0;
    }
    return _frags.back().available_bytes();
}

inline void iobuf::create_new_fragment(size_t sz) {
    auto asz = _alloc_sz.next_allocation_size(sz);
    auto f = new fragment(ss::temporary_buffer<char>(asz), fragment::empty{});
    _frags.push_back(*f);
}
inline iobuf::placeholder iobuf::reserve(size_t sz) {
    reserve_memory(sz);
    _size += sz;
    auto it = std::prev(_frags.end());
    placeholder p(it, it->size(), sz);
    it->reserve(sz);
    return p;
}
/// only ensures that a segment of at least reservation is avaible
/// as an empty details::io_fragment
inline void iobuf::reserve_memory(size_t reservation) {
    if (auto b = available_bytes(); b < reservation) {
        if (b > 0) {
            _frags.back().trim();
        }
        create_new_fragment(reservation); // make space if not enough
    }
}

[[gnu::always_inline]] void inline iobuf::prepend(
  ss::temporary_buffer<char> b) {
    _size += b.size();
    auto f = new fragment(std::move(b), fragment::full{});
    _frags.push_front(*f);
}
[[gnu::always_inline]] void inline iobuf::prepend(iobuf b) {
    while (!b._frags.empty()) {
        b._frags.pop_back_and_dispose([this](fragment* f) {
            prepend(f->share());
            delete f; // NOLINT
        });
    }
}
[[gnu::always_inline]] void inline iobuf::append(const char* ptr, size_t size) {
    if (size == 0) {
        return;
    }
    if (likely(size <= available_bytes())) {
        _size += _frags.back().append(ptr, size);
        return;
    }
    size_t i = 0;
    while (size > 0) {
        if (available_bytes() == 0) {
            create_new_fragment(size);
        }
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        const size_t sz = _frags.back().append(ptr + i, size);
        _size += sz;
        i += sz;
        size -= sz;
    }
}

/// appends the contents of buffer; might pack values into existing space
[[gnu::always_inline]] inline void iobuf::append(ss::temporary_buffer<char> b) {
    if (b.size() <= available_bytes()) {
        append(b.get(), b.size());
        return;
    }
    if (available_bytes() > 0) {
        if (_frags.back().is_empty()) {
            _frags.pop_back();
        } else {
            // happens when we are merge iobufs
            _frags.back().trim();
            _alloc_sz.reset();
        }
    }
    _size += b.size();
    // intrusive list manages the lifetime
    auto f = new fragment(std::move(b), fragment::full{});
    _frags.push_back(*f);
}
/// appends the contents of buffer; might pack values into existing space
inline void iobuf::append(iobuf o) {
    if (available_bytes()) {
        _frags.back().trim();
    }
    while (!o._frags.empty()) {
        o._frags.pop_front_and_dispose([this](fragment* f) {
            append(f->share());
            delete f; // NOLINT
        });
    }
}
/// used for iostreams
inline void iobuf::pop_front() {
    _size -= _frags.front().size();
    _frags.pop_front_and_dispose([](fragment* f) {
        delete f; // NOLINT
    });
}
inline void iobuf::trim_front(size_t n) {
    while (!_frags.empty()) {
        auto& f = _frags.front();
        if (f.size() > n) {
            _size -= n;
            f.trim_front(n);
            return;
        }
        pop_front();
    }
}

/// \brief wraps an iobuf so it can be used as an input stream data source
ss::input_stream<char> make_iobuf_input_stream(iobuf io);

/// \brief wraps the iobuf to be used as an output stream sink
ss::output_stream<char> make_iobuf_output_stream(iobuf io);

/// \brief exactly like input_stream<char>::read_exactly but returns iobuf
ss::future<iobuf> read_iobuf_exactly(ss::input_stream<char>& in, size_t n);

/// \brief keeps the iobuf in the deferred destructor of scattered_msg<char>
/// and wraps each details::io_fragment as a scattered_message<char>::static()
/// const char*
ss::scattered_message<char> iobuf_as_scattered(iobuf b);

std::vector<iobuf> iobuf_share_foreign_n(iobuf, size_t n);
