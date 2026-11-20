#include "../src/json.h"
#include <cstdio>
using namespace mc;
static int fails = 0;
#define CHECK(x) do { if (!(x)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #x); fails++; } } while (0)
int main() {
    JVal v;
    CHECK(JsonParser::parse("{\"num\": 123, \"name\":\"Temple \\u00e9\\n\", \"exits\":{\"n\":101,\"s\":\"102\"}, \"a\":[1,2.5,true,null], \"hp\":\"45\"}", v));
    CHECK(v["num"].num() == 123 && v["num"].str() == "123");
    CHECK(v["name"].str() == "Temple \xc3\xa9\n");
    CHECK(v["exits"]["n"].str() == "101" && v["exits"]["s"].str() == "102" && v["exits"].o.size() == 2);
    CHECK(v["a"].a.size() == 4 && v["a"].a[1].num() == 2.5 && v["a"].a[2].b && v["a"].a[3].t == JVal::Null);
    CHECK(v["hp"].num() == 45 && v["missing"].num(7) == 7);
    CHECK(!JsonParser::parse("{\"a\":", v) && !JsonParser::parse("{} x", v) && !JsonParser::parse("[1,", v));
    CHECK(JsonParser::parse("  [ ] ", v) && JsonParser::parse("42", v) && v.num() == 42);
    printf(fails ? "%d FAILURES\n" : "json tests OK\n", fails); return fails ? 1 : 0;
}
