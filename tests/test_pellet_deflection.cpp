// Included by the projectile harness: real pellet updates, controllable player
// defense state, and source rectangle/trajectory expectations independent of it.
static void signed_subpixels()
{
    for(int pixel:{-999,-640,-16,-8,-1,0,1,8,640,999}) {
        assert(TO_SP(pixel)==pixel*16);
        int inplace=pixel;TO_SP_INPLACE(inplace);assert(inplace==pixel*16);
        Subpixel position;position.set(pixel);
        assert(position.v==pixel*16 && position.to_pixel()==pixel);
    }
    assert(Subpixel::None()==-999*16);
}

static void deflection_bounds()
{
    const auto saved_player=player;
    orb_set_position(10000,10000);shots_initialize();deflecting=invincible=true;
    stage_score=0;effects.clear();
    for(int difficulty=0;difficulty<4;++difficulty) for(int left:{0,300,608}) {
        rank=rank_t(difficulty);player={left,player_top,PLAYER,0};
        for(int dx=-17;dx<=41;++dx) for(int dy=-25;dy<=33;++dy) for(int fraction:{0,1,15}) {
            Pellets.init();Pellets.add_group(100,100,PG_1,16);
            auto &p=Pellets.at(0);
            const int x=(left+dx)*16+fraction,y=(player_top+dy)*16+fraction;
            p.cur_left.v=x;p.cur_top.v=y;p.velocity.x.v=p.velocity.y.v=0;
            const bool visible=x>=-8*16 && x<=640*16 && y<=400*16;
            // Expand source's strict 8x8-vs-48x48 overlap, after flooring Q4
            // to pixels: left-16 < x < left+40; top-24 < y < top+32.
            const bool hit=visible && dx> -16 && dx<40 && dy> -24 && dy<32;
            assert(!Pellets.update());
            assert(p.moving==visible && p.cur_left.v==x && p.cur_top.v==y);
            assert(p.velocity.x.v==(hit ? (dx<=12 ? -128 : 128) : 0) && !p.velocity.y.v);
            assert(!p.decay_frame && p.motion_type==PM_REGULAR && !stage_score && effects.empty());
        }
    }
    player=saved_player;deflecting=false;
}

static void deflected_motion()
{
    const auto saved_player=player;player={300,player_top,PLAYER,0};
    orb_set_position(10000,10000);shots_initialize();invincible=true;rank=RANK_EASY;pellet_speed=0;
    for(auto motion:{PM_REGULAR,PM_GRAVITY,PM_SLING_AIMED,PM_FALL_STRAIGHT_FROM_TOP_THEN_REGULAR,PM_SPIN,PM_CHASE})
        for(int side:{-1,1}) {
            const int x=player.x_position+(side<0 ? 4 : 20);
            Pellets.init();Pellets.add_single(x,360,0,16,motion,32,x-16,360);
            auto &p=Pellets.at(0);p.sling_direction=1;deflecting=true;
            assert(!Pellets.update() && p.motion_type==PM_REGULAR);
            assert(p.from_group==PG_NONE && p.velocity.x.v==side*128 && !p.velocity.y.v);
            const auto before=p;deflecting=false;
            assert(!Pellets.update());
            assert(p.cur_left.v==before.cur_left.v+side*128 && p.cur_top.v==before.cur_top.v);
            assert(p.velocity.x.v==side*128 && !p.velocity.y.v && p.angle==before.angle);
            assert(p.spin_center.x.v==before.spin_center.x.v && p.spin_center.y.v==before.spin_center.y.v);
        }
    // The actual Kikuri ring from the review: defend for one frame only.
    Pellets.init();
    const int aim=iatan2(player_center_y()+4-224,player_center_x()+4-320);
    Pellets.add_single(332,224,aim,48,PM_SPIN,32,312,224);
    for(int tick=1;tick<=66;++tick) {deflecting=tick==66;assert(!Pellets.update(true));}
    const auto before=Pellets.at(0);assert(before.motion_type==PM_REGULAR && before.velocity.x.v==128);
    deflecting=false;Pellets.update();
    assert(Pellets.at(0).cur_left.v==before.cur_left.v+128 && Pellets.at(0).cur_top.v==before.cur_top.v);
    player=saved_player;Pellets.init();
}

static void interlaced_contacts()
{
    const auto saved_player=player;player={300,player_top,PLAYER,0};
    rank=RANK_EASY;pellet_speed=0;invincible=true;deflecting=false;shots_initialize();
    const auto fill=[](int x,int y) {
        for(int i=0;i<8;++i) {
            Pellets.add_group(x,y,PG_1,16);
            Pellets.at(i).velocity.x.v=16;Pellets.at(i).velocity.y.v=0;
        }
    };
    for(bool interlaced:{false,true}) {
        Pellets.init();stage_score=0;orb_set_position(160,120);fill(160,120);
        assert(!Pellets.interlace_field && !Pellets.update(interlaced) && Pellets.interlace_field);
        assert(stage_score==(interlaced ? 40 : 80) && Pellets.alive_count()==8);
        for(int i=0;i<8;++i) {
            const bool hit=!interlaced || (i&1);
            assert(Pellets.at(i).cur_left.v==160*16+16); // ALL slots moved
            assert(Pellets.at(i).decay_frame==int(hit) && Pellets.at(i).velocity.x.v==(hit ? 8 : 16));
        }
        assert(!Pellets.update(interlaced) && !Pellets.interlace_field && stage_score==80);
        for(int i=0;i<8;++i)
            assert(Pellets.at(i).decay_frame==(interlaced && !(i&1) ? 1 : 2));
    }
    // Empty passes still select parity; fresh init restarts at odd slots.
    Pellets.init();stage_score=0;assert(!Pellets.update(true) && Pellets.interlace_field);
    fill(160,120);Pellets.update(true);assert(!Pellets.interlace_field && stage_score==40);
    for(int i=0;i<8;++i) assert(Pellets.at(i).decay_frame==int(!(i&1)));

    orb_set_position(10000,10000);Pellets.init();stage_score=0;deflecting=true;
    for(int i=0;i<8;++i) {
        Pellets.add_group(320,360,PG_1,16);Pellets.at(i).velocity.x.v=Pellets.at(i).velocity.y.v=0;
    }
    assert(!Pellets.update(true));
    for(int i=0;i<8;++i) assert(Pellets.at(i).velocity.x.v==((i&1) ? 128 : 0));
    assert(!Pellets.update(true));
    for(int i=0;i<8;++i) {
        assert(Pellets.at(i).velocity.x.v==128 && !Pellets.at(i).velocity.y.v);
        assert(Pellets.at(i).cur_left.v==320*16+((i&1) ? 128 : 0));
    }
    assert(!stage_score);
    deflecting=false;

    // Player damage and ofuda interception do not share the defensive gate.
    for(bool grace:{false,true}) {
        Pellets.init();invincible=grace;orb_set_position(10000,10000);
        Pellets.add_group(308,player_top+4,PG_1,16);
        Pellets.at(0).velocity.x.v=Pellets.at(0).velocity.y.v=0;
        assert(Pellets.update(true)==!grace); // slot0 is INELIGIBLE for orb/deflection
    }
    invincible=true;
    for(int empty=0;empty<2;++empty) {
        Pellets.init();shots_initialize();stage_score=0;
        if(empty) Pellets.update(true);
        orb_set_position(100,100);Pellets.add_group(100,100,PG_1,16);
        Pellets.at(0).velocity.x.v=Pellets.at(0).velocity.y.v=0;
        shots_add(100,100);effects.clear();
        assert(!Pellets.update(true) && !Pellets.at(0).moving && !stage_score);
        assert(shots_get_state().sprite_number[0]==1 && effects==std::vector<int>{SFX_SHOT_HIT});
    }
    shots_initialize();Pellets.init();stage_score=0;
    Pellets.spawn_with_cloud=true;Pellets.add_group(100,100,PG_1,16);Pellets.spawn_with_cloud=false;
    for(int tick=1;tick<=8;++tick) {
        assert(!Pellets.update(true) && Pellets.interlace_field==bool(tick&1) && !stage_score);
        assert(Pellets.at(0).moving==(tick==8));
    }
    assert(Pellets.at(0).cloud_released && !Pellets.at(0).decay_frame);
    Pellets.update(true);assert(!Pellets.at(0).decay_frame && !stage_score); // slot0 skips update9
    Pellets.update(true);assert(Pellets.at(0).decay_frame==1 && stage_score==10);
    orb_set_position(10000,10000);Pellets.update(true);
    assert(Pellets.at(0).decay_frame==2); // decay advances on an ineligible slot too
    assert(Pellets.interlace_field);Pellets.decay();assert(Pellets.interlace_field);
    Pellets.reset_nonclouds();assert(Pellets.interlace_field);
    Pellets.init();assert(!Pellets.interlace_field && !Pellets.alive_count());
    player=saved_player;deflecting=false;invincible=true;
}

static void pellet_deflection_regressions()
{
    const auto saved_rank=rank;const int saved_speed=pellet_speed;
    signed_subpixels();deflection_bounds();deflected_motion();interlaced_contacts();
    rank=saved_rank;pellet_speed=saved_speed;deflecting=false;Pellets.init();
    std::puts("Pellet deflection: signed Q4 conversion/left wall, every strict edge/fraction/rank, source direction tie and persistent horizontal motion for spin/gravity/sling/chase/fall pellets passed");
    std::puts("Defensive parity: odd/even orb/deflection cadence, full-rate movement/player hits/ofuda/decay, empty passes, cloud release, clear/reset boundaries and no duplicate score passed");
}
