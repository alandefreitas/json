//
// Copyright (c) 2019 Vinnie Falco (vinnie.falco@gmail.com)
// Copyright (c) 2020 Krystian Stasiowski (sdkrystian@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://wwboost.org/LICENSE_1_0.txt)
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
    sizeof(writer::temp_) >=
    max_number_chars + 1);

// ensure room for \uXXXX escape plus one
BOOST_STATIC_ASSERT(
    sizeof(writer::temp_) >= 7);

//------------------------------------------------

bool
writer::
write_literal(
    char const* s,
    std::size_t n)
{
    auto const avail = available();
    if(avail >= n)
    {
        std::memcpy(dest_, s, n);
        dest_ += n;
        return true;
    }

    // partial output
    std::memcpy(dest_, s, avail);
    dest_ += avail;
    s += avail;
    n -= avail;

    // suspend
    stack.push(n);
    stack.push(s);
    push_resume(
        [](writer& w) -> bool
        {
            char const* s;
            std::size_t n;
            w.stack.pop(s);
            w.stack.pop(n);
            return w.write_literal(s, n);
        });
    return false;
}

bool
writer::
write_null()
{
    return write_literal("null", 4);
}

bool
writer::
write_bool(
    bool b)
{
    if(b)
        return write_literal("true", 4);
    return write_literal("false", 5);
}

bool
writer::
write_int64(
    std::int64_t v)
{
    auto s = detail::write_int64(
        temp_, sizeof(temp_), v);
    return write_literal(s.data(), s.size());
}

bool
writer::
write_uint64(
    std::uint64_t v)
{
    auto s = detail::write_uint64(
        temp_, sizeof(temp_), v);
    return write_literal(s.data(), s.size());
}

bool
writer::
write_double(
    double v)
{
    auto s = detail::write_double(
        temp_, sizeof(temp_), v);
    return write_literal(s.data(), s.size());
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
writer::
write_string(
    char const* s,
    std::size_t n)
{
    enum state : char
    {
        str1, str2, str3, str4, esc1,
        utf1, utf2, utf3, utf4, utf5
    };

    state st;
    if(! stack.empty())
    {
        stack.pop(st);
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
    if(! append('\"'))
    {
        st = str1;
        goto suspend;
    }

do_str2:
    // handle the first contiguous
    // run of unescaped characters.
    if(! empty())
    {
        if(n > 0)
        {
            auto const avail = available();
            std::size_t n1;
            if(avail >= n)
                n1 = detail::count_unescaped(s, n);
            else
                n1 = detail::count_unescaped(s, avail);
            if(n1 > 0)
            {
                append_unsafe(s, n1);
                s += n1;
                n -= n1;
                if(empty())
                {
                    st = str2;
                    goto suspend;
                }
            }
        }
        else
        {
            // done
            append_unsafe('\"');
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
    while(! empty())
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
                append_unsafe(ch);
            }
            else if(c != 'u')
            {
                append_unsafe('\\');
                if(! append(c))
                {
                    temp_[0] = c;
                    st = esc1;
                    goto suspend;
                }
            }
            else
            {
                if(available() >= 6)
                {
                    append_unsafe("\\u00", 4);
                    append_unsafe(string_hex[static_cast<
                        unsigned char>(ch) >> 4]);
                    append_unsafe(string_hex[static_cast<
                        unsigned char>(ch) & 15]);
                }
                else
                {
                    append_unsafe('\\');
                    temp_[0] = string_hex[static_cast<
                        unsigned char>(ch) >> 4];
                    temp_[1] = string_hex[static_cast<
                        unsigned char>(ch) & 15];
                    goto do_utf1;
                }
            }
        }
        else
        {
            // done
            append_unsafe('\"');
            return true;
        }
    }
    st = str3;
    goto suspend;

do_str4:
    if(! append('\"'))
    {
        st = str4;
        goto suspend;
    }

do_esc1:
    if(! append(temp_[0]))
    {
        st = esc1;
        goto suspend;
    }
    goto do_str3;

do_utf1:
    if(! append('u'))
    {
        st = utf1;
        goto suspend;
    }
do_utf2:
    if(! append('0'))
    {
        st = utf2;
        goto suspend;
    }
do_utf3:
    if(! append('0'))
    {
        st = utf3;
        goto suspend;
    }
do_utf4:
    if(! append(temp_[0]))
    {
        st = utf4;
        goto suspend;
    }
do_utf5:
    if(! append(temp_[1]))
    {
        st = utf5;
        goto suspend;
    }
    goto do_str3;

suspend:
    stack.push(st);
    stack.push(n);
    stack.push(s);
    push_resume(
        [](writer& w)
        {
            char const* s;
            std::size_t n;

            w.stack.pop(s);
            w.stack.pop(n);
            return w.write_string(s, n);
        });
    return false;
}

} // detail
} // json
} // boost

#endif
