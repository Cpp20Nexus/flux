
#ifndef FLUX_OP_INPLACE_REVERSE_HPP_INCLUDED
#define FLUX_OP_INPLACE_REVERSE_HPP_INCLUDED

#include <flux/core.hpp>

namespace flux {

namespace detail {

struct inplace_reverse_fn {
    template <bidirectional_sequence Seq>
        requires bounded_sequence<Seq> &&
                 element_swappable_with<Seq, Seq>
    constexpr void operator()(Seq&& seq) const
    {
        auto first = flux::first(seq);
        auto last = flux::last(seq);

        while (first != last && first != flux::dec(seq, last)) {
            flux::swap_at(seq, first, last);
            flux::inc(seq, first);
        }
    }
};

} // namespace detail

inline constexpr auto inplace_reverse = detail::inplace_reverse_fn{};

} // namespace flux

#endif // FLUX_OP_INPLACE_REVERSE_HPP_INCLUDED