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
    return detail::write_null(w_);
}

bool
serializer::
init_string()
{
    string_view s = static_cast<
        string_view>(*reinterpret_cast<
            string const*>(pt_));
    return detail::write_string(
        w_, s.data(), s.size());
}

bool
serializer::
init_string_view()
{
    string_view s(reinterpret_cast<
        char const*>(pt_), pn_);
    return detail::write_string(
        w_, s.data(), s.size());
}

bool
serializer::
init_object()
{
    auto const& obj =
        *reinterpret_cast<
            object const*>(pt_);
    return write_object(
        w_, obj, obj.begin());
}

bool
serializer::
init_array()
{
    auto const& arr =
        *reinterpret_cast<
            array const*>(pt_);
    return write_array(
        w_, arr, arr.begin());
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
    detail::write_context& w,
    array const& arr,
    array::const_iterator it)
{
    enum state : char
    {
        arr1, arr2, arr3, arr4
    };

    state st;
    auto const end = arr.end();
    if(! w.stack.empty())
    {
        w.stack.pop(st);
        switch(st)
        {
        default:
        case state::arr1: goto do_arr1;
        case state::arr2: goto do_arr2;
        case state::arr3: goto do_arr3;
        case state::arr4: goto do_arr4;
        }
    }

do_arr1:
    if(! w.append('['))
    {
        st = arr1;
        goto suspend;
    }
    if(it == end)
        goto do_arr4;
    for(;;)
    {
    do_arr2:
        if(! write_value(w, *it))
        {
            st = arr2;
            goto suspend;
        }
        ++it;
        if(it == end)
            break;
    do_arr3:
        if(! w.append(','))
        {
            st = arr3;
            goto suspend;
        }
    }
do_arr4:
    if(! w.append(']'))
    {
        st = arr4;
        goto suspend;
    }
    return true;

suspend:
    w.stack.push(st);
    w.stack.push(it);
    w.stack.push(&arr);
    w.push_resume(
        [](detail::write_context& w)
        {
            array const* pa;
            array::const_iterator it;

            w.stack.pop(pa);
            w.stack.pop(it);
            return write_array(w, *pa, it);
        });
    return false;
}

bool
serializer::
write_object(
    detail::write_context& w,
    object const& obj,
    object::const_iterator it)
{
    enum state : char
    {
        obj1, obj2, obj3, obj4, obj5, obj6
    };

    state st;
    auto const end = obj.end();
    if(! w.stack.empty())
    {
        w.stack.pop(st);
        switch(st)
        {
        default:
        case state::obj1: goto do_obj1;
        case state::obj2:
        {
            // key string
            resume_fn fn;
            w.stack.pop(fn);
            if(! fn(w))
                return false;
            goto do_obj3;
        }
        case state::obj3: goto do_obj3;
        case state::obj4: goto do_obj4;
        case state::obj5: goto do_obj5;
        case state::obj6: goto do_obj6;
        }
    }

do_obj1:
    if(! w.append('{'))
    {
        st = obj1;
        goto suspend;
    }
    if(it == end)
        goto do_obj6;
    for(;;)
    {
        // key
        if(! detail::write_string(w,
            it->key().data(), it->key().size()))
        {
            st = obj2;
            goto suspend;
        }
do_obj3:
        if(! w.append(':'))
        {
            st = obj3;
            goto suspend;
        }
do_obj4:
        if(! write_value(w, it->value()))
        {
            st = obj4;
            goto suspend;
        }
        ++it;
        if(it == end)
            break;
do_obj5:
        if(! w.append(','))
        {
            st = obj5;
            goto suspend;
        }
    }
do_obj6:
    if(! w.append('}'))
    {
        st = obj6;
        goto suspend;
    }
    return true;

suspend:
    w.stack.push(st);
    w.stack.push(it);
    w.stack.push(&obj);
    w.push_resume(
        [](detail::write_context& w)
        {
            object const* po;
            object::const_iterator it;

            w.stack.pop(po);
            w.stack.pop(it);
            return write_object(w, *po, it);
        });
    return false;
}

bool
serializer::
write_value(
    detail::write_context& w,
    value const& jv)
{
    if(! w.stack.empty())
    {
        resume_fn fn;
        w.stack.pop(fn);
        if(! fn(w))
            goto suspend;
        return true;
    }

    switch(jv.kind())
    {
    case kind::array:
    {
        auto const& arr = jv.get_array();
        return write_array(w, arr, arr.begin());
    }

    case kind::object:
    {
        auto const& obj = jv.get_object();
        if(write_object(w, obj, obj.begin()))
            return true;
        break;
    }

    case kind::string:
    {
        string_view s = static_cast<
            string_view>(jv.get_string());
        if(detail::write_string(
                w, s.data(), s.size()))
            return true;
        break;
    }

    case kind::int64:
        if(detail::write_int64(
                w, jv.get_int64()))
            return true;
        break;

    case kind::uint64:
        if(detail::write_uint64(
                w, jv.get_uint64()))
            return true;
        break;

    case kind::double_:
        if(detail::write_double(
                w, jv.get_double()))
            return true;
        break;

    case kind::bool_:
        if(jv.get_bool())
        {
            if(detail::write_true(w))
                return true;
        }
        else
        {
            if(detail::write_false(w))
                return true;
        }
        break;

    default:
    case kind::null:
        if(detail::write_null(w))
            return true;
        break;
    }

suspend:
    w.push_resume(
        [](detail::write_context& w)
        {
            resume_fn fn;
            w.stack.pop(fn);
            return fn(w);
        });
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
