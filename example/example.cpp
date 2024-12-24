#include <iostream>
#include "redisfmt/redisfmt.hpp"

using namespace std;
using namespace rdsfmt;

int main() {
    redisContext *c = redisConnect("your redis host", 10019);
    if (c == NULL || c->err) {
        if (c) {
            printf("Error: %s\n", c->errstr);
            // handle error
        } else {
            printf("Can't allocate redis context\n");
        }
    }


    RedisMgr mgr;
    mgr.Initialize(c);
    cout << mgr.AUTH("nothing here").value_or("AUTH faild") << endl;
    cout << mgr.SELECT(11).value_or("SELECT 11 faild") << endl;
    mgr.HSET("test", "space1 test1", "space2 test2");
    cout << mgr.HGET<std::string>("test", "space1").value_or("no field space1") << endl;
    cout << mgr.HGET<std::string>("test", "space1 test1").value_or("no field space1 test1") << endl;

    return 0;
}