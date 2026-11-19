// Included by test_konngara_review.cpp: exercise the actual main.cpp statics,
// not a parallel practice game loop. Pixel/timing oracles remain separate.
extern bool invincible;
namespace {
struct KikuriGameProbe {
    int score=0,bonus=0,old_starts=0,old_fades=0,old_stops=0;
    int fade=0,title=0,intro=0,defeat=0,results=0,exit_fade=0,black=0;
    int kanji=0,banner=0;
    bool first_mask=false;
    unsigned game_frame=0,timer=0;
    int bombs=0,remaining_lives=0;
    size_t blocks=0,bytes=0,objects=0,allocations=0;
    OrbState orb{};
    KikuriPhase1 frozen;
    std::vector<u16> actor_palette,scenery,scene_palette;
} kikuri_game;

void check_kikuri_title_mask(int index)
{
    assert(test_bgs[2].sx==32 && test_bgs[2].sy==32);
    assert(test_bgs[2].x==(index%2)*32 && test_bgs[2].y==CANVAS_H);
    const u16 *glyph=font16x16_find(JIS_TOUHOU_REIIDEN[index]);assert(glyph);
    for(int y=0;y<24;++y) for(int x=0;x<32;++x) {
        const bool ink=x>=8 && x<24 && y>=4 && y<20 && (glyph[y-4]&(0x8000>>(x-8)));
        assert(*canvas_at(test_bgs[2].x+x,CANVAS_H+y)==(ink ? 0 : 0x8000));
    }
    // Use the selected font and real scenery, not fixed-copy glyph pixels.
    // A transparent glyph must actually expose nonblack bitmap colors.
    const auto *pixels=reinterpret_cast<const u8*>(BG_BMP_RAM(4));
    bool visible=false;
    for(int y=0;y<192;++y) for(int x=0;x<256;++x)
        if(!*canvas_at(test_bgs[2].x+x/8,CANVAS_H+y/8))
            visible|=BG_PALETTE[pixels[y*512+x+test_bgs[3].x]]!=0;
    assert(visible);
}

void observe_kikuri_entry()
{
    auto &p=kikuri_game;test_keys_down=KEY_B;test_keys_held=0;
    assert(stage_score==p.score && lives==6 && rem_bombs==4 && frame==p.game_frame);
    if(current_stage==3) {
        if(REG_MASTER_BRIGHT&31) {
            assert(REG_MASTER_BRIGHT==(0x8000|++p.fade) && p.fade<=16);
            assert(!REG_MASTER_BRIGHT_SUB); // bonus dimming has been restored before fade
        }
        return;
    }
    assert(!REG_MASTER_BRIGHT_SUB);
    assert(current_scene==4 && current_stage==4 && stage_timer==4000 && p.fade==16);
    assert(kikuri.state().frame==0 && kikuri.state().health.hp==14 && kikuri.state().hp_displayed==0);
    if(!g_canvas_target) {
        ++p.title;assert(test_kikuri_music_starts==p.old_starts);
        assert(REG_MASTER_BRIGHT==(p.title==1 ? 0x8010 : 0)); // first complete mask is still held black
        assert(std::equal(p.scenery.begin(),p.scenery.end(),BG_BMP_RAM(4)));
        const bool kanji=p.title<=49; // black-held first mask, then six8-frame glyphs
        assert(test_bgs[3].visible==kanji);
        if(kanji) {
            assert(test_bgs[3].x==32 && test_bgs[3].y==0);
            assert(std::equal(p.scene_palette.begin(),p.scene_palette.end(),BG_PALETTE));
            check_kikuri_title_mask(p.title==1 ? 0 : (p.title-2)/8);
            if(p.title>1) ++p.kanji;
        } else {
            assert(++p.banner<=70 && BG_PALETTE[0]==0);
            assert(test_bgs[2].sx==256 && test_bgs[2].sy==256);
            assert(!test_bgs[2].x && !test_bgs[2].y);
            assert(std::equal(p.scene_palette.begin()+1,p.scene_palette.end(),BG_PALETTE+1));
        }
        for(int slot=0;slot<=41;++slot) assert(test_objs[slot].hidden);
    } else {
        assert(++p.intro<1100);
        assert(p.title==119 && p.kanji==48 && p.banner==70);
        assert(test_kikuri_music_starts==p.old_starts+1);
        assert(test_heap_count==p.blocks+2 && test_obj_attempts==p.objects);
        if(p.intro==1) {
            // Loading the view happens behind the hidden bitmap. Its retained
            // snapshot must still be the old source-black field, not scenery.
            assert(!test_bgs[3].visible && BG_PALETTE[0]==0);
            const auto *pixels=reinterpret_cast<const u8*>(BG_BMP_RAM(4));
            assert(std::all_of(pixels,pixels+128*1024,[](u8 pixel){return !pixel;}));
        } else assert(test_bgs[3].visible);
    }
}

u16 capture_kikuri_black(int x,int y)
{
    if(x || y) return 0x8000; // validate capture setup once, not49,152 times
    auto &p=kikuri_game;
    assert(!g_canvas_target && !player_is_hit && !player_bomb().active() && !player_bomb().pending());
    assert(test_heap_count==p.blocks && test_heap_bytes==p.bytes);
    assert(REG_MASTER_BRIGHT==0x8010 && !test_bgs[0].visible && !test_bgs[1].visible);
    assert(BG_PALETTE[0]==0 && test_bgs[3].visible);
    for(const auto &obj:test_objs) assert(obj.hidden);
    for(int i=0;i<256;++i) if(i/16!=KIKURI_SOUL_PALETTE)
        assert(p.actor_palette[i]==SPRITE_PALETTE[i]); // borrowed soul bank is restored on exit
    // Capture ignores native brightness: the visible BG3 and backdrop must
    // genuinely be black before the hardware writes the captured image to D.
    const auto *pixels=reinterpret_cast<const u8*>(BG_BMP_RAM(4));
    assert(std::all_of(pixels,pixels+128*1024,[](u8 pixel){return !pixel;}));
    return 0x8000;
}

void observe_kikuri_result_commit()
{
    auto &p=kikuri_game;
    if(p.exit_fade!=16 || p.first_mask) return;
    assert(REG_MASTER_BRIGHT==0x8010 && REG_MASTER_BRIGHT_SUB==0x8010 && !g_canvas_target);
    if(current_scene==6 && test_bgs[2].sx==32 && test_bgs[2].y==CANVAS_H) {
        assert(current_stage==0 && camera.left==32 && REG_VCOUNT>=192 && REG_VCOUNT<200);
        p.first_mask=true;
    }
}

void observe_kikuri_defeat()
{
    auto &p=kikuri_game;test_keys_down=KEY_B;test_keys_held=0;
    assert(!player_death_frame && !player_is_hit && lives==p.remaining_lives && rem_bombs==p.bombs);
    assert(frame==p.game_frame && test_obj_attempts==p.objects);
    assert(test_mima_fades==p.old_fades+1);
    if(current_scene==4) {
        assert(test_heap_attempts==p.allocations);
        assert(current_stage==4 && stage_timer==p.timer && kikuri.state().checkpoint && kikuri.state().health.hp==0);
        assert(test_mima_stops==p.old_stops);
        assert(!std::memcmp(&p.orb,&orb_get_state(),sizeof(p.orb)));
        if(g_canvas_target) {
            if(!p.defeat) p.frozen=kikuri;
            assert(++p.defeat<300 && !std::memcmp(&p.frozen,&kikuri,sizeof(kikuri)));
            assert(stage_score==p.score);
            for(const auto &laser:kikuri.state().lasers) assert(!laser.alive);
            for(const auto &pellet:Pellets.p) assert(!pellet.moving);
        } else {
            assert(test_heap_count==p.blocks && test_heap_bytes==p.bytes && stage_score==p.score+p.bonus);
            if(test_bgs[0].map==31) {
                assert(++p.results<1200);
                if(REG_MASTER_BRIGHT&31) {
                    assert(REG_MASTER_BRIGHT==(0x8000|++p.exit_fade) && p.exit_fade<=16);
                    assert(REG_MASTER_BRIGHT_SUB==REG_MASTER_BRIGHT);
                }
            }
        }
    } else {
        assert(current_scene==6 && current_stage==0 && stage_score==p.score+p.bonus && p.exit_fade==16);
        if(!p.first_mask) assert(++p.black==1 && REG_MASTER_BRIGHT==0x8010 && REG_MASTER_BRIGHT_SUB==0x8010);
        else assert(!REG_MASTER_BRIGHT && !REG_MASTER_BRIGHT_SUB);
    }
}

void check_kikuri_pause_and_miss()
{
    const auto boss=kikuri;const auto orb=orb_get_state();
    const auto timer=stage_timer,game_frame=frame;
    const auto allocations=test_heap_attempts,objects=test_obj_attempts;
    const auto pauses=test_mima_pauses,resumes=test_mima_resumes;
    const std::vector<u16> pixels(BG_BMP_RAM(16),BG_BMP_RAM(16)+256*192);
    music_pause();test_vblank_hook=[](){check_pause_dimming();test_keys_down=KEY_B;};
    assert(!pause_menu_run());music_resume();test_vblank_hook=nullptr;test_keys_down=0;
    assert(test_mima_pauses==pauses+1 && test_mima_resumes==resumes+1);
    assert(!std::memcmp(&boss,&kikuri,sizeof(kikuri)) && !std::memcmp(&orb,&orb_get_state(),sizeof(orb)));
    assert(stage_timer==timer && frame==game_frame && std::equal(pixels.begin(),pixels.end(),BG_BMP_RAM(16)));
    assert(test_heap_attempts==allocations && test_obj_attempts==objects);

    lives=2;rem_bombs=1;Pellets.init();Pellets.add_single(100,100,64,16);
    orb_set_position(500,200);
    death_probe={};death_probe.allocations=allocations;death_probe.objects=objects;
    test_vblank_hook=observe_player_death;player_is_hit=true;gameplay_tick(0,0);test_vblank_hook=nullptr;
    assert(death_probe.waits==99 && death_probe.kikuri_active && lives==1 && rem_bombs==2);
    assert(stage_timer==death_probe.timer+200 && player_is_invincible());
    assert(kikuri.state().health.hp==boss.state().health.hp);
    Pellets.init();
}
}

void test_kikuri_stage15()
{
    test_case_name="Stage15: natural entry";
    REG_MASTER_BRIGHT=REG_MASTER_BRIGHT_SUB=0; // normal run, not prior renderer-probe brightness
    start_new_game();stage_load(4,3);
    const auto blocks=test_heap_count,bytes=test_heap_bytes,objects=test_obj_attempts;
    lives=6;rem_bombs=4;stage_score=45678;stage_timer=600;frame=100;
    stage_cards_removed_count=stage_card_total;wait_for_shot=true;
    kikuri_game={};auto &p=kikuri_game;
    p.score=stage_score+stagebonus_compute(14,lives).total;p.game_frame=frame;
    p.old_starts=test_kikuri_music_starts;p.blocks=blocks;p.bytes=bytes;p.objects=objects;
    p.scenery.assign(BG_BMP_RAM(4),BG_BMP_RAM(4)+65536);
    p.scene_palette.assign(BG_PALETTE,BG_PALETTE+208);
    test_vblank_hook=observe_kikuri_entry;gameplay_tick(0,0);test_vblank_hook=nullptr;test_keys_down=0;
    assert(current_scene==4 && current_stage==4 && global_stage_num()==15 && !wait_for_shot);
    assert(p.fade==16 && p.title==119 && p.intro>950 && g_canvas_target);
    assert(test_kikuri_music_starts==p.old_starts+1 && kikuri.state().phase==2 && kikuri.state().frame==0);
    assert(stage_score==p.score && lives==6 && rem_bombs==4 && test_heap_count==blocks+2);
    assert(test_obj_count==79 && !REG_MASTER_BRIGHT && !REG_MASTER_BRIGHT_SUB);

    test_case_name="Stage15: reentry/combat wiring";
    initialize_irand(3456);const int random=irand();initialize_irand(3456);
    const int reentry_stops=test_mima_stops;
    stage_load(4,4,false,true);assert(irand()==random);
    assert(test_mima_stops==reentry_stops+1); // reset the real idempotent KAMI playback clock
    assert(stage_timer==4000 && kikuri.state().health.hp==14 && kikuri.state().hp_displayed==0);
    // Full attack cycles/ranks already have independent controller oracles.
    // This test covers wiring at their boundaries, not thousands of duplicate
    // bitmap frames. Only the approved100-frame palette transition is replayed.
    const bool old_invincible=invincible;invincible=true;
    const auto steady_allocations=test_heap_attempts;
    auto &s=const_cast<KikuriPhase1State&>(kikuri.state());
    orb_set_position(300,100);gameplay_tick(0,0);
    assert(s.health.hp==13); // real orb hit through the intentionally buggy predicate
    s.health.hp=10;s.health.invincible=false;orb_set_position(500,200);
    gameplay_tick(0,0);assert(s.phase==3);
    for(int tick=0;tick<100;++tick) {orb_set_position(500,200);gameplay_tick(0,0);}
    assert(s.phase==4 && s.lightball_step==5 && !s.checkpoint);
    s.soul_phase=2;gameplay_tick(0,0);assert(s.phase==5 && !s.checkpoint);
    gameplay_tick(0,0); // initialize phase5's rank interval normally
    s.frame=1600;gameplay_tick(0,0);assert(s.phase==6 && !s.checkpoint);
    // One live-effect miss/pause exercises the shared path for this renderer;
    // the same path in other blocks is not a separate implementation.
    s.pattern=1;s.frame=270;
    s.tears[0]={301,392,25,1,1,true};
    assert(shootout_laser_spawn(s.lasers[0],307,147,300,400,50,20,8));
    test_case_name="Stage15: pause/miss";check_kikuri_pause_and_miss();
    assert(test_heap_attempts==steady_allocations && !s.checkpoint);
    for(auto &laser:s.lasers) laser={};
    for(auto &tear:s.tears) tear={};
    // Sample each final block once; exhaustive timing remains in phase6 tests.
    for(int pattern=0;pattern<4;++pattern) {
        s.pattern=pattern;s.frame=0;orb_set_position(500,200);gameplay_tick(0,0);
        assert(s.pattern==pattern && !s.checkpoint);
    }

    // A full real bomb follows pending/active/recovery states without borrowing
    // local practice bombs or changing resident actor palettes.
    test_case_name="Stage15: bomb";
    rem_bombs=2;const int hp=kikuri.state().health.hp;
    const std::vector<u16> actor_palette(SPRITE_PALETTE,SPRITE_PALETTE+256);
    orb_set_position(500,200);gameplay_tick(KEY_X,KEY_X);assert(player_bomb().pending() && rem_bombs==2);
    for(int tick=0;tick<210;++tick) {orb_set_position(500,200);gameplay_tick(0,0);}
    assert(rem_bombs==1 && !player_bomb().active() && !video_bomb_effect_active() && kikuri.state().health.hp==hp);
    assert(std::equal(actor_palette.begin(),actor_palette.end(),SPRITE_PALETTE));
    assert(test_heap_attempts==steady_allocations && test_obj_attempts==objects);

    // Navigation shoulder keys never launch a separate game loop.
    const int song_starts=test_kikuri_music_starts;
    assert(!stage_navigate(KEY_SELECT,KEY_L|KEY_R) && !stage_navigate(0,KEY_RIGHT));
    assert(test_kikuri_music_starts==song_starts && g_canvas_target);

    for(auto &laser:s.lasers) laser={};
    s.health.hp=1;s.health.invincible=false;s.health.invincibility_frame=0;
    assert(shootout_laser_spawn(s.lasers[0],100,100,120,120));
    orb_set_position(300,100);stage_timer=777;lives=0;
    const int scene_starts=scene_plays[6];const auto captures=test_capture_count;
    test_case_name="Stage15: defeat/results";
    p={};p.score=stage_score+7000;p.bonus=roundbonus_compute(15,lives).total;
    p.timer=stage_timer;p.game_frame=frame;p.bombs=rem_bombs;p.remaining_lives=lives;
    p.orb=orb_get_state();p.old_fades=test_mima_fades;p.old_stops=test_mima_stops;
    p.blocks=blocks;p.bytes=bytes;p.objects=objects;p.allocations=test_heap_attempts;p.actor_palette=actor_palette;
    test_capture_source=capture_kikuri_black;test_vblank_hook=observe_kikuri_defeat;
    test_bg_update_hook=observe_kikuri_result_commit;
    player_is_hit=true;gameplay_tick(KEY_X,KEY_X); // pending bomb + fatal miss lose to boss defeat
    test_capture_source=nullptr;test_vblank_hook=nullptr;test_bg_update_hook=nullptr;test_keys_down=test_keys_held=0;
    const int center=p.orb.y_position+16;
    assert(p.defeat==std::max(center,399-center)/2+1+79+1);
    assert(p.results>200 && p.exit_fade==16 && p.black==1 && p.first_mask);
    assert(test_capture_count==captures+1 && scene_plays[6]==scene_starts+1);
    assert(current_scene==6 && current_stage==0 && wait_for_shot && stage_timer==1600);
    assert(stage_score==p.score+p.bonus && hiscore>=stage_score && lives==0 && rem_bombs==p.bombs);
    assert(!REG_MASTER_BRIGHT && !REG_MASTER_BRIGHT_SUB && !g_canvas_target);
    assert(test_heap_count==blocks && test_heap_bytes==bytes && test_obj_count==79);
    const int credited=stage_score;gameplay_tick(0,0);assert(stage_score==credited);

    // Real level-select enters15 and releases every boss lease on the way out.
    test_case_name="Stage15: level select";
    stage_load(4,3);assert(stage_navigate(KEY_SELECT,KEY_RIGHT));
    assert(current_scene==4 && current_stage==4 && g_canvas_target && kikuri.state().health.hp==14);
    assert(stage_navigate(KEY_SELECT,KEY_RIGHT));assert(current_stage==0 && !g_canvas_target);
    assert(stage_navigate(KEY_SELECT,KEY_LEFT));assert(current_stage==4 && g_canvas_target);
    assert(test_heap_count==blocks+2 && test_obj_count==79);
    // Fatal retry and NEW GAME use the real prompt and clean the old buffer.
    for(bool new_run:{false,true}) {
        test_case_name=new_run ? "Stage15: fatal new game" : "Stage15: fatal retry";
        lives=0;stage_score=65432;orb_set_position(500,200);
        death_probe={};death_probe.fatal=true;death_probe.new_run=new_run;
        death_probe.allocations=test_heap_attempts;death_probe.objects=test_obj_attempts;
        test_vblank_hook=observe_player_death;player_is_hit=true;gameplay_tick(0,0);
        test_vblank_hook=nullptr;test_keys_down=test_keys_held=0;
        assert(death_probe.waits==31 && death_probe.prompt>=3 && lives==START_LIVES && stage_score==0);
        assert(new_run ? current_scene==0 && current_stage==0 && !g_canvas_target && rem_bombs==2 :
            current_scene==4 && current_stage==4 && g_canvas_target && rem_bombs==1 && kikuri.state().health.hp==14);
    }
    invincible=old_invincible;start_new_game();camera.snap_center();
    assert(test_heap_count==blocks && test_heap_bytes==bytes && test_obj_count==79);
    std::puts("Stage15: real14->15/scenery-filled six kanji/black70-frame banner/KAMI reveal, preserved run/RNG, combat boundary wiring, live-effect pause/99-frame miss/bomb/fatal retry/new-game, lethal priority/one defeat, black capture/one tally/15->16 restored brightness and level-select cleanup passed");
}
