#include "../src/tile.h"
#include "../src/slope.h"
#include <stdio.h>
#include <math.h>

static int fails = 0, checks = 0;
#define CHECK(c) do{++checks; if(!(c)){++fails;printf("FAIL %d %s\n",__LINE__,#c);}}while(0)
#define NEAR(a,b) (fabsf((a)-(b)) < 1e-3f)
#define PIF 3.14159265358979323846f

enum { W = 16, H = 16 };
static unsigned char solid[W*H];
static float ang[W*H];
static signed char sdir[W*H];

static void reset_maps(void){ for (int i=0;i<W*H;++i){ solid[i]=0; ang[i]=-1.0f; sdir[i]=1; } }
static void set_slope(int tx,int ty,float a,int d){ ang[ty*W+tx]=a; sdir[ty*W+tx]=(signed char)d; }
static void set_solid(int tx,int ty){ solid[ty*W+tx]=1; }

int main(void){
    /* ---- 1. original whole-tile behavior is unchanged ---- */
    reset_maps();
    set_solid(1,1);
    set_slope(2,2,30,1); set_slope(3,3,120,-1); /* slope data must not leak into the old API */
    CHECK(tile_solid(solid,W,H,1,1)==1);
    CHECK(tile_solid(solid,W,H,2,2)==0);        /* slope tile is not solid to the old API */
    CHECK(tile_solid(solid,W,H,5,5)==0);
    {
        int tx=-1,ty=-1;
        CHECK(aabb_vs_tiles(solid,W,H,4,4,10,10,&tx,&ty)==1);
        CHECK(tx==1&&ty==1);
        CHECK(aabb_vs_tiles(solid,W,H,24,24,6,6,&tx,&ty)==0); /* (3,3) is a slope, not solid */
        CHECK(aabb_vs_tiles(solid,W,H,0,0,0,5,&tx,&ty)==-1);
    }

    /* ---- 2. contact differs at different positions inside one tile ---- */
    reset_maps(); set_slope(8,10,30,1);
    {
        float y2 = slope_surface_y(solid,ang,sdir,W,H,8,10, 8*8+2);
        float y6 = slope_surface_y(solid,ang,sdir,W,H,8,10, 8*8+6);
        CHECK(NEAR(y2, 88.0f - 2.0f*tanf(30.0f*PIF/180.0f)));
        CHECK(NEAR(y6, 88.0f - 6.0f*tanf(30.0f*PIF/180.0f)));
        CHECK(!NEAR(y2,y6));
    }
    {
        /* same drop, same tile, different local x -> different depth and landing y */
        slope_contact_t c1, c2;
        float x,y,vx,vy,px,py;
        x=64; y=76; vx=0; vy=100; px=x; py=y; y+=vy*0.07f; /* foot x=66 (lx=2) */
        CHECK(slope_move(solid,ang,sdir,W,H,px,py,&x,&y,4,4,&vx,&vy,&c1)==1);
        x=68; y=76; vx=0; vy=100; px=x; py=y; y+=vy*0.07f; /* foot x=70 (lx=6) */
        CHECK(slope_move(solid,ang,sdir,W,H,px,py,&x,&y,4,4,&vx,&vy,&c2)==1);
        CHECK(!NEAR(c1.depth,c2.depth));
        CHECK(c2.depth>c1.depth); /* deeper where the surface sits higher */
    }

    /* ---- 3. zero angle: flat push, horizontal speed fully preserved ---- */
    reset_maps(); set_slope(8,10,0,1);
    {
        /* slope lines pass through the tile's bottom corner, so a 0 deg
         * surface is the tile's bottom edge: y = 88 here */
        float x=66,y=84,vx=40,vy=5,px=x,py=y; /* foot (68,88) on surface y=88 */
        slope_contact_t ct;
        x+=vx*0.01f; y+=vy*0.01f;
        CHECK(slope_move(solid,ang,sdir,W,H,px,py,&x,&y,4,4,&vx,&vy,&ct)==1);
        CHECK(ct.is_slope==1);
        CHECK(NEAR(ct.nx,0)&&NEAR(ct.ny,-1));   /* push straight up */
        CHECK(NEAR(ct.slide_vx,40)&&NEAR(ct.slide_vy,0)); /* no downhill drift */
        CHECK(NEAR(y,84));
    }

    /* ---- 4. 45 deg slope: vertical fall becomes a down-slope slide ---- */
    reset_maps(); set_slope(8,10,45,1);
    {
        float x=66,y=80,vx=0,vy=50,px=x,py=y; /* foot (68,84) on surface y=84 */
        slope_contact_t ct;
        y+=vy*0.01f;
        CHECK(slope_move(solid,ang,sdir,W,H,px,py,&x,&y,4,4,&vx,&vy,&ct)==1);
        CHECK(NEAR(ct.nx,-sqrtf(0.5f))&&NEAR(ct.ny,-sqrtf(0.5f)));
        CHECK(ct.slide_vx<0&&ct.slide_vy>0);    /* slides down-slope (left+down) */
        CHECK(NEAR(ct.slide_vx,-25)&&NEAR(ct.slide_vy,25));
    }

    /* ---- 5. overhang (> 90 deg): push gains a downward component ---- */
    reset_maps(); set_slope(8,6,120,1);
    {
        float x=58,y=66,vx=30,vy=-80,px=x,py=y; /* foot (60,70) under the overhang */
        slope_contact_t ct;
        x+=vx*0.2f; y+=vy*0.2f;                 /* one big step into the ceiling */
        CHECK(slope_move(solid,ang,sdir,W,H,px,py,&x,&y,4,4,&vx,&vy,&ct)==1);
        CHECK(ct.ny>0);                          /* downward push: distinct from any ramp */
        CHECK(ct.nx<0);
        CHECK(ct.slide_vx<0&&ct.slide_vy<0);     /* deflected along the ceiling */
    }

    /* ---- 6. 90 deg wall: horizontal motion stopped, zero vertical push ---- */
    reset_maps(); set_slope(8,10,90,1);
    {
        float x=56,y=81,vx=20,vy=0,px=x,py=y;   /* foot (58,85), wall line x=64 */
        slope_contact_t ct;
        x+=vx*0.5f;                             /* desired foot (68,85) crosses x=64 */
        CHECK(slope_move(solid,ang,sdir,W,H,px,py,&x,&y,4,4,&vx,&vy,&ct)==1);
        CHECK(NEAR(ct.nx,-1)&&NEAR(ct.ny,0));
        CHECK(NEAR(x+2,64));                    /* clamped exactly at the wall */
        CHECK(NEAR(ct.slide_vx,0));
    }

    /* ---- 7. no tunneling at extreme speed ---- */
    reset_maps(); set_slope(8,10,30,1);
    {
        float x=66,y=36,vx=0,vy=2000,px=x,py=y; /* foot (68,40), ~46px above surface */
        slope_contact_t ct;
        float fx,fy,s;
        y+=vy*0.05f;                            /* desired foot (68,140): 100px step */
        CHECK(slope_move(solid,ang,sdir,W,H,px,py,&x,&y,4,4,&vx,&vy,&ct)==1);
        /* the no-clip invariant: the foot never ends on the solid side of
         * the surface it crossed (signed distance to the line >= 0) */
        fx=x+2; fy=y+4;
        s=(fx-64.0f)*ct.nx + (fy-88.0f)*ct.ny;
        CHECK(s > -1e-3f);
        CHECK(ct.slide_vx<0);                   /* and already sliding down-slope */
    }

    /* ---- 8. resting contact does not sink over 240 steps ---- */
    reset_maps(); set_slope(8,10,0,1);
    {
        float x=66,y=84,vx=0,vy=0,ymax=84;
        int i;
        for(i=0;i<240;++i){
            float px=x,py=y; slope_contact_t ct;
            vy+=300.0f/240.0f;
            y+=vy/240.0f;
            slope_move(solid,ang,sdir,W,H,px,py,&x,&y,4,4,&vx,&vy,&ct);
            if(y>ymax)ymax=y;
        }
        CHECK(NEAR(y,84));
        CHECK(ymax-84 < 0.02f);
    }

    /* ---- 9. mixed map: slope tile vs solid tile give distinct contacts ---- */
    reset_maps();
    set_slope(6,10,25,1);
    set_solid(4,11);
    {
        slope_contact_t cs, cf;
        float x,y,vx,vy,px,py;
        x=50; y=88.0f-4.0f*tanf(25.0f*PIF/180.0f)-4.0f; vx=0; vy=50; px=x; py=y; /* on slope lx=4 */
        y+=vy*0.01f;
        CHECK(slope_move(solid,ang,sdir,W,H,px,py,&x,&y,4,4,&vx,&vy,&cs)==1);
        CHECK(cs.is_slope==1 && cs.ny>-1.0f && cs.nx<0.0f); /* tilted normal */
        x=32; y=84; vx=0; vy=50; px=x; py=y;                /* on solid top y=88 */
        y+=vy*0.01f;
        CHECK(slope_move(solid,ang,sdir,W,H,px,py,&x,&y,4,4,&vx,&vy,&cf)==1);
        CHECK(cf.is_slope==0 && NEAR(cf.nx,0)&&NEAR(cf.ny,-1)); /* flat normal */
        CHECK(!NEAR(cs.nx,cf.nx)||!NEAR(cs.ny,cf.ny));          /* distinguishable */
    }

    printf("%d checks, %d failures\n",checks,fails);
    return fails?1:0;
}
