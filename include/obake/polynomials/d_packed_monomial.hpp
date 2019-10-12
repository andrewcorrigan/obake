// Copyright 2019 Francesco Biscani (bluescarni@gmail.com)::polynomials
//
// This file is part of the obake library.
//
// This Source Code Form is subject to the terms of the Mozilla
// Public License v. 2.0. If a copy of the MPL was not distributed
// with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

#ifndef OBAKE_POLYNOMIALS_D_PACKED_MONOMIAL_HPP
#define OBAKE_POLYNOMIALS_D_PACKED_MONOMIAL_HPP

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <initializer_list>
#include <iterator>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include <boost/container/small_vector.hpp>

#include <mp++/integer.hpp>

#include <obake/config.hpp>
#include <obake/detail/ignore.hpp>
#include <obake/detail/limits.hpp>
#include <obake/detail/mppp_utils.hpp>
#include <obake/detail/to_string.hpp>
#include <obake/exceptions.hpp>
#include <obake/k_packing.hpp>
#include <obake/math/safe_cast.hpp>
#include <obake/math/safe_convert.hpp>
#include <obake/polynomials/monomial_homomorphic_hash.hpp>
#include <obake/polynomials/monomial_pow.hpp>
#include <obake/ranges.hpp>
#include <obake/symbols.hpp>
#include <obake/type_traits.hpp>

namespace obake
{

namespace polynomials
{

namespace detail
{

// Small helper to determine the container size
// we need to store n exponents in a dynamic packed
// monomial of type T. U must be an
// unsigned integral type.
template <typename T, typename U>
constexpr auto dpm_nexpos_to_vsize(const U &n) noexcept
{
    static_assert(is_integral_v<U> && !is_signed_v<U>);
    return n / T::psize + static_cast<U>(n % T::psize != 0u);
}

} // namespace detail

// Dynamic packed monomial.
#if defined(OBAKE_HAVE_CONCEPTS)
template <KPackable T, unsigned NBits>
    requires(NBits >= 3u)
    && (NBits <= static_cast<unsigned>(::obake::detail::limits_digits<T>))
#else
template <typename T, unsigned NBits,
          typename = ::std::enable_if_t<is_k_packable_v<T> && (NBits >= 3u)
                                        && (NBits <= static_cast<unsigned>(::obake::detail::limits_digits<T>))>>
#endif
        class d_packed_monomial
{
public:
    // Alias for NBits.
    static constexpr unsigned nbits = NBits;

    // The number of exponents to be packed into each T instance.
    static constexpr unsigned psize = static_cast<unsigned>(::obake::detail::limits_digits<T>) / NBits;

    // Alias for T.
    using value_type = T;

    // The container type.
    using container_t = ::boost::container::small_vector<T, 1>;

    // Default constructor.
    d_packed_monomial() = default;

    // Constructor from symbol set.
    explicit d_packed_monomial(const symbol_set &ss)
        : m_container(::obake::safe_cast<typename container_t::size_type>(
            detail::dpm_nexpos_to_vsize<d_packed_monomial>(ss.size())))
    {
    }

    // Constructor from input iterator and size.
#if defined(OBAKE_HAVE_CONCEPTS)
    template <typename It>
    requires InputIterator<It> &&SafelyCastable<typename ::std::iterator_traits<It>::reference, T>
#else
    template <
        typename It,
        ::std::enable_if_t<::std::conjunction_v<is_input_iterator<It>,
                                                is_safely_castable<typename ::std::iterator_traits<It>::reference, T>>,
                           int> = 0>
#endif
        explicit d_packed_monomial(It it, ::std::size_t n)
    {
        // Prepare the container.
        const auto vsize = detail::dpm_nexpos_to_vsize<d_packed_monomial>(n);
        m_container.resize(::obake::safe_cast<typename container_t::size_type>(vsize));

        ::std::size_t counter = 0;
        for (auto &out : m_container) {
            k_packer<T> kp(psize);

            // Keep packing until we get to psize or we have
            // exhausted the input values.
            auto j = 0u;
            for (; j < psize && counter < n; ++j, ++counter, ++it) {
                kp << ::obake::safe_cast<T>(*it);
            }

            // This will be executed only at the last iteration
            // of the for loop, and it will zero pad
            // the last element of the container if psize does not
            // divide exactly n.
            for (; j < psize; ++j) {
                kp << T(0);
            }

            out = kp.get();
        }
    }

private:
    struct input_it_ctor_tag {
    };
    template <typename It>
    explicit d_packed_monomial(input_it_ctor_tag, It b, It e)
    {
        while (b != e) {
            k_packer<T> kp(psize);

            auto j = 0u;
            for (; j < psize && b != e; ++j, ++b) {
                kp << ::obake::safe_cast<T>(*b);
            }

            for (; j < psize; ++j) {
                kp << T(0);
            }

            m_container.push_back(kp.get());
        }
    }

public:
    // Ctor from a pair of input iterators.
#if defined(OBAKE_HAVE_CONCEPTS)
    template <typename It>
    requires InputIterator<It> &&SafelyCastable<typename ::std::iterator_traits<It>::reference, T>
#else
    template <
        typename It,
        ::std::enable_if_t<::std::conjunction_v<is_input_iterator<It>,
                                                is_safely_castable<typename ::std::iterator_traits<It>::reference, T>>,
                           int> = 0>
#endif
        explicit d_packed_monomial(It b, It e) : d_packed_monomial(input_it_ctor_tag{}, b, e)
    {
    }

    // Ctor from input range.
#if defined(OBAKE_HAVE_CONCEPTS)
    template <typename Range>
    requires InputRange<Range> &&SafelyCastable<typename ::std::iterator_traits<range_begin_t<Range>>::reference, T>
#else
    template <
        typename Range,
        ::std::enable_if_t<::std::conjunction_v<
                               is_input_range<Range>,
                               is_safely_castable<typename ::std::iterator_traits<range_begin_t<Range>>::reference, T>>,
                           int> = 0>
#endif
        explicit d_packed_monomial(Range &&r)
        : d_packed_monomial(input_it_ctor_tag{}, ::obake::begin(::std::forward<Range>(r)),
                            ::obake::end(::std::forward<Range>(r)))
    {
    }

    // Ctor from init list.
#if defined(OBAKE_HAVE_CONCEPTS)
    template <typename U>
    requires SafelyCastable<const U &, T>
#else
    template <typename U, ::std::enable_if_t<is_safely_castable_v<const U &, T>, int> = 0>
#endif
        explicit d_packed_monomial(::std::initializer_list<U> l)
        : d_packed_monomial(input_it_ctor_tag{}, l.begin(), l.end())
    {
    }

    container_t &_container()
    {
        return m_container;
    }
    const container_t &_container() const
    {
        return m_container;
    }

private:
    container_t m_container;
};

// Implementation of key_is_zero(). A monomial is never zero.
template <typename T, unsigned NBits>
inline bool key_is_zero(const d_packed_monomial<T, NBits> &, const symbol_set &)
{
    return false;
}

// Implementation of key_is_one(). A monomial is one if all its exponents are zero.
template <typename T, unsigned NBits>
inline bool key_is_one(const d_packed_monomial<T, NBits> &d, const symbol_set &)
{
    return ::std::all_of(d._container().cbegin(), d._container().cend(), [](const T &n) { return n == T(0); });
}

// Comparisons.
template <typename T, unsigned NBits>
inline bool operator==(const d_packed_monomial<T, NBits> &d1, const d_packed_monomial<T, NBits> &d2)
{
    return d1._container() == d2._container();
}

template <typename T, unsigned NBits>
inline bool operator!=(const d_packed_monomial<T, NBits> &d1, const d_packed_monomial<T, NBits> &d2)
{
    return !(d1 == d2);
}

// Hash implementation.
template <typename T, unsigned NBits>
inline ::std::size_t hash(const d_packed_monomial<T, NBits> &d)
{
    // NOTE: the idea is that we will mix the individual
    // hashes for every pack of exponents via addition.
    ::std::size_t ret = 0;
    for (const auto &n : d._container()) {
        ret += static_cast<::std::size_t>(n);
    }
    return ret;
}

// Symbol set compatibility implementation.
template <typename T, unsigned NBits>
inline bool key_is_compatible(const d_packed_monomial<T, NBits> &d, const symbol_set &s)
{
    const auto s_size = s.size();
    const auto &c = d._container();

    // Determine the size the container must have in order
    // to be able to represent s_size exponents.
    const auto exp_size = detail::dpm_nexpos_to_vsize<d_packed_monomial<T, NBits>>(s_size);

    // Check if c has the expected size.
    if (c.size() != exp_size) {
        return false;
    }

    // We need to check if the encoded values in the container
    // are within the limits. If NBits is maximal,
    // we don't need any checking as all encoded
    // values representable by T are allowed.
    if constexpr (NBits == ::obake::detail::limits_digits<T>) {
        return true;
    } else {
        // Fetch the elimits corresponding to a packing size of psize.
        const auto &e_lim = ::obake::detail::k_packing_get_elimits<T>(d_packed_monomial<T, NBits>::psize);
        for (const auto &n : c) {
            if constexpr (is_signed_v<T>) {
                if (n < e_lim[0] || n > e_lim[1]) {
                    return false;
                }
            } else {
                if (n > e_lim) {
                    return false;
                }
            }
        }

        return true;
    }
}

// Implementation of stream insertion.
// NOTE: requires that d is compatible with s.
template <typename T, unsigned NBits>
inline void key_stream_insert(::std::ostream &os, const d_packed_monomial<T, NBits> &d, const symbol_set &s)
{
    assert(polynomials::key_is_compatible(d, s));

    constexpr auto psize = d_packed_monomial<T, NBits>::psize;

    const auto &c = d._container();
    auto s_it = s.cbegin();
    const auto s_end = s.cend();

    T tmp;
    bool wrote_something = false;
    for (const auto &n : c) {
        k_unpacker<T> ku(n, psize);

        for (auto j = 0u; j < psize && s_it != s_end; ++j, ++s_it) {
            ku >> tmp;

            if (tmp != T(0)) {
                // The exponent of the current variable
                // is nonzero.
                if (wrote_something) {
                    // We already printed something
                    // earlier, make sure we put
                    // the multiplication sign
                    // in front of the variable
                    // name.
                    os << '*';
                }
                // Print the variable name.
                os << *s_it;
                wrote_something = true;
                if (tmp != T(1)) {
                    // The exponent is not unitary,
                    // print it.
                    os << "**" << ::obake::detail::to_string(tmp);
                }
            }
        }
    }

    if (!wrote_something) {
        // We did not write anything to the stream.
        // It means that all variables have zero
        // exponent, thus we print only "1".
        os << '1';
    }
}

// Implementation of tex stream insertion.
// NOTE: requires that d is compatible with s.
template <typename T, unsigned NBits>
inline void key_tex_stream_insert(::std::ostream &os, const d_packed_monomial<T, NBits> &d, const symbol_set &s)
{
    assert(polynomials::key_is_compatible(d, s));

    constexpr auto psize = d_packed_monomial<T, NBits>::psize;

    const auto &c = d._container();
    auto s_it = s.cbegin();
    const auto s_end = s.cend();

    // Use separate streams for numerator and denominator
    // (the denominator is used only in case of negative powers).
    ::std::ostringstream oss_num, oss_den, *cur_oss;

    T tmp;
    // Go through a multiprecision integer for the stream
    // insertion. This allows us not to care about potential
    // overflow conditions when manipulating the exponents
    // below.
    ::mppp::integer<1> tmp_mp;
    for (const auto &n : c) {
        k_unpacker<T> ku(n, psize);

        for (auto j = 0u; j < psize && s_it != s_end; ++j, ++s_it) {
            // Extract the current exponent into
            // tmp_mp.
            ku >> tmp;
            tmp_mp = tmp;

            const auto sgn = tmp_mp.sgn();
            if (sgn != 0) {
                // Non-zero exponent, we will write something.
                if (sgn == 1) {
                    // Positive exponent, we will write
                    // to the numerator stream.
                    cur_oss = &oss_num;
                } else {
                    // Negative exponent: take the absolute value
                    // and write to the denominator stream.
                    tmp_mp.neg();
                    cur_oss = &oss_den;
                }

                // Print the symbol name.
                *cur_oss << '{' << *s_it << '}';

                // Raise to power, if the exponent is not one.
                if (!tmp_mp.is_one()) {
                    *cur_oss << "^{" << tmp_mp << '}';
                }
            }
        }
    }

    const auto num_str = oss_num.str(), den_str = oss_den.str();

    if (!num_str.empty() && !den_str.empty()) {
        // We have both negative and positive exponents,
        // print them both in a fraction.
        os << "\\frac{" << num_str << "}{" << den_str << '}';
    } else if (!num_str.empty() && den_str.empty()) {
        // Only positive exponents.
        os << num_str;
    } else if (num_str.empty() && !den_str.empty()) {
        // Only negative exponents, display them as 1/something.
        os << "\\frac{1}{" << den_str << '}';
    }
}

// Implementation of symbols merging.
// NOTE: requires that m is compatible with s, and ins_map consistent with s.
template <typename T, unsigned NBits>
inline d_packed_monomial<T, NBits> key_merge_symbols(const d_packed_monomial<T, NBits> &d,
                                                     const symbol_idx_map<symbol_set> &ins_map, const symbol_set &s)
{
    assert(polynomials::key_is_compatible(d, s));
    // The last element of the insertion map must be at most s.size(), which means that there
    // are symbols to be appended at the end.
    assert(ins_map.empty() || ins_map.rbegin()->first <= s.size());

    constexpr auto psize = d_packed_monomial<T, NBits>::psize;

    const auto &c = d._container();
    symbol_idx idx = 0;
    const auto s_size = s.size();
    auto map_it = ins_map.begin();
    const auto map_end = ins_map.end();
    T tmp;
    // NOTE: store the merged monomial in a temporary vector and then pack it
    // at the end.
    // NOTE: perhaps we could use a small_vector here with a static size
    // equal to psize, for the case in which everything fits in a single
    // packed value.
    ::std::vector<T> tmp_v;
    for (const auto &n : c) {
        k_unpacker<T> ku(n, psize);

        for (auto j = 0u; j < psize && idx < s_size; ++j, ++idx) {
            if (map_it != map_end && map_it->first == idx) {
                // We reached an index at which we need to
                // insert new elements. Insert as many
                // zeroes as necessary in the temporary vector.
                tmp_v.insert(tmp_v.end(), ::obake::safe_cast<decltype(tmp_v.size())>(map_it->second.size()), T(0));
                // Move to the next element in the map.
                ++map_it;
            }

            // Add the existing element to tmp_v.
            ku >> tmp;
            tmp_v.push_back(tmp);
        }
    }

    assert(idx == s_size);

    // We could still have symbols which need to be appended at the end.
    if (map_it != map_end) {
        tmp_v.insert(tmp_v.end(), ::obake::safe_cast<decltype(tmp_v.size())>(map_it->second.size()), T(0));
        assert(map_it + 1 == map_end);
    }

    return d_packed_monomial<T, NBits>(tmp_v);
}

// Implementation of monomial_mul().
// NOTE: requires a, b and out to be compatible with ss.
template <typename T, unsigned NBits>
inline void monomial_mul(d_packed_monomial<T, NBits> &out, const d_packed_monomial<T, NBits> &a,
                         const d_packed_monomial<T, NBits> &b, [[maybe_unused]] const symbol_set &ss)
{
    // Verify the inputs.
    assert(polynomials::key_is_compatible(a, ss));
    assert(polynomials::key_is_compatible(b, ss));
    assert(polynomials::key_is_compatible(out, ss));

    // NOTE: check whether using pointers + restrict helps here
    // (in which case we'd have to add the requirement to monomial_mul()
    // that out must be distinct from a/b).
    ::std::transform(a._container().cbegin(), a._container().cend(), b._container().cbegin(), out._container().begin(),
                     [](const T &x, const T &y) { return x + y; });

    // Verify the output as well.
    assert(polynomials::key_is_compatible(out, ss));
}

namespace detail
{

// Small helper to detect if 2 types
// are the same d_packed_monomial type.
template <typename, typename>
struct same_d_packed_monomial : ::std::false_type {
};

template <typename T, unsigned NBits>
struct same_d_packed_monomial<d_packed_monomial<T, NBits>, d_packed_monomial<T, NBits>> : ::std::true_type {
};

template <typename T, typename U>
inline constexpr bool same_d_packed_monomial_v = same_d_packed_monomial<T, U>::value;

} // namespace detail

// Monomial overflow checking.
// NOTE: this assumes that all the monomials in the 2 ranges
// are compatible with ss.
// NOTE: this can be parallelised. We need:
// - a good heuristic (should not be too difficult given
//   the constraints on d_packed_monomial),
// - the random-access iterator concept.
#if defined(OBAKE_HAVE_CONCEPTS)
template <typename R1, typename R2>
requires InputRange<R1> &&InputRange<R2> &&
    detail::same_d_packed_monomial_v<remove_cvref_t<typename ::std::iterator_traits<range_begin_t<R1>>::reference>,
                                     remove_cvref_t<typename ::std::iterator_traits<range_begin_t<R2>>::reference>>
#else
template <typename R1, typename R2,
          ::std::enable_if_t<
              ::std::conjunction_v<is_input_range<R1>, is_input_range<R2>,
                                   detail::same_d_packed_monomial<
                                       remove_cvref_t<typename ::std::iterator_traits<range_begin_t<R1>>::reference>,
                                       remove_cvref_t<typename ::std::iterator_traits<range_begin_t<R2>>::reference>>>,
              int> = 0>
#endif
    inline bool monomial_range_overflow_check(R1 &&r1, R2 &&r2, const symbol_set &ss)
{
    using pm_t = remove_cvref_t<typename ::std::iterator_traits<range_begin_t<R1>>::reference>;
    using value_type = typename pm_t::value_type;
    using int_t = ::mppp::integer<1>;

    const auto s_size = ss.size();

    if (s_size == 0u) {
        // If the monomials have zero variables,
        // there cannot be overflow.
        return true;
    }

    // Get out the begin/end iterators.
    auto b1 = ::obake::begin(::std::forward<R1>(r1));
    const auto e1 = ::obake::end(::std::forward<R1>(r1));
    auto b2 = ::obake::begin(::std::forward<R2>(r2));
    const auto e2 = ::obake::end(::std::forward<R2>(r2));

    if (b1 == e1 || b2 == e2) {
        // If either range is empty, there will be no overflow.
        return true;
    }

    // Prepare the limits vectors.
    auto [limits1, limits2] = [s_size]() {
        if constexpr (is_signed_v<value_type>) {
            ::std::vector<::std::pair<value_type, value_type>> minmax1, minmax2;
            minmax1.reserve(static_cast<decltype(minmax1.size())>(s_size));
            minmax2.reserve(static_cast<decltype(minmax2.size())>(s_size));

            return ::std::make_tuple(::std::move(minmax1), ::std::move(minmax2));
        } else {
            ::std::vector<value_type> max1, max2;
            max1.reserve(static_cast<decltype(max1.size())>(s_size));
            max2.reserve(static_cast<decltype(max2.size())>(s_size));

            return ::std::make_tuple(::std::move(max1), ::std::move(max2));
        }
    }();

    // Fetch the packed size.
    constexpr auto psize = pm_t::psize;

    // Init with the first elements of the ranges.
    {
        // NOTE: if the iterators return copies
        // of the monomials, rather than references,
        // capturing them via const
        // ref will extend their lifetimes.
        const auto &init1 = *b1;
        const auto &init2 = *b2;

        assert(polynomials::key_is_compatible(init1, ss));
        assert(polynomials::key_is_compatible(init2, ss));

        const auto &c1 = init1._container();
        const auto &c2 = init2._container();
        assert(c1.size() == c2.size());

        symbol_idx idx = 0;
        value_type tmp1, tmp2;

        for (decltype(c1.size()) i = 0; i < c1.size(); ++i) {
            k_unpacker<value_type> ku1(c1[i], psize), ku2(c2[i], psize);

            for (auto j = 0u; j < psize && idx < s_size; ++j, ++idx) {
                ku1 >> tmp1;
                ku2 >> tmp2;

                if constexpr (is_signed_v<value_type>) {
                    limits1.emplace_back(tmp1, tmp1);
                    limits2.emplace_back(tmp2, tmp2);
                } else {
                    limits1.emplace_back(tmp1);
                    limits2.emplace_back(tmp2);
                }
            }
        }
    }

    // Helper to examine the rest of the ranges.
    auto update_minmax = [&ss, s_size
#if defined(_MSC_VER) && !defined(__clang__)
                          ,
                          psize
#endif
    ](auto b, auto e, auto &limits) {
        ::obake::detail::ignore(ss);

        for (++b; b != e; ++b) {
            const auto &cur = *b;

            assert(polynomials::key_is_compatible(cur, ss));

            symbol_idx idx = 0;
            value_type tmp;
            for (const auto &n : cur._container()) {
                k_unpacker<value_type> ku(n, psize);

                for (auto j = 0u; j < psize && idx < s_size; ++j, ++idx) {
                    ku >> tmp;

                    if constexpr (is_signed_v<value_type>) {
                        limits[idx].first = ::std::min(limits[idx].first, tmp);
                        limits[idx].second = ::std::max(limits[idx].second, tmp);
                    } else {
                        limits[idx] = ::std::max(limits[idx], tmp);
                    }
                }
            }
        }
    };

    // Examine the rest of the ranges.
    update_minmax(b1, e1, limits1);
    update_minmax(b2, e2, limits2);

    // Fetch the nbits value for pm_t.
    constexpr auto nbits = pm_t::nbits;

    // Now add the limits via interval arithmetics
    // and check for overflow. Use mppp::integer for the check.
    for (decltype(limits1.size()) i = 0; i < s_size; ++i) {
        if constexpr (is_signed_v<value_type>) {
            const auto add_min = int_t{limits1[i].first} + limits2[i].first;
            const auto add_max = int_t{limits1[i].second} + limits2[i].second;

            // NOTE: need to special-case nbits == bit width, in which case
            // the component limits are the full numerical range of the type.
            const auto lim_min
                = nbits == static_cast<unsigned>(::obake::detail::limits_digits<value_type>)
                      ? ::obake::detail::limits_min<value_type>
                      : ::obake::detail::k_packing_get_climits<value_type>(nbits, static_cast<unsigned>(i % psize))[0];
            const auto lim_max
                = nbits == static_cast<unsigned>(::obake::detail::limits_digits<value_type>)
                      ? ::obake::detail::limits_max<value_type>
                      : ::obake::detail::k_packing_get_climits<value_type>(nbits, static_cast<unsigned>(i % psize))[1];

            // NOTE: an overflow condition will likely result in an exception
            // or some other error handling. Optimise for the non-overflow case.
            if (obake_unlikely(add_min < lim_min || add_max > lim_max)) {
                return false;
            }
        } else {
            const auto add_max = int_t{limits1[i]} + limits2[i];

            // NOTE: like above, special-case nbits == bit width.
            const auto lim_max
                = nbits == static_cast<unsigned>(::obake::detail::limits_digits<value_type>)
                      ? ::obake::detail::limits_max<value_type>
                      : ::obake::detail::k_packing_get_climits<value_type>(nbits, static_cast<unsigned>(i % psize));

            if (obake_unlikely(add_max > lim_max)) {
                return false;
            }
        }
    }

    return true;
}

// Implementation of key_degree().
// NOTE: this assumes that d is compatible with ss.
template <typename T, unsigned NBits>
inline T key_degree(const d_packed_monomial<T, NBits> &d, const symbol_set &ss)
{
    assert(polynomials::key_is_compatible(d, ss));

    constexpr auto psize = d_packed_monomial<T, NBits>::psize;

    const auto s_size = ss.size();

    symbol_idx idx = 0;
    T tmp;
    // NOTE: do the computation in multiprecision,
    // convert back to T at the end.
    ::mppp::integer<1> retval;
    for (const auto &n : d._container()) {
        k_unpacker<T> ku(n, psize);

        for (auto j = 0u; j < psize && idx < s_size; ++j, ++idx) {
            ku >> tmp;
            retval += tmp;
        }
    }

    return static_cast<T>(retval);
}

// Implementation of key_p_degree().
// NOTE: this assumes that d and si are compatible with ss.
template <typename T, unsigned NBits>
inline T key_p_degree(const d_packed_monomial<T, NBits> &d, const symbol_idx_set &si, const symbol_set &ss)
{
    assert(polynomials::key_is_compatible(d, ss));
    assert(si.empty() || *(si.end() - 1) < ss.size());

    constexpr auto psize = d_packed_monomial<T, NBits>::psize;

    const auto s_size = ss.size();

    symbol_idx idx = 0;
    T tmp;
    auto si_it = si.begin();
    const auto si_it_end = si.end();
    // NOTE: do the computation in multiprecision,
    // convert back to T at the end.
    ::mppp::integer<1> retval;
    for (const auto &n : d._container()) {
        k_unpacker<T> ku(n, psize);

        for (auto j = 0u; j < psize && idx < s_size && si_it != si_it_end; ++j, ++idx) {
            ku >> tmp;

            if (idx == *si_it) {
                retval += tmp;
                ++si_it;
            }
        }
    }

    assert(si_it == si_it_end);

    return static_cast<T>(retval);
}

// Monomial exponentiation.
// NOTE: this assumes that d is compatible with ss.
template <typename T, unsigned NBits, typename U,
          ::std::enable_if_t<::std::disjunction_v<::obake::detail::is_mppp_integer<U>,
                                                  is_safely_convertible<const U &, ::mppp::integer<1> &>>,
                             int> = 0>
inline d_packed_monomial<T, NBits> monomial_pow(const d_packed_monomial<T, NBits> &d, const U &n, const symbol_set &ss)
{
    assert(polynomials::key_is_compatible(d, ss));

    // NOTE: exp will be a const ref if n is already
    // an mppp integer, a new value otherwise.
    decltype(auto) exp = [&n]() -> decltype(auto) {
        if constexpr (::obake::detail::is_mppp_integer_v<U>) {
            return n;
        } else {
            ::mppp::integer<1> ret;

            if (obake_unlikely(!::obake::safe_convert(ret, n))) {
                if constexpr (is_stream_insertable_v<const U &>) {
                    // Provide better error message if U is ostreamable.
                    ::std::ostringstream oss;
                    static_cast<::std::ostream &>(oss) << n;
                    obake_throw(::std::invalid_argument, "Invalid exponent for monomial exponentiation: the exponent ("
                                                             + oss.str()
                                                             + ") cannot be converted into an integral value");
                } else {
                    obake_throw(::std::invalid_argument, "Invalid exponent for monomial exponentiation: the exponent "
                                                         "cannot be converted into an integral value");
                }
            }

            return ret;
        }
    }();

    constexpr auto psize = d_packed_monomial<T, NBits>::psize;

    const auto s_size = ss.size();

    // Prepare the return value.
    const auto &c_in = d._container();
    d_packed_monomial<T, NBits> retval;
    auto &c_out = retval._container();
    c_out.reserve(c_in.size());

    // Unpack, multiply in arbitrary-precision arithmetic, re-pack.
    T tmp;
    symbol_idx idx = 0;
    remove_cvref_t<decltype(exp)> tmp_int;
    for (const auto &np : c_in) {
        k_unpacker<T> ku(np, psize);
        k_packer<T> kp(psize);

        auto j = 0u;
        for (; j < psize && idx < s_size; ++j, ++idx) {
            ku >> tmp;
            tmp_int = tmp;
            tmp_int *= exp;
            kp << static_cast<T>(tmp_int);
        }

        // NOTE: this loop will be executed only at the
        // last np, and only if psize does not divide
        // exactly s_size.
        for (; j < psize; ++j) {
            kp << T(0);
        }

        c_out.push_back(kp.get());
    }

    return retval;
}

} // namespace polynomials

// Lift to the obake namespace.
template <typename T, unsigned NBits>
using d_packed_monomial = polynomials::d_packed_monomial<T, NBits>;

// Specialise monomial_has_homomorphic_hash.
template <typename T, unsigned NBits>
inline constexpr bool monomial_hash_is_homomorphic<d_packed_monomial<T, NBits>> = true;

} // namespace obake

#endif
