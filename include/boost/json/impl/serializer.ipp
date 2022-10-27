//
// Copyright (c) 2019 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
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

//----------------------------------------------------------

serializer::
~serializer() noexcept = default;

//----------------------------------------------------------

bool
serializer::
init_null()
{
    return w_.write_null();
}

bool
serializer::
init_string()
{
    auto const& str =
        *reinterpret_cast<
            string const*>(pt_);
    string_view s(str.data(), str.size());
    return w_.write_string(
        s.data(), s.size());
}

bool
serializer::
init_string_view()
{
    string_view s(reinterpret_cast<
        char const*>(pt_), pn_);
    return w_.write_literal(
        s.data(), s.size());
}

bool
serializer::
init_object()
{
    auto const& obj =
        *reinterpret_cast<
            object const*>(pt_);
    return write_object(w_, obj);
}

bool
serializer::
init_array()
{
    auto const& arr =
        *reinterpret_cast<
            array const*>(pt_);
    return write_array(w_, arr);
}

bool
serializer::
init_value()
{
    auto const& jv =
        *reinterpret_cast<
            value const*>(pt_);
    return write_value(w_, jv);
}

bool
serializer::
write_array(
    detail::writer& w,
    array const& arr)
{
    enum state : char
    {
        arr1, arr2, arr3
    };

    state st;
    auto const end = arr.end();
    array::const_iterator it{};
    if(w.stack.empty())
    {
        it = arr.begin();
    }
    else
    {
        w.stack.pop(it);
        w.stack.pop(st);
        if(! w.do_resume())
            goto suspend;
        switch(st)
        {
        default:
        case state::arr1: goto do_arr1;
        case state::arr2: goto do_arr2;
        case state::arr3: goto do_arr3;
        }
    }

do_arr1:
    if(! w.append('['))
    {
        st = arr1;
        goto suspend;
    }
    if(it == end)
        goto do_arr3;
    for(;;)
    {
        if(! write_value(w, *it++))
        {
            st = arr2;
            goto suspend;
        }
    do_arr2:
        if(it == end)
            break;
        if(! w.append(','))
        {
            st = arr2;
            goto suspend;
        }
    }
do_arr3:
    if(! w.append(']'))
    {
        st = arr3;
        goto suspend;
    }
    return true;

suspend:
    w.stack.push(st);
    w.stack.push(it);
    w.stack.push(&arr);
    w.push_resume(
        [](detail::writer& w)
        {
            array const* pa;
            w.stack.pop(pa);
            return write_array(w, *pa);
        });
    return false;
}

bool
serializer::
write_object(
    detail::writer& w,
    object const& obj)
{
    enum state : char
    {
        obj1, obj2, obj3, obj4
    };

    state st;
    object::const_iterator it{};
    auto const end = obj.end();
    if(w.stack.empty())
    {
        it = obj.begin();
    }
    else
    {
        w.stack.pop(it);
        w.stack.pop(st);
        if(! w.do_resume())
            goto suspend;
        switch(st)
        {
        default:
        case state::obj1: goto do_obj1;
        case state::obj2: goto do_obj2;
        case state::obj3: goto do_obj3;
        case state::obj4: goto do_obj4;
        }
    }

do_obj1:
    if(! w.append('{'))
    {
        st = obj1;
        goto suspend;
    }
    if(it == end)
        goto do_obj4;
    for(;;)
    {
        // key
        if(! w.write_string(
            it->key().data(), it->key().size()))
        {
            st = obj2;
            goto suspend;
        }
do_obj2:
        if(! w.append(':'))
        {
            st = obj2;
            goto suspend;
        }
        if(! write_value(w, (*it++).value()))
        {
            st = obj3;
            goto suspend;
        }
do_obj3:
        if(it == end)
            break;
        if(! w.append(','))
        {
            st = obj3;
            goto suspend;
        }
    }
do_obj4:
    if(! w.append('}'))
    {
        st = obj4;
        goto suspend;
    }
    return true;

suspend:
    w.stack.push(st);
    w.stack.push(it);
    w.stack.push(&obj);
    w.push_resume(
        [](detail::writer& w)
        {
            object const* po;
            w.stack.pop(po);
            return write_object(w, *po);
        });
    return false;
}

bool
serializer::
write_value(
    detail::writer& w,
    value const& jv)
{
#if 0
    if(! w.stack.empty())
    {
        resume_fn fn;
        w.stack.pop(fn);
        if(! fn(w))
            goto suspend;
        return true;
    }
#endif

    switch(jv.kind())
    {
    case kind::array:
    {
        auto const& arr = jv.get_array();
        return write_array(w, arr);
    }

    case kind::object:
    {
        auto const& obj = jv.get_object();
        if(write_object(w, obj))
            return true;
        break;
    }

    case kind::string:
    {
        auto const& str = jv.get_string();
        if(w.write_string(str.data(), str.size()))
            return true;
        break;
    }

    case kind::int64:
        if(w.write_int64(jv.get_int64()))
            return true;
        break;

    case kind::uint64:
        if(w.write_uint64(jv.get_uint64()))
            return true;
        break;

    case kind::double_:
        if(w.write_double(jv.get_double()))
            return true;
        break;

    case kind::bool_:
        if(w.write_bool(jv.get_bool()))
            return true;
        break;

    default:
    case kind::null:
        if(w.write_null())
            return true;
        break;
    }

#if 0
suspend:
    w.push_resume(
        [](detail::writer& w)
        {
            resume_fn fn;
            w.stack.pop(fn);
            return fn(w);
        });
#endif
    return false;
}

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

    w_.prepare(dest, size);
    if(w_.stack.empty())
    {
        if((this->*init_)())
        {
            done_ = true;
            pt_ = nullptr;
        }
    }
    else
    {
        resume_fn fn;
        w_.stack.pop(fn);
        if(fn(w_))
        {
            done_ = true;
            pt_ = nullptr;
        }
    }
    return string_view(
        dest, w_.data() - dest);
}

//----------------------------------------------------------

serializer::
serializer() noexcept
{
}

void
serializer::
reset(value const* p) noexcept
{
    pt_ = p;
    init_ = &serializer::init_value;
    w_.stack.clear();
    done_ = false;
}

void
serializer::
reset(array const* p) noexcept
{
    pt_ = p;
    init_ = &serializer::init_array;
    w_.stack.clear();
    done_ = false;
}

void
serializer::
reset(object const* p) noexcept
{
    pt_ = p;
    init_ = &serializer::init_object;
    w_.stack.clear();
    done_ = false;
}

void
serializer::
reset(string const* p) noexcept
{
    pt_ = p;
    init_ = &serializer::init_string;
    w_.stack.clear();
    done_ = false;
}

void
serializer::
reset(string_view sv) noexcept
{
    pt_ = sv.data();
    pn_ = sv.size();
    init_ = &serializer::init_string_view;
    w_.stack.clear();
    done_ = false;
}

string_view
serializer::
read(char* dest, std::size_t size)
{
    if(! pt_)
    {
        static value const null;
        reset(&null);
    }
    return read_some(dest, size);
}

BOOST_JSON_NS_END

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#endif
