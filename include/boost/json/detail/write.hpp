//
// Copyright (c) 2019 Vinnie Falco (vinnie.falco@gmail.com)
// Copyright (c) 2020 Krystian Stasiowski (sdkrystian@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/boostorg/json
//

#ifndef BOOST_JSON_DETAIL_WRITE_HPP
#define BOOST_JSON_DETAIL_WRITE_HPP

#include <boost/json/string_view.hpp>
#include <boost/json/detail/stack.hpp>

namespace boost {
namespace json {
namespace detail {

struct write_context
{
    char* dest_;
    char const* end_;

public:
    detail::stack stack;
    char temp[28];

    // provide an output buffer
    void
    prepare(
        char* dest,
        std::size_t size)
    {
        dest_ = dest;
        end_ = dest + size;
    }

    // return true if there is no space
    bool
    empty() const noexcept
    {
        return dest_ == end_;
    }

    // return the amount of space available
    std::size_t
    available() const noexcept
    {
        return static_cast<
            std::size_t>(end_ - dest_);
    }

    // return true if there
    // is space for `n` chars
    bool
    has_space(
        std::size_t n) const noexcept
    {
        return available() >= n;
    }

    // return true if there is space
    bool
    append(char c) noexcept
    {
        if(! empty())
        {
            *dest_++ = c;
            return true;
        }
        return false;
    }

    // append one char
    void
    append_unsafe(
        char c) noexcept
    {
        BOOST_ASSERT(! empty());
        *dest_++ = c;
    }

    // append chars in s, unchecked
    void
    append_unsafe(
        char const* s,
        std::size_t n) noexcept
    {
        BOOST_ASSERT(available() >= n);
        std::memcpy(dest_, s, n);
        dest_ += n;
    }

    // push a resume function
    void
    push_resume(
        bool (*fn)(write_context&))
    {
        stack.push(fn);

        // hack to make things work for now
        stack.push(char(127));
    }
};

//------------------------------------------------

BOOST_JSON_DECL
bool
write_null(
    write_context& w);

BOOST_JSON_DECL
bool
write_string(
    write_context& w,
    char const* s,
    std::size_t n);

} // detail
} // json
} // boost

#endif
