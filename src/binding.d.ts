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

export type NodeGlesWebGL2RenderingContext = WebGL2RenderingContext & {
  drawingBufferColorSpace: "srgb" | "display-p3";
  readonly drawingBufferFormat: number;
  unpackColorSpace: "srgb" | "display-p3";
  drawingBufferStorage(sizedFormat: number, width: number, height: number): void;
  createVertexArray(): WebGLVertexArrayObject | number | null;
  bindVertexArray(vertexArray: WebGLVertexArrayObject | number | null): void;
  deleteVertexArray(vertexArray: WebGLVertexArrayObject | number | null): void;
  isVertexArray(vertexArray: WebGLVertexArrayObject | number | null): boolean;
  drawArraysInstanced(
    mode: number, first: number, count: number, instanceCount: number): void;
  drawElementsInstanced(
    mode: number, count: number, type: number, offset: number,
    instanceCount: number): void;
  vertexAttribDivisor(index: number, divisor: number): void;
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
