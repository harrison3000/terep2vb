// Camera (mode in rzSetCamera), runs on the CPU once per frame and makes the
// view-projection:
//   chase            target vertex "on the rope", looking at it (with the bubble);
//   frozen           the chase position stops where it is, still looking
//                    at the target;
//   internal         at the target vertex, oriented by the host's axes
//                    (rzSetCameraInternalAxes); coming from the chase it goes
//                    in with a smooth flight, otherwise (and going out) a hard cut;
//   overview         fixed, the whole terrain (also with no target);
//   free             wherever the host says (rzSetCameraFree), no target; a
//                    hard cut going in, and going out the others fly from where
//                    it was (as leaving the overview), but the frozen one,
//                    which stays right there.

#include <cmath>

#include "rz_internal.h"

namespace rz {

namespace {

// Dead zone, in the camera's angle only: a bubble of radius kCameraBubbleRadius
// around the smoothed target (look), which it pushes from inside. While the
// target moves inside it, the centre does not move; when it reaches the wall,
// the centre is dragged the least to keep it on the surface. Returns the centre
// (where the camera looks). The eye's position is not affected.
Vec3 aimBubble(RzContext* ctx, Vec3 look) {
    if (!ctx->aimValid) {
        ctx->aimPoint = look;
        ctx->aimValid = true;
        return look;
    }
    const Vec3 d = look - ctx->aimPoint;
    const float len = sqrtf(dot(d, d));
    const float radius = kCameraBubbleRadius * ctx->cellSize;
    if (len > radius) {
        const float s = (len - radius) / len;
        ctx->aimPoint = { ctx->aimPoint.x + d.x * s, ctx->aimPoint.y + d.y * s, ctx->aimPoint.z + d.z * s };
    }
    return ctx->aimPoint;
}

// The terrain's height (world) at the world's point (x, z), bilinear; 0 without terrain.
// With the continuation built, it also counts outside the map (camera behind the
// target near the border).
float groundHeight(const RzContext* ctx, float x, float z) {
    if (!ctx->hasTerrain()) return 0.0f;
    if (!ctx->extHeights.empty()) return extendedGroundHeight(ctx, x, z);
    const float invCell = 1.0f / ctx->cellSize;      // one division per frame
    float gx = x * invCell, gz = z * invCell;
    if (gx < 0.0f) gx = 0.0f;
    if (gz < 0.0f) gz = 0.0f;
    if (gx > 254.999f) gx = 254.999f;
    if (gz > 254.999f) gz = 254.999f;
    const int32_t ix = int32_t(gx), iz = int32_t(gz);
    const float fx = gx - float(ix), fz = gz - float(iz);
    const uint8_t* h = ctx->heights.data() + iz * kGridSize + ix;
    const float top = float(h[0]) + (float(h[1]) - float(h[0])) * fx;
    const float bot = float(h[kGridSize]) + (float(h[kGridSize + 1]) - float(h[kGridSize])) * fx;
    return (top + (bot - top) * fz) * ctx->heightScale;
}

// The eye's bubble against the terrain: the ground does not get into a sphere
// of radius kTerrainBubble around it. Samples the ground at the centre and on
// two rings of 8 directions (half the radius and the radius); at each point,
// the horizontal distance d asks the eye to be at least sqrt(R² - d²) over the
// ground there. Only pushes up (the terrain is a height map: going up always
// works). Continuous: the maximum of continuous functions, no jumps as the eye moves.
Vec3 terrainBubble(const RzContext* ctx, Vec3 eye) {
    const float r = kTerrainBubble * ctx->cellSize;
    float need = groundHeight(ctx, eye.x, eye.z) + r;
    for (int32_t ring = 1; ring <= 2; ++ring) {
        const float d = r * 0.5f * float(ring), up = sqrtf(r * r - d * d);
        for (int32_t k = 0; k < 8; ++k) {
            const float a = float(k) * (kPi / 4.0f);
            const float h = groundHeight(ctx, eye.x + cosf(a) * d, eye.z + sinf(a) * d) + up;
            if (h > need) need = h;
        }
    }
    if (eye.y < need) eye.y = need;
    return eye;
}

// Camera "on the rope": moves the chase camera's state on a step of `frames`
// frames of 60 Hz (the dampings by stepFraction).
//   - horizontal: if the target goes past the rope's length, the camera is
//     pulled to the rope's distance (stays behind, in the direction it came
//     from); if it gets too near, it backs away. In between, the rope is slack
//     and the camera does not move. The movement to the wanted position is damped.
//   - vertical: tries to be `followHeight` over the target, also damped, and
//     never under the ground.
void updateFollowCamera(RzContext* ctx, Vec3 target, float frames) {
    const float cs = ctx->cellSize;
    const float rope = ctx->followDistance * cs;
    const float minRope = 0.5f * rope;
    const float k = stepFraction(ctx->followStiffness, frames);

    if (!ctx->followInitialized) {
        // Starts on the target's +z side (the same as the overview's), already at
        // rest; when the target moves, the rope takes the camera behind it.
        ctx->followEye = { target.x, target.y + ctx->followHeight * cs, target.z + rope };
        ctx->followInitialized = true;
        ctx->followAppliedDistance = ctx->followDistance;
    }

    Vec3 eye = ctx->followEye;

    // The rope changed length (the host's zoom): the current horizontal distance
    // changes in the same proportion, at once. Without this, the camera would only
    // react when the target left the rope's slack band.
    if (ctx->followAppliedDistance != ctx->followDistance) {
        const float scale = ctx->followDistance / ctx->followAppliedDistance;
        eye.x = target.x + (eye.x - target.x) * scale;
        eye.z = target.z + (eye.z - target.z) * scale;
        ctx->followAppliedDistance = ctx->followDistance;
    }

    const float dx = eye.x - target.x;
    const float dz = eye.z - target.z;
    const float dist = sqrtf(dx * dx + dz * dz);

    float wantX = eye.x, wantZ = eye.z;
    if (dist > rope || dist < minRope) {
        const float len = (dist > rope) ? rope : minRope;
        if (dist > 1e-4f) {
            const float scale = len / dist;
            wantX = target.x + dx * scale;
            wantZ = target.z + dz * scale;
        } else {
            wantX = target.x + len;                    // on top of the target: out towards +x
            wantZ = target.z;
        }
    }
    eye.x += (wantX - eye.x) * k;
    eye.z += (wantZ - eye.z) * k;

    // Height: the wanted one is never less than kFollowClearance over the ground.
    // Going up is damped faster than going down, not to get into the hill; the hard
    // clamp (kFollowMinClearance) is only a safety net.
    const float ground = groundHeight(ctx, eye.x, eye.z);
    float wantY = target.y + ctx->followHeight * cs;
    if (wantY < ground + kFollowClearance * cs) wantY = ground + kFollowClearance * cs;
    const float kY = (wantY > eye.y && ctx->followStiffness < kFollowClimb) ? stepFraction(kFollowClimb, frames) : k;
    eye.y += (wantY - eye.y) * kY;
    if (eye.y < ground + kFollowMinClearance * cs) eye.y = ground + kFollowMinClearance * cs;

    ctx->followEye = eye;
}

Vec3 lerp3(Vec3 a, Vec3 b, float t) {
    return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t };
}

// Rest position behind the target, in the flight's direction (the target ahead).
Vec3 restEye(const RzContext* ctx, Vec3 target, Vec3 dir) {
    const float cs = ctx->cellSize;
    const float rope = ctx->followDistance * cs;
    return { target.x - dir.x * rope, target.y + ctx->followHeight * cs, target.z - dir.z * rope };
}

// Change of target: sets up the transition from the current eye and point
// looked at (also in the middle of another transition).
void startRetarget(RzContext* ctx, Vec3 target) {
    const float cs = ctx->cellSize;
    ctx->retargetPending = false;
    ctx->retargetActive = true;
    ctx->retargetT = 0.0f;
    ctx->retargetEye0 = ctx->followEye;
    ctx->retargetLook0 = ctx->followLook;

    const float dx = target.x - ctx->followLook.x;
    const float dz = target.z - ctx->followLook.z;
    const float d = sqrtf(dx * dx + dz * dz);
    if (d <= kRetargetTurnDistance * cs) {
        ctx->retargetArc = false;
        ctx->retargetStep = 1.0f / kRetargetTurnFrames;
        return;
    }
    ctx->retargetArc = true;
    ctx->retargetDir = { dx / d, 0.0f, dz / d };
    float lift = d / kRetargetArcFull;
    if (lift > 1.0f) lift = 1.0f;
    ctx->retargetLift = lift * kRetargetArcHeight * cs;
    // smoothstep peaks at 1.5x the average speed; the arc adds ~2x the rise
    const Vec3 move = restEye(ctx, target, ctx->retargetDir) - ctx->followEye;
    const float path = sqrtf(dot(move, move)) + 2.0f * ctx->retargetLift;
    float frames = 1.5f * path / (kRetargetMaxSpeed * cs);
    if (frames < kRetargetMinFrames) frames = kRetargetMinFrames;
    ctx->retargetStep = 1.0f / frames;
}

// A frame of the transition. The destination follows the target (it may be moving).
void updateRetarget(RzContext* ctx, Vec3 target) {
    float t = ctx->retargetT + ctx->retargetStep * ctx->frameStep;
    if (t > 1.0f) t = 1.0f;
    ctx->retargetT = t;
    const float e = t * t * (3.0f - 2.0f * t);                  // smoothstep

    if (!ctx->retargetArc) {
        ctx->followLook = lerp3(ctx->retargetLook0, target, e);
        updateFollowCamera(ctx, ctx->followLook, ctx->frameStep); // the rope brings the camera
    } else {
        // The look goes ahead of the eye (reaches the target at 2/3 of the flight): at
        // the top of the arc the camera looks ahead and down, not at its own feet
        float tl = t * 1.5f;
        if (tl > 1.0f) tl = 1.0f;
        ctx->followLook = lerp3(ctx->retargetLook0, target, tl * tl * (3.0f - 2.0f * tl));
        const float cs = ctx->cellSize;
        Vec3 eye = lerp3(ctx->retargetEye0, restEye(ctx, target, ctx->retargetDir), e);
        eye.y += ctx->retargetLift * sinf(kPi * e);
        const float floorY = groundHeight(ctx, eye.x, eye.z) + kFollowClearance * cs;
        if (eye.y < floorY) eye.y = floorY;
        ctx->followEye = eye;
    }
    if (t >= 1.0f) ctx->retargetActive = false;                 // on the normal rope again
}

float smooth01(float t) {
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

// Moves the transition to/from the overview on a frame; returns the smoothed
// progress and the raw one (t) in *rawT.
float stepOverview(RzContext* ctx, float* rawT) {
    float t = ctx->overviewT + ctx->frameStep / kOverviewFrames;
    if (t >= 1.0f) {
        t = 1.0f;
        ctx->overviewActive = false;
    }
    ctx->overviewT = t;
    *rawT = t;
    return smooth01(t);
}

// Perspective projection in WebGL's clip space (−w ≤ z ≤ w), the camera looking
// towards −z.
Mat4 perspective(float focalX, float focalY, float nearPlane, float farPlane) {
    const float invRange = 1.0f / (nearPlane - farPlane);
    Mat4 p{};
    p[0, 0] = focalX;
    p[1, 1] = focalY;
    p[2, 2] = (farPlane + nearPlane) * invRange;
    p[2, 3] = 2.0f * nearPlane * farPlane * invRange;
    p[3, 2] = -1.0f;
    return p;
}

// Overview: looks at the terrain's centre from the +z side, with a fixed
// pitch, at the distance at which the bounding sphere (radius R) fits the FOV.
void overviewCamera(const RzContext* ctx, Vec3 center, Vec3* eye) {
    constexpr float degToRad = kPi / 180.0f;
    const float pitch = kOverviewPitchDegrees * degToRad;
    // focal = 1 / tan(half FOV), on the axis of the smaller FOV: the sphere is
    // nearly tangent to the edges and the box's corners (lower than the sphere) fit
    const float focal = ctx->focalX > ctx->focalY ? ctx->focalX : ctx->focalY;
    const float d = ctx->terrainRadius * focal;
    *eye = center + Vec3{ 0.0f, sinf(pitch) * d, cosf(pitch) * d };
}

} // namespace

// Moves the camera on a frame and returns the view-projection matrix, already in
// WebGL's convention.
Mat4 updateCamera(RzContext* ctx) {
    const float cs = ctx->cellSize;
    const float hs = ctx->heightScale;
    const Vec3 terrainCenter = { 127.5f * cs, 127.5f * hs, 127.5f * cs };

    const int32_t target = ctx->cameraTargetObject;
    const bool hasTarget = target >= 0 && target < int32_t(ctx->objects.size()) &&
                           ctx->objects[target].alive && ctx->objects[target].positioned &&
                           ctx->cameraTargetVertex < ctx->objects[target].vertexCount();
    const bool freeCam = ctx->cameraMode == RZ_CAMERA_FREE && ctx->freeValid;
    const int32_t mode = freeCam ? RZ_CAMERA_FREE
                       : hasTarget && ctx->cameraMode != RZ_CAMERA_FREE ? ctx->cameraMode : RZ_CAMERA_OVERVIEW;
    const bool following = mode != RZ_CAMERA_OVERVIEW && !freeCam;    // chase, frozen or internal
    const bool internal  = mode == RZ_CAMERA_INTERNAL;

    Vec3 eye, at;
    Vec3 up = { 0.0f, 1.0f, 0.0f };                       // the camera's "up" (the internal one tilts)
    if (freeCam) {
        // No transitions nor state: the chase starts afresh on the way back
        // (camLastFollowing false: it flies from here to behind the target)
        eye = ctx->freeEye;
        at = eye + Vec3{ ctx->freeForward.x * cs, ctx->freeForward.y * cs, ctx->freeForward.z * cs };
        ctx->targetPos = eye;
        ctx->aimValid = false;
        ctx->followInitialized = false;
        ctx->retargetPending = ctx->retargetActive = false;
        ctx->overviewActive = false;
        ctx->overviewBlend = 0.0f;
    } else if (internal) {
        // Hard cut: no transitions. The chase is kept ready behind the target
        // (by the internal camera's front), so that going back is a cut too.
        const Vec3 raw = ctx->objects[target].world[ctx->cameraTargetVertex];
        ctx->targetPos = raw;
        ctx->retargetPending = ctx->retargetActive = false;
        ctx->overviewActive = false;
        ctx->overviewBlend = 0.0f;
        Vec3 forward = ctx->internalForward;
        if (ctx->internalValid) {
            up = ctx->internalUp;
        } else {                                          // no axes yet: looks where the chase looked
            Vec3 d = raw - ctx->camLastEye;
            d.y = 0.0f;
            const float l = sqrtf(dot(d, d));
            forward = l > 1e-4f ? Vec3{ d.x / l, 0.0f, d.z / l } : Vec3{ 0.0f, 0.0f, -1.0f };
        }
        // Coming from the chase/frozen: flies from the current eye to the vertex,
        // turning from the current orientation to the axes' (a hard cut otherwise)
        if (ctx->camHasLast && ctx->camLastMode != RZ_CAMERA_INTERNAL) {
            ctx->internalEnterActive = ctx->camLastMode != RZ_CAMERA_OVERVIEW;
            ctx->internalEnterT = 0.0f;
            ctx->internalEye0 = ctx->camLastEye;
            ctx->internalF0 = ctx->camForward;
            ctx->internalU0 = ctx->camUp;
        }
        eye = raw;
        if (ctx->internalEnterActive) {
            // front and up interpolated; lookAt makes them orthonormal
            float t = ctx->internalEnterT + ctx->frameStep / kInternalEnterFrames;
            if (t >= 1.0f) { t = 1.0f; ctx->internalEnterActive = false; }
            ctx->internalEnterT = t;
            const float e = smooth01(t);
            eye = lerp3(ctx->internalEye0, raw, e);
            forward = lerp3(ctx->internalF0, forward, e);
            up = lerp3(ctx->internalU0, up, e);
        }
        at = eye + Vec3{ forward.x * cs, forward.y * cs, forward.z * cs };
        Vec3 h = { forward.x, 0.0f, forward.z };
        const float lh = sqrtf(dot(h, h));
        h = lh > 1e-4f ? Vec3{ h.x / lh, 0.0f, h.z / lh } : Vec3{ 0.0f, 0.0f, -1.0f };
        ctx->followEye = restEye(ctx, raw, h);
        ctx->followLook = raw;
        ctx->followLastRaw = raw;
        ctx->followInitialized = true;
        ctx->followAppliedDistance = ctx->followDistance;
        ctx->aimValid = false;
    } else if (following) {
        // Chase: camera on the rope, looking at the smoothed target vertex
        // (the same point pulls the rope, so the camera's height does not shake either)
        const Vec3 raw = ctx->objects[target].world[ctx->cameraTargetVertex];
        ctx->targetPos = raw;
        if (!ctx->followInitialized) {
            ctx->retargetPending = ctx->retargetActive = false;
            if (ctx->camHasLast && ctx->camLastMode == RZ_CAMERA_FREE && mode == RZ_CAMERA_FOLLOW_FREEZE) {
                // From the free one to the frozen one: stays where the free one was (the
                // host picks its place by flying), already looking at the target
                ctx->followEye = ctx->camLastEye;
                ctx->followLook = raw;
                ctx->followLastRaw = raw;
                ctx->followInitialized = true;
                ctx->followAppliedDistance = ctx->followDistance;
                ctx->overviewActive = false;
                ctx->overviewBlend = 0.0f;
            } else if (ctx->camHasLast && !ctx->camLastFollowing) {
                // Leaving the overview (or halfway into it): flies from where the
                // camera is to behind the target
                ctx->overviewActive = true;
                ctx->overviewToward = false;
                ctx->overviewT = 0.0f;
                ctx->overviewBlend0 = ctx->overviewBlend;
                ctx->overviewEye0 = ctx->camLastEye;
                ctx->overviewLook0 = ctx->camLastLook;
                float dx = raw.x - ctx->camLastEye.x, dz = raw.z - ctx->camLastEye.z;
                const float d = sqrtf(dx * dx + dz * dz);
                if (d > 1e-4f) { dx /= d; dz /= d; } else { dx = 0.0f; dz = -1.0f; }
                ctx->overviewDir = { dx, 0.0f, dz };
                ctx->followInitialized = true;
                ctx->followAppliedDistance = ctx->followDistance;
            } else {
                ctx->overviewActive = false;
                ctx->overviewBlend = 0.0f;
            }
        }
        if (ctx->retargetPending) {
            ctx->overviewActive = false;               // another target in the middle: normal flight
            startRetarget(ctx, raw);
        }
        if (ctx->overviewActive) {
            float t;
            const float e = stepOverview(ctx, &t);
            Vec3 rest = restEye(ctx, raw, ctx->overviewDir);
            Vec3 eye = lerp3(ctx->overviewEye0, rest, e);
            // comes down only in the second half: over the way, the camera stays high
            eye.y = ctx->overviewEye0.y + (rest.y - ctx->overviewEye0.y) * smooth01(2.0f * t - 1.0f);
            const float floorY = groundHeight(ctx, eye.x, eye.z) + kFollowClearance * cs;
            if (eye.y < floorY) eye.y = floorY;
            ctx->followEye = eye;
            ctx->followLook = lerp3(ctx->overviewLook0, raw, smooth01(1.5f * t));
            ctx->overviewBlend = ctx->overviewBlend0 * (1.0f - smooth01(2.0f * t - 1.0f));   // together with the descent
        } else if (ctx->retargetActive) {
            updateRetarget(ctx, raw);
        } else {
            const bool frozen = mode == RZ_CAMERA_FOLLOW_FREEZE && ctx->followInitialized;
            if (!ctx->followInitialized) {
                ctx->followLook = raw;
                ctx->followLastRaw = raw;
            }
            // In steps of up to a frame of 60 Hz, with the target moving from the
            // previous frame's position to the current one: at a low frame rate, the
            // rope and the look stay as at 60 Hz (in a single step, a fast target
            // would open a gap before the rope pulls)
            const int32_t steps = ctx->frameStep > 1.0f ? int32_t(ceilf(ctx->frameStep)) : 1;
            const float frames = ctx->frameStep / float(steps);
            const float kXZ = stepFraction(kFollowLookXZ, frames), kY = stepFraction(kFollowLookY, frames);
            for (int32_t s = 1; s <= steps; ++s) {
                const Vec3 r = lerp3(ctx->followLastRaw, raw, float(s) / float(steps));
                Vec3& look = ctx->followLook;
                look.x += (r.x - look.x) * kXZ;
                look.z += (r.z - look.z) * kXZ;
                look.y += (r.y - look.y) * kY;
                if (!frozen) updateFollowCamera(ctx, look, frames);   // frozen: the eye stays where it is
            }
        }
        if (!ctx->overviewActive && ctx->overviewBlend > 0.0f) {
            // transition interrupted by another target: the fog ends by itself
            ctx->overviewBlend -= ctx->frameStep / kOverviewFrames;
            if (ctx->overviewBlend < 0.0f) ctx->overviewBlend = 0.0f;
        }
        ctx->followLastRaw = raw;
        if (mode != RZ_CAMERA_FOLLOW_FREEZE)                   // (the frozen one stays where it was put)
            ctx->followEye = terrainBubble(ctx, ctx->followEye);   // the bubble: the ground does not come near
        eye = ctx->followEye;
        if (ctx->overviewActive || ctx->retargetActive || mode == RZ_CAMERA_FOLLOW_FREEZE) {
            at = ctx->followLook;
            ctx->aimValid = false;                     // transitions and frozen: looks straight at the target
        } else {
            // Dead zone in the angle (slow manoeuvres, back and forth, do not
            // rock the camera); the rope goes on following the target
            at = aimBubble(ctx, ctx->followLook);
        }
    } else {
        ctx->aimValid = false;
        ctx->followInitialized = false;
        ctx->retargetPending = ctx->retargetActive = false;
        Vec3 ovEye;
        overviewCamera(ctx, terrainCenter, &ovEye);
        if (ctx->camHasLast && ctx->camLastFollowing) {
            // Going to the overview (or halfway out of it)
            ctx->overviewActive = true;
            ctx->overviewToward = true;
            ctx->overviewT = 0.0f;
            ctx->overviewBlend0 = ctx->overviewBlend;
            ctx->overviewEye0 = ctx->camLastEye;
            ctx->overviewLook0 = ctx->camLastLook;
        } else if (!ctx->camHasLast) {
            ctx->overviewActive = false;
        }
        if (ctx->overviewActive && ctx->overviewToward) {
            float t;
            const float e = stepOverview(ctx, &t);
            eye = lerp3(ctx->overviewEye0, ovEye, e);
            // rises already in the first half, not to go through a hill
            eye.y = ctx->overviewEye0.y + (ovEye.y - ctx->overviewEye0.y) * smooth01(2.0f * t);
            const float floorY = groundHeight(ctx, eye.x, eye.z) + kFollowClearance * cs;
            if (eye.y < floorY) eye.y = floorY;
            at = lerp3(ctx->overviewLook0, terrainCenter, e);
            ctx->overviewBlend = ctx->overviewBlend0 + (1.0f - ctx->overviewBlend0) * smooth01(2.0f * t);   // together with the rise
        } else {
            ctx->overviewActive = false;
            eye = ovEye;
            at = terrainCenter;
            ctx->overviewBlend = 1.0f;
        }
    }
    ctx->camHasLast = true;
    ctx->camLastFollowing = following;
    ctx->camLastMode = mode;
    if (!internal) ctx->internalEnterActive = false;
    ctx->camLastEye = eye;
    ctx->camLastLook = at;

    // Fog: following, around the eye (rzSetFog); in the overview, around the
    // map's centre, only past the terrain (hides the end of the continuation).
    // In the transition, origin and distances are interpolated. Shadows: cascades
    // following, the whole terrain's map in the overview (changing halfway).
    const float b = ctx->overviewBlend;
    const float ovNear = ctx->terrainRadius, ovFar = ctx->terrainRadius + kOverviewFogDepth * cs;
    ctx->eyePos = eye;
    ctx->fogOn  = following || ctx->overviewActive || ctx->hasTerrain();   // without terrain, the overview has nothing to hide
    ctx->fogOrigin = lerp3(eye, terrainCenter, b);
    ctx->fogNear = ctx->fogStart * cs + (ovNear - ctx->fogStart * cs) * b;
    ctx->fogFar  = ctx->fogEnd * cs + (ovFar - ctx->fogEnd * cs) * b;
    ctx->shadowWholeTerrain = b >= 0.5f;
    ctx->wallOn = (following || freeCam) && b < 0.5f;

    const Mat4 view = lookAt(eye, at, up);
    ctx->camRight   = { view[0, 0], view[0, 1], view[0, 2] };
    ctx->camUp      = { view[1, 0], view[1, 1], view[1, 2] };
    ctx->camForward = { -view[2, 0], -view[2, 1], -view[2, 2] };
    const Vec3 toAt = at - eye;
    const float focusDist = sqrtf(dot(toAt, toAt));

    // near/far: the whole terrain (sphere of radius R) and the point looked at stay
    // inside the frustum in depth.
    const Vec3 toTerrain = eye - terrainCenter;
    const float terrainDist = sqrtf(dot(toTerrain, toTerrain));
    const float radius = ctx->terrainRadius;
    float nearPlane = terrainDist - radius;
    if (nearPlane > 0.5f * focusDist) nearPlane = 0.5f * focusDist;
    if (nearPlane < 0.002f * radius) nearPlane = 0.002f * radius;   // ~0.36 tile: the ground near the camera
    if (internal) nearPlane = kInternalNear * cs;                   // inside the object: the dashboard is near
    if (freeCam && nearPlane > kFreeNear * cs) nearPlane = kFreeNear * cs;   // close to the ground: it is near
    float farPlane = terrainDist + radius;
    // With fog, nothing past its end shows: the far plane goes up to it (better precision)
    const Vec3 toFog = ctx->fogOrigin - eye;
    const float fogFar = (sqrtf(dot(toFog, toFog)) + ctx->fogFar) * 1.02f;
    if (farPlane > fogFar) farPlane = fogFar;
    if (farPlane < nearPlane * 2.0f) farPlane = nearPlane * 2.0f;
    ctx->nearPlane = nearPlane;
    ctx->farPlane  = farPlane;

    return perspective(ctx->focalX, ctx->focalY, nearPlane, farPlane) * view;
}

// View matrix looking from `eye` to `at`, with `up` (need be neither unit nor
// perpendicular) upwards (right-handed system, the camera looking towards
// −z in view space).
Mat4 lookAt(Vec3 eye, Vec3 at, Vec3 up) {
    Vec3 f = at - eye;
    const float invF = 1.0f / sqrtf(dot(f, f));
    f = { f.x * invF, f.y * invF, f.z * invF };
    Vec3 r = cross(f, up);
    const float invR = 1.0f / sqrtf(dot(r, r));
    r = { r.x * invR, r.y * invR, r.z * invR };
    const Vec3 u = cross(r, f);

    Mat4 v = Mat4::identity();
    v[0, 0] =  r.x; v[0, 1] =  r.y; v[0, 2] =  r.z; v[0, 3] = -dot(r, eye);
    v[1, 0] =  u.x; v[1, 1] =  u.y; v[1, 2] =  u.z; v[1, 3] = -dot(u, eye);
    v[2, 0] = -f.x; v[2, 1] = -f.y; v[2, 2] = -f.z; v[2, 3] =  dot(f, eye);
    return v;
}

} // namespace rz
