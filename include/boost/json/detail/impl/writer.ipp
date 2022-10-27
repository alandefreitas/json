//
// Copyright (c) 2019 Vinnie Falco (vinnie.falco@gmail.com)
// Copyright (c) 2020 Krystian Stasiowski (sdkrystian@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/boostorg/json
//

#ifndef BOOST_JSON_DETAIL_IMPL_WRITER_IPP
#define BOOST_JSON_DETAIL_IMPL_WRITER_IPP

#include <boost/json/detail/writer.hpp>
#include <boost/json/detail/sse2.hpp>
#include <boost/json/detail/format.hpp> // for max_number_chars
#include <boost/static_assert.hpp>

namespace boost {
namespace json {
namespace detail {

// ensure room for largest printed number
BOOST_STATIC_ASSERT(
    sizeof(writer::temp) >=
    max_number_chars + 1);

// ensure room for \uXXXX escape plus one
BOOST_STATIC_ASSERT(
    sizeof(writer::temp) >= 7);

//------------------------------------------------

// resume output of a string literal,
// e.g. "null", "true", "false", "nan".
static
bool
resume_literal(
    writer& w)
{
    char const* p;
    std::size_t n;

    w.stack.pop(p);
    w.stack.pop(n);

    auto const avail =
        w.available();

    if(avail >= n)
    {
        w.append_unsafe(p, n);
        return true;
    }

    w.append_unsafe(p, avail);
    p += avail;
    n -= avail;

    w.stack.push(n);
    w.stack.push(p);
    w.push_resume(&resume_literal);
    return false;
}

bool
write_null(
    writer& w)
{
    static char const* const s = "null";
    if(w.has_space(4))
    {
        w.append_unsafe(s, 4);
        return true;
    }

    w.stack.push(std::size_t(4));
    w.stack.push(s);
    return resume_literal(w);
}

bool
write_bool(
    writer& w,
    bool b)
{
    if(b)
    {
        static char const* const s = "true";
        if(w.has_space(4))
        {
            w.append_unsafe(s, 4);
            return true;
        }

        w.stack.push(std::size_t(4));
        w.stack.push(s);
        return resume_literal(w);
    }
    else
    {
        static char const* const s = "false";
        if(w.has_space(5))
        {
            w.append_unsafe(s, 5);
            return true;
        }

        w.stack.push(std::size_t(5));
        w.stack.push(s);
        return resume_literal(w);
    }
}

bool
write_int64(
    writer& w,
    std::int64_t v)
{
    using T = std::int64_t;
    auto const N = 
        std::numeric_limits<T>::digits10 + 1 +
        std::numeric_limits<T>::is_signed;
    if(w.has_space(N))
    {
        auto const n = 
            detail::format_int64(w.data(), v);
        w.advance_unsafe(n);
        return true;
    }

    BOOST_STATIC_ASSERT(sizeof(w.temp) >= N);
    std::size_t const n = 
        detail::format_int64(w.temp, v);
    w.stack.push(n);
    w.stack.push((char const*)(w.temp));
    return resume_literal(w);
}

bool
write_uint64(
    writer& w,
    std::uint64_t v)
{
    using T = std::uint64_t;
    auto const N = 
        std::numeric_limits<T>::digits10 + 1 +
        std::numeric_limits<T>::is_signed;
    if(w.has_space(N))
    {
        auto const n = 
            detail::format_uint64(w.data(), v);
        w.advance_unsafe(n);
        return true;
    }

    BOOST_STATIC_ASSERT(sizeof(w.temp) >= N);
    std::size_t const n = 
        detail::format_uint64(w.temp, v);
    w.stack.push(n);
    w.stack.push((char const*)(w.temp));
    return resume_literal(w);
}

bool
write_double(
    writer& w,
    double v)
{
    auto const N = max_number_chars;
    if(w.has_space(N))
    {
        auto const n = 
            detail::format_double(w.data(), v);
        w.advance_unsafe(n);
        return true;
    }

    BOOST_STATIC_ASSERT(sizeof(w.temp) >= N);
    std::size_t const n = 
        detail::format_double(w.temp, v);
    w.stack.push(n);
    w.stack.push((char const*)(w.temp));
    return resume_literal(w);
}

//------------------------------------------------

static constexpr char
string_hex[] = "0123456789abcdef";

static constexpr char
string_esc[] =
    "uuuuuuuubtnufruuuuuuuuuuuuuuuuuu"
    "\0\0\"\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
    "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\\\0\0\0"
    "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
    "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
    "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
    "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
    "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0";

bool
write_string(
    writer& w,
    char const* s,
    std::size_t n)
{
    enum state : char
    {
        str1, str2, str3, str4, esc1,
        utf1, utf2, utf3, utf4, utf5
    };

    state st;
    if(! w.stack.empty())
    {
        w.stack.pop(st);
        switch(st)
        {
        case state::str1: goto do_str1;
        case state::str2: goto do_str2;
        case state::str3: goto do_str3;
        case state::str4: goto do_str4;
        case state::esc1: goto do_esc1;
        case state::utf1: goto do_utf1;
        case state::utf2: goto do_utf2;
        case state::utf3: goto do_utf3;
        case state::utf4: goto do_utf4;
        case state::utf5: goto do_utf5;
        }
    }

do_str1:
    if(! w.append('\"'))
    {
        st = str1;
        goto suspend;
    }

do_str2:
    // handle the first contiguous
    // run of unescaped characters.
    if(! w.empty())
    {
        if(n > 0)
        {
            auto const avail = w.available();
            std::size_t n1;
            if(avail >= n)
                n1 = detail::count_unescaped(s, n);
            else
                n1 = detail::count_unescaped(s, avail);
            if(n1 > 0)
            {
                w.append_unsafe(s, n1);
                s += n1;
                n -= n1;
                if(w.empty())
                {
                    st = str2;
                    goto suspend;
                }
            }
        }
        else
        {
            // done
            w.append_unsafe('\"');
            return true;
        }
    }
    else
    {
        st = str2;
        goto suspend;
    }

do_str3:
    // loop over escaped and unescaped characters
    while(! w.empty())
    {
        if(n > 0)
        {
            auto const ch = *s;
            auto const c = string_esc[
                static_cast<unsigned char>(ch)];
            ++s;
            --n;
            if(! c)
            {
                w.append_unsafe(ch);
            }
            else if(c != 'u')
            {
                w.append_unsafe('\\');
                if(! w.append(c))
                {
                    w.temp[0] = c;
                    st = esc1;
                    goto suspend;
                }
            }
            else
            {
                if(w.available() >= 6)
                {
                    w.append_unsafe("\\u00", 4);
                    w.append_unsafe(string_hex[static_cast<
                        unsigned char>(ch) >> 4]);
                    w.append_unsafe(string_hex[static_cast<
                        unsigned char>(ch) & 15]);
                }
                else
                {
                    w.append_unsafe('\\');
                    w.temp[0] = string_hex[static_cast<
                        unsigned char>(ch) >> 4];
                    w.temp[1] = string_hex[static_cast<
                        unsigned char>(ch) & 15];
                    goto do_utf1;
                }
            }
        }
        else
        {
            // done
            w.append_unsafe('\"');
            return true;
        }
    }
    st = str3;
    goto suspend;

do_str4:
    if(! w.append('\"'))
    {
        st = str4;
        goto suspend;
    }

do_esc1:
    if(! w.append(w.temp[0]))
    {
        st = esc1;
        goto suspend;
    }
    goto do_str3;

do_utf1:
    if(! w.append('u'))
    {
        st = utf1;
        goto suspend;
    }
do_utf2:
    if(! w.append('0'))
    {
        st = utf2;
        goto suspend;
    }
do_utf3:
    if(! w.append('0'))
    {
        st = utf3;
        goto suspend;
    }
do_utf4:
    if(! w.append(w.temp[0]))
    {
        st = utf4;
        goto suspend;
    }
do_utf5:
    if(! w.append(w.temp[1]))
    {
        st = utf5;
        goto suspend;
    }
    goto do_str3;

suspend:
    w.stack.push(st);
    w.stack.push(n);
    w.stack.push(s);
    w.push_resume(
        [](writer& w)
        {
            char const* s;
            std::size_t n;

            w.stack.pop(s);
            w.stack.pop(n);
            return write_string(w, s, n);
        });
    return false;
}

} // detail
} // json
} // boost

#endif
