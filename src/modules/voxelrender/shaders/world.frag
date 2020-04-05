$in vec3 v_pos;
$in vec3 v_norm;
$in vec4 v_color;
$in vec4 v_clipspace;
$in float v_ambientocclusion;
uniform mat4 u_viewprojection;
uniform sampler2D u_entitiesdepthmap;

#include "_fog.frag"
#include "_shadowmap.frag"
#include "_daynight.frag"

uniform mediump vec3 u_lightdir;
uniform lowp vec3 u_diffuse_color;
uniform lowp vec3 u_ambient_color;
uniform float u_time;
layout(location = 0) $out vec4 o_color;

// https://thebookofshaders.com
float random (in vec2 st) {
	return fract(sin(dot(st.xy, vec2(12.9898, 78.233))) * 43758.5453123);
}

float checker(in vec2 pos) {
	vec2 c = floor(pos);
	float variance = 0.05 * random(c);
	float checker = mod(c.x + c.y, 2.0);
	return mix(0.95 + variance, 1.0 - variance, checker);
}

vec3 baseColor(in vec3 normal) {
	float checkerBoardFactor = 1.0;
	if (abs(normal.y) >= 0.999) {
		checkerBoardFactor = checker(v_pos.xz);
	} else if (abs(normal.x) >= 0.999) {
		checkerBoardFactor = checker(v_pos.yz);
	} else if (abs(normal.z) >= 0.999) {
		checkerBoardFactor = checker(v_pos.xy);
	}
	return v_color.rgb * checkerBoardFactor;
}

vec2 clipSpaceToTexCoords(vec4 clipSpace){
	vec2 ndc = (clipSpace.xy / clipSpace.w);
	vec2 texCoords = ndc / 2.0 + 0.5;
	return clamp(texCoords, 0.0, 1.0);
}

/**
 * @brief If the player is behind some objects - we will make those fragments that
 * are between the camera eye and the player transparent
 */
float resolveAlpha() {
	vec2 ndc = clipSpaceToTexCoords(v_clipspace);
	float depth = gl_FragCoord.z;
	float characterDepth = $texture2D(u_entitiesdepthmap, ndc).r;
	if (depth > characterDepth) {
		return 0.3;
	}
	return v_color.a;
}

void main(void) {
	float alpha = resolveAlpha();
	if (alpha <= 0.01) {
		discard;
	}
	float ndotl = abs(dot(v_norm, u_lightdir));
	vec3 diffuse = u_diffuse_color * ndotl;
	vec3 ambientColor = dayTimeColor(u_ambient_color, u_time);
	vec3 voxelColor = baseColor(v_norm);
	float bias = max(0.05 * (1.0 - ndotl), 0.005);
	vec3 shadowColor = shadow(vec4(v_lightspacepos, 1.0), bias, voxelColor, diffuse, ambientColor);
	vec3 linearColor = shadowColor * v_ambientocclusion;
	o_color = fog(v_pos, linearColor, alpha);
}
