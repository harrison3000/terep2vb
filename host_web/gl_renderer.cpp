/*
 * The page's OpenGL renderer: the game's side of the renderizeitor library
 * (rz/, WebGL 2 on the page's canvas), which once was the SDL host's.  The
 * library draws the map and the cars its own way, with its own camera and
 * textures; the game's own drawing (game_frame) is not
 * called, so what game_frame does besides drawing is done here: the engine
 * sound, the keys that change the game (Tab, Q, [, ], F9) and the text, which
 * the page draws over the canvas (web_text).
 *
 * This file has a surprising amount of human generated code and gambiarras :)
 *
 */
#include <algorithm>
#include <cmath>
#include <cstdio>
#include "../host/host.hpp"
#include "vec3f.hpp"
#include "rz/renderizeitor.h"
#include "web.hpp"

static RzContext* ctx;

// The internal camera's filter (IIR, one step per physics tick,
// gl_tick): the reference vertex and the camera's axes of the car it
// is on.  Only while it is on (the library smooths the other cameras);
// restarted when the car or the camera changes.
struct CamFilter {
    bool ready;                 // false: start from the next sample
    Vec3f pos;                  // the reference vertex (camera_pt), in tiles
    Vec3f vel;                  // its velocity, per tick, filtered
    Vec3f fwd, up;              // where the internal camera looks, and its up

    void step(Car &car, uint16_t camera_pt, bool up_bias);  // one tick; up_bias: F2
};

struct carrodef_s{
    int32_t id;
    int16_t n_points;
    uint16_t camera_pt;
};

carrodef_s carros[5];
int currTgt = 0;
// The camera of F1..F5: the low 4 bits the library's mode (RZ_CAMERA_*),
// CAM_UP for F2 (the internal one looking a little up, filtered more):
// F1 0x01, F2 0x11.
constexpr int CAM_UP = 0x10;
static int cam_mode = RZ_CAMERA_FOLLOW;
static bool cam_internal() { return (cam_mode & 0x0f) == RZ_CAMERA_INTERNAL; }
static CamFilter cam_filter;

static void updateCars();
static int isLine(uint16_t[], int);
static void loadCar(int);
static void updateSprites();
static void updateCamDists(float frames);

#define F_8_24(_val) ({ \
    const float scale = 1.0f / (float)(1 << 24); \
    float vv = _val;   \
    vv * scale;        \
})

#define F_UV(_u, _v, _uv) ({ \
    TexCoord uv_ = (_uv); \
    float scale = 1.0f / 65535.0f;      \
    _u = (float)uv_.u * scale; \
    _v = (float)uv_.v * scale; \
})

extern int broke_some_ice;

// How much of the way to the new sample the filters go each tick
// (120 Hz): light for F1, which follows the car, stronger for F2.
constexpr float CAM_FILTER_F1 = 0.35f;
constexpr float CAM_FILTER_F2 = 0.12f;
constexpr float CAM_FILTER_POS = 0.5f;
constexpr float CAM_FILTER_VEL = 0.20f;  // the reference vertex's velocity, for the position
constexpr float CAM_UP_BIAS_F2 = 0.7f;  // F2's up leans towards the sky

// Field of view (vertical, degrees): fixed in the internal cameras; in the
// chase camera FOV_SLOW up to FOV_SLOW_SPEED, then up to FOV_FAST at FOV_FAST_SPEED
// and over (tiles per tick; 0.1 is 12 tiles a second), squared in
// between; the other cameras keep the library's default.  Each frame
// fov_target is set for the camera and fov_current goes FOV_FILTER of the
// way to it (about half a second to settle): no jump in a crash, nor when
// changing camera.
constexpr float FOV_INTERNAL = 60, FOV_SLOW = 50, FOV_FAST = 70, FOV_DEFAULT = 50;
constexpr float FOV_SLOW_SPEED = 0.03f, FOV_FAST_SPEED = 0.1f, FOV_FILTER = 0.04f;
static float fov_target = FOV_SLOW, fov_current = FOV_SLOW;

// b -> a, from the 16.16 positions (any unit: only directions are used)
static Vec3f delta(const Pos &a, const Pos &b)
{
    Vec3 d = a - b;
    return { (float)d.x, (float)d.y, (float)d.z };
}

// A vertex of the game in the library's units (tiles), z kept off the
// underflow (a car upside down in a low part of the map).
static Vec3f tiles(const Pos &p)
{
    auto z = p.z.raw < 0xfe000000 ? p.z.raw : 0;
    return { F_8_24(p.x.raw), F_8_24(p.y.raw), F_8_24(z) };
}

// A velocity of the game (8.24 cells per tick) in tiles per tick.
static Vec3f tiles(const Vec3 &v) { return { F_8_24((float)v.x), F_8_24((float)v.y), F_8_24((float)v.z) }; }

static RzVertex rz(Vec3f v) { return { v.x, v.y, v.z }; }

static uint8_t banner_color;               // as game_frame's, which is not called here
static int draw_distance = DRAW_DISTANCE_DEFAULT;
static bool fog_changed = true;            // rzSetFog at the next frame

void gl_set_draw_distance(int tiles)
{
    draw_distance = tiles;
    fog_changed = true;
}

// The fog for a draw distance: it starts there (linear with the slider)
// and is complete at end = 120 - 116 (1 - t)^k, t the slider from 0 to 1:
// 4 at the minimum, 120 at the maximum, never more, and k such that the
// default gives the library's own fog (30, 65).
static void apply_fog()
{
    constexpr float MIN = DRAW_DISTANCE_MIN, DEF = DRAW_DISTANCE_DEFAULT, MAX = DRAW_DISTANCE_MAX;
    static const float k = std::log((120 - 65) / 116.0f) / std::log(1 - (DEF - MIN) / (MAX - MIN));
    float start = (float)draw_distance, t = (start - MIN) / (MAX - MIN);
    rzSetFog(ctx, start, 120 - 116 * std::pow(1 - t, k));
    fog_changed = false;
}

static int canvas_width, canvas_height;    // for the text

bool gl_start(int width, int height, int quality)
{
    banner_color = 0;
    fog_changed = true;
    cam_mode = RZ_CAMERA_FOLLOW;            // the library's default
    fov_target = fov_current = FOV_SLOW;

    canvas_width = width;
    canvas_height = height;
    // the shadows, the GPU's heaviest work: high, three maps of 2048 (the
    // library's); medium, one of 2048 up to 25 tiles; low, one of 512 up to 20
    static const struct { int size, cascades; float reach; } shadows[] = { { 0, 3, 0 }, { 2048, 1, 25 }, { 512, 1, 20 } };
    const auto &q = shadows[quality < 0 ? 0 : quality > 2 ? 2 : quality];
    if (rzCreate(width, height, q.size, q.cascades, q.reach, &ctx) != RZ_OK)
        return false;
    // the map's layers: height and tile, 256 x 256, change while playing;
    // the tiles before the heights, so the terrain's mesh is built once
    rzSetTerrainScale(ctx, 1.0f, 16.0f/255.5f);
    int32_t size;
    const uint8_t *maptex = web_file(fname_maptex, &size);
    rzLoadTileAtlas(ctx, maptex, size);
    rzSetTileMap(ctx, colormap(), 256,256);
    rzSetHeightmap(ctx, heightmap(), 256,256);

    const uint8_t *textures = web_file(fname_textures, &size);
    if (textures)
        rzLoadFallbackTexture(ctx, textures, size);

    if (rzGetError(ctx, NULL) != RZ_OK){
        //TODO actually say the error
        rzDestroy(ctx);
        return false;
    }

    for(int i = 0; i < 5; i++){
        carros[i].id = -1;
        if(i >= world.num_cars){
            continue;
        }

        loadCar(i);

    }

    currTgt = 0;
    rzSetCameraTarget(ctx, carros[0].id, carros[0].camera_pt);

    updateCars();

    rzSetBackgroundColor(ctx, 255);
    
    //TODO one more error checking here
    
    return true;
}

// game_frame's text, in the page's font: at (x, y) of the original 320 x 200
// screen, in palette colour colour.  That screen is fitted in the canvas and
// centred across, whatever the canvas's shape; the text of its top half stays
// at the canvas's top, that of the bottom half at its bottom (on a tall
// phone screen they do not end up in the middle).
static void draw_text(const char *s, int x, int y, uint8_t colour)
{
    std::string text;
    for (; *s && *s != 13; s++)
        if (*s == 27 && s[1])
            s++;                            // a colour change: ignored
        else
            text += *s == 9 ? ' ' : *s;
    const float px = std::min(canvas_width / 320.0f, canvas_height / 200.0f);   // canvas pixels per pixel of the original
    const float left = (canvas_width - 320 * px) / 2;
    const float top = y < 100 ? (y - 2) * px : canvas_height - (200 - (y - 2)) * px;
    web_text(left + x * px, top, 7.5f * px, host_palette(colour), text.c_str());
}

static void draw_texts()
{
    draw_text(title_text, 10, 10, 15);
    draw_text(subtitle_text, 100, 190, 15);
    if (!extra_options.hide_banner) {
        const char *banner = draw_distance <= DRAW_DISTANCE_MIN ? "Silent Terep2"
                           : draw_distance >= DRAW_DISTANCE_MAX ? "Terep2:\nDefinitive Edition"
                                                                : "GAMBIARRA FOREVER!";
        bool longer = draw_distance >= DRAW_DISTANCE_MAX;       // would run into the subtitle
        draw_text(banner, 5, longer ? 181 : 190, (uint8_t)(banner_color >> 2));
    }
    banner_color++;
}

// Another camera (F1..F5): the internal camera's filter starts again when
// the library's mode changes (not from F1 to F2 or back).
static void set_camera(int c)
{
    rzSetCamera(ctx, c & 0x0f);
    if ((c & 0x0f) != (cam_mode & 0x0f))
        cam_filter.ready = false;
    cam_mode = c;
}

// The keys game_frame would handle (keys[0] is the last key pressed); the
// cameras' keys are the library's business.
static void game_keys()
{
    switch (keys[0]) {
    case 0x0f:{                             // Tab: next car for view 1
        world.player_car[0]++;
        if (world.player_car[0] >= (uint16_t)world.num_cars){
            world.player_car[0] = 0;
        }
        world.player_car[1] = world.player_car[0];
        auto c = carros[world.player_car[0]];
        rzSetCameraTarget(ctx, c.id, c.camera_pt);
        currTgt = c.id;
        cam_filter.ready = false;
        break;
    }
    case 59: set_camera(RZ_CAMERA_INTERNAL); break;             // F1: internal
    case 60: set_camera(RZ_CAMERA_INTERNAL | CAM_UP); break;    // F2: internal, has a upright bias as the cars rolls
    case 61: set_camera(RZ_CAMERA_FOLLOW_FREEZE); break;        // F3: the chase camera stops where it is
    case 62: set_camera(RZ_CAMERA_FOLLOW); break;               // F4: chase
    case 63: set_camera(RZ_CAMERA_OVERVIEW); break;             // F5: overview
    case 0x1a:                              // [ and ]: gravity
        world.gravity += 50;
        break;
    case 0x1b:
        world.gravity -= 50;
        break;
    }
    keys[0] = 0;
}

// One step towards the car's current reference vertex and axes (the first
// one after a reset starts there).
void CamFilter::step(Car &car, uint16_t camera_pt, bool up_bias)
{
    const float k = up_bias ? CAM_FILTER_F2 : CAM_FILTER_F1;
    Vec3f p = tiles(car.points[camera_pt].pos);
    // Car::axis_x and axis_y (physics.cpp) in float, from the exact
    // differences of the body vertices 4..7: to the right, and to the front
    // (axis_y, (4 - 6) + (5 - 7), points to the back: here it is reversed)
    const CarVertex *v = car.vertices();
    Vec3f r = delta(v[7].pos, v[6].pos).normalized();
    Vec3f f = (delta(v[6].pos, v[4].pos) + delta(v[7].pos, v[5].pos)).normalized();
    Vec3f u = f.cross(r);                               // unit-sized: the bias below is in those units
    if (up_bias)
        u.z += CAM_UP_BIAS_F2;
    u = u.normalized();

    if (!ready) {
        *this = { true, p, tiles(car.points[camera_pt].vel), f, u };
        return;
    }
    // the position: first moved with the reference vertex's velocity (what
    // physics added to it this tick), itself filtered, then filtered; plain
    // IIR would trail behind by about v (1 - k) / k
    vel = vel.toward(tiles(car.points[camera_pt].vel), CAM_FILTER_VEL);
    pos = (pos + vel).toward(p, CAM_FILTER_POS);
    fwd = fwd.toward(f, k).normalized();
    up = up.toward(u, k).normalized();
}

// After each physics tick (120 Hz, two or so per frame): the state of the
// game at that tick, for what needs more than one sample per frame.  Here:
// one step of the internal camera's filter, for the car it is on.
void gl_tick()
{
    int ci = world.player_car[0];
    if (cam_internal() && carros[ci].id >= 0)
        cam_filter.step(car_get(ci), carros[ci].camera_pt, cam_mode & CAM_UP);
}

void gl_resize(int width, int height)
{
    canvas_width = width;
    canvas_height = height;
    rzSetSize(ctx, width, height);
}

void gl_frame(float frames)
{
    game_keys();
    engine_sound(world.player_car[0], 0);      // what draw_view does for the sound

    if(broke_some_ice){
        rzUpdateTerrain(ctx);
        broke_some_ice = 0;
    }
    
    updateCars();
    updateSprites();
    updateCamDists(frames);
    if (fog_changed)
        apply_fog();
    // the field of view (FOV_*): the chase camera's by the speed of its vertex
    if (cam_internal()) {
        fov_target = FOV_INTERNAL;
    } else if (cam_mode == RZ_CAMERA_FOLLOW) {
        int ci = world.player_car[0];
        float speed = tiles(car_get(ci).points[carros[ci].camera_pt].vel).length();
        float spdd = std::clamp((speed - FOV_SLOW_SPEED) / (FOV_FAST_SPEED - FOV_SLOW_SPEED), 0.0f, 1.0f);
        fov_target = FOV_SLOW + (FOV_FAST - FOV_SLOW) * spdd * spdd;
    } else {
        fov_target = FOV_DEFAULT;
    }
    fov_current += (fov_target - fov_current) * (1 - std::pow(1 - FOV_FILTER, frames));  // FOV_FILTER a 60 Hz
    rzSetCameraFov(ctx, fov_current);
    if (cam_internal()) {
        // the filtered axes (gl_tick) of the car the camera is on
        const CamFilter &c = cam_filter;
        if (c.ready) {
            RzVertex f = rz(c.fwd), u = rz(c.up);
            rzSetCameraInternalAxes(ctx, &f, &u);
        }
    }

    rzRender(ctx, frames);

    draw_texts();
}

void gl_stop()
{
    rzDestroy(ctx);
    for(int i = 0; i < 5; i++){
        carros[i].id = -1;
    }
}

static void updateCars(){
    for(int ci = 0; ci< 5 ; ci++){
        int id = carros[ci].id;
        if(id < 0){
            continue;
        }

        int n = carros[ci].n_points;
        Car &car = car_get(ci);

        RzVertex ptrs[n];

        for(int i =0; i< n; i++){
            ptrs[i] = rz(tiles(car.points[i].pos));
        }
        // the reference vertex (where the camera is): filtered, see gl_tick;
        // most cars do not use it for any polygon
        if (cam_internal() && cam_filter.ready && ci == world.player_car[0])
            ptrs[carros[ci].camera_pt] = rz(cam_filter.pos);

        //TODO review this constant, the limits are 45 deg
        float steering = (float)car.steer.raw * -0.0001f;
        rzUpdateObjectWheels(ctx, id, steering);

        float spd = F_8_24(car.wheel_speed[0]);
        rzSetObjectWheelSpin(ctx, id, spd);

        rzUpdateObjectVertices(ctx, id, ptrs);
    }
}

static void loadCar(int i){
    auto car = car_get(i);
    carros[i].n_points = car.points.size();

    int idCar;
    rzCreateObject(ctx, carros[i].n_points, &idCar);
    carros[i].id = idCar;
    carros[i].camera_pt = car.ref_index;
    cam_filter.ready = false;

    int32_t size;
    const uint8_t *pcx = car.skin.empty() ? nullptr : web_file(car_pcx_name(i), &size);
    if(pcx){
        rzLoadObjectTexture(ctx, idCar, pcx, size);
    }

    int wi = 0;

    for (const auto &o : car.model_ops){
        if(o.op == 4){ //flat polygon
            auto color = o.word & 0xff;
            auto pts = o.outline;

            int n = pts.size();
            uint16_t idxs[n];
            int flag = 0;

            for(int i = 0; i < n; i++){
                int v = pts[i].point;
                flag |= v & 1;
                idxs[i] = v >> 1;
            }

            if(flag){
                //shadow polygon, we dont need that
                continue;
            }

            if(isLine(idxs, n)){
                rzAddObjectLine(ctx, idCar, idxs[0], idxs[1], 0.006f, color);
                continue;
            }

            if(color >= 240){
                int tone = color & 0xf;
                if(color == 240){
                    //special case, all the known cars use the 0 value
                    //4..10 looked the nicest to me, but its a matter of taste
                    tone = 6;
                }
                rzAddObjectTranslucentPolygon(ctx, idCar, idxs, n, tone);
            }else{
                rzAddObjectPolygon(ctx, idCar, idxs, n, color);
            }

            continue;
        }
        if(o.op == 8){ //OP_TEXTURED
            auto pts = o.outline;

            int n = pts.size();

            RzTexVertex vtx[n];

            for(int i =0; i < n;i++){
                auto v = pts[i];
                TexCoord attr = { v.attr.a, v.attr.b };
                
                vtx[i].index = v.point >> 1;
                F_UV(vtx[i].u, vtx[i].v, attr);                
            }
            
            rzAddObjectTexturedPolygon(ctx, idCar, vtx, n);
            continue;
        }
        if(o.op == 10){
            //will be actually handled below
            wi++;
            continue;
        }

        //printf("unhandled: %d\n", o.op);
    }

    if(wi == 4){
        RzWheel wee[4] = {0};

        int widx = 0;
        RzWheelFace f, b;
        for (const auto &w : car.model_ops){
            if(w.op != 10){
                continue;
            }
            auto pi = w.p[0] >> 1;
            auto p = car_get(i).points[pi];

            wee[widx].hubVertex = pi;
            wee[widx].front = p.wheel != 1;
            wee[widx].diameter = (float)p.radius / 150.0f;

            if(widx == 0){
                F_UV(f.u0, f.v0, w.views[0].uv[0]);
                F_UV(f.u1, f.v1, w.views[0].uv[2]);
                
                F_UV(b.u0, b.v0, w.views[8].uv[0]);
                F_UV(b.u1, b.v1, w.views[8].uv[2]);
            }
            
            widx++;
        }

        rzSetObjectWheels(ctx, idCar, wee);
        rzSetObjectWheelFaces(ctx, idCar, &f, &b);
    }else{
        printf("WAT? %d wheels? HOW?\n", wi);
    }
}

static int isLine(uint16_t vertx[], int n){
    //checks if its a polygon with only 2 distinct vertices
    uint8_t z[256] = {0};
    for(int i = 0;i < n;i++){
        auto v = vertx[i];
        z[v] = 1;
    }
    int cnt = 0;
    for(size_t i = 0;i < sizeof(z);i++){
        cnt += z[i];
    }
    return cnt == 2;
}

static void updateSprites(){
    auto n_sprites = world.particle_count;

    RzSprite spr[256];

    for(int i = 0; i < n_sprites && i < 256; i++){
        auto s = world.particles[i];
        auto ds = &spr[i];
        
        ds[0] = {
            .x = F_8_24(s.pos.x.raw),
            .y = F_8_24(s.pos.y.raw),
            .z = F_8_24(s.pos.z.raw),
        };

        //the game always sets the lifetime to 0x64 (100 in decimal)
        //so we make the size inversely proportional to the lifetime, to make the particle expand
        //110 so they begin visible and not 0 size
        int sz = 110 - s.life;

        if(smoke_quad_of_kind[(uint8_t)s.kind >> 4] != 0){ // not the smoke puff
            ds->type = RZ_SPRITE_DEBRIS;
            ds->opacity = 1.0f;
            ds->color = s.kind;
            if(s.life < 32){
                float o = (float)s.life * 0.03125f;
                ds->opacity = o;
            }
        } else {
            ds->type = RZ_SPRITE_SMOKE;
            ds->opacity = 0.8f;
            if(s.life < 50){
                float o = (float)s.life * 0.016f;
                ds->opacity = o;
            }
        }

        ds->size = (float)sz * 0.005f;
        ds->z += ds->size * 0.3f; //add a small amount to z proportional to the size, so the dont spawn buried
    }

    rzSetSprites(ctx,spr,n_sprites);
}

// 0.04 tiles per frame of 60 Hz while the key is held: frames of them
static void updateCamDists(float frames){
    static float camD = 3.0f;
    static float camH = 1.0f;
    const float step = 0.04f * frames;

    if((keys[78] & 0x80) == 0){//plus
        camH += step;
        camH = fminf(camH, 12.0f);
    }else if((keys[74] & 0x80) == 0){ //minus
        camH -= step;
        camH = fmaxf(camH, 0.5f);
    }else if((keys[53] & 0x80) == 0){  // /
        camD += step;
        camD = fminf(camD, 10.0f);
    }else if((keys[55] & 0x80) == 0){ //asterisk
        camD -= step;
        camD = fmaxf(camD, 0.5f);
    } else{
        return;
    }

    rzSetCameraFollow(ctx, camD, camH, 0.08f);
}
