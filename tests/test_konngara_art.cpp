// Actual KGA1 renderer/loader/preview against a separately decoded source page.
#include <nds.h>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <utility>
#include <vector>
#include "heap_probe.hpp"
#include "konngara_art_render.hpp"
#include "konngara_combat_render.hpp"
#include "boss_gfx.hpp"
#include "main/player/player.hpp"
#include "canvas.hpp"
#include "camera.hpp"
#include "video.hpp"
#include "music.hpp"
#include "sprites/Pellet.h"

int background_scroll_x() { return 32; }
bool player_is_hit;
bool player_is_invincible() { return true; }
const PlayerState &player_get_state() { static PlayerState player{304,368,PLAYER,0}; return player; }
namespace {
bool allow_music = true, playing;
int starts, stops, sfx_stops, sounds, ticks;
std::vector<u8> preview;
std::vector<u8> defeat_reference;
std::vector<int> defeat_sounds;
int defeat_ticks=0,defeat_checked=0;
bool defeat_testing=false;
constexpr size_t frame_bytes = 320 * 192 * 2;

std::vector<u8> read(const char *path)
{
    std::ifstream input(path, std::ios::binary); assert(input.good());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
unsigned word(const u8 *p) { return p[0] | (p[1] << 8); }
unsigned dword(const u8 *p) { return word(p) | (word(p + 2) << 16); }
u16 expected(const u8 *frame, int x, int y) { return word(frame + (y * 320 + x) * 2); }

void present(KonngaraCombatView &view)
{
    canvas_buffer_prepare(); REG_VCOUNT = 192;
    assert(view.present() && !canvas_buffer_ready());
}

unsigned object_color(int slot, int x, int y, unsigned color)
{
    const auto &o = test_objs[slot];
    const int widths[] = {8,16,32,32,64,8,32,64,8,16,16,32};
    const int heights[] = {8,16,16,32,64,16,8,32,32,8,32,64};
    int sx=x-o.x,sy=y-o.y,w=widths[o.size],h=heights[o.size];
    if(o.hidden || sx<0 || sy<0 || sx>=w || sy>=h) return color;
    unsigned index=reinterpret_cast<const u8*>(o.gfx)[((sy/8)*(w/8)+sx/8)*64+(sy%8)*8+sx%8];
    return index ? SPRITE_PALETTE[index] : color;
}

void check_frame(const u8 *reference, int left, bool full)
{
    if (full) {
        for (int y = 0; y < VIEW_H; ++y)
            for (int x = 0; x < VIEW_W; ++x)
                assert(*canvas_at(x, y) == expected(reference, x + left, y));
    } else {
        for (int y : {0, 23, 24, 32, 47, 60, 96, 191})
            for (int x : {0, 17, 64, 127, 128, 192, 255})
                assert(*canvas_at(x, y) == expected(reference, x + left, y));
    }
}

void observe()
{
    assert(playing && ticks < 681);
    int index;
    bool full;
    if (ticks < 360) {
        index = ticks / 30;
        full = ticks % 30 == 0;
    } else {
        const int frame = ticks - 360;
        const int keyframes[] = {50, 60, 100, 120, 140, 160, 170};
        index = frame < 10 ? 12 : 13;
        full = frame == 0 || frame == 10;
        for (int cel = 0; cel < 7; ++cel) if (frame >= keyframes[cel]) {
            index = 14 + cel;
            full |= frame == keyframes[cel];
        }
    }
    check_frame(preview.data() + index * frame_bytes, 32, full);
    assert(sounds == int(ticks >= 410));
    assert(REG_WININ == 0x1D1D && test_bgs[0].priority == 1 && camera.left == 64);
    assert(test_objs[OAM_SLOT_HUD_LIVES].priority == 1 && test_objs[OAM_SLOT_ORB_ARROW].priority == 1);
    ++ticks;
}

void check_defeat_frame(int tick)
{
    const unsigned count=dword(defeat_reference.data());
    if(unsigned(defeat_checked)>=count) return;
    const u8 *record=defeat_reference.data()+4+defeat_checked*(4+frame_bytes);
    if(dword(record)!=unsigned(tick)) return;
    const auto *indices=reinterpret_cast<const u8*>(BG_BMP_RAM(4));
    for(int y=0;y<192;++y) for(int x=0;x<256;++x)
        assert((BG_PALETTE[indices[y*512+x]]|0x8000)==expected(record+4,x+32,y));
    for(int i=0;i<128;++i) assert(test_objs[i].hidden);
    ++defeat_checked;
}

void observe_defeat()
{
    // VBlank callbacks run BEFORE the next coherent graphics commit.
    if(defeat_ticks) {
        check_defeat_frame(defeat_ticks-1);
        assert(!test_bgs[0].visible && !test_bgs[1].visible && !test_bgs[2].visible);
        assert(test_bgs[3].visible);
    }
    assert(++defeat_ticks<=421);
}
}

bool music_play_track(NativeTrackId track)
{
    assert(track == NATIVE_ALICE && REG_WININ == 0x1D1D && *canvas_at(0, 0) == 0x8000);
    ++starts; playing = allow_music;
    return playing;
}
void music_stop() { ++stops; playing = false; }
void sfx_stop() { ++sfx_stops; }
void mdrv2_se_play(int id) {
    if(defeat_testing) { assert(!playing && id==7);defeat_sounds.push_back(defeat_ticks-1); }
    else { assert(playing && id == 8 && ticks == 410); ++sounds; }
}

int main()
{
    const auto asset = read("nitro:/konngara_art.bin");
    const auto commands = read("commands.bin");
    preview = read("preview.bin");
    assert(preview.size() == 21 * frame_bytes);
    assert(konngara_art_asset_valid(asset.data(), asset.size()));
    assert(!konngara_art_asset_valid(nullptr, asset.size()));
    for (size_t size : {size_t(0), size_t(16), size_t(128215), asset.size() - 1})
        assert(!konngara_art_asset_valid(asset.data(), size));
    auto bad = asset;
    for (size_t offset : {size_t(0), size_t(4), size_t(6), size_t(8), size_t(10),
                          size_t(12), size_t(16), size_t(64), size_t(68)}) {
        bad[offset] = 255;
        assert(!konngara_art_asset_valid(bad.data(), bad.size()));
        bad[offset] = asset[offset];
    }
    const unsigned patch = dword(asset.data() + 64);
    bad[patch] = bad[patch + 1] = 255;
    assert(!konngara_art_asset_valid(bad.data(), bad.size()));
    bad = asset; bad[patch + 2] = 0;
    assert(!konngara_art_asset_valid(bad.data(), bad.size()));
    bad = asset; bad[patch + 3] = 1;
    assert(!konngara_art_asset_valid(bad.data(), bad.size()));

    video_init(); canvas_init(); camera.left = 64;
    for (int i = 0; i < 256; ++i) { BG_PALETTE[i] = i; SPRITE_PALETTE[i] = i ^ 0x7fff; }
    const std::vector<u16> resident(std::begin(test_vram.pixels), std::end(test_vram.pixels));
    const std::vector<u16> bgpal(std::begin(BG_PALETTE), std::end(BG_PALETTE));
    const std::vector<u16> objpal(std::begin(SPRITE_PALETTE), std::end(SPRITE_PALETTE));
    const auto obj_attempts = test_obj_attempts;
    {
        KonngaraArtwork art;
        assert(!art.draw() && !art.reset() && !art.slash_put(0));
        assert(!art.set_direction(KonngaraDirection::Right));
        assert(!art.set_expression(KonngaraExpression::Closed));
        for (int fail = 0; fail < 3; ++fail) {
            test_heap_fail_after = fail;
            assert(!art.load() && !art.draw());
            assert(test_heap_count == 0 && test_heap_bytes == 0);
        }
        test_heap_fail_after = -1;
        assert(art.load());
        assert(test_heap_count == 3 && test_heap_bytes == asset.size() + 128000 + 131072);
        auto allocations = test_heap_attempts;
        assert(art.load() && test_heap_attempts == allocations);
        assert(!art.set_hit_flash(true));
        test_heap_fail_after = 0;
        assert(!art.prepare_hit_flash() && test_heap_count == 3);
        test_heap_fail_after = -1;
        assert(art.prepare_hit_flash() && test_heap_count == 4);
        allocations = test_heap_attempts;
        assert(art.prepare_hit_flash() && test_heap_attempts == allocations);
        assert(!art.set_direction(KonngaraDirection(-1)) && !art.set_direction(KonngaraDirection(3)));
        assert(!art.set_expression(KonngaraExpression(-1)) && !art.set_expression(KonngaraExpression(4)));
        assert(!art.slash_put(-1) && !art.slash_put(7) && !art.draw(-1) && !art.draw(65));
        const unsigned count = dword(commands.data());
        assert(commands.size() == 4 + count * (8 + frame_bytes));
        for (unsigned i = 0; i < count; ++i) {
            const u8 *command = commands.data() + 4 + i * (8 + frame_bytes);
            const int op = dword(command), arg = dword(command + 4);
            bool okay = false;
            switch (op) {
            case 0: okay = art.reset(); break;
            case 1: okay = art.set_direction(KonngaraDirection(arg)); break;
            case 2: okay = art.set_expression(KonngaraExpression(arg)); break;
            case 3: okay = art.slash_put(arg); break;
            case 4: okay = art.set_hit_flash(arg); break;
            }
            assert(okay);
            for (int left : {0, 1, 32, 63, 64}) {
                assert(art.draw(left));
                check_frame(command + 8, left, true);
            }
            assert(test_heap_attempts == allocations && test_obj_attempts == obj_attempts);
        }
        art.release(); art.release();
        assert(!art.draw() && test_heap_count == 0 && test_heap_bytes == 0);
        assert(art.load()); // explicit release/reload and destructor ownership
    }
    assert(test_heap_count == 0 && test_heap_bytes == 0);
    for (int frame = -1; frame <= 201; ++frame) {
        const int frames[] = {50, 60, 100, 120, 140, 160, 170};
        int expected_cel = -1;
        for (int i = 0; i < 7; ++i) if (frame == frames[i]) expected_cel = i;
        assert(konngara_slash_cel_on_frame(frame) == expected_cel);
    }
    test_vblank_hook = observe;
    for (int fail = 0; fail < 3; ++fail) {
        test_heap_fail_after = fail;
        assert(!konngara_art_preview() && starts == 0);
        assert(test_heap_count == 0 && test_heap_bytes == 0);
    }
    test_heap_fail_after = -1;
    for (int repeat = 0; repeat < 20; ++repeat) {
        ticks = sounds = 0;
        assert(konngara_art_preview() && ticks == 681 && sounds == 1);
        assert(starts == repeat + 1 && stops == repeat + 1 && sfx_stops == repeat + 1);
        assert(test_heap_count == 0 && test_heap_bytes == 0 && test_obj_attempts == obj_attempts);
        assert(REG_WININ == 0x1919 && test_bgs[0].priority == 0 && camera.left == 64);
        assert(test_objs[OAM_SLOT_HUD_LIVES].priority == 0 && test_objs[OAM_SLOT_ORB_ARROW].priority == 0);
        assert(std::all_of(canvas_at(0, 0), canvas_at(0, VIEW_H), [](u16 p) { return p == 0; }));
    }
    {
        CanvasForegroundScope outer;
        ticks = sounds = 0;
        assert(konngara_art_preview() && ticks == 681 && REG_WININ == 0x1D1D);
        assert(test_heap_count == 0 && test_heap_bytes == 0);
    }
    test_vblank_hook = nullptr;
    allow_music = false;
    assert(!konngara_art_preview() && test_heap_count == 0 && test_heap_bytes == 0);
    const int before = starts;
    assert(std::rename("nitro:/konngara_art.bin", "nitro:/konngara_art.saved") == 0);
    assert(!konngara_art_preview() && starts == before);
    auto write = [](const std::vector<u8> &bytes) {
        std::ofstream file("nitro:/konngara_art.bin", std::ios::binary);
        file.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    };
    for (const auto &bytes : {std::vector<u8>(asset.begin(), asset.begin() + 15),
                             std::vector<u8>(asset.begin(), asset.end() - 1), bad}) {
        write(bytes);
        assert(!konngara_art_preview() && starts == before);
        assert(test_heap_count == 0 && test_heap_bytes == 0);
    }
    assert(std::rename("nitro:/konngara_art.saved", "nitro:/konngara_art.bin") == 0);
    assert(std::equal(bgpal.begin(), bgpal.end(), BG_PALETTE));
    assert(std::equal(objpal.begin(), objpal.end(), SPRITE_PALETTE));
    for (size_t i = 0; i < resident.size(); ++i)
        if (i < 0x40000 / 2 || i >= 0x40000 / 2 + 256 * 192) assert(resident[i] == test_vram.pixels[i]);
    assert(REG_WININ == 0x1919 && test_bgs[0].priority == 0 && test_obj_attempts == obj_attempts);
    assert(test_vram.before == 0xDEADBEEF && test_vram.after == 0xDEADBEEF);
    {
        const auto gpu_asset = read("nitro:/konngara_combat.bin");
        const auto slash_asset = read("nitro:/konngara_slash.bin");
        const auto gpu_commands = read("combat.bin");
        constexpr size_t directory = 48 + 208 * 64, backdrop = directory + 16 * 8;
        constexpr size_t poses = backdrop + 61440, texture = poses + 144 * 64;
        assert(konngara_combat_asset_valid(gpu_asset.data(), gpu_asset.size()));
        assert(!konngara_combat_asset_valid(nullptr, gpu_asset.size()));
        for (size_t size : {size_t(0), size_t(16), gpu_asset.size() - 1, gpu_asset.size() + 1})
            assert(!konngara_combat_asset_valid(gpu_asset.data(), size));
        for (size_t offset : {size_t(0), size_t(4), size_t(6), size_t(8), size_t(10), size_t(12),
                              size_t(17), size_t(48), directory, directory + 2, directory + 4,
                              backdrop, poses + 1, texture, texture + 320}) {
            auto invalid = gpu_asset; invalid[offset] = 255;
            assert(!konngara_combat_asset_valid(invalid.data(), invalid.size()));
        }
        for (const auto &change : {std::pair<size_t, u8>{backdrop, 0}, {texture, 111}, {texture + 320, 112}}) {
            auto invalid = gpu_asset; invalid[change.first] = change.second;
            assert(!konngara_combat_asset_valid(invalid.data(), invalid.size()));
        }
        KonngaraCombatView view;
        KonngaraCombatState state{};
        state.direction = KonngaraDirection::Center;
        state.expression = KonngaraExpression::Neutral;
        assert(!view.frame(state));
        camera.snap_center(); canvas_clear();
        for (int fail = 0; fail < 3; ++fail) {
            test_heap_fail_after = fail;
            assert(!view.begin() && test_heap_count == 0 && test_obj_attempts == obj_attempts);
        }
        test_heap_fail_after = -1; test_obj_fail_after = 0;
        assert(!view.begin() && test_heap_count == 0 && test_obj_count == 0);
        test_obj_fail_after = -1;
        assert(std::equal(bgpal.begin(), bgpal.end(), BG_PALETTE));
        assert(std::equal(objpal.begin(), objpal.end(), SPRITE_PALETTE));
        assert(std::rename("nitro:/konngara_combat.bin", "nitro:/konngara_combat.saved") == 0);
        assert(!view.begin() && test_heap_count == 0);
        for (const auto &bytes : {std::vector<u8>(gpu_asset.begin(), gpu_asset.end() - 1),
                                 std::vector<u8>(gpu_asset.size(), 0)}) {
            { std::ofstream file("nitro:/konngara_combat.bin", std::ios::binary);
              file.write(reinterpret_cast<const char *>(bytes.data()), bytes.size()); }
            assert(!view.begin() && test_heap_count == 0);
        }
        assert(std::rename("nitro:/konngara_combat.saved", "nitro:/konngara_combat.bin") == 0);
        assert(std::rename("nitro:/konngara_slash.bin", "nitro:/konngara_slash.saved") == 0);
        assert(!view.begin() && test_heap_count == 0);
        { std::ofstream file("nitro:/konngara_slash.bin",std::ios::binary);file.write("KGS1",4); }
        assert(!view.begin() && test_heap_count == 0);
        assert(std::rename("nitro:/konngara_slash.saved", "nitro:/konngara_slash.bin") == 0);
        assert(view.begin() && view.begin());
        assert(test_heap_count == 3 && test_heap_bytes == gpu_asset.size() + slash_asset.size() + 262144);
        assert(test_obj_count == 2 && test_obj_used == 8192 && boss_gfx() == test_objs[BOSS_OAM_SLOT].gfx);
        // Preparing, unflushed presentation and late VBlank cannot touch live
        // VRAM or palettes. Only the early, flushed commit may publish them.
        const std::vector<u16> untouched(test_vram.pixels, test_vram.pixels + 384*1024/2);
        const std::vector<u16> untouched_obj(test_obj_vram, test_obj_vram + 16384);
        REG_VCOUNT=100;
        assert(view.frame(state) && !view.present());
        canvas_buffer_prepare(); REG_VCOUNT=200; assert(!view.present());
        assert(std::equal(untouched.begin(),untouched.end(),test_vram.pixels));
        assert(std::equal(untouched_obj.begin(),untouched_obj.end(),test_obj_vram));
        assert(std::equal(bgpal.begin(),bgpal.end(),BG_PALETTE));
        assert(std::equal(objpal.begin(),objpal.end(),SPRITE_PALETTE));
        present(view);
        assert(test_bgs[3].type == BgType_Bmp8 && test_bgs[3].size == BgSize_B8_512x256);
        assert(test_bgs[3].priority == 3 && test_bgs[2].priority == 0);
        const auto attempts = test_heap_attempts;
        const auto obj_allocations = test_obj_attempts;
        const std::vector<u16> cached_bg(BG_BMP_RAM(4), BG_BMP_RAM(4) + 65536);
        const unsigned count = dword(gpu_commands.data());
        assert(gpu_commands.size() == 4 + count * (8 + frame_bytes));
        for (unsigned i = 0; i < count; ++i) {
            const u8 *command = gpu_commands.data() + 4 + i * (8 + frame_bytes);
            const unsigned op = dword(command), arg = dword(command + 4);
            const auto before = view.work();
            if (!op) {
                view.end(); assert(test_objs[BOSS_OAM_SLOT].hidden);
                state = {}; state.direction = KonngaraDirection::Center;
                assert(view.begin());
            } else if (op == 1) state.direction = KonngaraDirection(arg);
            else if (op == 2) state.expression = KonngaraExpression(arg);
            else if (op == 4) state.health.flashing = arg;
            else if (op == 5) state.siddham_white = arg;
            assert(view.frame(state));
            present(view);
            if (op == 4 || op == 5) assert(view.work().head_uploads == before.head_uploads);
            const auto work = view.work();
            assert(work.background_bytes == 61440);
            for (int left : {0, 1, 32, 63, 64}) {
                camera.left = left;
                assert(view.frame(state) && view.begin());
                present(view);
                assert(view.work().head_uploads == work.head_uploads && view.work().palette_writes == work.palette_writes);
                assert(view.work().background_bytes == 61440 && test_bgs[3].x == left && test_bgs[3].y == 0);
                const auto &obj = test_objs[BOSS_OAM_SLOT];
                assert(obj.format == SpriteColorFormat_256Color && obj.priority == OAM_PRIORITY_GAMEPLAY && !obj.hidden);
                assert(obj.x == 140 - left && obj.y == 32 && obj.size == SpriteSize_64x64 && obj.palette == 0);
                const auto *texture = reinterpret_cast<const u8 *>(obj.gfx);
                const auto *background = reinterpret_cast<const u8 *>(BG_BMP_RAM(4));
                for (int y = 0; y < 192; ++y) for (int x = 0; x < 256; ++x) {
                    unsigned color = BG_PALETTE[background[y * 512 + left + x]];
                    const int sx = x - obj.x, sy = y - obj.y;
                    if (sx >= 0 && sx < 64 && sy >= 0 && sy < 64) {
                        const unsigned index = texture[((sy / 8) * 8 + sx / 8) * 64 + (sy % 8) * 8 + sx % 8];
                        if (index) color = SPRITE_PALETTE[index];
                    }
                    assert((color | 0x8000) == expected(command + 8, x + left, y));
                }
                assert(REG_WININ == 0x1919 && test_bgs[0].priority == 0);
                assert(std::equal(bgpal.begin() + 208, bgpal.end(), BG_PALETTE + 208));
                assert(std::equal(objpal.begin(), objpal.begin() + 112, SPRITE_PALETTE));
                assert(std::equal(cached_bg.begin(), cached_bg.end(), BG_BMP_RAM(4)));
                assert(std::all_of(canvas_at(0, 0), canvas_at(0, VIEW_H), [](u16 p) { return p == 0; }));
            }
        }
        state.direction = KonngaraDirection(3); assert(!view.frame(state));
        state.direction = KonngaraDirection::Center;
        state.expression = KonngaraExpression(4); assert(!view.frame(state));
        state.expression = KonngaraExpression::Neutral;
        state.siddham_white = 16; assert(!view.frame(state));
        state.siddham_white = 0;
        canvas_playfield_clip(true);
        state.visual_count = 2;
        state.visuals[0] = {300, 200, 9}; state.visuals[1] = {320, 200, 7};
        assert(view.frame(state));
        assert(std::any_of(canvas_at(0, 24), canvas_at(0, VIEW_H), [](u16 p) { return p != 0; }));
        state.visual_count = 0; state.ray_active = true;
        const auto work = view.work();
        for (int left : {0, 1, 32, 63, 64}) for (const auto &end : {std::pair<int,int>{0,264}, {0,64}, {639,64}, {639,264}}) {
            canvas_begin_frame(); camera.left = left;
            state.ray_left = end.first; state.ray_top = end.second;
            assert(view.frame(state));
            assert(*canvas_at(camera.x(410), Camera::y(70)) == (word(gpu_asset.data() + 28) | 0x8000));
            assert(std::all_of(canvas_at(0, 0), canvas_at(0, 24), [](u16 p) { return p == 0; }));
            assert(view.work().background_bytes == work.background_bytes && view.work().head_uploads == work.head_uploads);
        }
        state.ray_active = false; canvas_begin_frame();
        assert(view.frame(state) && std::all_of(canvas_at(0,0), canvas_at(0,192), [](u16 p) { return p == 0; }));
        for (int left : {0,64}) {
            camera.left = left;
            auto &laser = state.lasers[0]; laser = {};
            assert(shootout_laser_spawn(laser,410,70,0,400));
            bool seen = false;
            for (int age = 0; age < 120; ++age) {
                shootout_laser_update(laser); canvas_begin_frame();
                std::vector<u16> pixels(256*192);
                struct Context { std::vector<u16> *pixels; int left; } context{&pixels,left};
                shootout_laser_visit(laser,[](int x,int y,uint16_t dots,void *p) {
                    auto &c=*static_cast<Context*>(p);
                    for (int bit=0;bit<16;++bit) if(dots&(0x8000>>bit)) {
                        const int sx=((x&~7)+bit)/2-c.left,sy=y/2-8;
                        if(sx>=0 && sx<256 && sy>=24 && sy<192) (*c.pixels)[sy*256+sx]=0xffff;
                    }
                },&context);
                assert(view.frame(state));
                assert(std::equal(pixels.begin(),pixels.end(),canvas_at(0,0)));
                seen |= std::any_of(pixels.begin(),pixels.end(),[](u16 p){return p!=0;});
                // Pellets compose AFTER lasers. A premature flush is invalid
                // once a pellet writes, and neither may touch live BG2 yet.
                const std::vector<u16> live(BG_BMP_RAM(CANVAS_BMP_BLOCK),BG_BMP_RAM(CANVAS_BMP_BLOCK)+256*192);
                canvas_buffer_prepare();
                const u16 palette[]={0,0x7fff};
                int px=20+age%60,py=120;
                canvas_put_indexed(px,py,4,4,Pellet_cels[0],palette);
                for(int y=0;y<4;++y) for(int x=0;x<4;++x)
                    if(Pellet_cels[0][y*4+x]) pixels[(py+y)*256+px+x]=0xffff;
                assert(!canvas_buffer_ready() && !view.present());
                assert(std::equal(live.begin(),live.end(),BG_BMP_RAM(CANVAS_BMP_BLOCK)));
                present(view);
                assert(std::equal(pixels.begin(),pixels.end(),BG_BMP_RAM(CANVAS_BMP_BLOCK)));
                assert(view.work().background_bytes==work.background_bytes && view.work().head_uploads==work.head_uploads);
            }
            assert(seen && !laser.alive);
        }
        canvas_begin_frame();assert(view.frame(state));present(view);
        assert(std::all_of(BG_BMP_RAM(CANVAS_BMP_BLOCK),BG_BMP_RAM(CANVAS_BMP_BLOCK)+256*192,[](u16 p){return !p;}));
        // Only explicit end/begin resets reallocate; all camera/pose/flash
        // frames are allocation-free and reuse the same ROM-lifetime boss OBJ.
        assert(test_heap_attempts == attempts + 6 && test_obj_attempts == obj_allocations + 2);
        view.end(); view.end();
        assert(test_bgs[3].type == BgType_Bmp8 && test_bgs[3].size == BgSize_B8_512x256);
    }
    assert(test_heap_count == 0 && test_heap_bytes == 0);
    assert(std::equal(bgpal.begin(), bgpal.end(), BG_PALETTE));
    assert(std::equal(objpal.begin(), objpal.end(), SPRITE_PALETTE));
    {
        const auto asset = read("nitro:/konngara_slash.bin"), commands = read("slash.bin");
        assert(konngara_slash_asset_valid(asset.data(),asset.size()));
        assert(!konngara_slash_asset_valid(nullptr,asset.size()));
        for (size_t size : {size_t(0),size_t(16),asset.size()-1,asset.size()+1})
            assert(!konngara_slash_asset_valid(asset.data(),size));
        const size_t frame = dword(asset.data()+32+12);
        for (size_t offset : {size_t(0),size_t(4),size_t(8),size_t(12),size_t(16),size_t(21),size_t(24),size_t(28),frame+1,frame+4,frame+8,frame+10,frame+12}) {
            auto bad = asset; bad[offset] = 255;
            assert(!konngara_slash_asset_valid(bad.data(),bad.size()));
        }
        KonngaraCombatView view; KonngaraCombatState state{}; state.direction=KonngaraDirection::Center;
        const auto allocations = test_obj_attempts;
        const unsigned count=dword(commands.data());
        unsigned last_work=0;
        for (unsigned i=0;i<count;++i) {
            const u8 *command=commands.data()+4+i*(8+frame_bytes);
            unsigned op=dword(command),arg=dword(command+4);
            if (!op) { view.end();state={};state.direction=KonngaraDirection::Center;assert(view.begin());last_work=0; }
            else if (op==1) state.direction=KonngaraDirection(arg);
            else if (op==2) { state.expression=KonngaraExpression(arg);if (!arg) state.slash_cel=-1; }
            else if (op==3) { state.slash_cel=arg;if (!arg) ++state.slash_cycles; }
            else if (op==4) state.health.flashing=arg;
            const auto before=view.work();
            assert(view.frame(state));
            present(view);
            if (op==4) assert(view.work().background_bytes==before.background_bytes && view.work().head_uploads==before.head_uploads && view.work().sprite_bytes==before.sprite_bytes);
            const auto work=view.work();
            if (op==3) assert(work.slash_uploads==last_work+1);
            last_work=work.slash_uploads;
            for(int left:{0,1,32,63,64}) {
                camera.left=left;assert(view.frame(state));
                present(view);
                assert(view.work().background_bytes==work.background_bytes && view.work().head_uploads==work.head_uploads && view.work().palette_writes==work.palette_writes && view.work().sprite_bytes==work.sprite_bytes);
                const auto &head=test_objs[BOSS_OAM_SLOT];
                assert(head.hidden==(state.slash_cel>=0));
                const auto *bg=reinterpret_cast<const u8*>(BG_BMP_RAM(4));
                for(int y=0;y<192;++y) for(int x=0;x<256;++x) {
                    unsigned color=BG_PALETTE[bg[y*512+x+left]];
                    color=object_color(BOSS_OAM_SLOT,x,y,color);
                    for(int slot=75;slot<107;++slot) color=object_color(slot,x,y,color);
                    if((color|0x8000)!=expected(command+8,x+left,y)) {
                        std::fprintf(stderr,"Hybrid mismatch command%u op%u arg%u cycles%d cel%d crop%d at%d,%d actual%04x expected%04x\n",
                            i,op,arg,state.slash_cycles,state.slash_cel,left,x+left,y,color|0x8000,expected(command+8,x+left,y));
                        assert(false);
                    }
                }
                assert(std::equal(bgpal.begin()+208,bgpal.end(),BG_PALETTE+208));
                assert(std::equal(objpal.begin(),objpal.begin()+112,SPRITE_PALETTE));
                assert(REG_WININ==0x1919 && test_objs[OAM_SLOT_HUD_HP].priority==0);
            }
        }
        assert(view.work().slash_uploads==14);
        assert(view.work().background_bytes==61440+1342);
        view.end();assert(test_obj_attempts==allocations+2 && test_heap_count==0);
        for(int slot=75;slot<107;++slot) assert(test_objs[slot].hidden);
        assert(test_obj_count==1 && test_obj_used==4096 && !g_canvas_target);
        assert(std::equal(bgpal.begin(),bgpal.end(),BG_PALETTE));
        assert(std::equal(objpal.begin(),objpal.end(),SPRITE_PALETTE));
    }
    {
        const auto data=read("nitro:/konngara_defeat.bin");
        assert(konngara_defeat_asset_valid(data.data(),data.size()));
        assert(!konngara_defeat_asset_valid(nullptr,data.size()));
        for(size_t size:{size_t(0),size_t(16),data.size()-1,data.size()+1})
            assert(!konngara_defeat_asset_valid(data.data(),size));
        const size_t signatures=dword(data.data()+116),pixels=dword(data.data()+120);
        for(size_t offset:{size_t(0),size_t(4),size_t(6),size_t(8),size_t(10),size_t(12),size_t(16),size_t(112),size_t(114),size_t(116),size_t(120),signatures,pixels}) {
            auto bad=data;bad[offset]=255;assert(!konngara_defeat_asset_valid(bad.data(),bad.size()));
        }
        KonngaraCombatView view;assert(view.begin());
        const auto bytes=test_heap_bytes,blocks=test_heap_count;
        const std::vector<u16> live(BG_BMP_RAM(4),BG_BMP_RAM(4)+65536);
        const int before_stop=stops;
        test_heap_fail_after=0;assert(!view.defeat());test_heap_fail_after=-1;
        assert(test_heap_bytes==bytes && test_heap_count==blocks && stops==before_stop+1);
        assert(std::rename("nitro:/konngara_defeat.bin","nitro:/konngara_defeat.saved")==0);
        assert(!view.defeat());
        { std::ofstream f("nitro:/konngara_defeat.bin",std::ios::binary);f.write("KGD1",4); }
        assert(!view.defeat());
        assert(std::rename("nitro:/konngara_defeat.saved","nitro:/konngara_defeat.bin")==0);
        assert(std::equal(live.begin(),live.end(),BG_BMP_RAM(4)));
        assert(std::equal(bgpal.begin(),bgpal.end(),BG_PALETTE));
        defeat_reference=read("defeat.bin");defeat_testing=true;test_vblank_hook=observe_defeat;
        assert(view.defeat());test_vblank_hook=nullptr;defeat_testing=false;
        check_defeat_frame(420);
        assert(defeat_ticks==421 && unsigned(defeat_checked)==dword(defeat_reference.data()));
        std::vector<int> expected_sounds;
        for(int f=4;f<=64;f+=4) expected_sounds.push_back(90+f-1);
        for(int f=0;f<150;f+=8) expected_sounds.push_back(155+f);
        for(int f=152;f<220;f+=8) expected_sounds.push_back(155+f);
        assert(defeat_sounds==expected_sounds && defeat_sounds.size()==44 && stops==before_stop+4);
        assert(test_heap_count==1 && test_heap_bytes==262144 && test_obj_count==1 && test_obj_used==4096);
        KonngaraCombatState state{};assert(!view.frame(state) && !view.present() && !view.defeat());
        view.end();view.end();
        assert(test_heap_count==0 && test_heap_bytes==0 && !g_canvas_target);
        assert(std::equal(bgpal.begin(),bgpal.end(),BG_PALETTE));
        assert(std::equal(objpal.begin(),objpal.end(),SPRITE_PALETTE));
        for(int i=0;i<4;++i) assert(test_bgs[i].visible);
        // If HP reached zero in an earlier phase, hit grace has expired before
        // phase7: do not invent a new white hit flash on the kuji artwork.
        assert(view.begin());defeat_reference=read("defeat_normal.bin");
        defeat_ticks=defeat_checked=0;defeat_sounds.clear();defeat_testing=true;
        test_vblank_hook=observe_defeat;assert(view.defeat(false));
        test_vblank_hook=nullptr;defeat_testing=false;
        assert(defeat_ticks==421 && defeat_checked==1 && defeat_sounds==expected_sounds);
        view.end();assert(test_heap_count==0 && test_heap_bytes==0 && test_obj_count==1);
    }
    std::puts("Konngara defeat: independent source kuji/flash/fade/scroll pixels,421-frame timeline,44 exact SFX7 events, VBlank commits, missing/malformed/allocation rollback and full cleanup passed");
    std::printf("Konngara artwork: all BOS directions/expressions and persistent GRZ patches match source pixels at five camera offsets; asset %zu bytes, peak owned heap %zu bytes\n",
        asset.size(), asset.size() + 128000 + 131072);
    std::puts("Konngara artwork: source slash keyframes/SFX8, 20 preview replays, three allocation failures, audio/file failures, HUD/camera restoration, zero OBJ requests and zero retained heap passed");
    std::puts("Konngara combat GPU: exact chronological GRP/BOS heads + palettes at five camera crops; zero camera/flash bitmap uploads; shared head +4KiB lease; palette/background restoration and allocation/file failure rollback passed");
    std::puts("Konngara hybrid GPU: exact first/repeat closed/aim sprites and post-slash tracking; one1342-byte BG patch, seven OBJ keys per slash, zero camera/flash uploads, stable text/actor palettes and VBlank-only presentation passed");
}
