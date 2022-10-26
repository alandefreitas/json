//
// Copyright (c) 2020 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/boostorg/json
//

#include <boost/json/detail/config.hpp>
#include <boost/json/detail/write.hpp>

#include "test_suite.hpp"

namespace boost {
namespace json {

struct experimental_test
{
    void
    run()
    {
    }
};

TEST_SUITE(
    experimental_test,
    "boost.json.experimental");

} // json
} // boost
