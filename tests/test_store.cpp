#include "../src/store.h"
#include <cstdio>
#include <cstdlib>
using namespace mc;
static int fails = 0;
#define CHECK(x) do { if (!(x)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #x); fails++; } } while (0)
int main() {
    setenv("MC_DATA", "/tmp/mc_test_data", 1);
    Profile p; p.id = newId(); p.name = "Test\nMUD"; p.host = "x.org"; p.port = 4242; p.tls = true; p.user = "bob"; p.setPassword("s3cret;=\\"); p.postLogin = "a\nb";
    saveProfiles({p});
    auto v = loadProfiles();
    CHECK(v.size() == 1 && v[0].name == "Test\nMUD" && v[0].port == 4242 && v[0].tls && v[0].password() == "s3cret;=\\" && v[0].postLogin == "a\nb");
    Settings s; s.theme = "Amber"; s.fontSize = 14; s.numpad = false; saveSettings(s);
    Settings t = loadSettings(); CHECK(t.theme == "Amber" && t.fontSize == 14 && !t.numpad && t.scrollback == 5000);
    CHECK(safeName("a b/c") == "a_b_c");
    printf(fails ? "%d FAILURES\n" : "store tests OK\n", fails); return fails ? 1 : 0;
}
