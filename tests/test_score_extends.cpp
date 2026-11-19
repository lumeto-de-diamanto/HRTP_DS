// Included by the existing projectile/gameplay harness; no extra executable.
#include <climits>
#include "main/extend.hpp"

static void score_extends_rules()
{
    ScoreExtends extends;
    int lives=3;
    for(int score:{INT_MIN,-1,0,399999}) {
        assert(!extends.update(score,lives));
        assert(lives==3 && extends.next_score()==400000);
    }
    assert(extends.update(400000,lives) && lives==4 && extends.next_score()==800000);
    assert(!extends.update(400000,lives) && lives==4);
    assert(!extends.update(799999,lives));
    assert(extends.update(800000,lives) && lives==5);

    // A score jump is caught up one call at a time, even if it spans a cap.
    assert(extends.update(2000000,lives) && lives==6 && extends.next_score()==1600000);
    assert(!extends.update(2000000,lives) && lives==6 && extends.next_score()==2000000);
    --lives;
    assert(extends.update(2000000,lives) && lives==6 && extends.next_score()==2400000);
    --lives;
    assert(!extends.update(2399999,lives) && lives==5); // capped threshold was not banked
    assert(extends.update(2400000,lives) && lives==6);

    extends.reset();lives=0;
    assert(extends.update(400000,lives) && lives==1); // last-life rescue
    extends.reset();lives=12;
    assert(!extends.update(400000,lives) && lives==12 && extends.next_score()==800000);
    extends.reset();lives=6;
    for(int i=0;i<INT_MAX/400000;++i) assert(!extends.update(INT_MAX,lives));
    assert(extends.next_score()==int64_t(INT_MAX/400000+1)*400000);
    assert(extends.next_score()>INT_MAX && !extends.update(INT_MAX,lives));
    extends.reset();lives=3;
    assert(!extends.update(0,lives) && extends.update(400000,lives) && lives==4);
    std::puts("Score extends: exact400k boundaries, one threshold per check, six-spare-life cap/consumption, large jumps, last-life rescue, reset and signed-score overflow safety passed");
}
