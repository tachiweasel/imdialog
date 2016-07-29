// imgui.vs.glsl

uniform vec2 uWindowSize;

attribute vec2 aPosition;
attribute vec2 aTextureUV;
attribute vec4 aColor;

varying vec2 vTextureUV;
varying vec4 vColor;

void main(void) {
    vTextureUV = aTextureUV;
    vColor = aColor;
    gl_Position = vec4(mix(vec2(-1.0), vec2(1.0), aPosition / uWindowSize), 0.0, 1.0);
}

