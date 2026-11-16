#include "missile.hpp"
#include "main/math/trig.hpp"
#include "main/player/player.hpp"
#include "sound_effects.hpp"

bool MissilePool::add(int left,int top,double velocity_x,double velocity_y)
{
    for(auto &m:missiles_) if(!m.flag) {
        m.x=left*16;m.y=top*16;m.previous_x=-1;
        m.vx=int(velocity_x*16);m.vy=int(velocity_y*16);
        m.flag=1;m.visible=false;return true;
    }
    return false;
}

void MissilePool::reset() { for(auto &m:missiles_) { m.flag=0;m.visible=false; } }
int MissilePool::alive_count() const { int count=0;for(const auto &m:missiles_) count+=m.flag!=0;return count; }

bool MissilePool::update(unsigned gameplay_frame)
{
    for(int i=0;i<MISSILE_COUNT;++i) {
        auto &m=missiles_[i];if(!m.flag) continue;
        const bool selected=(i&1)==int(gameplay_frame&1);
        if(selected && m.previous_x!=-1) m.visible=false;
        if(m.flag==1) {
            m.x+=m.vx;m.y+=m.vy;
            const int x=m.x>>4,y=m.y>>4;
            if(x<PLAYFIELD_LEFT || x>PLAYFIELD_RIGHT-MISSILE_W || y<PLAYFIELD_TOP || y>PLAYFIELD_BOTTOM-1) {
                m.flag=2;m.visible=false;
                // Deliberately retain the last rendered OTHER coordinate;
                // source explosion anchors are not the current moving point.
                if(x<PLAYFIELD_LEFT) m.previous_x=PLAYFIELD_LEFT*16;
                else if(x>PLAYFIELD_RIGHT-MISSILE_W) m.previous_x=(PLAYFIELD_RIGHT-MISSILE_W)*16;
                if(y<PLAYFIELD_TOP) m.previous_y=PLAYFIELD_TOP*16;
                else if(y>PLAYFIELD_BOTTOM-1) m.previous_y=(PLAYFIELD_BOTTOM-MISSILE_H/2)*16;
            }
        } else if(selected && ++m.flag>3) {
            m.flag=0;m.visible=false;mdrv2_se_play(7);
        }
    }
    for(int i=0;i<MISSILE_COUNT;++i) {
        auto &m=missiles_[i];if(!m.flag || (i&1)!=int(gameplay_frame&1)) continue;
        if(m.flag==1) {
            m.previous_x=m.x;m.previous_y=m.y;
            m.draw_x=m.x>>4;m.draw_y=m.y>>4;m.cel=iatan2(m.vy,m.vx)/16;
        } else {
            m.draw_x=m.previous_x>>4;m.draw_y=m.previous_y>>4;m.cel=16+m.flag-2;
        }
        m.visible=true;
    }
    if(player_is_invincible()) return false;
    const int left=player_get_state().x_position;
    for(const auto &m:missiles_) if(m.flag) {
        const int x=m.x>>4,y=m.y>>4;
        if(x>left-8 && x<left+24 && y>player_top-8 && y<player_top+PLAYER_H) return true;
    }
    return false;
}
