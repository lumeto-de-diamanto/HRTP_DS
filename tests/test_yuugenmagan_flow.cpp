// Included after the real main.cpp probes. No replacement practice game loop.
namespace {
struct YuugenGameProbe {
    int score=0,bonus=0,old_starts=0,old_stops=0,old_fades=0;
    int fade=0,title=0,handoff=0,defeat=0,results=0,exit_fade=0,black=0;
    int remaining_lives=0,after_lives=0,bombs=0,origin=0;
    bool first_mask=false;
    unsigned game_frame=0,timer=0;
    size_t blocks=0,bytes=0,objects=0,allocations=0,sfx=0;
    OrbState orb{};
    YuugenMaganPhase1 frozen;
    std::vector<u16> palette,actors;
} yuugen_game;

void observe_yuugen_entry()
{
    auto &p=yuugen_game;test_keys_down=KEY_B;test_keys_held=0;
    assert(stage_score==p.score && lives==6 && rem_bombs==4 && frame==p.game_frame);
    if(current_stage==3) {
        if(REG_MASTER_BRIGHT&31) {
            assert(REG_MASTER_BRIGHT==(0x8000|++p.fade) && p.fade<=16);
            assert(!REG_MASTER_BRIGHT_SUB);
        }
        return;
    }
    assert(current_scene==1 && current_stage==4 && stage_timer==3000 && p.fade==16);
    assert(yuugenmagan.state().phase==0 && yuugenmagan.state().frame==0);
    assert(yuugenmagan.state().health.hp==16 && yuugenmagan.state().hp_displayed==0);
    assert(!REG_MASTER_BRIGHT_SUB && test_obj_attempts==p.objects);
    assert(test_bgs[3].x==32 && test_bgs[3].y==0 && test_bgs[3].visible);
    assert(background_scroll_x()==32);
    if(!g_canvas_target) {
        ++p.title;assert(test_mima_music_starts==p.old_starts);
        assert(test_heap_count==p.blocks+1); // title uses hardware BG2, not the RAM lease
        assert(REG_MASTER_BRIGHT==(p.title==1 ? 0x8010 : 0));
        for(int slot=0;slot<48;++slot) assert(test_objs[slot].hidden);
        if(p.title<=49) {
            const int index=p.title==1 ? 0 : (p.title-2)/8;
            const u16 *glyph=font16x16_find(JIS_TOUHOU_REIIDEN[index]);assert(glyph);
            assert(test_bgs[2].sx==32 && test_bgs[2].sy==32);
            assert(test_bgs[2].x==(index%2)*32 && test_bgs[2].y==CANVAS_H);
            for(int y=0;y<24;++y) for(int x=0;x<32;++x) {
                const bool ink=x>=8 && x<24 && y>=4 && y<20 && (glyph[y-4]&(0x8000>>(x-8)));
                assert(*canvas_at(test_bgs[2].x+x,CANVAS_H+y)==(ink ? 0 : 0x8000));
            }
            // Arena index/signature pixels are installed before the first mask.
            const auto *pixels=reinterpret_cast<const u8*>(BG_BMP_RAM(4));
            assert(std::any_of(pixels,pixels+192*512,[](u8 pixel){return pixel>=16;}));
            assert(BG_PALETTE[15]==RGB15(10,10,10));
        } else assert(test_bgs[2].sx==256 && test_bgs[2].sy==256);
    } else {
        assert(++p.handoff==1 && p.title==119);
        assert(test_mima_music_starts==p.old_starts); // source puts actors before starting LEGEND
        assert(test_heap_count==p.blocks+2 && !regular_bomb_canvas_owned);
    }
}

u16 capture_yuugen_default(int x,int y)
{
    if(x || y) return 0x8000|((x+y)&31);
    auto &p=yuugen_game;
    assert(!g_canvas_target && !player_is_hit && !player_bomb().active() && !player_bomb().pending());
    assert(test_heap_count==p.blocks && test_heap_bytes==p.bytes);
    assert(!REG_MASTER_BRIGHT && test_bgs[3].visible && test_bgs[3].x==camera.left);
    assert(test_objs[0].size==SpriteSize_16x16 && !std::memcmp(test_objs[0].gfx,Miko_tiles,128));
    for(int slot=1;slot<48;++slot) assert(test_objs[slot].hidden);
    assert(std::all_of(canvas_at(0,0),canvas_at(0,CANVAS_H),[](u16 ink){return !ink;}));
    assert(std::equal(p.palette.begin(),p.palette.end(),BG_PALETTE));
    assert(std::equal(p.actors.begin(),p.actors.end(),SPRITE_PALETTE));
    return 0x8000;
}

void observe_yuugen_result_commit()
{
    auto &p=yuugen_game;
    if(p.exit_fade!=16 || p.first_mask) return;
    assert(REG_MASTER_BRIGHT==0x8010 && REG_MASTER_BRIGHT_SUB==0x8010 && !g_canvas_target);
    if(current_scene==3 && test_bgs[2].sx==32 && test_bgs[2].y==CANVAS_H) {
        assert(current_stage==0 && camera.left==32 && REG_VCOUNT>=192 && REG_VCOUNT<200);
        p.first_mask=true;
    }
}

void observe_yuugen_defeat()
{
    auto &p=yuugen_game;test_keys_down=KEY_B;test_keys_held=0;
    assert(!player_death_frame && !player_is_hit && rem_bombs==p.bombs && frame==p.game_frame);
    assert(test_obj_attempts==p.objects && test_mima_fades==p.old_fades+1);
    if(current_scene==1) {
        assert(current_stage==4 && stage_timer==p.timer && yuugenmagan.state().checkpoint);
        assert(test_heap_attempts==p.allocations && test_mima_stops==p.old_stops);
        assert(!std::memcmp(&p.orb,&orb_get_state(),sizeof(p.orb)));
        if(g_canvas_target) {
            if(!p.defeat) p.frozen=yuugenmagan;
            assert(++p.defeat<=207 && !std::memcmp(&p.frozen,&yuugenmagan,sizeof(yuugenmagan)));
            assert(lives==p.remaining_lives && stage_score==p.score && test_effects.size()==p.sfx+1); // lethal hit only
            assert(yuugenmagan.state().defeat_phase==p.origin);
            assert(yuugenmagan.missiles().alive_count()==(p.origin==11 ? 1 : 0));
            assert(Pellets.alive_count()==1 && Pellets.at(1).cloud_frame==1);
        } else {
            assert(lives==p.after_lives && stage_score==p.score+p.bonus && hiscore>=stage_score);
            assert(test_heap_count==p.blocks && test_heap_bytes==p.bytes);
            if(test_bgs[0].map==31) {
                assert(++p.results<1200);
                if(REG_MASTER_BRIGHT&31) {
                    assert(REG_MASTER_BRIGHT==(0x8000|++p.exit_fade) && p.exit_fade<=16);
                    assert(REG_MASTER_BRIGHT_SUB==REG_MASTER_BRIGHT);
                }
            }
        }
    } else {
        assert(current_scene==3 && current_stage==0 && p.exit_fade==16);
        assert(stage_score==p.score+p.bonus && lives==p.after_lives);
        if(!p.first_mask) assert(++p.black==1 && REG_MASTER_BRIGHT==0x8010 && REG_MASTER_BRIGHT_SUB==0x8010);
        else assert(!REG_MASTER_BRIGHT && !REG_MASTER_BRIGHT_SUB);
    }
}

void yuugen_tick_away()
{
    invincible=true;orb_set_position(320,240);gameplay_tick(0,0);
}

void yuugen_reach(int target)
{
    auto &s=const_cast<YuugenMaganPhase1State&>(yuugenmagan.state());
    while(s.phase==0) yuugen_tick_away();
    for(int hp:{15,12,10,8,2}) {
        if(s.phase>=target) return;
        s.health.hp=hp;s.health.invincible=false;yuugen_tick_away();
        const int transition=s.phase;
        for(int ticks=0;s.phase==transition;++ticks) {assert(ticks<71);yuugen_tick_away();}
    }
    while(s.phase!=target) {assert(s.phase==11 || s.phase==12);yuugen_tick_away();}
}
}

void test_yuugenmagan_stage10()
{
    test_case_name="Makai Stage10: natural entry";
    const auto old_rank=rank;rank=RANK_NORMAL;
    REG_MASTER_BRIGHT=REG_MASTER_BRIGHT_SUB=0;start_new_game();stage_load(1,3);
    const auto blocks=test_heap_count,bytes=test_heap_bytes,objects=test_obj_attempts;
    const std::vector<u16> actors(SPRITE_PALETTE,SPRITE_PALETTE+256);
    lives=6;rem_bombs=4;stage_score=45678;stage_timer=600;frame=100;
    stage_cards_removed_count=stage_card_total;wait_for_shot=true;
    yuugen_game={};auto &p=yuugen_game;
    p.score=stage_score+stagebonus_compute(9,lives).total;p.game_frame=frame;
    p.old_starts=test_mima_music_starts;p.blocks=blocks;p.bytes=bytes;p.objects=objects;
    initialize_irand(3456);const int random=irand();initialize_irand(3456);
    const auto extends=score_extends;
    test_vblank_hook=observe_yuugen_entry;gameplay_tick(0,0);test_vblank_hook=nullptr;test_keys_down=0;
    assert(current_scene==1 && current_stage==4 && global_stage_num()==10 && !wait_for_shot);
    assert(p.fade==16 && p.title==119 && p.handoff==1 && test_mima_music_starts==p.old_starts+1);
    assert(stage_score==p.score && lives==6 && rem_bombs==4 && stage_timer==3000 && frame==100);
    assert(!std::memcmp(&extends,&score_extends,sizeof(extends)) && irand()==random);
    assert(g_canvas_target && test_heap_count==blocks+2 && test_heap_bytes==bytes+65266+131072 && test_obj_count==79);
    assert(!REG_MASTER_BRIGHT && !REG_MASTER_BRIGHT_SUB);

    test_case_name="Makai Stage10: interactive opening/all blocks";
    const bool old_invincible=invincible;invincible=true;
    const auto allocations=test_heap_attempts;
    for(int tick=0;tick<331;++tick) yuugen_tick_away();
    assert(yuugenmagan.state().phase==1 && yuugenmagan.state().health.hp==16 && yuugenmagan.state().hp_displayed==16);
    orb_set_position(80,128);gameplay_tick(0,0);
    assert(yuugenmagan.state().phase==2 && yuugenmagan.state().health.hp==15 && stage_score==p.score+5000);
    yuugen_reach(13);
    assert(!yuugenmagan.state().checkpoint && test_heap_attempts==allocations && test_obj_attempts==objects);

    // Shared pause and surviving-miss path: every controller/missile byte is frozen.
    test_case_name="Makai Stage10: pause/miss";
    const auto boss=yuugenmagan;const auto orb=orb_get_state();const auto timer=stage_timer,game_frame=frame;
    const std::vector<u16> pixels(BG_BMP_RAM(16),BG_BMP_RAM(16)+256*192);
    music_pause();test_vblank_hook=[](){check_pause_dimming();test_keys_down=KEY_B;};
    assert(!pause_menu_run());music_resume();test_vblank_hook=nullptr;test_keys_down=0;
    assert(!std::memcmp(&boss,&yuugenmagan,sizeof(boss)) && !std::memcmp(&orb,&orb_get_state(),sizeof(orb)));
    assert(stage_timer==timer && frame==game_frame && std::equal(pixels.begin(),pixels.end(),BG_BMP_RAM(16)));
    auto &pool=const_cast<MissilePool&>(yuugenmagan.missiles());pool.add(300,240,0,0);
    lives=1;rem_bombs=1;Pellets.init();Pellets.add_single(100,100,64,16);orb_set_position(500,200);
    death_probe={};death_probe.allocations=allocations;death_probe.objects=objects;
    test_vblank_hook=observe_player_death;player_is_hit=true;gameplay_tick(0,0);test_vblank_hook=nullptr;
    assert(death_probe.waits==99 && death_probe.yuugenmagan_active && lives==0 && rem_bombs==2);
    assert(stage_timer==death_probe.timer+200 && player_is_invincible());Pellets.init();

    // Qualifying bomb damage uses the real run's last-life/global-frame rule.
    test_case_name="Makai Stage10: bombs";
    for(int remaining:{1,0}) {
        stage_load(1,4);for(int tick=0;tick<331;++tick) yuugen_tick_away();
        lives=remaining;rem_bombs=2;frame=48;
        orb_set_position(10000,200);gameplay_tick(KEY_X,KEY_X);
        for(int tick=0;tick<=50;++tick) {orb_set_position(10000,200);gameplay_tick(0,0);}
        assert(yuugenmagan.state().health.hp==16-(remaining==0 ? 1 : 0));
        assert(rem_bombs==1 && player_bomb().active() && !regular_bomb_canvas_owned);
        const auto allocated=test_heap_attempts;
        for(int tick=51;tick<=172;++tick) {orb_set_position(10000,200);gameplay_tick(0,0);}
        assert(!player_bomb().active() && !video_bomb_effect_active() && test_heap_attempts==allocated);
        assert(std::equal(actors.begin(),actors.begin()+16,SPRITE_PALETTE));
        assert(std::equal(actors.begin()+32,actors.begin()+48,SPRITE_PALETTE+32));
    }

    // Source11 retains missiles,13 clears them. Damage and bonus extends are
    // checked separately, with resource tally computed between those awards.
    for(int variant=0;variant<3;++variant) {
        test_case_name="Makai Stage10: defeat/results";
        stage_load(1,4);const std::vector<u16> arena(BG_PALETTE,BG_PALETTE+208);
        yuugen_reach(variant==0 ? 11 : 13);
        auto &s=const_cast<YuugenMaganPhase1State&>(yuugenmagan.state());
        s.health.hp=1;s.health.invincible=false;s.health.invincibility_frame=0;
        lives=variant==0 ? 1 : 0;rem_bombs=2;stage_timer=777;
        stage_score=variant==0 ? 395000 : variant==1 ? 394999 : 1000;score_extends.reset();
        pool.reset();pool.add(300,240,0,0);pool.update(0);
        Pellets.init();Pellets.add_single(100,140,0,32);Pellets.spawn_with_cloud=true;
        Pellets.add_single(100,140,0,32);Pellets.spawn_with_cloud=false;shots_add(240,200);
        orb_set_position(80,128);
        p={};p.score=stage_score+5000;p.remaining_lives=lives;p.origin=variant==0 ? 11 : 13;
        p.bombs=rem_bombs;p.timer=stage_timer;p.game_frame=frame;p.orb=orb_get_state();
        p.old_fades=test_mima_fades;p.old_stops=test_mima_stops;p.blocks=blocks;p.bytes=bytes;
        p.objects=objects;p.allocations=test_heap_attempts;p.sfx=test_effects.size();p.palette=arena;
        p.actors.assign(SPRITE_PALETTE,SPRITE_PALETTE+256);
        std::copy(actors.begin()+48,actors.begin()+64,p.actors.begin()+48); // borrowed boss bank restored at capture
        p.bonus=roundbonus_compute(10,lives+(variant==0 ? 1 : 0)).total;
        p.after_lives=lives+(variant<2 ? 1 : 0);
        const auto captures=test_capture_count;const int scene_starts=scene_plays[3];
        const auto misses=std::count(test_effects.begin(),test_effects.end(),SFX_MISS);
        test_capture_source=capture_yuugen_default;test_vblank_hook=observe_yuugen_defeat;
        test_bg_update_hook=observe_yuugen_result_commit;
        player_is_hit=true;gameplay_tick(KEY_X,KEY_X); // pending bomb and spare/fatal miss both lose to lethal HP
        test_capture_source=nullptr;test_vblank_hook=nullptr;test_bg_update_hook=nullptr;test_keys_down=test_keys_held=0;
        assert(p.defeat==207 && p.results>200 && p.exit_fade==16 && p.black==1 && p.first_mask);
        assert(test_capture_count==captures+1 && scene_plays[3]==scene_starts+1);
        assert(current_scene==3 && current_stage==0 && global_stage_num()==11 && wait_for_shot && stage_timer==1400);
        assert(stage_score==p.score+p.bonus && hiscore>=stage_score && lives==p.after_lives && rem_bombs==2);
        assert(std::count(test_effects.begin(),test_effects.end(),SFX_MISS)==misses);
        assert(!REG_MASTER_BRIGHT && !REG_MASTER_BRIGHT_SUB && !g_canvas_target);
        assert(test_heap_count==blocks && test_heap_bytes==bytes && test_obj_count==79);
        const int credited=stage_score;gameplay_tick(0,0);assert(stage_score==credited);
    }

    test_case_name="Makai Stage10: navigation/retry/new game";
    stage_load(1,3);
    assert(!stage_navigate(KEY_SELECT,KEY_Y) && !stage_navigate(KEY_SELECT|KEY_L,KEY_Y));
    assert(current_scene==1 && current_stage==3 && !g_canvas_target);
    assert(stage_navigate(KEY_SELECT,KEY_RIGHT));assert(current_stage==4 && g_canvas_target);
    assert(!stage_navigate(KEY_SELECT,KEY_Y) && !stage_navigate(KEY_SELECT|KEY_L,KEY_Y));
    assert(stage_navigate(KEY_SELECT,KEY_RIGHT));assert(current_stage==0 && !g_canvas_target);
    assert(stage_navigate(KEY_SELECT,KEY_LEFT));assert(current_stage==4 && g_canvas_target);
    const int old_stops=test_mima_stops;stage_load(1,4,false,true);
    assert(test_mima_stops==old_stops+1 && yuugenmagan.state().frame==0 && stage_timer==3000);
    for(bool new_run:{false,true}) {
        lives=0;stage_score=65432;yuugen_reach(1);orb_set_position(500,200);
        death_probe={};death_probe.fatal=true;death_probe.new_run=new_run;
        death_probe.allocations=test_heap_attempts;death_probe.objects=test_obj_attempts;
        test_vblank_hook=observe_player_death;player_is_hit=true;gameplay_tick(0,0);
        test_vblank_hook=nullptr;test_keys_down=test_keys_held=0;
        assert(death_probe.waits==31 && death_probe.prompt>=3 && lives==START_LIVES && stage_score==0);
        assert(new_run ? current_scene==0 && current_stage==0 && !g_canvas_target && rem_bombs==2 :
            current_scene==1 && current_stage==4 && g_canvas_target && rem_bombs==1 && yuugenmagan.state().health.hp==16);
    }
    for(int scene:{2,6,0}) {
        stage_load(1,4);stage_load(scene,4); // cross-boss shared cache/OBJ handoffs
        assert(test_obj_count==(scene==6 ? 80 : 79));
        stage_load(0,0);assert(test_heap_count==blocks && test_heap_bytes==bytes);
    }
    assert(scene_stage_count(1)==5 && scene_stage_count(3)==4 && scene_stage_count(5)==4);
    rank=old_rank;invincible=old_invincible;start_new_game();camera.snap_center();
    assert(test_heap_count==blocks && test_heap_bytes==bytes && test_obj_count==79);
    std::puts("Makai Stage10: real9->10/scenery-backed title/interactive opening/one LEGEND start, all13 phases, five-eye radar and normal run resources, pause/99-frame miss/last-life bomb/fatal retry/new game, phase11/13 lethal priority/frozen defeat/default-pose capture, exact gameplay-vs-bonus extends/one tally/black-held10->11, no YuugenMagan shortcuts and leak-free navigation/cache handoffs passed");
}
