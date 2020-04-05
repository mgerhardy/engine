// attributes from the VAOs
$in vec3 a_pos;
$in uvec3 a_info;

#ifdef INSTANCED
// instanced rendering
$in vec3 a_offset;
#endif // INSTANCED

#ifndef MATERIALOFFSET
#define MATERIALOFFSET 0
#endif // MATERIALOFFSET
int materialoffset = MATERIALOFFSET;

uniform mat4 u_model;
uniform vec4 u_clipplane;
uniform mat4 u_viewprojection;
uniform sampler2D u_texture;
#define MATERIALCOLORS 256
layout(std140) uniform u_materialblock {
	vec4 u_materialcolor[MATERIALCOLORS];
};

const vec3 c_normals[6] = vec3[](
	vec3( 1.0,  0.0,  0.0),
	vec3( 0.0,  1.0,  0.0),
	vec3( 0.0,  0.0,  1.0),
	vec3(-1.0,  0.0,  0.0),
	vec3( 0.0, -1.0,  0.0),
	vec3( 0.0,  0.0, -1.0)
);

$out vec3 v_pos;
$out vec3 v_norm;
$out vec4 v_color;
$out vec4 v_clipspace;
$out float v_ambientocclusion;

#include "_fog.vert"
#include "_shadowmap.vert"
#include "_ambientocclusion.vert"

void main(void) {
	uint a_ao = a_info[0];
	uint a_colorindex = a_info[1];
	uint a_face = a_info[2];
	vec4 pos = u_model * vec4(a_pos, 1.0);
#ifdef INSTANCED
	pos += vec4(a_offset, 0.0);
#endif // INSTANCED
	v_pos = pos.xyz;
	v_clipspace = u_viewprojection * pos;
	v_norm = c_normals[a_face];

	gl_ClipDistance[0] = dot(pos, u_clipplane);

	int materialColorIndex = int(a_colorindex) + materialoffset;
	vec3 materialColor = u_materialcolor[materialColorIndex % MATERIALCOLORS].rgb;
	vec3 colornoise = texture(u_texture, abs(pos.xz) / 256.0 / 10.0).rgb;
	float alpha = u_materialcolor[a_colorindex].a;
	v_color = clamp(vec4(materialColor * colornoise * 1.8, alpha), 0.0, 1.0);
	v_ambientocclusion = aovalues[a_ao];

#if cl_shadowmap == 1
	v_lightspacepos = v_pos;
	v_viewz = (u_viewprojection * vec4(v_lightspacepos, 1.0)).w;
#endif // cl_shadowmap

	gl_Position = u_viewprojection * pos;
}
