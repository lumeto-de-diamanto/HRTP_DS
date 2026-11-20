// Included after main.cpp by the live-game harness. Verify actual boss routing
// selects defensive parity, not just the isolated pellet update's optional flag.
#include "sprites/Pellet.h"

namespace {
void pellet_fullrate_rendering()
{
    start_new_game();Pellets.init();camera.snap_center();
    for(int i=0;i<8;++i) {
        Pellets.add_group(160+i*32,160,PG_1,16);
        Pellets.at(i).decay_frame=i%3==0 ? 0 : i%3==1 ? 1 : 11;
    }
    const auto heap=test_heap_attempts,objects=test_obj_attempts;
    for(bool field:{false,true}) {
        Pellets.interlace_field=field;const auto frozen=Pellets;
        canvas_clear();canvas_playfield_clip(true);pellet_render_frame();
        for(int i=0;i<8;++i) for(int y=0;y<4;++y) for(int x=0;x<4;++x)
            assert(*canvas_at(camera.x(160+i*32)+x,Camera::y(160)+y)==
                (Pellet_cels[i%3][y*4+x] ? 0xffff : 0));
        assert(!std::memcmp(&frozen,&Pellets,sizeof(Pellets)));
    }
    assert(test_heap_attempts==heap && test_obj_attempts==objects);
    canvas_playfield_clip(false);canvas_clear();
}
}

void test_pellet_flow()
{
    pellet_fullrate_rendering();
    const auto saved_rank=rank;rank=RANK_NORMAL;
    const struct {int scene,stage;bool interlaced;} cases[]={
        {0,0,false},{0,4,false},{1,4,false},{2,4,false},
        {3,4,true},{4,4,true},{6,4,true},{5,0,false}
    };
    for(const auto &test:cases) {
        stage_load(test.scene,test.stage);
        if(stage_has_singyoku(test.scene,test.stage))
            for(int tick=0;singyoku_state().phase==SG_ENTRANCE;++tick) {assert(tick<300);singyoku_update();}
        Pellets.init();Items.init();shots_initialize();stage_card_total=stage_obstacle_total=stage_cards_removed_count=0;
        stage_score=0;stage_cardcombo_cur=stage_cardcombo_max=0;score_extends.reset();
        lives=3;rem_bombs=2;stage_timer=1000;pellet_speed=0;pellet_speed_frame=0;wait_for_shot=false;
        orb_initialize();orb_set_position(100,300);orb_set_velocity_x(OVX_0);orb_force_new(0.0,OF_IMMEDIATE);
        for(int i=0;i<8;++i) {
            Pellets.add_group(100+i*2,300,PG_1,16);
            Pellets.at(i).velocity.x.v=Pellets.at(i).velocity.y.v=0;
        }
        assert(!Pellets.interlace_field);gameplay_tick(0,0);
        assert(Pellets.interlace_field && Pellets.alive_count()==8);
        assert(stage_score==(test.interlaced ? 40 : 80));
        for(int i=0;i<8;++i) assert(Pellets.at(i).decay_frame==int(!test.interlaced || (i&1)));
        const auto frozen=Pellets;render_frame();assert(!std::memcmp(&frozen,&Pellets,sizeof(Pellets)));
        gameplay_tick(0,0);assert(!Pellets.interlace_field && stage_score==80);

        if(test.scene==4) {
            test_vblank_hook=[](){assert(!Pellets.interlace_field);test_keys_down=KEY_B;};
            assert(!pause_menu_run());test_vblank_hook=nullptr;test_keys_down=test_keys_held=0;
            assert(!Pellets.interlace_field);
            // A surviving miss advances one normal pellet pass, then freezes
            // parity throughout every animation wait, clear and recovery draw.
            lives=1;player_is_hit=true;
            test_vblank_hook=[](){assert(Pellets.interlace_field);};
            gameplay_tick(0,0);test_vblank_hook=nullptr;
            assert(lives==0 && Pellets.interlace_field);
            stage_load(4,4);assert(!Pellets.interlace_field); // retry starts fresh
        }
    }
    rank=saved_rank;start_new_game();assert(!Pellets.interlace_field);
    gameplay_tick(0,0);assert(!Pellets.interlace_field); // serving does not advance it
    std::puts("Pellet flow: actual regular/SinGyoku/YuugenMagan/Mima full-rate and Elis/Kikuri/Konngara alternating defenses, all-slot intact/shatter pixels, pure renders, pause/miss parity preservation and fresh retry/new-run phase passed");
}
