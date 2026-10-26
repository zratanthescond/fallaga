#version 460 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aUV;
layout (location = 3) in float aType;
layout (location = 4) in float aMarkMask;
layout (location = 5) in float aSurfacePriority;
layout (location = 6) in vec2 aMetricUV;

out vec3 FragPos;
out vec3 Normal;
out vec2 TexCoords;
out float RoadType;
out float MarkMask;
out vec2 MetricUV;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

// Same junction uniforms as the fragment shader (shared by name across stages):
// used here to drop the kerb where a side street joins.
uniform int  u_joinContactCount;
uniform vec4 u_joinContactCenterT[32]; // host center xz and tangent xz
uniform vec4 u_joinContactInfo[32];    // host side, white mouth half-span, pavement half-span, radius
uniform vec4 u_joinContactDims[32];    // host road width

// ---- Sidewalk elevation ------------------------------------------------------
// KEEP THIS AT 0. RoadSystem.cpp already raises the sidewalk band on the mesh
// itself (kSidewalkRaise, pillars straddling the kerb lines at u = 0.146 / 0.154)
// and TerrainManager::conformTerrainToRoadMesh grades the ground to that same
// 0.12 m, so a lift here would be applied on top of the built mesh. The extra
// lift floats the outer ribbon edge above the graded ground and opens a dark
// slit along both road sides. See RoadSystem.cpp:31-37.
const float CURB_HEIGHT_M = 0.0;
const float CURB_U        = 0.15;   // must match swWidth in street.frag / RoadSpline::kSidewalkWidthU

void main() {
    vec3 world = vec3(model * vec4(aPos, 1.0));

    int roadT = int(aType + 0.5);
    if (CURB_HEIGHT_M > 0.0 && (roadT == 0 || roadT == 1)) {
        float u = aUV.x;
        float lift = (1.0 - smoothstep(CURB_U - 0.004, CURB_U + 0.004, u)) +
                            smoothstep(1.0 - CURB_U - 0.004, 1.0 - CURB_U + 0.004, u);

        // Dropped kerb where a side street joins (same test the fragment shader uses to hide the brick).
        float supp = 0.0;
        for (int j = 0; j < 32; ++j) {
            if (j >= u_joinContactCount) break;
            vec4 centerT = u_joinContactCenterT[j];
            vec4 info = u_joinContactInfo[j];
            vec2 hostTangent = normalize(centerT.zw);
            vec2 hostRight = vec2(hostTangent.y, -hostTangent.x);
            float along = dot(world.xz - centerT.xy, hostTangent);
            float outward = dot(world.xz - centerT.xy, hostRight) * info.x;
            if (abs(along) < info.y && outward > u_joinContactDims[j].x * 0.30) supp = 1.0;
        }

        // Taper the very outer shoulder back down to the terrain so there is no hovering edge.
        float shoulder = smoothstep(0.0, 0.05, min(u, 1.0 - u));

        world.y += CURB_HEIGHT_M * clamp(lift, 0.0, 1.0) * clamp(aMarkMask, 0.0, 1.0)
                 * (1.0 - supp) * shoulder;
    }

    FragPos = world;
    Normal = mat3(transpose(inverse(model))) * aNormal;
    TexCoords = aUV;
    RoadType = aType;
    MarkMask = aMarkMask;
    MetricUV = aMetricUV;
    vec4 clipPos = projection * view * vec4(FragPos, 1.0);
    // Ground roads share nearly the same height. Terrain-conform interpolation can
    // leave tiny depth differences between overlapping ribbons, so draw order alone
    // does not reliably select the dominant surface. Bias only clip-space depth (not
    // FragPos/world Y) so the prioritized road owns overlap pixels without a raised
    // asphalt patch or a visible height step.
    float roadPriorityDepth = aSurfacePriority * 0.000001;
    clipPos.z -= 2.0 * roadPriorityDepth * clipPos.w;
    gl_Position = clipPos;
}
