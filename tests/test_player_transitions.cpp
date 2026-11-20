// Included by the existing viewport harness. Exercise the real controller AND
// OAM renderer at attack boundaries, with original movement/timing as oracle.
#include "sprites/Miko.h"
#include "sprites/Miko32.h"
#include "sprites/Miko48.h"

extern u16 *g_regular_gfx[];
extern u16 *g_32_gfx[];
extern u16 *g_48_gfx[];

namespace {
void transition_pose(PlayerSpritesheet sheet,int cel)
{
    const auto before=player_get_state();
    assert(before.spritesheet==sheet && before.sprite_number==cel);
    const auto *tiles=reinterpret_cast<const u8*>(sheet==PLAYER ? Miko_tiles :
        sheet==PLAYER_32 ? Miko32_tiles : Miko48_tiles);
    const size_t bytes=sheet==PLAYER ? 128 : sheet==PLAYER_32 ? 256 : 512;
    const u16 *gfx=sheet==PLAYER ? g_regular_gfx[cel] : sheet==PLAYER_32 ? g_32_gfx[cel] : g_48_gfx[cel];
    const auto heap=test_heap_attempts,objects=test_obj_attempts,copies=test_dma_copy_calls;
    for(int repeat=0;repeat<2;++repeat) {
        player_render_frame();
        const auto &obj=test_objs[0];
        assert(obj.gfx==gfx && !obj.hflip && !obj.hidden && obj.palette==0);
        assert(obj.size==(sheet==PLAYER ? SpriteSize_16x16 : sheet==PLAYER_32 ? SpriteSize_32x16 : SpriteSize_32x32));
        assert(obj.x==camera.x(before.x_position)-(sheet==PLAYER ? 0 : 8));
        assert(obj.y==Camera::y(before.y_position)-(sheet==PLAYER_48 ? 16 : 0));
        assert(!std::memcmp(obj.gfx,tiles+cel*bytes,bytes));
        assert(!std::memcmp(&before,&player_get_state(),sizeof(before)));
    }
    assert(test_heap_attempts==heap && test_obj_attempts==objects && test_dma_copy_calls==copies);
}

int transition_step(int x,int delta) { return std::max(PLAYER_LEFT_MIN,std::min(PLAYER_LEFT_MAX,x+delta)); }
int transition_idle(bool right) { return right ? SPRITE_IDLE_RIGHT : SPRITE_IDLE_LEFT; }
void transition_reset()
{
    player_initialize();shots_initialize();rem_bombs=0;player_is_hit=false;
    orb_set_position(10000,10000); // actual orb collision must not perturb the animation probe
}
void transition_face(bool right)
{
    transition_reset();player_update(right ? KEY_RIGHT : KEY_LEFT,0);player_update(0,0);
}

void transition_plain_slides()
{
    for(bool right:{false,true}) for(int start:{PLAYER_LEFT_MIN,PLAYER_LEFT_START,PLAYER_LEFT_MAX})
        for(int held:{0,int(KEY_LEFT),int(KEY_RIGHT)}) {
            transition_reset();player.x_position=start;
            const int direction=right ? KEY_RIGHT : KEY_LEFT,delta=right ? 6 : -6;
            const auto sounds=test_effects.size();
            player_update(direction|KEY_A,KEY_A);
            int x=transition_step(start,right ? 4 : -4);
            assert(player.x_position==x && player_is_sliding() && player_is_invincible_to_orb());
            transition_pose(PLAYER_32,right ? SPRITE_SLIDE_RIGHT0 : SPRITE_SLIDE_LEFT0);
            for(int tick=1;tick<=13;++tick) {
                player_update(held,0);x=transition_step(x,delta);
                assert(player.x_position==x && !player_is_deflecting());
                assert(player_is_sliding()==(tick<13) && player_is_invincible_to_orb()==(tick<13));
                if(tick<13) transition_pose(PLAYER_32,(right ? SPRITE_SLIDE_RIGHT0 : SPRITE_SLIDE_LEFT0)+(tick-1)%2);
                else transition_pose(PLAYER,transition_idle(right));
            }
            // Direction held during a slide takes effect only on the next
            // regular tick; deliberate turns must still work, including walls.
            player_update(held,0);
            x=transition_step(x,held==KEY_LEFT ? -4 : held==KEY_RIGHT ? 4 : 0);
            assert(player.x_position==x);
            transition_pose(PLAYER,held ? (held==KEY_RIGHT ? SPRITE_WALK_RIGHT0 : SPRITE_WALK_LEFT0)+1 : transition_idle(right));
            assert(test_effects.size()==sounds+1 && test_effects.back()==SFX_SLIDE);
        }
}

void transition_swings_and_grace(int deflection_frames)
{
    for(bool facing_right:{false,true}) {
        transition_face(facing_right);
        const int x=player.x_position;
        player_update(KEY_A,KEY_A);transition_pose(PLAYER_48,SPRITE_SWING0);
        for(int tick=1;tick<=24;++tick) {
            player_update(0,0);
            assert(player.x_position==x && !player_is_sliding());
            assert(player_is_deflecting()==(tick<=deflection_frames));
            assert(player_is_invincible_to_orb()==(tick<24));
            if(tick<24) transition_pose(PLAYER_48,SPRITE_SWING0+(tick-1)/3);
            else transition_pose(PLAYER,transition_idle(facing_right));
        }
        player_update(0,0);transition_pose(PLAYER,transition_idle(facing_right));

        for(bool slide_right:{false,true}) {
            transition_face(facing_right);
            int left=player.x_position;
            player_update(KEY_A,KEY_A);
            player_update(slide_right ? KEY_RIGHT : KEY_LEFT,0);
            assert(player.x_position==left && player_is_sliding() && !player_is_deflecting());
            transition_pose(PLAYER_32,slide_right ? SPRITE_SLIDE_RIGHT0 : SPRITE_SLIDE_LEFT0);
            for(int tick=2;tick<=13;++tick) {
                player_update(0,0);left=transition_step(left,slide_right ? 6 : -6);
                assert(player.x_position==left && player_is_sliding()==(tick<13));
                if(tick<13) transition_pose(PLAYER_32,(slide_right ? SPRITE_SLIDE_RIGHT0 : SPRITE_SLIDE_LEFT0)+(tick-1)%2);
                else transition_pose(PLAYER,transition_idle(facing_right));
            }
            // Retain the ORIGINAL grace-period facing quirk, not slide direction.
            player_update(0,0);transition_pose(PLAYER,transition_idle(facing_right));
        }
    }
}

void transition_combos()
{
    // moving flipkick, stationary flipkick, slidekick, seven-shot combo
    for(bool right:{false,true}) for(int kind=0;kind<4;++kind) {
        transition_reset();
        const int direction=right ? KEY_RIGHT : KEY_LEFT,opposite=right ? KEY_LEFT : KEY_RIGHT;
        const auto sounds=test_effects.size();
        player_update(direction|KEY_A,KEY_A);
        for(int tick=1;tick<=12;++tick) player_update(0,0); // release opens the existing combo window
        player_update(kind==3 ? KEY_B : KEY_A|(kind==1 ? opposite : kind==2 ? direction : 0),0);
        transition_pose(PLAYER,transition_idle(right)); // early-return handoff must not reuse slide index2
        int x=PLAYER_LEFT_START+(right ? 1 : -1)*(4+13*6);
        assert(player.x_position==x && !player_is_sliding() && player_is_deflecting()==(kind!=2));
        const int duration=kind<2 ? 29 : kind==2 ? 14 : 20;
        for(int tick=1;tick<=duration;++tick) {
            player_update(0,0);
            x=transition_step(x,(right ? 1 : -1)*(kind==0 ? 2 : kind==2 ? 6 : 0));
            assert(player.x_position==x && !player_is_sliding());
            assert(player_is_invincible_to_orb()==(tick<duration));
            assert(player_is_deflecting()==(kind<2 ? tick<=20 : kind==3 ? tick<=4 : false));
            if(tick==duration) transition_pose(PLAYER,transition_idle(right));
            else if(kind<2) transition_pose(PLAYER_48,(right ? SPRITE_FLIPKICK_RIGHT0 : SPRITE_FLIPKICK_LEFT0)+
                (tick<=10 ? 0 : (tick-7)/4));
            else if(kind==2) transition_pose(PLAYER_32,right ? SPRITE_SLIDEKICK_RIGHT : SPRITE_SLIDEKICK_LEFT);
            else transition_pose(PLAYER_48,(right ? SPRITE_SHOTCOMBO_RIGHT0 : SPRITE_SHOTCOMBO_LEFT0)+(tick-1)/6);
            if(kind==3) {
                const auto &shots=shots_get_state();
                const int count=tick<2 ? 0 : (tick-2)/3+1;
                const int offsets[]={-8,8,0,-16,0,16,0};
                for(int i=0;i<SHOT_COUNT;++i) {
                    assert(shots.alive[i]==(i<count));
                    if(i<count) assert(shots.x_position[i]==x+PLAYER_W/4+offsets[i] && shots.y_position[i]==player_top);
                }
            }
        }
        player_update(0,0);transition_pose(PLAYER,transition_idle(right));
        assert(std::count(test_effects.begin()+sounds,test_effects.end(),SFX_SLIDE)==2);
        assert(std::count(test_effects.begin()+sounds,test_effects.end(),SFX_SHOT)==(kind==3 ? 7 : 0));
    }
}

void transition_combo_chains()
{
    // Continue via actual held inputs, not private controller state. Check
    // second/third special attacks and refuse a fourth after the third kick.
    for(bool right:{false,true}) for(int finish:{2,3}) for(int kind=0;kind<4;++kind) {
        transition_reset();
        const int direction=right ? KEY_RIGHT : KEY_LEFT,opposite=right ? KEY_LEFT : KEY_RIGHT;
        const auto sounds=test_effects.size();
        player_update(direction|KEY_A,KEY_A);
        for(int tick=1;tick<=12;++tick) player_update(0,0);
        player_update(KEY_A,0);
        int x=PLAYER_LEFT_START+(right ? 1 : -1)*(4+13*6);
        for(int special=1;special<=finish;++special) {
            const int current_kind=special==finish ? kind : 0;
            const int duration=current_kind<2 ? 29 : current_kind==2 ? 14 : 20;
            for(int tick=1;tick<=duration;++tick) {
                const bool chaining=tick==duration && special<finish;
                const int next_kind=special+1==finish ? kind : 0;
                const int held=chaining ? (next_kind==3 ? KEY_B :
                    KEY_A|(next_kind==1 ? opposite : next_kind==2 ? direction : 0)) :
                    (finish==3 && tick==duration && current_kind<2 ? KEY_A : 0);
                player_update(held,0);
                x=transition_step(x,(right ? 1 : -1)*(current_kind==0 ? 2 : current_kind==2 ? 6 : 0));
                assert(player.x_position==x && !player_is_sliding());
                assert(player_is_invincible_to_orb()==(tick<duration || chaining));
                assert(player_is_deflecting()==(chaining ? next_kind!=2 :
                    tick==duration ? false : current_kind<2 ? tick<=20 : current_kind==3 ? tick<=4 : false));
                if(tick==duration && !chaining) transition_pose(PLAYER,transition_idle(right));
                else if(current_kind<2) transition_pose(PLAYER_48,(right ? SPRITE_FLIPKICK_RIGHT0 : SPRITE_FLIPKICK_LEFT0)+
                    (tick<=10 ? 0 : (tick-7)/4));
                else if(current_kind==2) transition_pose(PLAYER_32,right ? SPRITE_SLIDEKICK_RIGHT : SPRITE_SLIDEKICK_LEFT);
                else transition_pose(PLAYER_48,(right ? SPRITE_SHOTCOMBO_RIGHT0 : SPRITE_SHOTCOMBO_LEFT0)+(tick-1)/6);
            }
        }
        assert(std::count(test_effects.begin()+sounds,test_effects.end(),SFX_SLIDE)==finish+1);
        assert(std::count(test_effects.begin()+sounds,test_effects.end(),SFX_SHOT)==(kind==3 ? 7 : 0));
        const int offsets[]={-8,8,0,-16,0,16,0};
        const auto &shots=shots_get_state();
        for(int i=0;i<SHOT_COUNT;++i) {
            assert(shots.alive[i]==(kind==3 && i<7));
            if(shots.alive[i]) assert(shots.x_position[i]==x+PLAYER_W/4+offsets[i] && shots.y_position[i]==player_top);
        }
        player_update(0,0);transition_pose(PLAYER,transition_idle(right));
    }
}
}

static void check_player_transitions()
{
    const auto saved_rank=rank;
    const int saved_bombs=rem_bombs;
    const auto heap=test_heap_attempts,objects=test_obj_attempts;
    const unsigned timer=stage_timer;
    const int score=stage_score,speed=pellet_speed;
    const std::vector<u16> palette(SPRITE_PALETTE,SPRITE_PALETTE+256);
    const std::vector<u16> tiles(test_obj_vram,test_obj_vram+test_obj_capacity/2);
    initialize_irand(274);const int random=irand();initialize_irand(274);
    for(int difficulty=RANK_EASY;difficulty<=RANK_LUNATIC;++difficulty) {
        rank=static_cast<rank_t>(difficulty);
        transition_plain_slides();
        transition_swings_and_grace(difficulty==RANK_EASY ? 15 : difficulty==RANK_NORMAL ? 12 : difficulty==RANK_HARD ? 10 : 8);
        transition_combos();
        transition_combo_chains();
    }
    assert(irand()==random && stage_timer==timer && stage_score==score && pellet_speed==speed);
    assert(test_heap_attempts==heap && test_obj_attempts==objects);
    assert(std::equal(palette.begin(),palette.end(),SPRITE_PALETTE));
    assert(std::equal(tiles.begin(),tiles.end(),test_obj_vram));
    rank=saved_rank;rem_bombs=saved_bombs;
    player_initialize();orb_initialize();shots_initialize();camera.snap_center();player_render_frame();
    std::puts("Reimu transitions: both slide directions/walls/held or reversed input, all-rank swing and combo exits, full three-special chains/all finishing attacks/fourth refusal, preserved grace-period facing, exact movement/deflection/repel/shot timing, correct sheet/OAM pixels and pure redraws with unchanged RNG/palettes/tiles/allocations passed");
}
