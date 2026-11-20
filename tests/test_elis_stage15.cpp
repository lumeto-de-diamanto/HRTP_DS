// Included after the real main.cpp; boundary wiring, not another practice loop.
namespace {
struct ElisGameProbe {
    int score=0,bonus=0,starts=0,fades=0,stops=0;
    int fade=0,title=0,intro=0,defeat=0,results=0,exit_fade=0,black=0;
    int bombs=0,before_lives=0,after_lives=0,gameplay_extend=0,defeat_waits=0;
    unsigned game_frame=0,timer=0;
    bool first_mask=false;
    size_t blocks=0,bytes=0,objects=0,allocations=0;
    OrbState orb{};PlayerState player{};ElisPhase1 frozen;
    std::vector<u16> actors;
} elis_game;
void observe_elis_entry()
{
    auto &p=elis_game;test_keys_down=KEY_B|KEY_SELECT;test_keys_held=KEY_RIGHT;
    assert(stage_score==p.score && lives==6 && rem_bombs==4 && frame==p.game_frame);
    if(current_stage==3) {
        if(REG_MASTER_BRIGHT&31) assert(REG_MASTER_BRIGHT==(0x8000|++p.fade) && p.fade<=16 && !REG_MASTER_BRIGHT_SUB);
        return;
    }
    assert(current_scene==3 && current_stage==4 && stage_timer==4000 && p.fade==16 && !REG_MASTER_BRIGHT_SUB);
    assert(elis.state().phase==1 && !elis.state().frame && elis.state().health.hp==14 && !elis.state().hp_displayed);
    assert(test_obj_attempts==p.objects && test_bgs[3].x==32 && !test_bgs[3].y && background_scroll_x()==32);
    if(!g_canvas_target) {
        assert(++p.title<=119 && test_elis_music_starts==p.starts && test_heap_count==p.blocks+1);
        assert(REG_MASTER_BRIGHT==(p.title==1 ? 0x8010 : 0));
        for(int slot=0;slot<48;++slot) assert(test_objs[slot].hidden);
        if(p.title<=49) {
            const int index=p.title==1 ? 0 : (p.title-2)/8;
            const u16 *glyph=font16x16_find(JIS_TOUHOU_REIIDEN[index]);assert(glyph);
            for(int y=0;y<24;++y) for(int x=0;x<32;++x) {
                const bool ink=x>=8 && x<24 && y>=4 && y<20 && (glyph[y-4]&(0x8000>>(x-8)));
                assert(*canvas_at(test_bgs[2].x+x,CANVAS_H+y)==(ink ? 0 : 0x8000));
            }
            const auto *pixels=reinterpret_cast<const u8*>(BG_BMP_RAM(4));
            assert(std::any_of(pixels,pixels+192*512,[](u8 pixel){return pixel>=16;}));
        }
    } else {
        assert(++p.intro<=961 && p.title==119 && test_elis_music_starts==p.starts+1);
        assert(test_heap_count==p.blocks+2 && !regular_bomb_canvas_owned);
        if(p.intro==1) {p.orb=orb_get_state();p.player=player_get_state();}
        assert(!std::memcmp(&p.orb,&orb_get_state(),sizeof(p.orb)) && !std::memcmp(&p.player,&player_get_state(),sizeof(p.player)));
        if(p.intro<=960) assert(test_objs[OAM_SLOT_ORB_ARROW].hidden);
    }
}
u16 capture_elis_black(int x,int y)
{
    if(x || y) return 0x8000;
    auto &p=elis_game;
    assert(!g_canvas_target && !player_is_hit && !player_bomb().active() && !player_bomb().pending());
    assert(test_heap_count==p.blocks && test_heap_bytes==p.bytes && REG_MASTER_BRIGHT==0x8010);
    assert(!test_bgs[0].visible && !test_bgs[1].visible && test_bgs[3].visible && !BG_PALETTE[0]);
    for(const auto &obj:test_objs) assert(obj.hidden);
    assert(std::equal(p.actors.begin(),p.actors.end(),SPRITE_PALETTE));
    const auto *pixels=reinterpret_cast<const u8*>(BG_BMP_RAM(4));
    assert(std::all_of(pixels,pixels+128*1024,[](u8 pixel){return !pixel;}));
    return 0x8000;
}
void observe_elis_result_commit()
{
    auto &p=elis_game;if(p.exit_fade!=16 || p.first_mask) return;
    assert(REG_MASTER_BRIGHT==0x8010 && REG_MASTER_BRIGHT_SUB==0x8010 && !g_canvas_target);
    if(current_scene==5 && test_bgs[2].sx==32 && test_bgs[2].y==CANVAS_H) {
        assert(!current_stage && camera.left==32 && REG_VCOUNT>=192 && REG_VCOUNT<200);p.first_mask=true;
    }
}
void observe_elis_defeat()
{
    auto &p=elis_game;test_keys_down=KEY_B;test_keys_held=0;
    assert(!player_death_frame && !player_is_hit && rem_bombs==p.bombs && frame==p.game_frame);
    assert(test_obj_attempts==p.objects && test_mima_fades==p.fades+1);
    if(current_scene==3) {
        assert(current_stage==4 && stage_timer==p.timer && elis.state().checkpoint && !elis.state().health.hp);
        assert(test_heap_attempts==p.allocations && test_mima_stops==p.stops);
        assert(!std::memcmp(&p.orb,&orb_get_state(),sizeof(p.orb)));
        if(g_canvas_target) {
            if(!p.defeat) p.frozen=elis;
            assert(++p.defeat<=p.defeat_waits && !std::memcmp(&p.frozen,&elis,sizeof(elis)) && stage_score==p.score);
            assert(lives==p.before_lives+(p.defeat==p.defeat_waits ? p.gameplay_extend : 0));
            assert(!elis.missiles().alive_count());for(const auto &laser:elis.state().lasers) assert(!laser.alive);
            assert(Pellets.alive_count()==1 && Pellets.at(1).cloud_frame==1);
        } else {
            assert(stage_score==p.score+p.bonus && lives==p.after_lives && test_heap_count==p.blocks && test_heap_bytes==p.bytes);
            if(test_bgs[0].map==31) {
                assert(++p.results<1200);
                if(REG_MASTER_BRIGHT&31) assert(REG_MASTER_BRIGHT==(0x8000|++p.exit_fade) && p.exit_fade<=16 && REG_MASTER_BRIGHT_SUB==REG_MASTER_BRIGHT);
            }
        }
    } else {
        assert(current_scene==5 && !current_stage && p.exit_fade==16 && stage_score==p.score+p.bonus && lives==p.after_lives);
        if(!p.first_mask) assert(++p.black==1 && REG_MASTER_BRIGHT==0x8010 && REG_MASTER_BRIGHT_SUB==0x8010);
        else assert(!REG_MASTER_BRIGHT && !REG_MASTER_BRIGHT_SUB);
    }
}
void elis_tick_away()
{
    invincible=true;orb_set_position(10000,200);gameplay_tick(0,0);
}
}
void test_elis_stage15()
{
    const auto old_rank=rank;const bool old_invincible=invincible;rank=RANK_NORMAL;
    const bool manual=test_kikuri_manual_ticks;test_kikuri_manual_ticks=false;test_kikuri_tick_available=true;
    test_case_name="Makai Stage15: natural14->15 entry";
    REG_MASTER_BRIGHT=REG_MASTER_BRIGHT_SUB=0;start_new_game();stage_load(3,3);
    const auto blocks=test_heap_count,bytes=test_heap_bytes,objects=test_obj_attempts;
    lives=6;rem_bombs=4;stage_score=45678;stage_timer=600;frame=100;
    stage_cards_removed_count=stage_card_total;wait_for_shot=true;
    elis_game={};auto &p=elis_game;p.score=stage_score+stagebonus_compute(14,lives).total;p.game_frame=frame;
    p.starts=test_elis_music_starts;p.blocks=blocks;p.bytes=bytes;p.objects=objects;
    initialize_irand(3456);const int random=irand();initialize_irand(3456);const auto extends=score_extends;
    test_vblank_hook=observe_elis_entry;gameplay_tick(0,0);test_vblank_hook=nullptr;test_keys_down=test_keys_held=0;
    assert(current_scene==3 && current_stage==4 && global_stage_num()==15 && !wait_for_shot);
    assert(p.fade==16 && p.title==119 && p.intro==961 && test_elis_music_starts==p.starts+1);
    assert(stage_score==p.score && lives==6 && rem_bombs==4 && stage_timer==4000 && frame==100);
    assert(!std::memcmp(&extends,&score_extends,sizeof(extends)) && irand()==random);
    assert(test_heap_count==blocks+2 && test_heap_bytes==bytes+219654 && g_canvas_target && test_obj_attempts==objects);
    const auto allocations=test_heap_attempts;
    test_case_name="Makai Stage15: all blocks/real contact";
    invincible=true;orb_set_position(264,84);gameplay_tick(0,0);assert(elis.state().health.hp==13 && stage_score==p.score+7000);
    auto &s=const_cast<ElisPhase1State&>(elis.state());
    s.health.hp=10;s.health.invincible=false;s.pattern=0;s.teleport_done=false;s.frame=80;elis_tick_away();assert(s.phase==2);
    for(int i=0;i<81;++i) elis_tick_away();
    assert(s.phase==3 && !s.frame);
    for(int i=0;i<132;++i) elis_tick_away();
    assert(s.attack_started && !s.frame);
    for(int i=0;i<61;++i) elis_tick_away();
    assert(!s.pattern);
    s.health.hp=6;s.health.invincible=false;s.pattern=0;s.teleport_done=false;s.frame=80;elis_tick_away();assert(s.phase==4);
    // Count real presentations, not just logical updates: scroll waits must
    // share the ordinary VBlank rather than restoring the old42 frozen holds.
    static int elis_transition_waits;elis_transition_waits=0;
    test_vblank_hook=[](){++elis_transition_waits;};
    for(int i=0;i<182;++i) elis_tick_away();
    test_vblank_hook=nullptr;
    assert(elis_transition_waits==182 && s.phase==5 && s.bat_form && !s.frame);
    for(int i=0;i<3;++i) elis_tick_away();
    s.transforming=true;s.frame=0;for(int i=0;i<101;++i) elis_tick_away();assert(!s.bat_form && !s.frame);
    for(int i=0;i<132;++i) elis_tick_away();
    assert(s.attack_started);

    test_case_name="Makai Stage15: shared pause/miss";
    const auto frozen=elis;const auto orb=orb_get_state();const auto game_frame=frame,timer=stage_timer;
    music_pause();test_vblank_hook=[](){check_pause_dimming();test_keys_down=KEY_B;};
    assert(!pause_menu_run());music_resume();test_vblank_hook=nullptr;test_keys_down=0;
    assert(!std::memcmp(&frozen,&elis,sizeof(elis)) && !std::memcmp(&orb,&orb_get_state(),sizeof(orb)) && frame==game_frame && stage_timer==timer);
    auto &pool=const_cast<MissilePool&>(elis.missiles());pool.add(100,100,1,1);
    lives=1;rem_bombs=1;Pellets.init();Pellets.add_single(100,100,64,16);orb_set_position(10000,200);
    death_probe={};death_probe.allocations=allocations;death_probe.objects=objects;
    test_vblank_hook=observe_player_death;player_is_hit=true;gameplay_tick(0,0);test_vblank_hook=nullptr;
    assert(death_probe.waits==99 && death_probe.elis_active && !lives && rem_bombs==2 && stage_timer==death_probe.timer+200);
    Pellets.init();
    test_case_name="Makai Stage15: last-life bomb";
    const int hp=s.health.hp;frame=48;orb_set_position(10000,200);gameplay_tick(KEY_X,KEY_X);
    for(int i=0;i<210;++i) elis_tick_away();
    assert(rem_bombs==1 && !player_bomb().active() && !video_bomb_effect_active() && s.health.hp<hp && s.health.hp>0);
    assert(!regular_bomb_canvas_owned && test_heap_attempts==allocations && test_obj_attempts==objects);

    test_case_name="Makai Stage15: fatal retry";
    const int retry_starts=test_elis_music_starts;orb_set_position(10000,200);lives=0;
    death_probe={};death_probe.fatal=true;death_probe.allocations=test_heap_attempts;death_probe.objects=objects;
    test_vblank_hook=observe_player_death;player_is_hit=true;gameplay_tick(0,0);test_vblank_hook=nullptr;test_keys_down=test_keys_held=0;
    assert(death_probe.elis_active && death_probe.prompt>=3 && test_elis_music_starts==retry_starts+1);
    assert(lives==START_LIVES && rem_bombs==1 && !frame && stage_timer==4000 && s.phase==1 && s.health.hp==14);

    // Two score boundaries cover gameplay-vs-bonus extends, not a phase matrix.
    for(int variant=0;variant<2;++variant) {
        test_case_name="Makai Stage15: defeat/results/16 handoff";stage_load(3,4);
        s.phase=5;s.health.hp=1;s.health.invincible=false;s.health.invincibility_frame=0;
        s.bat_form=variant==0;s.transforming=variant==1;s.frame=0;s.image=5;
        pool.add(100,160,0,1);pool.update(0);assert(shootout_laser_spawn(s.lasers[0],312,124,300,400));
        Pellets.init();Pellets.add_single(100,140,0,32);Pellets.spawn_with_cloud=true;
        Pellets.add_single(100,140,0,32);Pellets.spawn_with_cloud=false;shots_add(240,200);
        lives=variant==0 ? 1 : 0;rem_bombs=2;stage_timer=777;frame=100;
        stage_score=variant==0 ? 393000 : 392999;score_extends.reset();orb_set_position(variant==0 ? 300 : 280,variant==0 ? 128 : 120);
        p={};p.score=stage_score+7000;p.gameplay_extend=variant==0;p.before_lives=lives;p.after_lives=lives+1;
        p.bonus=roundbonus_compute(15,lives+p.gameplay_extend).total;p.bombs=rem_bombs;p.timer=stage_timer;p.game_frame=frame;
        p.orb=orb_get_state();const int center=p.orb.y_position+16;p.defeat_waits=std::max(center,399-center)/2+81;
        p.fades=test_mima_fades;p.stops=test_mima_stops;p.blocks=blocks;p.bytes=bytes;p.objects=objects;p.allocations=test_heap_attempts;
        p.actors.assign(SPRITE_PALETTE,SPRITE_PALETTE+256);
        const auto captures=test_capture_count;const int scene_starts=scene_plays[5],misses=std::count(test_effects.begin(),test_effects.end(),SFX_MISS);
        test_capture_source=capture_elis_black;test_vblank_hook=observe_elis_defeat;test_bg_update_hook=observe_elis_result_commit;
        player_is_hit=true;gameplay_tick(KEY_X,KEY_X);
        test_capture_source=nullptr;test_vblank_hook=nullptr;test_bg_update_hook=nullptr;test_keys_down=test_keys_held=0;
        assert(p.defeat==p.defeat_waits && p.results>200 && p.exit_fade==16 && p.black==1 && p.first_mask);
        assert(test_capture_count==captures+1 && scene_plays[5]==scene_starts+1 && current_scene==5 && !current_stage && global_stage_num()==16);
        assert(stage_score==p.score+p.bonus && hiscore>=stage_score && lives==p.after_lives && rem_bombs==2 && wait_for_shot && stage_timer==1600);
        assert(std::count(test_effects.begin(),test_effects.end(),SFX_MISS)==misses && !REG_MASTER_BRIGHT && !REG_MASTER_BRIGHT_SUB);
        assert(!g_canvas_target && test_heap_count==blocks && test_heap_bytes==bytes && test_obj_attempts==objects);
        const int credited=stage_score;gameplay_tick(0,0);assert(stage_score==credited);
    }
    test_case_name="Makai Stage15: removed shortcuts/navigation cleanup";stage_load(3,3);
    assert(!stage_navigate(KEY_SELECT,KEY_X|KEY_Y) && stage_navigate(KEY_SELECT,KEY_RIGHT));
    assert(current_stage==4 && g_canvas_target && !stage_navigate(KEY_SELECT,KEY_X|KEY_Y));
    assert(stage_navigate(KEY_SELECT,KEY_RIGHT) && !current_stage && !g_canvas_target);
    assert(stage_navigate(KEY_SELECT,KEY_LEFT) && current_stage==4 && g_canvas_target);
    start_new_game();assert(!g_canvas_target && !current_scene && !current_stage && scene_stage_count(5)==4);
    test_kikuri_manual_ticks=manual;rank=old_rank;invincible=old_invincible;
    std::puts("Makai Stage15: natural14->15/scenery title/one KAMI2/frozen799-frame intro, all five blocks and live run resources, shared pause/miss/last-life bomb/fatal retry, lethal priority/clear/clouds, exact gameplay-vs-bonus extends/black capture/one tally/16 handoff, removed X/Y shortcuts and navigation/new-game cleanup passed");
}
