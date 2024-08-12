
// Copyright (c) 2024 Tristan Brindle (tcbrindle at gmail dot com)
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#ifndef FLUX_OP_FLATTEN_WITH_HPP_INCLUDED
#define FLUX_OP_FLATTEN_WITH_HPP_INCLUDED

#include <flux/core.hpp>

#include <flux/source/single.hpp>

namespace flux {

namespace detail {

// Workaround for std::variant::emplace<N> not being constexpr in libc++
// See P2231 (C++20 DR)
template <std::size_t N>
inline constexpr auto variant_emplace =
[]<typename... Types>(std::variant<Types...>& variant, auto&&... args) {
    if constexpr (__cpp_lib_variant >= 202106L) {
        variant.template emplace<N>(FLUX_FWD(args)...);
    } else {
        if (std::is_constant_evaluated()) {
            variant = std::variant<Types...>(std::in_place_index<N>, FLUX_FWD(args)...);
        } else {
            variant.template emplace<N>(FLUX_FWD(args)...);
        }
    }
};

template <sequence Base, multipass_sequence Pattern>
struct flatten_with_adaptor : inline_sequence_base<flatten_with_adaptor<Base, Pattern>>
{
private:
    using InnerSeq = element_t<Base>;

    FLUX_NO_UNIQUE_ADDRESS Base base_;
    FLUX_NO_UNIQUE_ADDRESS Pattern pattern_;
    optional<InnerSeq> inner_ = nullopt;

public:
    constexpr flatten_with_adaptor(decays_to<Base> auto&& base,
                                   decays_to<Pattern> auto&& pattern)
        : base_(FLUX_FWD(base)),
          pattern_(FLUX_FWD(pattern))
    {}

    struct flux_sequence_traits {
    private:
        using self_t = flatten_with_adaptor;
        using element_type =
            std::common_reference_t<element_t<InnerSeq>, element_t<Pattern>>;
        using rvalue_element_type =
            std::common_reference_t<rvalue_element_t<InnerSeq>, rvalue_element_t<Pattern>>;

        struct cursor_type {
            cursor_t<Base> outer_cur;
            std::variant<cursor_t<Pattern>, cursor_t<InnerSeq>> inner_cur{};
        };

        static constexpr auto satisfy(self_t& self, cursor_type& cur) -> void
        {
            while (true) {
                if (cur.inner_cur.index() == 0) {
                    if (!flux::is_last(self.pattern_, std::get<0>(cur.inner_cur))) {
                        break;
                    }

                    self.inner_.emplace(flux::read_at(self.base_, cur.outer_cur));
                    variant_emplace<1>(cur.inner_cur, flux::first(*self.inner_));
                } else {
                    FLUX_ASSERT(self.inner_.has_value());
                    if (!flux::is_last(*self.inner_, std::get<1>(cur.inner_cur))) {
                        break;
                    }

                    flux::inc(self.base_, cur.outer_cur);
                    if (!flux::is_last(self.base_, cur.outer_cur)) {
                        variant_emplace<0>(cur.inner_cur, flux::first(self.pattern_));
                    } else {
                        break;
                    }
                }
            }
        }

    public:
        using value_type = std::common_type_t<value_t<InnerSeq>, value_t<Pattern>>;

        static constexpr auto first(self_t& self) -> cursor_type
        {
            cursor_type cur{.outer_cur = flux::first(self.base_)};
            if (!flux::is_last(self.base_, cur.outer_cur)) {
                self.inner_.emplace(flux::read_at(self.base_, cur.outer_cur));
                variant_emplace<1>(cur.inner_cur, flux::first(*self.inner_));
                satisfy(self, cur);
            }
            return cur;
        }

        static constexpr auto is_last(self_t& self, cursor_type const& cur) -> bool
        {
            return flux::is_last(self.base_, cur.outer_cur);
        }

        static constexpr auto inc(self_t& self, cursor_type& cur) -> void
        {
            if (cur.inner_cur.index() == 0) {
                flux::inc(self.pattern_, std::get<0>(cur.inner_cur));
            } else {
                FLUX_ASSERT(self.inner_.has_value());
                flux::inc(*self.inner_, std::get<1>(cur.inner_cur));
            }
            satisfy(self, cur);
        }

        static constexpr auto read_at(self_t& self, cursor_type const& cur)
            -> element_type
        {
            if (cur.inner_cur.index() == 0) {
                return static_cast<element_type>(
                    flux::read_at(self.pattern_, std::get<0>(cur.inner_cur)));
            } else {
                FLUX_ASSERT(self.inner_.has_value());
                return static_cast<element_type>(
                    flux::read_at(*self.inner_, std::get<1>(cur.inner_cur)));
            }
        }

        static constexpr auto move_at(self_t& self, cursor_type const& cur)
            -> rvalue_element_type
        {
            if (cur.inner_cur.index() == 0) {
                return static_cast<rvalue_element_type>(
                    flux::move_at(self.pattern_, std::get<0>(cur.inner_cur)));
            } else {
                FLUX_ASSERT(self.inner_.has_value());
                return static_cast<rvalue_element_type>(
                    flux::move_at(*self.inner_, std::get<1>(cur.inner_cur)));
            }
        }

        static constexpr auto last(self_t& self) -> cursor_type
            requires bounded_sequence<Base>
        {
            return cursor_type{.outer_cur = flux::last(self)};
        }
    };
};

struct flatten_with_fn {

    template <adaptable_sequence Seq, adaptable_sequence Pattern>
        requires sequence<element_t<Seq>> &&
                 multipass_sequence<Pattern> &&
                 flatten_with_compatible<element_t<Seq>, Pattern>
    constexpr auto operator()(Seq&& seq, Pattern&& pattern) const
        -> sequence auto
    {
        return flatten_with_adaptor<std::decay_t<Seq>, std::decay_t<Pattern>>(
            FLUX_FWD(seq), FLUX_FWD(pattern));
    }

    template <adaptable_sequence Seq>
        requires sequence<element_t<Seq>> &&
                 std::movable<value_t<element_t<Seq>>>
    constexpr auto operator()(Seq&& seq, value_t<element_t<Seq>> value) const
        -> sequence auto
    {
        return (*this)(FLUX_FWD(seq), flux::single(std::move(value)));
    }
};

} // namespace detail

FLUX_EXPORT inline constexpr auto flatten_with = detail::flatten_with_fn{};

template <typename Derived>
template <adaptable_sequence Pattern>
    requires sequence<element_t<Derived>> &&
             multipass_sequence<Pattern> &&
             detail::flatten_with_compatible<element_t<Derived>, Pattern>
constexpr auto inline_sequence_base<Derived>::flatten_with(Pattern&& pattern) &&
{
    return flux::flatten_with(std::move(derived()), FLUX_FWD(pattern));
}

template <typename Derived>
template <typename Value>
    requires sequence<element_t<Derived>> &&
             std::constructible_from<value_t<element_t<Derived>>, Value&&>
constexpr auto inline_sequence_base<Derived>::flatten_with(Value value) &&
{
    return flux::flatten_with(std::move(derived()), std::move(value));
}

} // namespace flux

#endif // FLUX_OP_FLATTEN_WITH_HPP_INCLUDED