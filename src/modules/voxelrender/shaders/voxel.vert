// attributes from the VAOs
$in vec3 a_pos;
$in uvec3 a_info;

uniform mat4 u_model;
uniform mat4 u_viewprojection;
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

$out vec4 v_pos;
$out vec3 v_norm;
$out vec4 v_color;
$out float v_ambientocclusion;

#include "_shadowmap.vert"
#include "_ambientocclusion.vert"

void main(void) {
	uint a_ao = a_info[0];
	uint a_colorindex = a_info[1];
	uint a_face = a_info[2];
	v_norm = c_normals[a_face];
	v_pos = u_model * vec4(a_pos, 1.0);

	int materialColorIndex = int(a_colorindex);
	vec3 materialColor = u_materialcolor[materialColorIndex % MATERIALCOLORS].rgb;
	v_color = vec4(materialColor, 1.0);

	v_ambientocclusion = aovalues[a_ao];

#if cl_shadowmap == 1
	v_lightspacepos = v_pos.xyz;
	v_viewz = (u_viewprojection * vec4(v_lightspacepos, 1.0)).w;
#endif // cl_shadowmap

	gl_Position = u_viewprojection * v_pos;
}
