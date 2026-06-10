/**
 * @license
 * Copyright 2018 Google Inc. All Rights Reserved.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 * =============================================================================
 */

type WebGLResourceHandle<T> = T | number | null;
type WebGLNonNullResourceHandle<T> = T | number;

export type NodeGlesANGLEInstancedArrays = {
  readonly VERTEX_ATTRIB_ARRAY_DIVISOR_ANGLE: number;
  drawArraysInstancedANGLE(
    mode: number, first: number, count: number, instanceCount: number): void;
  drawElementsInstancedANGLE(
    mode: number, count: number, type: number, offset: number,
    instanceCount: number): void;
  vertexAttribDivisorANGLE(index: number, divisor: number): void;
};

export type NodeGlesOESVertexArrayObject = {
  readonly VERTEX_ARRAY_BINDING_OES: number;
  createVertexArrayOES(): WebGLResourceHandle<WebGLVertexArrayObject>;
  bindVertexArrayOES(
    vertexArray: WebGLResourceHandle<WebGLVertexArrayObject>): void;
  deleteVertexArrayOES(
    vertexArray: WebGLResourceHandle<WebGLVertexArrayObject>): void;
  isVertexArrayOES(
    vertexArray: WebGLResourceHandle<WebGLVertexArrayObject>): boolean;
};

export type NodeGlesWEBGLDrawBuffers = {
  readonly MAX_DRAW_BUFFERS_WEBGL: number;
  readonly MAX_COLOR_ATTACHMENTS_WEBGL: number;
  readonly COLOR_ATTACHMENT0_WEBGL: number;
  readonly COLOR_ATTACHMENT1_WEBGL: number;
  readonly COLOR_ATTACHMENT2_WEBGL: number;
  readonly COLOR_ATTACHMENT3_WEBGL: number;
  readonly COLOR_ATTACHMENT4_WEBGL: number;
  readonly COLOR_ATTACHMENT5_WEBGL: number;
  readonly COLOR_ATTACHMENT6_WEBGL: number;
  readonly COLOR_ATTACHMENT7_WEBGL: number;
  readonly COLOR_ATTACHMENT8_WEBGL: number;
  readonly COLOR_ATTACHMENT9_WEBGL: number;
  readonly COLOR_ATTACHMENT10_WEBGL: number;
  readonly COLOR_ATTACHMENT11_WEBGL: number;
  readonly COLOR_ATTACHMENT12_WEBGL: number;
  readonly COLOR_ATTACHMENT13_WEBGL: number;
  readonly COLOR_ATTACHMENT14_WEBGL: number;
  readonly COLOR_ATTACHMENT15_WEBGL: number;
  readonly DRAW_BUFFER0_WEBGL: number;
  readonly DRAW_BUFFER1_WEBGL: number;
  readonly DRAW_BUFFER2_WEBGL: number;
  readonly DRAW_BUFFER3_WEBGL: number;
  readonly DRAW_BUFFER4_WEBGL: number;
  readonly DRAW_BUFFER5_WEBGL: number;
  readonly DRAW_BUFFER6_WEBGL: number;
  readonly DRAW_BUFFER7_WEBGL: number;
  readonly DRAW_BUFFER8_WEBGL: number;
  readonly DRAW_BUFFER9_WEBGL: number;
  readonly DRAW_BUFFER10_WEBGL: number;
  readonly DRAW_BUFFER11_WEBGL: number;
  readonly DRAW_BUFFER12_WEBGL: number;
  readonly DRAW_BUFFER13_WEBGL: number;
  readonly DRAW_BUFFER14_WEBGL: number;
  readonly DRAW_BUFFER15_WEBGL: number;
  drawBuffersWEBGL(buffers: number[] | Uint32Array): void;
};

export type NodeGlesEXTBlendMinmax = {
  readonly MAX_EXT: number;
  readonly MIN_EXT: number;
};

export type NodeGlesEXTColorBufferFloat = {};

export type NodeGlesEXTColorBufferHalfFloat = {};

export type NodeGlesEXTFragDepth = {};

export type NodeGlesEXTFloatBlend = {};

export type NodeGlesEXTSRGB = {
  readonly SRGB_EXT: number;
  readonly SRGB_ALPHA_EXT: number;
  readonly SRGB8_ALPHA8_EXT: number;
  readonly FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING_EXT: number;
};

export type NodeGlesEXTShaderTextureLod = {};

export type NodeGlesEXTTextureFilterAnisotropic = {
  readonly MAX_TEXTURE_MAX_ANISOTROPY_EXT: number;
  readonly TEXTURE_MAX_ANISOTROPY_EXT: number;
};

export type NodeGlesEXTTextureMirrorClampToEdge = {
  readonly MIRROR_CLAMP_TO_EDGE_EXT: number;
};

export type NodeGlesOESElementIndexUint = {};

export type NodeGlesOESStandardDerivatives = {
  readonly FRAGMENT_SHADER_DERIVATIVE_HINT_OES: number;
};

export type NodeGlesOESTextureFloat = {};

export type NodeGlesOESTextureFloatLinear = {};

export type NodeGlesOESTextureHalfFloat = {
  readonly HALF_FLOAT_OES: number;
};

export type NodeGlesOESTextureHalfFloatLinear = {};

export type NodeGlesWEBGLDebugRendererInfo = {
  readonly UNMASKED_VENDOR_WEBGL: number;
  readonly UNMASKED_RENDERER_WEBGL: number;
};

export type NodeGlesWEBGLDepthTexture = {
  readonly UNSIGNED_INT_24_8_WEBGL: number;
};

export type NodeGlesWEBGLCompressedTextureS3TC = {
  readonly COMPRESSED_RGB_S3TC_DXT1_EXT: number;
  readonly COMPRESSED_RGBA_S3TC_DXT1_EXT: number;
  readonly COMPRESSED_RGBA_S3TC_DXT3_EXT: number;
  readonly COMPRESSED_RGBA_S3TC_DXT5_EXT: number;
};

export type NodeGlesWEBGLCompressedTextureS3TCSRGB = {
  readonly COMPRESSED_SRGB_S3TC_DXT1_EXT: number;
  readonly COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT: number;
  readonly COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT: number;
  readonly COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT: number;
};

export type NodeGlesWEBGLLoseContext = {
  loseContext(): void;
  restoreContext(): void;
};

export type NodeGlesWebGL2RenderingContext = WebGL2RenderingContext & {
  drawingBufferColorSpace: "srgb" | "display-p3";
  readonly drawingBufferFormat: number;
  unpackColorSpace: "srgb" | "display-p3";
  destroy(): void;
  dispose(): void;
  drawingBufferStorage(sizedFormat: number, width: number, height: number): void;
  createVertexArray(): WebGLResourceHandle<WebGLVertexArrayObject>;
  bindVertexArray(vertexArray: WebGLResourceHandle<WebGLVertexArrayObject>): void;
  deleteVertexArray(
    vertexArray: WebGLResourceHandle<WebGLVertexArrayObject>): void;
  isVertexArray(vertexArray: WebGLResourceHandle<WebGLVertexArrayObject>): boolean;
  createQuery(): WebGLResourceHandle<WebGLQuery>;
  deleteQuery(query: WebGLResourceHandle<WebGLQuery>): void;
  isQuery(query: WebGLResourceHandle<WebGLQuery>): boolean;
  beginQuery(target: number, query: WebGLNonNullResourceHandle<WebGLQuery>): void;
  endQuery(target: number): void;
  getQuery(
    target: number, pname: number): WebGLResourceHandle<WebGLQuery>;
  getQueryParameter(
    query: WebGLNonNullResourceHandle<WebGLQuery>, pname: number): unknown;
  createSampler(): WebGLResourceHandle<WebGLSampler>;
  deleteSampler(sampler: WebGLResourceHandle<WebGLSampler>): void;
  isSampler(sampler: WebGLResourceHandle<WebGLSampler>): boolean;
  bindSampler(unit: number, sampler: WebGLResourceHandle<WebGLSampler>): void;
  getSamplerParameter(
    sampler: WebGLResourceHandle<WebGLSampler>, pname: number): unknown;
  createTransformFeedback(): WebGLResourceHandle<WebGLTransformFeedback>;
  deleteTransformFeedback(
    transformFeedback: WebGLResourceHandle<WebGLTransformFeedback>): void;
  isTransformFeedback(
    transformFeedback: WebGLResourceHandle<WebGLTransformFeedback>): boolean;
  bindTransformFeedback(
    target: number,
    transformFeedback: WebGLResourceHandle<WebGLTransformFeedback>): void;
  beginTransformFeedback(primitiveMode: number): void;
  endTransformFeedback(): void;
  transformFeedbackVaryings(
    program: WebGLNonNullResourceHandle<WebGLProgram>, varyings: string[],
    bufferMode: number): void;
  getTransformFeedbackVarying(
    program: WebGLNonNullResourceHandle<WebGLProgram>,
    index: number): WebGLActiveInfo | null;
  pauseTransformFeedback(): void;
  resumeTransformFeedback(): void;
  fenceSync(condition: number, flags: number): WebGLSync | null;
  deleteSync(sync: WebGLSync | null): void;
  isSync(sync: WebGLSync | null): boolean;
  drawArraysInstanced(
    mode: number, first: number, count: number, instanceCount: number): void;
  drawElementsInstanced(
    mode: number, count: number, type: number, offset: number,
    instanceCount: number): void;
  vertexAttribDivisor(index: number, divisor: number): void;
  vertexAttribI4i(
    index: number, x: number, y: number, z: number, w: number): void;
  vertexAttribI4iv(index: number, values: Int32Array | number[]): void;
  vertexAttribI4ui(
    index: number, x: number, y: number, z: number, w: number): void;
  vertexAttribI4uiv(index: number, values: Uint32Array | number[]): void;
  vertexAttribIPointer(
    index: number, size: number, type: number, stride: number,
    offset: number): void;
  getVertexAttribIiv(index: number, pname: number): number[];
  getVertexAttribIuiv(index: number, pname: number): number[];
  readPixels(
    x: number, y: number, width: number, height: number, format: number,
    type: number, pixels: ArrayBufferView | number, dstOffset?: number): void;
  texImage2D(
    target: number, level: number, internalformat: number, width: number,
    height: number, border: number, format: number, type: number,
    source: ArrayBufferView | number | null, srcOffset?: number): void;
  texSubImage2D(
    target: number, level: number, xoffset: number, yoffset: number,
    width: number, height: number, format: number, type: number,
    source: ArrayBufferView | number, srcOffset?: number): void;
  texImage3D(
    target: number, level: number, internalformat: number, width: number,
    height: number, depth: number, border: number, format: number, type: number,
    source: ArrayBufferView | number | null, srcOffset?: number): void;
  texSubImage3D(
    target: number, level: number, xoffset: number, yoffset: number,
    zoffset: number, width: number, height: number, depth: number,
    format: number, type: number, source: ArrayBufferView | number,
    srcOffset?: number): void;
  compressedTexImage2D(
    target: number, level: number, internalformat: number, width: number,
    height: number, border: number, source: ArrayBufferView | number,
    srcOffsetOrOffset?: number, srcLengthOverride?: number): void;
  compressedTexImage3D(
    target: number, level: number, internalformat: number, width: number,
    height: number, depth: number, border: number,
    source: ArrayBufferView | number, srcOffsetOrOffset?: number,
    srcLengthOverride?: number): void;
  compressedTexSubImage2D(
    target: number, level: number, xoffset: number, yoffset: number,
    width: number, height: number, format: number,
    source: ArrayBufferView | number, srcOffsetOrOffset?: number,
    srcLengthOverride?: number): void;
  compressedTexSubImage3D(
    target: number, level: number, xoffset: number, yoffset: number,
    zoffset: number, width: number, height: number, depth: number,
    format: number, source: ArrayBufferView | number,
    srcOffsetOrOffset?: number, srcLengthOverride?: number): void;
  getExtension(extensionName: "ANGLE_instanced_arrays"):
    NodeGlesANGLEInstancedArrays | null;
  getExtension(extensionName: "EXT_blend_minmax"):
    NodeGlesEXTBlendMinmax | null;
  getExtension(extensionName: "EXT_color_buffer_float"):
    NodeGlesEXTColorBufferFloat | null;
  getExtension(extensionName: "WEBGL_color_buffer_float"):
    NodeGlesEXTColorBufferFloat | null;
  getExtension(extensionName: "EXT_color_buffer_half_float"):
    NodeGlesEXTColorBufferHalfFloat | null;
  getExtension(extensionName: "EXT_frag_depth"):
    NodeGlesEXTFragDepth | null;
  getExtension(extensionName: "EXT_float_blend"):
    NodeGlesEXTFloatBlend | null;
  getExtension(extensionName: "EXT_sRGB"):
    NodeGlesEXTSRGB | null;
  getExtension(extensionName: "EXT_shader_texture_lod"):
    NodeGlesEXTShaderTextureLod | null;
  getExtension(extensionName: "EXT_texture_filter_anisotropic"):
    NodeGlesEXTTextureFilterAnisotropic | null;
  getExtension(extensionName: "EXT_texture_mirror_clamp_to_edge"):
    NodeGlesEXTTextureMirrorClampToEdge | null;
  getExtension(extensionName: "OES_element_index_uint"):
    NodeGlesOESElementIndexUint | null;
  getExtension(extensionName: "OES_standard_derivatives"):
    NodeGlesOESStandardDerivatives | null;
  getExtension(extensionName: "OES_texture_float"):
    NodeGlesOESTextureFloat | null;
  getExtension(extensionName: "OES_texture_float_linear"):
    NodeGlesOESTextureFloatLinear | null;
  getExtension(extensionName: "OES_texture_half_float"):
    NodeGlesOESTextureHalfFloat | null;
  getExtension(extensionName: "OES_texture_half_float_linear"):
    NodeGlesOESTextureHalfFloatLinear | null;
  getExtension(extensionName: "OES_vertex_array_object"):
    NodeGlesOESVertexArrayObject | null;
  getExtension(extensionName: "WEBGL_debug_renderer_info"):
    NodeGlesWEBGLDebugRendererInfo | null;
  getExtension(extensionName: "WEBGL_depth_texture"):
    NodeGlesWEBGLDepthTexture | null;
  getExtension(extensionName: "WEBGL_compressed_texture_s3tc"):
    NodeGlesWEBGLCompressedTextureS3TC | null;
  getExtension(extensionName: "WEBGL_compressed_texture_s3tc_srgb"):
    NodeGlesWEBGLCompressedTextureS3TCSRGB | null;
  getExtension(extensionName: "WEBGL_draw_buffers"):
    NodeGlesWEBGLDrawBuffers | null;
  getExtension(extensionName: "WEBGL_lose_context"):
    NodeGlesWEBGLLoseContext | null;
};

export interface NodeJsGlBinding {
  createWebGLRenderingContext(
    width: number,
    height: number,
    client_major_es_version: number,
    client_minor_es_version: number,
    webgl_compatibility: boolean,
    enabled_extensions?: string[],
    disabled_extensions?: string[]
    ): WebGLRenderingContext | NodeGlesWebGL2RenderingContext;
}
