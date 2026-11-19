#pragma once
#include <nds.h>
#include <cstddef>
#include "boss_gfx.hpp"
#include "main/boss/kikuri_intro.hpp"
#include "main/boss/kikuri_phase1.hpp"

bool kikuri_art_asset_valid(const void *data,size_t size);
constexpr int KIKURI_SOUL_PALETTE=3;
constexpr int KIKURI_SOUL_OBJ_BYTES=5*128; // three cels + two clipped-edge variants
// Approved art/entrance/full combat/defeat. Borrows BG3/BG2 and the resident
// boss OBJ cache (no new lease); souls use OAM40/41, palette3,16x16/4bpp.
// Never changes the resident Reimu/orb OBJ palette. Ownership restores the
// background/palette and releases every RAM allocation.
class KikuriView {
    u8 *data_=nullptr;
    u16 *memory_=nullptr;
    u16 saved_palette_[208]{},palette_[208]{},sprite_colors_[16]{};
    u16 saved_master_=0;
    u16 saved_soul_palette_[16]{};
    KikuriSoul souls_[2]{};
    bool souls_uploaded_=false;
    int clipped_cel_[2]{-1,-1},clipped_rows_[2]{-1,-1};
    uint16_t shroud_rows_[192]{};
    bool changed_rows_[192]{};
    int white_=-1,inversion_=-1,shown_white_=-1,shown_inversion_=-1;
    int lightball_=-1,shown_lightball_=-1;
    int shroud_camera_=-1,brightness_=0;
    bool shrouded_=false,clear_canvas_=false;
    bool active_=false,installed_=false,defeated_=false;
    unsigned signatures_=0,sprites_=0;
    struct RippleCache {int generation=-1,left=0,frame=1;};
    RippleCache ripples_[KIKURI_TEAR_COUNT]{};
    void ripple_stamp(u8 *pixels,int left,int column,int phase);
    void ripple(int index,const KikuriTear &tear);
    void colors(int white,int inversion=0,int lightball=0);
    bool publish_palette();
    void install_background();
    void upload_soul(u16 *target,int cel,int clip_rows=0);
    void present_souls(); // early VBlank, before shake/OAM commit
    void soul_cutout(const KikuriSoul &soul); // keep earlier BG2 ink behind OBJ
    void soul_actor_priority(int priority);
public:
    KikuriView()=default;
    ~KikuriView() { end(); }
    KikuriView(const KikuriView &)=delete;
    KikuriView &operator=(const KikuriView &)=delete;
    bool begin();
    // Retained BG3 phase: do not run canvas_begin_frame/prepare/present here.
    // prepare_still() switches the borrowed RAM back to its normal BG2 format.
    bool entrance(const KikuriEntrance &timeline,const KikuriEntranceFrame &frame);
    bool present_entrance(); // sparse BG3 row commits; call in early VBlank
    void prepare_still(bool flash=false);
    bool still(bool flash=false);
    void prepare_combat(const KikuriPhase1State &state);
    void draw_combat(const KikuriPhase1State &state); // after canvas_begin_frame
    bool present_combat(int death_shake=-1); // -1 uses bomb shake; misses supply0/4
    bool sprite(int group,int cel,int left,int top); // software tear(1) / ripple(2)
    bool defeat(const KikuriPhase1State &state); // blocking source timing, native white->black
    u16 normal_brightness() const { return saved_master_; }
    void end(bool hold_black=false); // victory keeps native black through results setup
};
// Approved music-clock entrance. Caller starts KAMI once; no dev controls or
// local score/resources. Timer, RNG, player and orb remain frozen throughout.
bool kikuri_entrance_play(KikuriView &view);
