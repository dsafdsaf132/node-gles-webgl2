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

#include "egl_context_wrapper.h"

#include "utils.h"

#include "angle/include/EGL/egl.h"
#include "angle/include/EGL/eglext.h"

#include <cstdio>
#include <vector>

namespace nodejsgl {

EGLContextWrapper::EGLContextWrapper(napi_env env,
                                     const GLContextOptions& context_options)
    : context(EGL_NO_CONTEXT), display(EGL_NO_DISPLAY), config(nullptr),
      surface(EGL_NO_SURFACE), drawing_buffer_width(0),
      drawing_buffer_height(0), drawing_buffer_format(GL_RGBA8),
      client_major_es_version(0), client_minor_es_version(0) {
  InitEGL(env, context_options);
  BindProcAddresses();
  RefreshGLVersion();
  RefreshGLExtensions();

#if DEBUG
  std::cerr << "** GL_EXTENSIONS:" << std::endl;
  gl_extensions->LogExtensions();
  std::cerr << std::endl;

  std::cerr << "** REQUESTABLE_EXTENSIONS:" << std::endl;
  angle_requestable_extensions->LogExtensions();
  std::cerr << std::endl;
#endif
}

void EGLContextWrapper::InitEGL(napi_env env,
                                const GLContextOptions& context_options) {
  std::vector<EGLAttrib> display_attributes;
  display_attributes.push_back(EGL_PLATFORM_ANGLE_TYPE_ANGLE);
  // Most NVIDIA drivers will not work properly with
  // EGL_PLATFORM_ANGLE_TYPE_OPENGLES_ANGLE, only enable this option on ARM
  // devices for now:
#if defined(__arm__)
  display_attributes.push_back(EGL_PLATFORM_ANGLE_TYPE_OPENGLES_ANGLE);
#else
  display_attributes.push_back(EGL_PLATFORM_ANGLE_TYPE_DEFAULT_ANGLE);
#endif

  display_attributes.push_back(EGL_NONE);

  display = eglGetPlatformDisplay(EGL_PLATFORM_ANGLE_ANGLE, nullptr,
                                  &display_attributes[0]);
  if (display == EGL_NO_DISPLAY) {
    // TODO(kreeger): This is the default path for Mac OS. Determine why egl has
    // to be initialized this way on Mac OS.
    display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY) {
      NAPI_THROW_ERROR(env, "No display");
      return;
    }
  }

  EGLint major;
  EGLint minor;
  if (!eglInitialize(display, &major, &minor)) {
    NAPI_THROW_ERROR(env, "Could not initialize display");
    return;
  }

  egl_extensions = std::unique_ptr<GLExtensionsWrapper>(
      new GLExtensionsWrapper(eglQueryString(display, EGL_EXTENSIONS)));
#if DEBUG
  std::cerr << "** EGL_EXTENSIONS:" << std::endl;
  egl_extensions->LogExtensions();
  std::cerr << std::endl;
#endif

  EGLint attrib_list[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                          EGL_RED_SIZE,     8,
                          EGL_GREEN_SIZE,   8,
                          EGL_BLUE_SIZE,    8,
                          EGL_ALPHA_SIZE,   8,
                          EGL_DEPTH_SIZE,   24,
                          EGL_STENCIL_SIZE, 8,
                          EGL_NONE};

  EGLint num_config;
  if (!eglChooseConfig(display, attrib_list, &config, 1, &num_config)) {
    NAPI_THROW_ERROR(env, "Failed creating a config");
    return;
  }

  eglBindAPI(EGL_OPENGL_ES_API);
  if (eglGetError() != EGL_SUCCESS) {
    NAPI_THROW_ERROR(env, "Failed to set OpenGL ES API");
    return;
  }

  EGLint config_renderable_type;
  if (!eglGetConfigAttrib(display, config, EGL_RENDERABLE_TYPE,
                          &config_renderable_type)) {
    NAPI_THROW_ERROR(env, "Failed to get EGL_RENDERABLE_TYPE");
    return;
  }

  // If the requested context is ES3 but the config cannot support ES3, request
  // ES2 instead.
  EGLint major_version = context_options.client_major_es_version;
  EGLint minor_version = context_options.client_minor_es_version;
  if ((config_renderable_type & EGL_OPENGL_ES3_BIT) == 0 &&
      major_version >= 3) {
    major_version = 2;
    minor_version = 0;
  }

  // Append attributes based on available features
  std::vector<EGLint> context_attributes;

  context_attributes.push_back(EGL_CONTEXT_MAJOR_VERSION_KHR);
  context_attributes.push_back(major_version);

  context_attributes.push_back(EGL_CONTEXT_MINOR_VERSION_KHR);
  context_attributes.push_back(minor_version);

  if (context_options.webgl_compatibility) {
    context_attributes.push_back(EGL_CONTEXT_WEBGL_COMPATIBILITY_ANGLE);
    context_attributes.push_back(EGL_TRUE);
  }

  // TODO(kreeger): This is only needed to avoid validation.
  // This is needed for OES_TEXTURE_HALF_FLOAT textures uploading as FLOAT
  context_attributes.push_back(EGL_CONTEXT_OPENGL_NO_ERROR_KHR);
  context_attributes.push_back(EGL_TRUE);

  context_attributes.push_back(EGL_NONE);

  context = eglCreateContext(display, config, EGL_NO_CONTEXT,
                             context_attributes.data());
  if (context == EGL_NO_CONTEXT) {
    NAPI_THROW_ERROR(env, "Could not create context");
    return;
  }

  EGLint surface_attribs[] = {EGL_WIDTH, (EGLint)context_options.width,
                              EGL_HEIGHT, (EGLint)context_options.height,
                              EGL_NONE};
  surface = eglCreatePbufferSurface(display, config, surface_attribs);
  if (surface == EGL_NO_SURFACE) {
    NAPI_THROW_ERROR(env, "Could not create surface");
    return;
  }

  EGLint actual_width = 0;
  EGLint actual_height = 0;
  eglQuerySurface(display, surface, EGL_WIDTH, &actual_width);
  eglQuerySurface(display, surface, EGL_HEIGHT, &actual_height);
  drawing_buffer_width = static_cast<uint32_t>(actual_width);
  drawing_buffer_height = static_cast<uint32_t>(actual_height);
  drawing_buffer_format = GL_RGBA8;

  if (!eglMakeCurrent(display, surface, surface, context)) {
    NAPI_THROW_ERROR(env, "Could not make context current");
    return;
  }
}

bool EGLContextWrapper::ResizeSurface(napi_env env, uint32_t width,
                                      uint32_t height) {
  if (display == EGL_NO_DISPLAY || context == EGL_NO_CONTEXT) {
    NAPI_THROW_ERROR(env, "EGL context is not initialized");
    return false;
  }

  width = width > 0 ? width : 1;
  height = height > 0 ? height : 1;

  EGLint surface_attribs[] = {EGL_WIDTH, static_cast<EGLint>(width), EGL_HEIGHT,
                              static_cast<EGLint>(height), EGL_NONE};
  EGLSurface next_surface =
      eglCreatePbufferSurface(display, config, surface_attribs);
  if (next_surface == EGL_NO_SURFACE) {
    NAPI_THROW_ERROR(env, "Could not create drawing buffer surface");
    return false;
  }

  if (!eglMakeCurrent(display, next_surface, next_surface, context)) {
    eglDestroySurface(display, next_surface);
    NAPI_THROW_ERROR(env, "Could not make drawing buffer surface current");
    return false;
  }

  if (surface != EGL_NO_SURFACE) {
    eglDestroySurface(display, surface);
  }
  surface = next_surface;

  EGLint actual_width = 0;
  EGLint actual_height = 0;
  eglQuerySurface(display, surface, EGL_WIDTH, &actual_width);
  eglQuerySurface(display, surface, EGL_HEIGHT, &actual_height);
  drawing_buffer_width = static_cast<uint32_t>(actual_width);
  drawing_buffer_height = static_cast<uint32_t>(actual_height);
  drawing_buffer_format = GL_RGBA8;
  return true;
}

void EGLContextWrapper::BindProcAddresses() {
  // Bind runtime function pointers.
  glActiveTexture = reinterpret_cast<PFNGLACTIVETEXTUREPROC>(
      eglGetProcAddress("glActiveTexture"));
  glAttachShader = reinterpret_cast<PFNGLATTACHSHADERPROC>(
      eglGetProcAddress("glAttachShader"));
  glBindAttribLocation = reinterpret_cast<PFNGLBINDATTRIBLOCATIONPROC>(
      eglGetProcAddress("glBindAttribLocation"));
  glBindBuffer =
      reinterpret_cast<PFNGLBINDBUFFERPROC>(eglGetProcAddress("glBindBuffer"));
  glBindBufferBase = reinterpret_cast<PFNGLBINDBUFFERBASEPROC>(
      eglGetProcAddress("glBindBufferBase"));
  glBindBufferRange = reinterpret_cast<PFNGLBINDBUFFERRANGEPROC>(
      eglGetProcAddress("glBindBufferRange"));
  glBindFramebuffer = reinterpret_cast<PFNGLBINDFRAMEBUFFERPROC>(
      eglGetProcAddress("glBindFramebuffer"));
  glBindRenderbuffer = reinterpret_cast<PFNGLBINDRENDERBUFFERPROC>(
      eglGetProcAddress("glBindRenderbuffer"));
  glBindSampler = reinterpret_cast<PFNGLBINDSAMPLERPROC>(
      eglGetProcAddress("glBindSampler"));
  glBindTexture = reinterpret_cast<PFNGLBINDTEXTUREPROC>(
      eglGetProcAddress("glBindTexture"));
  glBindTransformFeedback = reinterpret_cast<PFNGLBINDTRANSFORMFEEDBACKPROC>(
      eglGetProcAddress("glBindTransformFeedback"));
  glBindVertexArray = reinterpret_cast<PFNGLBINDVERTEXARRAYPROC>(
      eglGetProcAddress("glBindVertexArray"));
  glBeginQuery =
      reinterpret_cast<PFNGLBEGINQUERYPROC>(eglGetProcAddress("glBeginQuery"));
  glBeginTransformFeedback = reinterpret_cast<PFNGLBEGINTRANSFORMFEEDBACKPROC>(
      eglGetProcAddress("glBeginTransformFeedback"));
  glBlitFramebuffer = reinterpret_cast<PFNGLBLITFRAMEBUFFERPROC>(
      eglGetProcAddress("glBlitFramebuffer"));
  glBlendColor =
      reinterpret_cast<PFNGLBLENDCOLORPROC>(eglGetProcAddress("glBlendColor"));
  glBlendEquation = reinterpret_cast<PFNGLBLENDEQUATIONPROC>(
      eglGetProcAddress("glBlendEquation"));
  glBlendEquationSeparate = reinterpret_cast<PFNGLBLENDEQUATIONSEPARATEPROC>(
      eglGetProcAddress("glBlendEquationSeparate"));
  glBlendFunc =
      reinterpret_cast<PFNGLBLENDFUNCPROC>(eglGetProcAddress("glBlendFunc"));
  glBlendFuncSeparate = reinterpret_cast<PFNGLBLENDFUNCSEPARATEPROC>(
      eglGetProcAddress("glBlendFuncSeparate"));
  glBufferData =
      reinterpret_cast<PFNGLBUFFERDATAPROC>(eglGetProcAddress("glBufferData"));
  glBufferSubData = reinterpret_cast<PFNGLBUFFERSUBDATAPROC>(
      eglGetProcAddress("glBufferSubData"));
  glCheckFramebufferStatus = reinterpret_cast<PFNGLCHECKFRAMEBUFFERSTATUSPROC>(
      eglGetProcAddress("glCheckFramebufferStatus"));
  glClear = reinterpret_cast<PFNGLCLEARPROC>(eglGetProcAddress("glClear"));
  glClearBufferfi = reinterpret_cast<PFNGLCLEARBUFFERFIPROC>(
      eglGetProcAddress("glClearBufferfi"));
  glClearBufferfv = reinterpret_cast<PFNGLCLEARBUFFERFVPROC>(
      eglGetProcAddress("glClearBufferfv"));
  glClearBufferiv = reinterpret_cast<PFNGLCLEARBUFFERIVPROC>(
      eglGetProcAddress("glClearBufferiv"));
  glClearBufferuiv = reinterpret_cast<PFNGLCLEARBUFFERUIVPROC>(
      eglGetProcAddress("glClearBufferuiv"));
  glClearColor =
      reinterpret_cast<PFNGLCLEARCOLORPROC>(eglGetProcAddress("glClearColor"));
  glClearDepthf = reinterpret_cast<PFNGLCLEARDEPTHFPROC>(
      eglGetProcAddress("glClearDepthf"));
  glClearStencil = reinterpret_cast<PFNGLCLEARSTENCILPROC>(
      eglGetProcAddress("glClearStencil"));
  glClientWaitSync = reinterpret_cast<PFNGLCLIENTWAITSYNCPROC>(
      eglGetProcAddress("glClientWaitSync"));
  glColorMask =
      reinterpret_cast<PFNGLCOLORMASKPROC>(eglGetProcAddress("glColorMask"));
  glCompileShader = reinterpret_cast<PFNGLCOMPILESHADERPROC>(
      eglGetProcAddress("glCompileShader"));
  glCompressedTexImage2D = reinterpret_cast<PFNGLCOMPRESSEDTEXIMAGE2DPROC>(
      eglGetProcAddress("glCompressedTexImage2D"));
  glCompressedTexImage3D = reinterpret_cast<PFNGLCOMPRESSEDTEXIMAGE3DPROC>(
      eglGetProcAddress("glCompressedTexImage3D"));
  glCompressedTexSubImage2D =
      reinterpret_cast<PFNGLCOMPRESSEDTEXSUBIMAGE2DPROC>(
          eglGetProcAddress("glCompressedTexSubImage2D"));
  glCompressedTexSubImage3D =
      reinterpret_cast<PFNGLCOMPRESSEDTEXSUBIMAGE3DPROC>(
          eglGetProcAddress("glCompressedTexSubImage3D"));
  glCopyBufferSubData = reinterpret_cast<PFNGLCOPYBUFFERSUBDATAPROC>(
      eglGetProcAddress("glCopyBufferSubData"));
  glCopyTexImage2D = reinterpret_cast<PFNGLCOPYTEXIMAGE2DPROC>(
      eglGetProcAddress("glCopyTexImage2D"));
  glCopyTexSubImage2D = reinterpret_cast<PFNGLCOPYTEXSUBIMAGE2DPROC>(
      eglGetProcAddress("glCopyTexSubImage2D"));
  glCopyTexSubImage3D = reinterpret_cast<PFNGLCOPYTEXSUBIMAGE3DPROC>(
      eglGetProcAddress("glCopyTexSubImage3D"));
  glCreateProgram = reinterpret_cast<PFNGLCREATEPROGRAMPROC>(
      eglGetProcAddress("glCreateProgram"));
  glCreateShader = reinterpret_cast<PFNGLCREATESHADERPROC>(
      eglGetProcAddress("glCreateShader"));
  glCullFace =
      reinterpret_cast<PFNGLCULLFACEPROC>(eglGetProcAddress("glCullFace"));
  glDeleteBuffers = reinterpret_cast<PFNGLDELETEBUFFERSPROC>(
      eglGetProcAddress("glDeleteBuffers"));
  glDeleteFramebuffers = reinterpret_cast<PFNGLDELETEFRAMEBUFFERSPROC>(
      eglGetProcAddress("glDeleteFramebuffers"));
  glDeleteRenderbuffers = reinterpret_cast<PFNGLDELETERENDERBUFFERSPROC>(
      eglGetProcAddress("glDeleteRenderbuffers"));
  glDeleteProgram = reinterpret_cast<PFNGLDELETEPROGRAMPROC>(
      eglGetProcAddress("glDeleteProgram"));
  glDeleteQueries = reinterpret_cast<PFNGLDELETEQUERIESPROC>(
      eglGetProcAddress("glDeleteQueries"));
  glDeleteShader = reinterpret_cast<PFNGLDELETESHADERPROC>(
      eglGetProcAddress("glDeleteShader"));
  glDeleteSamplers = reinterpret_cast<PFNGLDELETESAMPLERSPROC>(
      eglGetProcAddress("glDeleteSamplers"));
  glDeleteSync =
      reinterpret_cast<PFNGLDELETESYNCPROC>(eglGetProcAddress("glDeleteSync"));
  glDeleteTextures = reinterpret_cast<PFNGLDELETETEXTURESPROC>(
      eglGetProcAddress("glDeleteTextures"));
  glDeleteTransformFeedbacks =
      reinterpret_cast<PFNGLDELETETRANSFORMFEEDBACKSPROC>(
          eglGetProcAddress("glDeleteTransformFeedbacks"));
  glDeleteVertexArrays = reinterpret_cast<PFNGLDELETEVERTEXARRAYSPROC>(
      eglGetProcAddress("glDeleteVertexArrays"));
  glDepthFunc =
      reinterpret_cast<PFNGLDEPTHFUNCPROC>(eglGetProcAddress("glDepthFunc"));
  glDepthMask =
      reinterpret_cast<PFNGLDEPTHMASKPROC>(eglGetProcAddress("glDepthMask"));
  glDepthRangef = reinterpret_cast<PFNGLDEPTHRANGEFPROC>(
      eglGetProcAddress("glDepthRangef"));
  glDrawArrays =
      reinterpret_cast<PFNGLDRAWARRAYSPROC>(eglGetProcAddress("glDrawArrays"));
  glDrawArraysInstanced = reinterpret_cast<PFNGLDRAWARRAYSINSTANCEDPROC>(
      eglGetProcAddress("glDrawArraysInstanced"));
  glDrawBuffers = reinterpret_cast<PFNGLDRAWBUFFERSPROC>(
      eglGetProcAddress("glDrawBuffers"));
  glDrawElements = reinterpret_cast<PFNGLDRAWELEMENTSPROC>(
      eglGetProcAddress("glDrawElements"));
  glDrawElementsInstanced = reinterpret_cast<PFNGLDRAWELEMENTSINSTANCEDPROC>(
      eglGetProcAddress("glDrawElementsInstanced"));
  glDrawRangeElements = reinterpret_cast<PFNGLDRAWRANGEELEMENTSPROC>(
      eglGetProcAddress("glDrawRangeElements"));
  glDetachShader = reinterpret_cast<PFNGLDETACHSHADERPROC>(
      eglGetProcAddress("glDetachShader"));
  glDisable =
      reinterpret_cast<PFNGLDISABLEPROC>(eglGetProcAddress("glDisable"));
  glDisableVertexAttribArray =
      reinterpret_cast<PFNGLDISABLEVERTEXATTRIBARRAYPROC>(
          eglGetProcAddress("glDisableVertexAttribArray"));
  glEnable = reinterpret_cast<PFNGLENABLEPROC>(eglGetProcAddress("glEnable"));
  glEnableVertexAttribArray =
      reinterpret_cast<PFNGLENABLEVERTEXATTRIBARRAYPROC>(
          eglGetProcAddress("glEnableVertexAttribArray"));
  glEndQuery =
      reinterpret_cast<PFNGLENDQUERYPROC>(eglGetProcAddress("glEndQuery"));
  glEndTransformFeedback = reinterpret_cast<PFNGLENDTRANSFORMFEEDBACKPROC>(
      eglGetProcAddress("glEndTransformFeedback"));
  glFenceSync =
      reinterpret_cast<PFNGLFENCESYNCPROC>(eglGetProcAddress("glFenceSync"));
  glFinish = reinterpret_cast<PFNGLFINISHPROC>(eglGetProcAddress("glFinish"));
  glFlush = reinterpret_cast<PFNGLFLUSHPROC>(eglGetProcAddress("glFlush"));
  glFramebufferRenderbuffer =
      reinterpret_cast<PFNGLFRAMEBUFFERRENDERBUFFERPROC>(
          eglGetProcAddress("glFramebufferRenderbuffer"));
  glFramebufferTexture2D = reinterpret_cast<PFNGLFRAMEBUFFERTEXTURE2DPROC>(
      eglGetProcAddress("glFramebufferTexture2D"));
  glFramebufferTextureLayer =
      reinterpret_cast<PFNGLFRAMEBUFFERTEXTURELAYERPROC>(
          eglGetProcAddress("glFramebufferTextureLayer"));
  glFrontFace =
      reinterpret_cast<PFNGLFRONTFACEPROC>(eglGetProcAddress("glFrontFace"));
  glGenerateMipmap = reinterpret_cast<PFNGLGENERATEMIPMAPPROC>(
      eglGetProcAddress("glGenerateMipmap"));
  glGenBuffers =
      reinterpret_cast<PFNGLGENBUFFERSPROC>(eglGetProcAddress("glGenBuffers"));
  glGenFramebuffers = reinterpret_cast<PFNGLGENFRAMEBUFFERSPROC>(
      eglGetProcAddress("glGenFramebuffers"));
  glGenQueries =
      reinterpret_cast<PFNGLGENQUERIESPROC>(eglGetProcAddress("glGenQueries"));
  glGenRenderbuffers = reinterpret_cast<PFNGLGENRENDERBUFFERSPROC>(
      eglGetProcAddress("glGenRenderbuffers"));
  glGenSamplers = reinterpret_cast<PFNGLGENSAMPLERSPROC>(
      eglGetProcAddress("glGenSamplers"));
  glGetAttribLocation = reinterpret_cast<PFNGLGETATTRIBLOCATIONPROC>(
      eglGetProcAddress("glGetAttribLocation"));
  glGetBufferParameteriv = reinterpret_cast<PFNGLGETBUFFERPARAMETERIVPROC>(
      eglGetProcAddress("glGetBufferParameteriv"));
  glGetError =
      reinterpret_cast<PFNGLGETERRORPROC>(eglGetProcAddress("glGetError"));
  glGetFramebufferAttachmentParameteriv =
      reinterpret_cast<PFNGLGETFRAMEBUFFERATTACHMENTPARAMETERIVPROC>(
          eglGetProcAddress("glGetFramebufferAttachmentParameteriv"));
  glGetIntegerv = reinterpret_cast<PFNGLGETINTEGERVPROC>(
      eglGetProcAddress("glGetIntegerv"));
  glGenTextures = reinterpret_cast<PFNGLGENTEXTURESPROC>(
      eglGetProcAddress("glGenTextures"));
  glGenTransformFeedbacks = reinterpret_cast<PFNGLGENTRANSFORMFEEDBACKSPROC>(
      eglGetProcAddress("glGenTransformFeedbacks"));
  glGenVertexArrays = reinterpret_cast<PFNGLGENVERTEXARRAYSPROC>(
      eglGetProcAddress("glGenVertexArrays"));
  glGetActiveAttrib = reinterpret_cast<PFNGLGETACTIVEATTRIBPROC>(
      eglGetProcAddress("glGetActiveAttrib"));
  glGetActiveUniform = reinterpret_cast<PFNGLGETACTIVEUNIFORMPROC>(
      eglGetProcAddress("glGetActiveUniform"));
  glGetActiveUniformBlockiv =
      reinterpret_cast<PFNGLGETACTIVEUNIFORMBLOCKIVPROC>(
          eglGetProcAddress("glGetActiveUniformBlockiv"));
  glGetActiveUniformBlockName =
      reinterpret_cast<PFNGLGETACTIVEUNIFORMBLOCKNAMEPROC>(
          eglGetProcAddress("glGetActiveUniformBlockName"));
  glGetActiveUniformsiv = reinterpret_cast<PFNGLGETACTIVEUNIFORMSIVPROC>(
      eglGetProcAddress("glGetActiveUniformsiv"));
  glGetAttachedShaders = reinterpret_cast<PFNGLGETATTACHEDSHADERSPROC>(
      eglGetProcAddress("glGetAttachedShaders"));
  glGetFragDataLocation = reinterpret_cast<PFNGLGETFRAGDATALOCATIONPROC>(
      eglGetProcAddress("glGetFragDataLocation"));
  glGetIntegeri_v = reinterpret_cast<PFNGLGETINTEGERI_VPROC>(
      eglGetProcAddress("glGetIntegeri_v"));
  glGetInternalformativ = reinterpret_cast<PFNGLGETINTERNALFORMATIVPROC>(
      eglGetProcAddress("glGetInternalformativ"));
  glGetProgramiv = reinterpret_cast<PFNGLGETPROGRAMIVPROC>(
      eglGetProcAddress("glGetProgramiv"));
  glGetProgramInfoLog = reinterpret_cast<PFNGLGETPROGRAMINFOLOGPROC>(
      eglGetProcAddress("glGetProgramInfoLog"));
  glGetQueryiv =
      reinterpret_cast<PFNGLGETQUERYIVPROC>(eglGetProcAddress("glGetQueryiv"));
  glGetQueryObjectuiv = reinterpret_cast<PFNGLGETQUERYOBJECTUIVPROC>(
      eglGetProcAddress("glGetQueryObjectuiv"));
  glGetRenderbufferParameteriv =
      reinterpret_cast<PFNGLGETRENDERBUFFERPARAMETERIVPROC>(
          eglGetProcAddress("glGetRenderbufferParameteriv"));
  glGetSamplerParameterfv = reinterpret_cast<PFNGLGETSAMPLERPARAMETERFVPROC>(
      eglGetProcAddress("glGetSamplerParameterfv"));
  glGetSamplerParameteriv = reinterpret_cast<PFNGLGETSAMPLERPARAMETERIVPROC>(
      eglGetProcAddress("glGetSamplerParameteriv"));
  glGetShaderiv = reinterpret_cast<PFNGLGETSHADERIVPROC>(
      eglGetProcAddress("glGetShaderiv"));
  glGetShaderInfoLog = reinterpret_cast<PFNGLGETSHADERINFOLOGPROC>(
      eglGetProcAddress("glGetShaderInfoLog"));
  glGetShaderPrecisionFormat =
      reinterpret_cast<PFNGLGETSHADERPRECISIONFORMATPROC>(
          eglGetProcAddress("glGetShaderPrecisionFormat"));
  glGetShaderSource = reinterpret_cast<PFNGLGETSHADERSOURCEPROC>(
      eglGetProcAddress("glGetShaderSource"));
  glGetString =
      reinterpret_cast<PFNGLGETSTRINGPROC>(eglGetProcAddress("glGetString"));
  glGetSynciv =
      reinterpret_cast<PFNGLGETSYNCIVPROC>(eglGetProcAddress("glGetSynciv"));
  glGetTexParameterfv = reinterpret_cast<PFNGLGETTEXPARAMETERFVPROC>(
      eglGetProcAddress("glGetTexParameterfv"));
  glGetTexParameteriv = reinterpret_cast<PFNGLGETTEXPARAMETERIVPROC>(
      eglGetProcAddress("glGetTexParameteriv"));
  glGetTransformFeedbackVarying =
      reinterpret_cast<PFNGLGETTRANSFORMFEEDBACKVARYINGPROC>(
          eglGetProcAddress("glGetTransformFeedbackVarying"));
  glGetUniformfv = reinterpret_cast<PFNGLGETUNIFORMFVPROC>(
      eglGetProcAddress("glGetUniformfv"));
  glGetUniformiv = reinterpret_cast<PFNGLGETUNIFORMIVPROC>(
      eglGetProcAddress("glGetUniformiv"));
  glGetUniformuiv = reinterpret_cast<PFNGLGETUNIFORMUIVPROC>(
      eglGetProcAddress("glGetUniformuiv"));
  glGetUniformBlockIndex = reinterpret_cast<PFNGLGETUNIFORMBLOCKINDEXPROC>(
      eglGetProcAddress("glGetUniformBlockIndex"));
  glGetUniformIndices = reinterpret_cast<PFNGLGETUNIFORMINDICESPROC>(
      eglGetProcAddress("glGetUniformIndices"));
  glGetUniformLocation = reinterpret_cast<PFNGLGETUNIFORMLOCATIONPROC>(
      eglGetProcAddress("glGetUniformLocation"));
  glGetVertexAttribfv = reinterpret_cast<PFNGLGETVERTEXATTRIBFVPROC>(
      eglGetProcAddress("glGetVertexAttribfv"));
  glGetVertexAttribIiv = reinterpret_cast<PFNGLGETVERTEXATTRIBIIVPROC>(
      eglGetProcAddress("glGetVertexAttribIiv"));
  glGetVertexAttribIuiv = reinterpret_cast<PFNGLGETVERTEXATTRIBIUIVPROC>(
      eglGetProcAddress("glGetVertexAttribIuiv"));
  glGetVertexAttribiv = reinterpret_cast<PFNGLGETVERTEXATTRIBIVPROC>(
      eglGetProcAddress("glGetVertexAttribiv"));
  glGetVertexAttribPointerv =
      reinterpret_cast<PFNGLGETVERTEXATTRIBPOINTERVPROC>(
          eglGetProcAddress("glGetVertexAttribPointerv"));
  glHint = reinterpret_cast<PFNGLHINTPROC>(eglGetProcAddress("glHint"));
  glIsBuffer =
      reinterpret_cast<PFNGLISBUFFERPROC>(eglGetProcAddress("glIsBuffer"));
  glIsEnabled =
      reinterpret_cast<PFNGLISENABLEDPROC>(eglGetProcAddress("glIsEnabled"));
  glIsFramebuffer = reinterpret_cast<PFNGLISFRAMEBUFFERPROC>(
      eglGetProcAddress("glIsFramebuffer"));
  glIsProgram =
      reinterpret_cast<PFNGLISPROGRAMPROC>(eglGetProcAddress("glIsProgram"));
  glIsQuery =
      reinterpret_cast<PFNGLISQUERYPROC>(eglGetProcAddress("glIsQuery"));
  glIsRenderbuffer = reinterpret_cast<PFNGLISRENDERBUFFERPROC>(
      eglGetProcAddress("glIsRenderbuffer"));
  glIsSampler =
      reinterpret_cast<PFNGLISSAMPLERPROC>(eglGetProcAddress("glIsSampler"));
  glIsShader =
      reinterpret_cast<PFNGLISSHADERPROC>(eglGetProcAddress("glIsShader"));
  glIsSync = reinterpret_cast<PFNGLISSYNCPROC>(eglGetProcAddress("glIsSync"));
  glIsTexture =
      reinterpret_cast<PFNGLISTEXTUREPROC>(eglGetProcAddress("glIsTexture"));
  glIsTransformFeedback = reinterpret_cast<PFNGLISTRANSFORMFEEDBACKPROC>(
      eglGetProcAddress("glIsTransformFeedback"));
  glIsVertexArray = reinterpret_cast<PFNGLISVERTEXARRAYPROC>(
      eglGetProcAddress("glIsVertexArray"));
  glInvalidateFramebuffer = reinterpret_cast<PFNGLINVALIDATEFRAMEBUFFERPROC>(
      eglGetProcAddress("glInvalidateFramebuffer"));
  glInvalidateSubFramebuffer =
      reinterpret_cast<PFNGLINVALIDATESUBFRAMEBUFFERPROC>(
          eglGetProcAddress("glInvalidateSubFramebuffer"));
  glLineWidth =
      reinterpret_cast<PFNGLLINEWIDTHPROC>(eglGetProcAddress("glLineWidth"));
  glLinkProgram = reinterpret_cast<PFNGLLINKPROGRAMPROC>(
      eglGetProcAddress("glLinkProgram"));
  glMapBufferRange = reinterpret_cast<PFNGLMAPBUFFERRANGEPROC>(
      eglGetProcAddress("glMapBufferRange"));
  glPauseTransformFeedback = reinterpret_cast<PFNGLPAUSETRANSFORMFEEDBACKPROC>(
      eglGetProcAddress("glPauseTransformFeedback"));
  glPixelStorei = reinterpret_cast<PFNGLPIXELSTOREIPROC>(
      eglGetProcAddress("glPixelStorei"));
  glPolygonOffset = reinterpret_cast<PFNGLPOLYGONOFFSETPROC>(
      eglGetProcAddress("glPolygonOffset"));
  glReadPixels =
      reinterpret_cast<PFNGLREADPIXELSPROC>(eglGetProcAddress("glReadPixels"));
  glReadBuffer =
      reinterpret_cast<PFNGLREADBUFFERPROC>(eglGetProcAddress("glReadBuffer"));
  glRenderbufferStorage = reinterpret_cast<PFNGLRENDERBUFFERSTORAGEPROC>(
      eglGetProcAddress("glRenderbufferStorage"));
  glRenderbufferStorageMultisample =
      reinterpret_cast<PFNGLRENDERBUFFERSTORAGEMULTISAMPLEPROC>(
          eglGetProcAddress("glRenderbufferStorageMultisample"));
  glResumeTransformFeedback =
      reinterpret_cast<PFNGLRESUMETRANSFORMFEEDBACKPROC>(
          eglGetProcAddress("glResumeTransformFeedback"));
  glSampleCoverage = reinterpret_cast<PFNGLSAMPLECOVERAGEPROC>(
      eglGetProcAddress("glSampleCoverage"));
  glSamplerParameterf = reinterpret_cast<PFNGLSAMPLERPARAMETERFPROC>(
      eglGetProcAddress("glSamplerParameterf"));
  glSamplerParameteri = reinterpret_cast<PFNGLSAMPLERPARAMETERIPROC>(
      eglGetProcAddress("glSamplerParameteri"));
  glScissor =
      reinterpret_cast<PFNGLSCISSORPROC>(eglGetProcAddress("glScissor"));
  glShaderSource = reinterpret_cast<PFNGLSHADERSOURCEPROC>(
      eglGetProcAddress("glShaderSource"));
  glStencilMask = reinterpret_cast<PFNGLSTENCILMASKPROC>(
      eglGetProcAddress("glStencilMask"));
  glStencilMaskSeparate = reinterpret_cast<PFNGLSTENCILMASKSEPARATEPROC>(
      eglGetProcAddress("glStencilMaskSeparate"));
  glStencilFunc = reinterpret_cast<PFNGLSTENCILFUNCPROC>(
      eglGetProcAddress("glStencilFunc"));
  glStencilFuncSeparate = reinterpret_cast<PFNGLSTENCILFUNCSEPARATEPROC>(
      eglGetProcAddress("glStencilFuncSeparate"));
  glStencilOp =
      reinterpret_cast<PFNGLSTENCILOPPROC>(eglGetProcAddress("glStencilOp"));
  glStencilOpSeparate = reinterpret_cast<PFNGLSTENCILOPSEPARATEPROC>(
      eglGetProcAddress("glStencilOpSeparate"));
  glTexImage2D =
      reinterpret_cast<PFNGLTEXIMAGE2DPROC>(eglGetProcAddress("glTexImage2D"));
  glTexImage3D =
      reinterpret_cast<PFNGLTEXIMAGE3DPROC>(eglGetProcAddress("glTexImage3D"));
  glTexParameteri = reinterpret_cast<PFNGLTEXPARAMETERIPROC>(
      eglGetProcAddress("glTexParameteri"));
  glTexParameterf = reinterpret_cast<PFNGLTEXPARAMETERFPROC>(
      eglGetProcAddress("glTexParameterf"));
  glTexStorage2D = reinterpret_cast<PFNGLTEXSTORAGE2DPROC>(
      eglGetProcAddress("glTexStorage2D"));
  glTexStorage3D = reinterpret_cast<PFNGLTEXSTORAGE3DPROC>(
      eglGetProcAddress("glTexStorage3D"));
  glTexSubImage2D = reinterpret_cast<PFNGLTEXSUBIMAGE2DPROC>(
      eglGetProcAddress("glTexSubImage2D"));
  glTexSubImage3D = reinterpret_cast<PFNGLTEXSUBIMAGE3DPROC>(
      eglGetProcAddress("glTexSubImage3D"));
  glTransformFeedbackVaryings =
      reinterpret_cast<PFNGLTRANSFORMFEEDBACKVARYINGSPROC>(
          eglGetProcAddress("glTransformFeedbackVaryings"));
  glUniform1f =
      reinterpret_cast<PFNGLUNIFORM1FPROC>(eglGetProcAddress("glUniform1f"));
  glUniform1fv =
      reinterpret_cast<PFNGLUNIFORM1FVPROC>(eglGetProcAddress("glUniform1fv"));
  glUniform1i =
      reinterpret_cast<PFNGLUNIFORM1IPROC>(eglGetProcAddress("glUniform1i"));
  glUniform1iv =
      reinterpret_cast<PFNGLUNIFORM1IVPROC>(eglGetProcAddress("glUniform1iv"));
  glUniform1ui =
      reinterpret_cast<PFNGLUNIFORM1UIPROC>(eglGetProcAddress("glUniform1ui"));
  glUniform1uiv = reinterpret_cast<PFNGLUNIFORM1UIVPROC>(
      eglGetProcAddress("glUniform1uiv"));
  glUniform2f =
      reinterpret_cast<PFNGLUNIFORM2FPROC>(eglGetProcAddress("glUniform2f"));
  glUniform2fv =
      reinterpret_cast<PFNGLUNIFORM2FVPROC>(eglGetProcAddress("glUniform2fv"));
  glUniform2i =
      reinterpret_cast<PFNGLUNIFORM2IPROC>(eglGetProcAddress("glUniform2i"));
  glUniform2iv =
      reinterpret_cast<PFNGLUNIFORM2IVPROC>(eglGetProcAddress("glUniform2iv"));
  glUniform2ui =
      reinterpret_cast<PFNGLUNIFORM2UIPROC>(eglGetProcAddress("glUniform2ui"));
  glUniform2uiv = reinterpret_cast<PFNGLUNIFORM2UIVPROC>(
      eglGetProcAddress("glUniform2uiv"));
  glUniform3f =
      reinterpret_cast<PFNGLUNIFORM3FPROC>(eglGetProcAddress("glUniform3f"));
  glUniform3fv =
      reinterpret_cast<PFNGLUNIFORM3FVPROC>(eglGetProcAddress("glUniform3fv"));
  glUniform3i =
      reinterpret_cast<PFNGLUNIFORM3IPROC>(eglGetProcAddress("glUniform3i"));
  glUniform3iv =
      reinterpret_cast<PFNGLUNIFORM3IVPROC>(eglGetProcAddress("glUniform3iv"));
  glUniform3ui =
      reinterpret_cast<PFNGLUNIFORM3UIPROC>(eglGetProcAddress("glUniform3ui"));
  glUniform3uiv = reinterpret_cast<PFNGLUNIFORM3UIVPROC>(
      eglGetProcAddress("glUniform3uiv"));
  glUniform4f =
      reinterpret_cast<PFNGLUNIFORM4FPROC>(eglGetProcAddress("glUniform4f"));
  glUniform4fv =
      reinterpret_cast<PFNGLUNIFORM4FVPROC>(eglGetProcAddress("glUniform4fv"));
  glUniform4i =
      reinterpret_cast<PFNGLUNIFORM4IPROC>(eglGetProcAddress("glUniform4i"));
  glUniform4iv =
      reinterpret_cast<PFNGLUNIFORM4IVPROC>(eglGetProcAddress("glUniform4iv"));
  glUniform4ui =
      reinterpret_cast<PFNGLUNIFORM4UIPROC>(eglGetProcAddress("glUniform4ui"));
  glUniform4uiv = reinterpret_cast<PFNGLUNIFORM4UIVPROC>(
      eglGetProcAddress("glUniform4uiv"));
  glUniformBlockBinding = reinterpret_cast<PFNGLUNIFORMBLOCKBINDINGPROC>(
      eglGetProcAddress("glUniformBlockBinding"));
  glUniformMatrix2fv = reinterpret_cast<PFNGLUNIFORMMATRIX2FVPROC>(
      eglGetProcAddress("glUniformMatrix2fv"));
  glUniformMatrix2x3fv = reinterpret_cast<PFNGLUNIFORMMATRIX2X3FVPROC>(
      eglGetProcAddress("glUniformMatrix2x3fv"));
  glUniformMatrix2x4fv = reinterpret_cast<PFNGLUNIFORMMATRIX2X4FVPROC>(
      eglGetProcAddress("glUniformMatrix2x4fv"));
  glUniformMatrix3fv = reinterpret_cast<PFNGLUNIFORMMATRIX3FVPROC>(
      eglGetProcAddress("glUniformMatrix3fv"));
  glUniformMatrix3x2fv = reinterpret_cast<PFNGLUNIFORMMATRIX3X2FVPROC>(
      eglGetProcAddress("glUniformMatrix3x2fv"));
  glUniformMatrix3x4fv = reinterpret_cast<PFNGLUNIFORMMATRIX3X4FVPROC>(
      eglGetProcAddress("glUniformMatrix3x4fv"));
  glUniformMatrix4fv = reinterpret_cast<PFNGLUNIFORMMATRIX4FVPROC>(
      eglGetProcAddress("glUniformMatrix4fv"));
  glUniformMatrix4x2fv = reinterpret_cast<PFNGLUNIFORMMATRIX4X2FVPROC>(
      eglGetProcAddress("glUniformMatrix4x2fv"));
  glUniformMatrix4x3fv = reinterpret_cast<PFNGLUNIFORMMATRIX4X3FVPROC>(
      eglGetProcAddress("glUniformMatrix4x3fv"));
  glUnmapBuffer = reinterpret_cast<PFNGLUNMAPBUFFERPROC>(
      eglGetProcAddress("glUnmapBuffer"));
  glUseProgram =
      reinterpret_cast<PFNGLUSEPROGRAMPROC>(eglGetProcAddress("glUseProgram"));
  glValidateProgram = reinterpret_cast<PFNGLVALIDATEPROGRAMPROC>(
      eglGetProcAddress("glValidateProgram"));
  glVertexAttrib1f = reinterpret_cast<PFNGLVERTEXATTRIB1FPROC>(
      eglGetProcAddress("glVertexAttrib1f"));
  glVertexAttrib1fv = reinterpret_cast<PFNGLVERTEXATTRIB1FVPROC>(
      eglGetProcAddress("glVertexAttrib1fv"));
  glVertexAttrib2f = reinterpret_cast<PFNGLVERTEXATTRIB2FPROC>(
      eglGetProcAddress("glVertexAttrib2f"));
  glVertexAttrib2fv = reinterpret_cast<PFNGLVERTEXATTRIB2FVPROC>(
      eglGetProcAddress("glVertexAttrib2fv"));
  glVertexAttrib3f = reinterpret_cast<PFNGLVERTEXATTRIB3FPROC>(
      eglGetProcAddress("glVertexAttrib3f"));
  glVertexAttrib3fv = reinterpret_cast<PFNGLVERTEXATTRIB3FVPROC>(
      eglGetProcAddress("glVertexAttrib3fv"));
  glVertexAttrib4f = reinterpret_cast<PFNGLVERTEXATTRIB4FPROC>(
      eglGetProcAddress("glVertexAttrib4f"));
  glVertexAttrib4fv = reinterpret_cast<PFNGLVERTEXATTRIB4FVPROC>(
      eglGetProcAddress("glVertexAttrib4fv"));
  glVertexAttribDivisor = reinterpret_cast<PFNGLVERTEXATTRIBDIVISORPROC>(
      eglGetProcAddress("glVertexAttribDivisor"));
  glVertexAttribI4i = reinterpret_cast<PFNGLVERTEXATTRIBI4IPROC>(
      eglGetProcAddress("glVertexAttribI4i"));
  glVertexAttribI4iv = reinterpret_cast<PFNGLVERTEXATTRIBI4IVPROC>(
      eglGetProcAddress("glVertexAttribI4iv"));
  glVertexAttribI4ui = reinterpret_cast<PFNGLVERTEXATTRIBI4UIPROC>(
      eglGetProcAddress("glVertexAttribI4ui"));
  glVertexAttribI4uiv = reinterpret_cast<PFNGLVERTEXATTRIBI4UIVPROC>(
      eglGetProcAddress("glVertexAttribI4uiv"));
  glVertexAttribIPointer = reinterpret_cast<PFNGLVERTEXATTRIBIPOINTERPROC>(
      eglGetProcAddress("glVertexAttribIPointer"));
  glVertexAttribPointer = reinterpret_cast<PFNGLVERTEXATTRIBPOINTERPROC>(
      eglGetProcAddress("glVertexAttribPointer"));
  glViewport =
      reinterpret_cast<PFNGLVIEWPORTPROC>(eglGetProcAddress("glViewport"));
  glWaitSync =
      reinterpret_cast<PFNGLWAITSYNCPROC>(eglGetProcAddress("glWaitSync"));

  // ANGLE specific
  glRequestExtensionANGLE = reinterpret_cast<PFNGLREQUESTEXTENSIONANGLEPROC>(
      eglGetProcAddress("glRequestExtensionANGLE"));
}

void EGLContextWrapper::RefreshGLVersion() {
  client_major_es_version = 0;
  client_minor_es_version = 0;
  if (!glGetString) {
    return;
  }

  const GLubyte* version_bytes = glGetString(GL_VERSION);
  if (!version_bytes) {
    return;
  }

  const char* version = reinterpret_cast<const char*>(version_bytes);
  for (const char* cursor = version; *cursor; ++cursor) {
    if (*cursor < '0' || *cursor > '9') {
      continue;
    }

    unsigned int major = 0;
    unsigned int minor = 0;
    const int parsed = std::sscanf(cursor, "%u.%u", &major, &minor);
    if (parsed >= 1) {
      client_major_es_version = major;
      client_minor_es_version = parsed >= 2 ? minor : 0;
      return;
    }
  }
}

void EGLContextWrapper::RefreshGLExtensions() {
  gl_extensions = std::unique_ptr<GLExtensionsWrapper>(new GLExtensionsWrapper(
      reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS))));

  angle_requestable_extensions = std::unique_ptr<GLExtensionsWrapper>(
      new GLExtensionsWrapper(reinterpret_cast<const char*>(
          glGetString(GL_REQUESTABLE_EXTENSIONS_ANGLE))));
}

EGLContextWrapper::~EGLContextWrapper() {
  if (display != EGL_NO_DISPLAY) {
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  }

  if (surface != EGL_NO_SURFACE) {
    if (!eglDestroySurface(display, surface)) {
      std::cerr << "Failed to delete EGL surface: " << std::endl;
    }
    surface = EGL_NO_SURFACE;
  }

  if (context != EGL_NO_CONTEXT) {
    if (!eglDestroyContext(display, context)) {
      std::cerr << "Failed to delete EGL context: " << std::endl;
    }
    context = EGL_NO_CONTEXT;
  }

  if (display != EGL_NO_DISPLAY) {
    eglTerminate(display);
    display = EGL_NO_DISPLAY;
  }

  // TODO(kreeger): Close context attributes.
  // TODO(kreeger): Cleanup global objects.
}

EGLContextWrapper* EGLContextWrapper::Create(
    napi_env env, const GLContextOptions& context_options) {
  return new EGLContextWrapper(env, context_options);
}

}  // namespace nodejsgl
