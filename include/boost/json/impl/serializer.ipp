//
// Copyright (c) 2019 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www_.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/boostorg/json
//

#ifndef BOOST_JSON_IMPL_SERIALIZER_IPP
#define BOOST_JSON_IMPL_SERIALIZER_IPP

#include <boost/json/serializer.hpp>
#include <boost/json/detail/format.hpp>
#include <boost/json/detail/sse2.hpp>
#include <ostream>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4127) // conditional expression is constant
#endif

BOOST_JSON_NS_BEGIN

enum class serializer::state : char
{
    str1, str2, str3, str4, esc1,
    utf1, utf2, utf3, utf4, utf5,
    num,
    arr1, arr2, arr3, arr4,
    obj1, obj2, obj3, obj4, obj5, obj6,

    resume_fn = 127 // hack to make things work for now
};

//----------------------------------------------------------

serializer::
~serializer() noexcept = default;

bool
serializer::
suspend(state st)
{
    w_.stack.push(st);
    return false;
}

bool
serializer::
suspend(
    state st,
    array::const_iterator it,
    array const* pa)
{
    w_.stack.push(pa);
    w_.stack.push(it);
    w_.stack.push(st);
    return false;
}

bool
serializer::
suspend(
    state st,
    object::const_iterator it,
    object const* po)
{
    w_.stack.push(po);
    w_.stack.push(it);
    w_.stack.push(st);
    return false;
}

// this is needed so that the serializer
// emits "null" when no value is selected.
bool
serializer::
init_null(stream& ss)
{
    w_.prepare(ss.data(), ss.remain());
    auto b = detail::write_null(w_);
    ss.advance(w_.data() - ss.data());
    return b;
}

template<bool StackEmpty>
bool
serializer::
write_string(stream& ss0)
{
    local_stream ss(ss0);
    local_const_stream cs(cs0_);
    if(! StackEmpty && ! w_.stack.empty())
    {
        state st;
        w_.stack.pop(st);
        switch(st)
        {
        default:
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
    static constexpr char hex[] = "0123456789abcdef";
    static constexpr char esc[] =
        "uuuuuuuubtnufruuuuuuuuuuuuuuuuuu"
        "\0\0\"\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
        "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\\\0\0\0"
        "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
        "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
        "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
        "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
        "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0";

    // opening quote
do_str1:
    if(BOOST_JSON_LIKELY(ss))
        ss.append('\x22'); // '"'
    else
        return suspend(state::str1);

    // fast loop,
    // copy unescaped
do_str2:
    if(BOOST_JSON_LIKELY(ss))
    {
        std::size_t n = cs.remain();
        if(BOOST_JSON_LIKELY(n > 0))
        {
            if(ss.remain() > n)
                n = detail::count_unescaped(
                    cs.data(), n);
            else
                n = detail::count_unescaped(
                    cs.data(), ss.remain());
            if(n > 0)
            {
                ss.append(cs.data(), n);
                cs.skip(n);
                if(! ss)
                    return suspend(state::str2);
            }
        }
        else
        {
            ss.append('\x22'); // '"'
            return true;
        }
    }
    else
    {
        return suspend(state::str2);
    }

    // slow loop,
    // handle escapes
do_str3:
    while(BOOST_JSON_LIKELY(ss))
    {
        if(BOOST_JSON_LIKELY(cs))
        {
            auto const ch = *cs;
            auto const c = esc[static_cast<
                unsigned char>(ch)];
            ++cs;
            if(! c)
            {
                ss.append(ch);
            }
            else if(c != 'u')
            {
                ss.append('\\');
                if(BOOST_JSON_LIKELY(ss))
                {
                    ss.append(c);
                }
                else
                {
                    w_.temp[0] = c;
                    return suspend(
                        state::esc1);
                }
            }
            else
            {
                if(BOOST_JSON_LIKELY(
                    ss.remain() >= 6))
                {
                    ss.append("\\u00", 4);
                    ss.append(hex[static_cast<
                        unsigned char>(ch) >> 4]);
                    ss.append(hex[static_cast<
                        unsigned char>(ch) & 15]);
                }
                else
                {
                    ss.append('\\');
                    w_.temp[0] = hex[static_cast<
                        unsigned char>(ch) >> 4];
                    w_.temp[1] = hex[static_cast<
                        unsigned char>(ch) & 15];
                    goto do_utf1;
                }
            }
        }
        else
        {
            ss.append('\x22'); // '"'
            return true;
        }
    }
    return suspend(state::str3);

do_str4:
    if(BOOST_JSON_LIKELY(ss))
        ss.append('\x22'); // '"'
    else
        return suspend(state::str4);

do_esc1:
    if(BOOST_JSON_LIKELY(ss))
        ss.append(w_.temp[0]);
    else
        return suspend(state::esc1);
    goto do_str3;

do_utf1:
    if(BOOST_JSON_LIKELY(ss))
        ss.append('u');
    else
        return suspend(state::utf1);
do_utf2:
    if(BOOST_JSON_LIKELY(ss))
        ss.append('0');
    else
        return suspend(state::utf2);
do_utf3:
    if(BOOST_JSON_LIKELY(ss))
        ss.append('0');
    else
        return suspend(state::utf3);
do_utf4:
    if(BOOST_JSON_LIKELY(ss))
        ss.append(w_.temp[0]);
    else
        return suspend(state::utf4);
do_utf5:
    if(BOOST_JSON_LIKELY(ss))
        ss.append(w_.temp[1]);
    else
        return suspend(state::utf5);
    goto do_str3;
}

template<bool StackEmpty>
bool
serializer::
write_number(stream& ss0)
{
    local_stream ss(ss0);
    if(StackEmpty || w_.stack.empty())
    {
        switch(jv_->kind())
        {
        default:
        case kind::int64:
            if(BOOST_JSON_LIKELY(
                ss.remain() >=
                    detail::max_number_chars))
            {
                ss.advance(detail::format_int64(
                    ss.data(), jv_->get_int64()));
                return true;
            }
            cs0_ = { w_.temp, detail::format_int64(
                w_.temp, jv_->get_int64()) };
            break;

        case kind::uint64:
            if(BOOST_JSON_LIKELY(
                ss.remain() >=
                    detail::max_number_chars))
            {
                ss.advance(detail::format_uint64(
                    ss.data(), jv_->get_uint64()));
                return true;
            }
            cs0_ = { w_.temp, detail::format_uint64(
                w_.temp, jv_->get_uint64()) };
            break;

        case kind::double_:
            if(BOOST_JSON_LIKELY(
                ss.remain() >=
                    detail::max_number_chars))
            {
                ss.advance(detail::format_double(
                    ss.data(), jv_->get_double()));
                return true;
            }
            cs0_ = { w_.temp, detail::format_double(
                w_.temp, jv_->get_double()) };
            break;
        }
    }
    else
    {
        state st;
        w_.stack.pop(st);
        BOOST_ASSERT(
            st == state::num);
    }
    auto const n = ss.remain();
    if(n < cs0_.remain())
    {
        ss.append(cs0_.data(), n);
        cs0_.skip(n);
        return suspend(state::num);
    }
    ss.append(
        cs0_.data(), cs0_.remain());
    return true;
}

template<bool StackEmpty>
bool
serializer::
write_array(stream& ss0)
{
    array const* pa;
    local_stream ss(ss0);
    array::const_iterator it;
    array::const_iterator end;
    if(StackEmpty || w_.stack.empty())
    {
        pa = pa_;
        it = pa->begin();
        end = pa->end();
    }
    else
    {
        state st;
        w_.stack.pop(st);
        w_.stack.pop(it);
        w_.stack.pop(pa);
        end = pa->end();
        switch(st)
        {
        default:
        case state::arr1: goto do_arr1;
        case state::arr2: goto do_arr2;
        case state::arr3: goto do_arr3;
        case state::arr4: goto do_arr4;
            break;
        }
    }
do_arr1:
    if(BOOST_JSON_LIKELY(ss))
        ss.append('[');
    else
        return suspend(
            state::arr1, it, pa);
    if(it == end)
        goto do_arr4;
    for(;;)
    {
do_arr2:
        jv_ = &*it;
        if(! write_value<StackEmpty>(ss))
            return suspend(
                state::arr2, it, pa);
        if(BOOST_JSON_UNLIKELY(
            ++it == end))
            break;
do_arr3:
        if(BOOST_JSON_LIKELY(ss))
            ss.append(',');
        else
            return suspend(
                state::arr3, it, pa);
    }
do_arr4:
    if(BOOST_JSON_LIKELY(ss))
        ss.append(']');
    else
        return suspend(
            state::arr4, it, pa);
    return true;
}

template<bool StackEmpty>
bool
serializer::
write_object(stream& ss0)
{
    object const* po;
    local_stream ss(ss0);
    object::const_iterator it;
    object::const_iterator end;
    if(StackEmpty || w_.stack.empty())
    {
        po = po_;
        it = po->begin();
        end = po->end();
    }
    else
    {
        state st;
        w_.stack.pop(st);
        w_.stack.pop(it);
        w_.stack.pop(po);
        end = po->end();
        switch(st)
        {
        default:
        case state::obj1: goto do_obj1;
        case state::obj2: goto do_obj2;
        case state::obj3: goto do_obj3;
        case state::obj4: goto do_obj4;
        case state::obj5: goto do_obj5;
        case state::obj6: goto do_obj6;
            break;
        }
    }
do_obj1:
    if(BOOST_JSON_LIKELY(ss))
        ss.append('{');
    else
        return suspend(
            state::obj1, it, po);
    if(BOOST_JSON_UNLIKELY(
        it == end))
        goto do_obj6;
    for(;;)
    {
        cs0_ = {
            it->key().data(),
            it->key().size() };
do_obj2:
        if(BOOST_JSON_UNLIKELY(
            ! write_string<StackEmpty>(ss)))
            return suspend(
                state::obj2, it, po);
do_obj3:
        if(BOOST_JSON_LIKELY(ss))
            ss.append(':');
        else
            return suspend(
                state::obj3, it, po);
do_obj4:
        jv_ = &it->value();
        if(BOOST_JSON_UNLIKELY(
            ! write_value<StackEmpty>(ss)))
            return suspend(
                state::obj4, it, po);
        ++it;
        if(BOOST_JSON_UNLIKELY(it == end))
            break;
do_obj5:
        if(BOOST_JSON_LIKELY(ss))
            ss.append(',');
        else
            return suspend(
                state::obj5, it, po);
    }
do_obj6:
    if(BOOST_JSON_LIKELY(ss))
    {
        ss.append('}');
        return true;
    }
    return suspend(
        state::obj6, it, po);
}

template<bool StackEmpty>
bool
serializer::
write_value(stream& ss)
{
    if(StackEmpty || w_.stack.empty())
    {
        auto const& jv(*jv_);
        switch(jv.kind())
        {
        default:
        case kind::object:
            po_ = &jv.get_object();
            return write_object<true>(ss);

        case kind::array:
            pa_ = &jv.get_array();
            return write_array<true>(ss);

        case kind::string:
        {
            auto const& js = jv.get_string();
            cs0_ = { js.data(), js.size() };
            return write_string<true>(ss);
        }

        case kind::int64:
        case kind::uint64:
        case kind::double_:
            return write_number<true>(ss);

        case kind::bool_:
            if(jv.get_bool())
            {
                w_.prepare(ss.data(), ss.remain());
                auto b = detail::write_true(w_);
                ss.advance(w_.data() - ss.data());
                return b;
            }
            else
            {
                w_.prepare(ss.data(), ss.remain());
                auto b = detail::write_false(w_);
                ss.advance(w_.data() - ss.data());
                return b;
            }

        case kind::null:
            w_.prepare(ss.data(), ss.remain());
            auto b = detail::write_null(w_);
            ss.advance(w_.data() - ss.data());
            return b;
        }
    }
    else
    {
        state st;
        w_.stack.peek(st);
        switch(st)
        {
        case state::resume_fn:
        {
            bool (*fn)(detail::write_context&);
            w_.stack.pop(st);
            w_.stack.pop(fn);
            w_.prepare(ss.data(), ss.remain());
            auto b = fn(w_);
            ss.advance(w_.data() - ss.data());
            return b;
        }

        default:
        case state::str1: case state::str2:
        case state::str3: case state::str4:
        case state::esc1:
        case state::utf1: case state::utf2:
        case state::utf3: case state::utf4:
        case state::utf5:
            return write_string<StackEmpty>(ss);

        case state::num:
            return write_number<StackEmpty>(ss);

        case state::arr1: case state::arr2:
        case state::arr3: case state::arr4:
            return write_array<StackEmpty>(ss);

        case state::obj1: case state::obj2:
        case state::obj3: case state::obj4:
        case state::obj5: case state::obj6:
            return write_object<StackEmpty>(ss);
        }
    }
}

inline
string_view
serializer::
read_some(
    char* dest, std::size_t size)
{
    // If this goes off it means you forgot
    // to call reset() before seriailzing a
    // new value, or you never checked done()
    // to see if you should stop.
    BOOST_ASSERT(! done_);

    stream ss(dest, size);
    if(w_.stack.empty())
        (this->*fn0_)(ss);
    else
        (this->*fn1_)(ss);
    if(w_.stack.empty())
    {
        done_ = true;
        jv_ = nullptr;
    }
    return string_view(
        dest, ss.used(dest));
}

//----------------------------------------------------------

serializer::
serializer() noexcept
{
    // ensure room for \uXXXX escape plus one
    BOOST_STATIC_ASSERT(
        sizeof(serializer::w_.temp) >= 7);
}

void
serializer::
reset(value const* p) noexcept
{
    pv_ = p;
    fn0_ = &serializer::write_value<true>;
    fn1_ = &serializer::write_value<false>;

    jv_ = p;
    w_.stack.clear();
    done_ = false;
}

void
serializer::
reset(array const* p) noexcept
{
    pa_ = p;
    fn0_ = &serializer::write_array<true>;
    fn1_ = &serializer::write_array<false>;
    w_.stack.clear();
    done_ = false;
}

void
serializer::
reset(object const* p) noexcept
{
    po_ = p;
    fn0_ = &serializer::write_object<true>;
    fn1_ = &serializer::write_object<false>;
    w_.stack.clear();
    done_ = false;
}

void
serializer::
reset(string const* p) noexcept
{
    cs0_ = { p->data(), p->size() };
    fn0_ = &serializer::write_string<true>;
    fn1_ = &serializer::write_string<false>;
    w_.stack.clear();
    done_ = false;
}

void
serializer::
reset(string_view sv) noexcept
{
    cs0_ = { sv.data(), sv.size() };
    fn0_ = &serializer::write_string<true>;
    fn1_ = &serializer::write_string<false>;
    w_.stack.clear();
    done_ = false;
}

string_view
serializer::
read(char* dest, std::size_t size)
{
    if(! jv_)
    {
        static value const null;
        jv_ = &null;
    }
    return read_some(dest, size);
}

BOOST_JSON_NS_END

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#endif
