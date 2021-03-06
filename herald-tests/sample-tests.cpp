//  Copyright 2021 Herald Project Contributors
//  SPDX-License-Identifier: Apache-2.0
//

#include "catch.hpp"

#include <iterator>

#include "herald/herald.h"

TEST_CASE("sample-basic", "[sample][basic]") {
  SECTION("sample-basic") {
    herald::analysis::sampling::Sample s(herald::datatype::Date(1234), herald::datatype::RSSI(-55));

    REQUIRE(s.taken.secondsSinceUnixEpoch() == 1234);
    REQUIRE(s.value == -55);
  }
}

TEST_CASE("samplelist-empty", "[samplelist][empty]") {
  SECTION("samplelist-empty") {
    herald::analysis::sampling::SampleList<herald::datatype::RSSI,5> sl;

    REQUIRE(sl.size() == 0);
    // REQUIRE(std::begin(sl) == std::end(sl));
  }
}

TEST_CASE("samplelist-notfull", "[samplelist][notfull]") {
  SECTION("samplelist-notfull") {
    herald::analysis::sampling::SampleList<herald::datatype::RSSI,5> sl;
    sl.push(1234,-55);
    sl.push(1244,-60);
    sl.push(1265,-58);

    REQUIRE(sl.size() == 3);
    REQUIRE(sl[0].value == -55);
    REQUIRE(sl[1].value == -60);
    REQUIRE(sl[2].value == -58);
  }
}

TEST_CASE("samplelist-exactlyfull", "[samplelist][exactlyfull]") {
  SECTION("samplelist-exactlyfull") {
    herald::analysis::sampling::SampleList<herald::datatype::RSSI,5> sl;
    sl.push(1234,-55);
    sl.push(1244,-60);
    sl.push(1265,-58);
    sl.push(1282,-61);
    sl.push(1294,-54);

    REQUIRE(sl.size() == 5);
    REQUIRE(sl[0].value == -55);
    REQUIRE(sl[1].value == -60);
    REQUIRE(sl[2].value == -58);
    REQUIRE(sl[3].value == -61);
    REQUIRE(sl[4].value == -54);
  }
}

TEST_CASE("samplelist-oneover", "[samplelist][oneover]") {
  SECTION("samplelist-oneover") {
    herald::analysis::sampling::SampleList<herald::datatype::RSSI,5> sl;
    sl.push(1234,-55);
    sl.push(1244,-60);
    sl.push(1265,-58);
    sl.push(1282,-61);
    sl.push(1294,-54);
    sl.push(1302,-47);

    REQUIRE(sl.size() == 5);
    REQUIRE(sl[0].value == -60);
    REQUIRE(sl[1].value == -58);
    REQUIRE(sl[2].value == -61);
    REQUIRE(sl[3].value == -54);
    REQUIRE(sl[4].value == -47);
  }
}

TEST_CASE("samplelist-threeover", "[samplelist][threeover]") {
  SECTION("samplelist-threeover") {
    herald::analysis::sampling::SampleList<herald::datatype::RSSI,5> sl;
    sl.push(1234,-55);
    sl.push(1244,-60);
    sl.push(1265,-58);
    sl.push(1282,-61);
    sl.push(1294,-54);
    sl.push(1302,-47);
    sl.push(1304,-48);
    sl.push(1305,-49);

    REQUIRE(sl.size() == 5);
    REQUIRE(sl[0].value == -61);
    REQUIRE(sl[1].value == -54);
    REQUIRE(sl[2].value == -47);
    REQUIRE(sl[3].value == -48);
    REQUIRE(sl[4].value == -49);
  }
}

TEST_CASE("samplelist-justunderfullagain", "[samplelist][justunderfullagain]") {
  SECTION("samplelist-justunderfullagain") {
    herald::analysis::sampling::SampleList<herald::datatype::RSSI,5> sl;
    sl.push(1234,-55);
    sl.push(1244,-60);
    sl.push(1265,-58);
    sl.push(1282,-61);
    sl.push(1294,-54);
    sl.push(1302,-47);
    sl.push(1304,-48);
    sl.push(1305,-49);
    sl.push(1306,-45);

    REQUIRE(sl.size() == 5);
    REQUIRE(sl[0].value == -54);
    REQUIRE(sl[1].value == -47);
    REQUIRE(sl[2].value == -48);
    REQUIRE(sl[3].value == -49);
    REQUIRE(sl[4].value == -45);
  }
}

TEST_CASE("samplelist-fullagain", "[samplelist][fullagain]") {
  SECTION("samplelist-fullagain") {
    herald::analysis::sampling::SampleList<herald::datatype::RSSI,5> sl;
    sl.push(1234,-55);
    sl.push(1244,-60);
    sl.push(1265,-58);
    sl.push(1282,-61);
    sl.push(1294,-54);
    sl.push(1302,-47);
    sl.push(1304,-48);
    sl.push(1305,-49);
    sl.push(1306,-45);
    sl.push(1307,-44);

    REQUIRE(sl.size() == 5);
    REQUIRE(sl[0].value == -47);
    REQUIRE(sl[1].value == -48);
    REQUIRE(sl[2].value == -49);
    REQUIRE(sl[3].value == -45);
    REQUIRE(sl[4].value == -44);
  }
}

// Now handle deletion by time

TEST_CASE("samplelist-clearoneold", "[samplelist][clearoneold]") {
  SECTION("samplelist-clearoneold") {
    herald::analysis::sampling::SampleList<herald::datatype::RSSI,5> sl;
    sl.push(1234,-55);
    sl.push(1244,-60);
    sl.push(1265,-58);
    sl.push(1282,-61);
    sl.push(1294,-54);
    sl.push(1302,-47);
    sl.push(1304,-48);
    sl.push(1305,-49);
    sl.push(1306,-45);
    sl.push(1307,-44);

    sl.clearBeforeDate(1304);

    REQUIRE(sl.size() == 4);
    REQUIRE(sl[0].value == -48);
    REQUIRE(sl[1].value == -49);
    REQUIRE(sl[2].value == -45);
    REQUIRE(sl[3].value == -44);
  }
}

TEST_CASE("samplelist-clearfourold", "[samplelist][clearfourold]") {
  SECTION("samplelist-clearfourold") {
    herald::analysis::sampling::SampleList<herald::datatype::RSSI,5> sl;
    sl.push(1234,-55);
    sl.push(1244,-60);
    sl.push(1265,-58);
    sl.push(1282,-61);
    sl.push(1294,-54);
    sl.push(1302,-47);
    sl.push(1304,-48);
    sl.push(1305,-49);
    sl.push(1306,-45);
    sl.push(1307,-44);

    sl.clearBeforeDate(1307);

    REQUIRE(sl.size() == 1);
    REQUIRE(sl[0].value == -44);
  }
}

TEST_CASE("samplelist-clearallold", "[samplelist][clearallold]") {
  SECTION("samplelist-clearallold") {
    herald::analysis::sampling::SampleList<herald::datatype::RSSI,5> sl;
    sl.push(1234,-55);
    sl.push(1244,-60);
    sl.push(1265,-58);
    sl.push(1282,-61);
    sl.push(1294,-54);
    sl.push(1302,-47);
    sl.push(1304,-48);
    sl.push(1305,-49);
    sl.push(1306,-45);
    sl.push(1307,-44);

    sl.clearBeforeDate(1308);

    REQUIRE(sl.size() == 0);
  }
}

// Now handle clear()
TEST_CASE("samplelist-clear", "[samplelist][clear]") {
  SECTION("samplelist-clear") {
    herald::analysis::sampling::SampleList<herald::datatype::RSSI,5> sl;
    sl.push(1234,-55);
    sl.push(1244,-60);
    sl.push(1265,-58);
    sl.push(1282,-61);
    sl.push(1294,-54);
    sl.push(1302,-47);
    sl.push(1304,-48);
    sl.push(1305,-49);
    sl.push(1306,-45);
    sl.push(1307,-44);

    sl.clear();

    REQUIRE(sl.size() == 0);
  }
}

// Now handle iterators
TEST_CASE("samplelist-iterator-empty", "[samplelist][iterator][empty]") {
  SECTION("samplelist-iterator-empty") {
    herald::analysis::sampling::SampleList<herald::datatype::RSSI,5> sl;
    
    auto iter = std::begin(sl);
    auto endIter = std::end(sl);
    REQUIRE(iter == endIter);
    REQUIRE(iter == sl.end());
    REQUIRE(endIter == sl.begin());
    REQUIRE(endIter == sl.end());
  }
}
TEST_CASE("samplelist-iterator-single", "[samplelist][iterator][single]") {
  SECTION("samplelist-iterator-single") {
    herald::analysis::sampling::SampleList<herald::datatype::RSSI,5> sl;
    sl.push(1234,-55);
    
    auto iter = std::begin(sl);
    auto endIter = std::end(sl);
    REQUIRE(iter != endIter);
    REQUIRE(iter != sl.end());
    REQUIRE(endIter != sl.begin());
    REQUIRE(endIter == sl.end());
    REQUIRE((*iter).value == -55);
    ++iter;
    REQUIRE(iter == endIter);
  }
}

TEST_CASE("samplelist-iterator-three", "[samplelist][iterator][three]") {
  SECTION("samplelist-iterator-three") {
    herald::analysis::sampling::SampleList<herald::datatype::RSSI,5> sl;
    sl.push(1234,-55);
    sl.push(1244,-60);
    sl.push(1265,-58);
    
    auto iter = std::begin(sl);
    auto endIter = std::end(sl);
    REQUIRE(iter != endIter);
    REQUIRE(iter != sl.end());
    REQUIRE(endIter != sl.begin());
    REQUIRE(endIter == sl.end());
    REQUIRE((*iter).value == -55);
    ++iter;
    REQUIRE((*iter).value == -60);
    ++iter;
    REQUIRE((*iter).value == -58);
    ++iter;
    REQUIRE(iter == endIter);
  }
}

TEST_CASE("samplelist-iterator-exactlyfull", "[samplelist][iterator][exactlyfull]") {
  SECTION("samplelist-iterator-exactlyfull") {
    herald::analysis::sampling::SampleList<herald::datatype::RSSI,5> sl;
    sl.push(1234,-55);
    sl.push(1244,-60);
    sl.push(1265,-58);
    sl.push(1282,-61);
    sl.push(1294,-54);
    
    auto iter = std::begin(sl);
    auto endIter = std::end(sl);
    REQUIRE(iter != endIter);
    REQUIRE(iter != sl.end());
    REQUIRE(endIter != sl.begin());
    REQUIRE(endIter == sl.end());
    REQUIRE((*iter).value == -55);
    ++iter;
    REQUIRE((*iter).value == -60);
    ++iter;
    REQUIRE((*iter).value == -58);
    ++iter;
    REQUIRE((*iter).value == -61);
    ++iter;
    REQUIRE((*iter).value == -54);
    ++iter;
    REQUIRE(iter == endIter);
  }
}

TEST_CASE("samplelist-iterator-oneover", "[samplelist][iterator][oneover]") {
  SECTION("samplelist-iterator-oneover") {
    herald::analysis::sampling::SampleList<herald::datatype::RSSI,5> sl;
    sl.push(1234,-55);
    sl.push(1244,-60);
    sl.push(1265,-58);
    sl.push(1282,-61);
    sl.push(1294,-54);
    sl.push(1302,-47);
    
    auto iter = std::begin(sl);
    auto endIter = std::end(sl);
    REQUIRE(iter != endIter);
    REQUIRE(iter != sl.end());
    REQUIRE(endIter != sl.begin());
    REQUIRE(endIter == sl.end());
    REQUIRE((*iter).value == -60);
    ++iter;
    REQUIRE((*iter).value == -58);
    ++iter;
    REQUIRE((*iter).value == -61);
    ++iter;
    REQUIRE((*iter).value == -54);
    ++iter;
    REQUIRE((*iter).value == -47);
    ++iter;
    REQUIRE(iter == endIter);
  }
}

TEST_CASE("samplelist-iterator-twoover", "[samplelist][iterator][twoover]") {
  SECTION("samplelist-iterator-twoover") {
    herald::analysis::sampling::SampleList<herald::datatype::RSSI,5> sl;
    sl.push(1234,-55);
    sl.push(1244,-60);
    sl.push(1265,-58);
    sl.push(1282,-61);
    sl.push(1294,-54);
    sl.push(1302,-47);
    sl.push(1304,-48);
    
    auto iter = std::begin(sl);
    auto endIter = std::end(sl);
    REQUIRE(iter != endIter);
    REQUIRE(iter != sl.end());
    REQUIRE(endIter != sl.begin());
    REQUIRE(endIter == sl.end());
    REQUIRE((*iter).value == -58);
    ++iter;
    REQUIRE((*iter).value == -61);
    ++iter;
    REQUIRE((*iter).value == -54);
    ++iter;
    REQUIRE((*iter).value == -47);
    ++iter;
    REQUIRE((*iter).value == -48);
    ++iter;
    REQUIRE(iter == endIter);
  }
}

TEST_CASE("samplelist-iterator-fullagain", "[samplelist][iterator][fullagain]") {
  SECTION("samplelist-iterator-fullagain") {
    herald::analysis::sampling::SampleList<herald::datatype::RSSI,5> sl;
    sl.push(1234,-55);
    sl.push(1244,-60);
    sl.push(1265,-58);
    sl.push(1282,-61);
    sl.push(1294,-54);
    sl.push(1302,-47);
    sl.push(1304,-48);
    sl.push(1305,-49);
    sl.push(1306,-45);
    sl.push(1307,-44);
    
    auto iter = std::begin(sl);
    auto endIter = std::end(sl);
    REQUIRE(iter != endIter);
    REQUIRE(iter != sl.end());
    REQUIRE(endIter != sl.begin());
    REQUIRE(endIter == sl.end());
    REQUIRE((*iter).value == -47);
    ++iter;
    REQUIRE((*iter).value == -48);
    ++iter;
    REQUIRE((*iter).value == -49);
    ++iter;
    REQUIRE((*iter).value == -45);
    ++iter;
    REQUIRE((*iter).value == -44);
    ++iter;
    REQUIRE(iter == endIter);
  }
}

TEST_CASE("samplelist-iterator-cleared", "[samplelist][iterator][cleared]") {
  SECTION("samplelist-iterator-cleared") {
    herald::analysis::sampling::SampleList<herald::datatype::RSSI,5> sl;
    sl.push(1234,-55);
    sl.push(1244,-60);
    sl.push(1265,-58);
    sl.push(1282,-61);
    sl.push(1294,-54);
    sl.push(1302,-47);
    sl.push(1304,-48);
    sl.push(1305,-49);
    sl.push(1306,-45);
    sl.push(1307,-44);

    sl.clear();
    
    auto iter = std::begin(sl);
    auto endIter = std::end(sl);
    REQUIRE(iter == endIter);
    REQUIRE(iter == sl.end());
    REQUIRE(endIter == sl.begin());
    REQUIRE(endIter == sl.end());
  }
}

// Now handle other container functionality required
