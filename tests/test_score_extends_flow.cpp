// Included after the existing main.cpp integration probes. Reuse the real
// stage/bonus/death paths rather than replay full fights or add a test command.
namespace {
void check_extend_hud()
{
    if(stage_has_boss(current_scene,current_stage)) {
        const int tile=1+('0'+lives-0x20)*2;
        for(int y=0;y<2;++y)
            assert(BG_MAP_RAM(30)[y*32+2]==((tile+y)|(TEXT_PAL_WHITE<<12)));
    } else for(int i=0;i<6;++i)
        assert(test_objs[OAM_SLOT_HUD_LIVES+i].hidden==(i>=lives));
}

void check_boss_bonus_extends()
{
    // SinGyoku bonus and lethal-hit boundaries, plus Mima/Kikuri's distinct
    // early-return/result-capture paths. Start immediately before defeat.
    for(int scenario=0;scenario<4;++scenario) {
        const int scene=scenario<2 ? 0 : scenario==2 ? 2 : 4;
        const int stage=scenario<2 ? 5 : scenario==2 ? 10 : 15;
        stage_load(scene,4);
        lives=3;rem_bombs=2;stage_timer=0;stage_cardcombo_max=0;pellet_speed=-4;
        if(scenario<2) {
            for(int i=0;i<200;++i) singyoku_update();
            auto &s=const_cast<SinGyokuState&>(singyoku_state());
            s.health.hp=1;s.health.invincible=false;s.health.invincibility_frame=0;
            orb_set_position(s.entity.left+24,s.entity.top+24);
        } else if(scenario==2) {
            auto &s=const_cast<MimaPhase1State&>(mima.state());
            s.phase=3;s.health.hp=0;s.checkpoint=true;
        } else {
            auto &s=const_cast<KikuriPhase1State&>(kikuri.state());
            s.phase=6;s.health.hp=0;s.checkpoint=true;
        }
        const auto bonus=roundbonus_compute(stage,scenario==1 ? 4 : 3);
        stage_score=scenario==1 ? 397000 : 400000-bonus.total-(scenario==0 ? 3000 : 0);
        const int expected=scenario==1 ? 400000+bonus.total : 400000;
        const auto sounds=std::count(test_effects.begin(),test_effects.end(),SFX_PICKUP);
        const auto objects=test_obj_attempts;
        test_vblank_hook=[](){test_keys_down=KEY_B;test_keys_held=0;};
        if(scenario==3) test_capture_source=[](int x,int y)->u16 {
            if(!x && !y) {
                assert(lives==4 && score_extends.next_score()==800000 && !g_canvas_target);
                assert(!test_bgs[0].visible && !test_bgs[1].visible && BG_PALETTE[0]==0);
                for(const auto &obj:test_objs) assert(obj.hidden);
                const auto *bg=reinterpret_cast<const u8*>(BG_BMP_RAM(4));
                assert(std::all_of(bg,bg+128*1024,[](u8 p){return !p;}));
            }
            return 0x8000;
        };
        gameplay_tick(0,0);
        test_vblank_hook=nullptr;test_capture_source=nullptr;test_keys_down=test_keys_held=0;
        assert(current_scene==(scenario<2 ? 1 : scenario==2 ? 4 : 6) && current_stage==0);
        assert(stage_score==expected && lives==4 && pellet_speed==-3);
        assert(score_extends.next_score()==800000 && test_obj_attempts==objects);
        assert(std::count(test_effects.begin(),test_effects.end(),SFX_PICKUP)==sounds+1);
        check_extend_hud();
        for(int i=0;i<3;++i) gameplay_tick(0,0); // serving/intro/result redraw cannot award again
        assert(stage_score==expected && lives==4 && score_extends.next_score()==800000);
        assert(std::count(test_effects.begin(),test_effects.end(),SFX_PICKUP)==sounds+1);
    }
}
}

void test_score_extends_flow()
{
    const bool saved_invincible=invincible;invincible=true;
    start_new_game();
    const auto objects=test_obj_attempts,heap=test_heap_attempts;
    // Exercise the real effects seam independently of the float speed helper.
    for(int speed:{-15,-4,0,19,20,25}) {
        score_extends.reset();stage_score=400000;lives=3;pellet_speed=speed;
        const auto sounds=test_effects.size();
        assert(score_extend_update() && lives==4 && pellet_speed==std::min(speed+1,20));
        assert(test_effects.size()==sounds+1 && test_effects.back()==SFX_PICKUP);
        assert(!score_extend_update() && test_effects.size()==sounds+1);
    }
    score_extends.reset();stage_score=1200000;lives=3;pellet_speed=-4;
    for(int i=0;i<3;++i) {
        assert(score_extend_update() && lives==4+i && pellet_speed==-3+i);
        assert(score_extends.next_score()==int64_t(2+i)*400000);
    }
    stage_score=1600000;
    const auto capped_sounds=test_effects.size();
    assert(!score_extend_update() && lives==6 && pellet_speed==-1);
    assert(score_extends.next_score()==2000000 && test_effects.size()==capped_sounds);
    --lives;assert(!score_extend_update() && lives==5); // no banking a capped award
    assert(test_obj_attempts==objects && test_heap_attempts==heap);

    // A real point pickup reaches exactly400k, changes the HUD and emits its
    // ordinary pickup sound plus ONE extend sound. Rendering/pause stay pure.
    stage_load(0,0);wait_for_shot=false;
    lives=3;stage_score=399000;point_value=0;pellet_speed=-4;
    orb_set_position(10000,200);
    Items.point[0]={player_get_state().x_position,player_get_state().y_position,2,IF_FALL,0,0};
    const auto pickups=std::count(test_effects.begin(),test_effects.end(),SFX_PICKUP);
    gameplay_tick(0,0);
    assert(stage_score==400000 && lives==4 && pellet_speed==-3 && score_extends.next_score()==800000);
    assert(std::count(test_effects.begin(),test_effects.end(),SFX_PICKUP)==pickups+2);
    check_extend_hud();
    stage_score=800000;wait_for_shot=true;
    render_frame();render_frame();gameplay_tick(0,0);
    test_vblank_hook=[](){test_keys_down=KEY_B;test_keys_held=0;};
    assert(!pause_menu_run());test_vblank_hook=nullptr;test_keys_down=0;
    assert(lives==4 && score_extends.next_score()==800000);
    gameplay_tick(KEY_B,KEY_B);
    assert(lives==5 && score_extends.next_score()==1200000 && pellet_speed==-2);
    check_extend_hud();
    stage_load(0,1,false,true);
    assert(stage_score==800000 && lives==5 && score_extends.next_score()==1200000);

    // The ordinary stage tally uses its pre-award resources, credits once,
    // and presents the new life throughout its blocking modal/next entrance.
    for(bool gameplay_award:{false,true}) {
        stage_load(0,0);lives=3;rem_bombs=2;stage_timer=0;frame=0;point_value=0;
        wait_for_shot=!gameplay_award;
        const auto bonus=stagebonus_compute(1,gameplay_award ? 4 : 3);
        static int expected_bonus_score;
        expected_bonus_score=gameplay_award ? 400000+bonus.total : 400000;
        stage_score=gameplay_award ? 399000 : 400000-bonus.total;
        if(gameplay_award) {
            orb_set_position(10000,200);
            Items.point[0]={player_get_state().x_position,player_get_state().y_position,2,IF_FALL,0,0};
        }
        stage_cards_removed_count=stage_card_total;
        test_vblank_hook=[](){
            assert(stage_score==expected_bonus_score && lives==4 && score_extends.next_score()==800000);
            check_extend_hud(); // includes a final-card award, not only a bonus-earned life
            test_keys_down=KEY_B;test_keys_held=0;
        };
        gameplay_tick(0,0);test_vblank_hook=nullptr;test_keys_down=0;
        assert(current_scene==0 && current_stage==1 && stage_score==expected_bonus_score && lives==4);
        check_extend_hud();
    }

    // Award BEFORE resolving a simultaneous last-life hit; preserve progress
    // through the99-frame miss. Only a subsequent fatal retry resets it.
    stage_load(0,1);wait_for_shot=false;lives=0;stage_score=400000;pellet_speed=-4;
    orb_set_position(10000,200);Pellets.init();Pellets.add_single(100,100,64,16);
    death_probe={};death_probe.allocations=test_heap_attempts;death_probe.objects=test_obj_attempts;
    test_vblank_hook=observe_player_death;player_is_hit=true;gameplay_tick(0,0);
    test_vblank_hook=nullptr;
    assert(death_probe.waits==99 && !death_probe.prompt && lives==0 && pellet_speed==-5);
    assert(stage_score==400000 && score_extends.next_score()==800000);
    death_probe={};death_probe.fatal=true;
    death_probe.allocations=test_heap_attempts;death_probe.objects=test_obj_attempts;
    test_vblank_hook=observe_player_death;player_is_hit=true;gameplay_tick(0,0);
    test_vblank_hook=nullptr;test_keys_down=test_keys_held=0;
    assert(death_probe.waits==31 && death_probe.prompt>=3 && lives==START_LIVES);
    assert(stage_score==0 && score_extends.next_score()==400000);
    stage_score=400000;assert(score_extend_update());
    start_new_game();assert(stage_score==0 && score_extends.next_score()==400000);

    check_boss_bonus_extends();
    // Final-boss points get the same post-defeat check without disturbing the
    // frozen421-frame sequence or redrawing resources over the ending hold.
    stage_load(6,4);lives=2;stage_score=400000;pellet_speed=-4;
    auto &final=const_cast<KonngaraCombatState&>(konngara.state());
    final.phase=7;final.health.hp=0;final.complete=true;
    const auto final_sounds=std::count(test_effects.begin(),test_effects.end(),SFX_PICKUP);
    gameplay_tick(0,0);
    assert(jigoku_cleared && lives==3 && score_extends.next_score()==800000 && pellet_speed==-3);
    assert(std::count(test_effects.begin(),test_effects.end(),SFX_PICKUP)==final_sounds+1);
    gameplay_tick(0,0);assert(lives==3 && score_extends.next_score()==800000);
    invincible=saved_invincible;start_new_game();camera.snap_center();
    std::puts("Score extend flow: raw+1/clamp/SFX15, real pickup/HUD, pure render/pause/serve, score-preserving handoff, one-shot stage/boss bonuses, pre-bonus resource accounting, last-life rescue/miss/continue/new-game reset, black Kikuri capture and final-boss terminal check passed");
}
