// imgui.fs.glsl

uniform sampler2D uTexture;

varying vec2 vTextureUV;
varying vec4 vColor;

void main(void) {
    gl_FragColor = vColor * texture2D(uTexture, vTextureUV);
}

