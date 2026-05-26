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
  beginQuery(target: number, query: WebGLResourceHandle<WebGLQuery>): void;
  getQueryParameter(
    query: WebGLResourceHandle<WebGLQuery>, pname: number): unknown;
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
  fenceSync(condition: number, flags: number): WebGLSync | null;
  deleteSync(sync: WebGLSync | null): void;
  isSync(sync: WebGLSync | null): boolean;
  drawArraysInstanced(
    mode: number, first: number, count: number, instanceCount: number): void;
  drawElementsInstanced(
    mode: number, count: number, type: number, offset: number,
    instanceCount: number): void;
  vertexAttribDivisor(index: number, divisor: number): void;
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
  getExtension(extensionName: "OES_vertex_array_object"):
    NodeGlesOESVertexArrayObject | null;
  getExtension(extensionName: "WEBGL_draw_buffers"):
    NodeGlesWEBGLDrawBuffers | null;
};

export interface NodeJsGlBinding {
  createWebGLRenderingContext(
    width: number,
    height: number,
    client_major_es_version: number,
    client_minor_es_version: number,
    webgl_compatbility: boolean
    ): WebGLRenderingContext | NodeGlesWebGL2RenderingContext;
}
