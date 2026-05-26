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

#ifndef NODEJS_GL_EGL_CONTEXT_WRAPPER_H_
#define NODEJS_GL_EGL_CONTEXT_WRAPPER_H_

#include <node_api.h>

// Use generated EGL includes from ANGLE:
#define EGL_EGL_PROTOTYPES 1

#include "angle/include/EGL/egl.h"
#include "angle/include/GLES2/gl2.h"
#include "angle/include/GLES2/gl2ext.h"
#include "angle/include/GLES3/gl3.h"

#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace nodejsgl {

// Provides initialization of EGL/GL context options.
struct GLContextOptions {
  bool webgl_compatibility = false;
  uint32_t client_major_es_version = 3;
  uint32_t client_minor_es_version = 0;
  uint32_t width = 1;
  uint32_t height = 1;
};

// Provides lookup of EGL/GL extensions.
class GLExtensionsWrapper {
 public:
  GLExtensionsWrapper(const char* extensions_str)
      : extensions_(extensions_str ? extensions_str : "") {}

  bool HasExtension(const char* name) {
    return extensions_.find(name) != std::string::npos;
  }

  const char* GetExtensions() { return extensions_.c_str(); }

#if DEBUG
  void LogExtensions() {
    std::string s(extensions_);
    std::string delim = " ";
    size_t pos = 0;
    std::string token;
    while ((pos = s.find(delim)) != std::string::npos) {
      token = s.substr(0, pos);
      s.erase(0, pos + delim.length());
      std::cerr << token << std::endl;
    }
  }
#endif

 private:
  std::string extensions_;
};

// Wraps an EGLContext instance for off screen usage.
class EGLContextWrapper {
 public:
  ~EGLContextWrapper();

  // Creates and in
  static EGLContextWrapper* Create(napi_env env,
                                   const GLContextOptions& context_options);

  bool ResizeSurface(napi_env env, uint32_t width, uint32_t height);
  void Destroy();
  bool IsCurrent() const;
  bool MakeCurrent() const;
  void FlushPendingSyncDeletes();

  // GLsync objects that could not be deleted immediately because the owner
  // context could not be rebound. Flushed during Destroy().
  std::vector<GLsync> pending_sync_deletes;

  EGLContext context;
  EGLDisplay display;
  EGLConfig config;
  EGLSurface surface;
  uint32_t drawing_buffer_width;
  uint32_t drawing_buffer_height;
  GLenum drawing_buffer_format;
  uint32_t client_major_es_version;
  uint32_t client_minor_es_version;

  std::unique_ptr<GLExtensionsWrapper> egl_extensions;
  std::unique_ptr<GLExtensionsWrapper> gl_extensions;
  std::unique_ptr<GLExtensionsWrapper> angle_requestable_extensions;

  // Function pointers
  PFNGLACTIVETEXTUREPROC glActiveTexture;
  PFNGLATTACHSHADERPROC glAttachShader;
  PFNGLBINDATTRIBLOCATIONPROC glBindAttribLocation;
  PFNGLBINDBUFFERPROC glBindBuffer;
  PFNGLBINDBUFFERBASEPROC glBindBufferBase;
  PFNGLBINDBUFFERRANGEPROC glBindBufferRange;
  PFNGLBINDFRAMEBUFFERPROC glBindFramebuffer;
  PFNGLBINDRENDERBUFFERPROC glBindRenderbuffer;
  PFNGLBINDSAMPLERPROC glBindSampler;
  PFNGLBINDTEXTUREPROC glBindTexture;
  PFNGLBINDTRANSFORMFEEDBACKPROC glBindTransformFeedback;
  PFNGLBINDVERTEXARRAYPROC glBindVertexArray;
  PFNGLBEGINQUERYPROC glBeginQuery;
  PFNGLBEGINTRANSFORMFEEDBACKPROC glBeginTransformFeedback;
  PFNGLBLITFRAMEBUFFERPROC glBlitFramebuffer;
  PFNGLBLENDCOLORPROC glBlendColor;
  PFNGLBLENDEQUATIONPROC glBlendEquation;
  PFNGLBLENDEQUATIONSEPARATEPROC glBlendEquationSeparate;
  PFNGLBLENDFUNCPROC glBlendFunc;
  PFNGLBLENDFUNCSEPARATEPROC glBlendFuncSeparate;
  PFNGLBUFFERDATAPROC glBufferData;
  PFNGLBUFFERSUBDATAPROC glBufferSubData;
  PFNGLCHECKFRAMEBUFFERSTATUSPROC glCheckFramebufferStatus;
  PFNGLCLEARPROC glClear;
  PFNGLCLEARBUFFERFIPROC glClearBufferfi;
  PFNGLCLEARBUFFERFVPROC glClearBufferfv;
  PFNGLCLEARBUFFERIVPROC glClearBufferiv;
  PFNGLCLEARBUFFERUIVPROC glClearBufferuiv;
  PFNGLCLEARCOLORPROC glClearColor;
  PFNGLCLEARDEPTHFPROC glClearDepthf;
  PFNGLCLEARSTENCILPROC glClearStencil;
  PFNGLCLIENTWAITSYNCPROC glClientWaitSync;
  PFNGLCOLORMASKPROC glColorMask;
  PFNGLCOMPILESHADERPROC glCompileShader;
  PFNGLCOMPRESSEDTEXIMAGE2DPROC glCompressedTexImage2D;
  PFNGLCOMPRESSEDTEXIMAGE3DPROC glCompressedTexImage3D;
  PFNGLCOMPRESSEDTEXSUBIMAGE2DPROC glCompressedTexSubImage2D;
  PFNGLCOMPRESSEDTEXSUBIMAGE3DPROC glCompressedTexSubImage3D;
  PFNGLCOPYBUFFERSUBDATAPROC glCopyBufferSubData;
  PFNGLCOPYTEXIMAGE2DPROC glCopyTexImage2D;
  PFNGLCOPYTEXSUBIMAGE2DPROC glCopyTexSubImage2D;
  PFNGLCOPYTEXSUBIMAGE3DPROC glCopyTexSubImage3D;
  PFNGLCREATEPROGRAMPROC glCreateProgram;
  PFNGLCREATESHADERPROC glCreateShader;
  PFNGLCULLFACEPROC glCullFace;
  PFNGLDELETEBUFFERSPROC glDeleteBuffers;
  PFNGLDELETEFRAMEBUFFERSPROC glDeleteFramebuffers;
  PFNGLDELETEPROGRAMPROC glDeleteProgram;
  PFNGLDELETEQUERIESPROC glDeleteQueries;
  PFNGLDELETERENDERBUFFERSPROC glDeleteRenderbuffers;
  PFNGLDELETESAMPLERSPROC glDeleteSamplers;
  PFNGLDELETESHADERPROC glDeleteShader;
  PFNGLDELETESYNCPROC glDeleteSync;
  PFNGLDELETETEXTURESPROC glDeleteTextures;
  PFNGLDELETETRANSFORMFEEDBACKSPROC glDeleteTransformFeedbacks;
  PFNGLDELETEVERTEXARRAYSPROC glDeleteVertexArrays;
  PFNGLDEPTHFUNCPROC glDepthFunc;
  PFNGLDEPTHMASKPROC glDepthMask;
  PFNGLDEPTHRANGEFPROC glDepthRangef;
  PFNGLDETACHSHADERPROC glDetachShader;
  PFNGLDISABLEPROC glDisable;
  PFNGLDISABLEVERTEXATTRIBARRAYPROC glDisableVertexAttribArray;
  PFNGLDRAWARRAYSPROC glDrawArrays;
  PFNGLDRAWARRAYSINSTANCEDPROC glDrawArraysInstanced;
  PFNGLDRAWBUFFERSPROC glDrawBuffers;
  PFNGLDRAWELEMENTSPROC glDrawElements;
  PFNGLDRAWELEMENTSINSTANCEDPROC glDrawElementsInstanced;
  PFNGLDRAWRANGEELEMENTSPROC glDrawRangeElements;
  PFNGLENABLEPROC glEnable;
  PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray;
  PFNGLENDQUERYPROC glEndQuery;
  PFNGLENDTRANSFORMFEEDBACKPROC glEndTransformFeedback;
  PFNGLFENCESYNCPROC glFenceSync;
  PFNGLFINISHPROC glFinish;
  PFNGLFLUSHPROC glFlush;
  PFNGLFRAMEBUFFERRENDERBUFFERPROC glFramebufferRenderbuffer;
  PFNGLFRAMEBUFFERTEXTURE2DPROC glFramebufferTexture2D;
  PFNGLFRAMEBUFFERTEXTURELAYERPROC glFramebufferTextureLayer;
  PFNGLFRONTFACEPROC glFrontFace;
  PFNGLGENERATEMIPMAPPROC glGenerateMipmap;
  PFNGLGENBUFFERSPROC glGenBuffers;
  PFNGLGENFRAMEBUFFERSPROC glGenFramebuffers;
  PFNGLGENQUERIESPROC glGenQueries;
  PFNGLGENRENDERBUFFERSPROC glGenRenderbuffers;
  PFNGLGENSAMPLERSPROC glGenSamplers;
  PFNGLGENTEXTURESPROC glGenTextures;
  PFNGLGENTRANSFORMFEEDBACKSPROC glGenTransformFeedbacks;
  PFNGLGENVERTEXARRAYSPROC glGenVertexArrays;
  PFNGLGETACTIVEATTRIBPROC glGetActiveAttrib;
  PFNGLGETACTIVEUNIFORMPROC glGetActiveUniform;
  PFNGLGETACTIVEUNIFORMBLOCKIVPROC glGetActiveUniformBlockiv;
  PFNGLGETACTIVEUNIFORMBLOCKNAMEPROC glGetActiveUniformBlockName;
  PFNGLGETACTIVEUNIFORMSIVPROC glGetActiveUniformsiv;
  PFNGLGETATTACHEDSHADERSPROC glGetAttachedShaders;
  PFNGLGETATTRIBLOCATIONPROC glGetAttribLocation;
  PFNGLGETBUFFERPARAMETERIVPROC glGetBufferParameteriv;
  /* PFNGLGETBUFFERSUBDATAPROC glGetBufferSubData; */
  PFNGLGETERRORPROC glGetError;
  PFNGLGETFRAMEBUFFERATTACHMENTPARAMETERIVPROC
  glGetFramebufferAttachmentParameteriv;
  PFNGLGETFRAGDATALOCATIONPROC glGetFragDataLocation;
  PFNGLGETINTEGERI_VPROC glGetIntegeri_v;
  PFNGLGETINTEGERVPROC glGetIntegerv;
  PFNGLGETINTERNALFORMATIVPROC glGetInternalformativ;
  PFNGLGETPROGRAMIVPROC glGetProgramiv;
  PFNGLGETPROGRAMINFOLOGPROC glGetProgramInfoLog;
  PFNGLGETQUERYIVPROC glGetQueryiv;
  PFNGLGETQUERYOBJECTUIVPROC glGetQueryObjectuiv;
  PFNGLGETRENDERBUFFERPARAMETERIVPROC glGetRenderbufferParameteriv;
  PFNGLGETSAMPLERPARAMETERFVPROC glGetSamplerParameterfv;
  PFNGLGETSAMPLERPARAMETERIVPROC glGetSamplerParameteriv;
  PFNGLGETSHADERIVPROC glGetShaderiv;
  PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog;
  PFNGLGETSHADERPRECISIONFORMATPROC glGetShaderPrecisionFormat;
  PFNGLGETSHADERSOURCEPROC glGetShaderSource;
  PFNGLGETSTRINGPROC glGetString;
  PFNGLGETSYNCIVPROC glGetSynciv;
  PFNGLGETTEXPARAMETERFVPROC glGetTexParameterfv;
  PFNGLGETTEXPARAMETERIVPROC glGetTexParameteriv;
  PFNGLGETTRANSFORMFEEDBACKVARYINGPROC glGetTransformFeedbackVarying;
  PFNGLGETUNIFORMFVPROC glGetUniformfv;
  PFNGLGETUNIFORMIVPROC glGetUniformiv;
  PFNGLGETUNIFORMUIVPROC glGetUniformuiv;
  PFNGLGETUNIFORMBLOCKINDEXPROC glGetUniformBlockIndex;
  PFNGLGETUNIFORMINDICESPROC glGetUniformIndices;
  PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation;
  PFNGLGETVERTEXATTRIBFVPROC glGetVertexAttribfv;
  PFNGLGETVERTEXATTRIBIIVPROC glGetVertexAttribIiv;
  PFNGLGETVERTEXATTRIBIUIVPROC glGetVertexAttribIuiv;
  PFNGLGETVERTEXATTRIBIVPROC glGetVertexAttribiv;
  PFNGLGETVERTEXATTRIBPOINTERVPROC glGetVertexAttribPointerv;
  PFNGLHINTPROC glHint;
  PFNGLISBUFFERPROC glIsBuffer;
  PFNGLISENABLEDPROC glIsEnabled;
  PFNGLISFRAMEBUFFERPROC glIsFramebuffer;
  PFNGLISPROGRAMPROC glIsProgram;
  PFNGLISQUERYPROC glIsQuery;
  PFNGLISRENDERBUFFERPROC glIsRenderbuffer;
  PFNGLISSAMPLERPROC glIsSampler;
  PFNGLISSHADERPROC glIsShader;
  PFNGLISSYNCPROC glIsSync;
  PFNGLISTEXTUREPROC glIsTexture;
  PFNGLISTRANSFORMFEEDBACKPROC glIsTransformFeedback;
  PFNGLISVERTEXARRAYPROC glIsVertexArray;
  PFNGLINVALIDATEFRAMEBUFFERPROC glInvalidateFramebuffer;
  PFNGLINVALIDATESUBFRAMEBUFFERPROC glInvalidateSubFramebuffer;
  PFNGLLINEWIDTHPROC glLineWidth;
  PFNGLLINKPROGRAMPROC glLinkProgram;
  PFNGLMAPBUFFERRANGEPROC glMapBufferRange;
  PFNGLPAUSETRANSFORMFEEDBACKPROC glPauseTransformFeedback;
  PFNGLPIXELSTOREIPROC glPixelStorei;
  PFNGLPOLYGONOFFSETPROC glPolygonOffset;
  PFNGLREADPIXELSPROC glReadPixels;
  PFNGLREADBUFFERPROC glReadBuffer;
  PFNGLRENDERBUFFERSTORAGEPROC glRenderbufferStorage;
  PFNGLRENDERBUFFERSTORAGEMULTISAMPLEPROC glRenderbufferStorageMultisample;
  PFNGLRESUMETRANSFORMFEEDBACKPROC glResumeTransformFeedback;
  PFNGLSAMPLECOVERAGEPROC glSampleCoverage;
  PFNGLSAMPLERPARAMETERFPROC glSamplerParameterf;
  PFNGLSAMPLERPARAMETERIPROC glSamplerParameteri;
  PFNGLSCISSORPROC glScissor;
  PFNGLSHADERSOURCEPROC glShaderSource;
  PFNGLSTENCILFUNCPROC glStencilFunc;
  PFNGLSTENCILFUNCSEPARATEPROC glStencilFuncSeparate;
  PFNGLSTENCILMASKPROC glStencilMask;
  PFNGLSTENCILMASKSEPARATEPROC glStencilMaskSeparate;
  PFNGLSTENCILOPPROC glStencilOp;
  PFNGLSTENCILOPSEPARATEPROC glStencilOpSeparate;
  PFNGLTEXIMAGE2DPROC glTexImage2D;
  PFNGLTEXIMAGE3DPROC glTexImage3D;
  PFNGLTEXPARAMETERIPROC glTexParameteri;
  PFNGLTEXPARAMETERFPROC glTexParameterf;
  PFNGLTEXSTORAGE2DPROC glTexStorage2D;
  PFNGLTEXSTORAGE3DPROC glTexStorage3D;
  PFNGLTEXSUBIMAGE2DPROC glTexSubImage2D;
  PFNGLTEXSUBIMAGE3DPROC glTexSubImage3D;
  PFNGLTRANSFORMFEEDBACKVARYINGSPROC glTransformFeedbackVaryings;
  PFNGLUNIFORM1FPROC glUniform1f;
  PFNGLUNIFORM1FVPROC glUniform1fv;
  PFNGLUNIFORM1IPROC glUniform1i;
  PFNGLUNIFORM1IVPROC glUniform1iv;
  PFNGLUNIFORM1UIPROC glUniform1ui;
  PFNGLUNIFORM1UIVPROC glUniform1uiv;
  PFNGLUNIFORM2FPROC glUniform2f;
  PFNGLUNIFORM2FVPROC glUniform2fv;
  PFNGLUNIFORM2IPROC glUniform2i;
  PFNGLUNIFORM2IVPROC glUniform2iv;
  PFNGLUNIFORM2UIPROC glUniform2ui;
  PFNGLUNIFORM2UIVPROC glUniform2uiv;
  PFNGLUNIFORM3FPROC glUniform3f;
  PFNGLUNIFORM3FVPROC glUniform3fv;
  PFNGLUNIFORM3IPROC glUniform3i;
  PFNGLUNIFORM3IVPROC glUniform3iv;
  PFNGLUNIFORM3UIPROC glUniform3ui;
  PFNGLUNIFORM3UIVPROC glUniform3uiv;
  PFNGLUNIFORM4FPROC glUniform4f;
  PFNGLUNIFORM4FVPROC glUniform4fv;
  PFNGLUNIFORM4IPROC glUniform4i;
  PFNGLUNIFORM4IVPROC glUniform4iv;
  PFNGLUNIFORM4UIPROC glUniform4ui;
  PFNGLUNIFORM4UIVPROC glUniform4uiv;
  PFNGLUNIFORMBLOCKBINDINGPROC glUniformBlockBinding;
  PFNGLUNIFORMMATRIX2FVPROC glUniformMatrix2fv;
  PFNGLUNIFORMMATRIX2X3FVPROC glUniformMatrix2x3fv;
  PFNGLUNIFORMMATRIX2X4FVPROC glUniformMatrix2x4fv;
  PFNGLUNIFORMMATRIX3FVPROC glUniformMatrix3fv;
  PFNGLUNIFORMMATRIX3X2FVPROC glUniformMatrix3x2fv;
  PFNGLUNIFORMMATRIX3X4FVPROC glUniformMatrix3x4fv;
  PFNGLUNIFORMMATRIX4FVPROC glUniformMatrix4fv;
  PFNGLUNIFORMMATRIX4X2FVPROC glUniformMatrix4x2fv;
  PFNGLUNIFORMMATRIX4X3FVPROC glUniformMatrix4x3fv;
  PFNGLUNMAPBUFFERPROC glUnmapBuffer;
  PFNGLUSEPROGRAMPROC glUseProgram;
  PFNGLVALIDATEPROGRAMPROC glValidateProgram;
  PFNGLVERTEXATTRIB1FPROC glVertexAttrib1f;
  PFNGLVERTEXATTRIB1FVPROC glVertexAttrib1fv;
  PFNGLVERTEXATTRIB2FPROC glVertexAttrib2f;
  PFNGLVERTEXATTRIB2FVPROC glVertexAttrib2fv;
  PFNGLVERTEXATTRIB3FPROC glVertexAttrib3f;
  PFNGLVERTEXATTRIB3FVPROC glVertexAttrib3fv;
  PFNGLVERTEXATTRIB4FPROC glVertexAttrib4f;
  PFNGLVERTEXATTRIB4FVPROC glVertexAttrib4fv;
  PFNGLVERTEXATTRIBDIVISORPROC glVertexAttribDivisor;
  PFNGLVERTEXATTRIBI4IPROC glVertexAttribI4i;
  PFNGLVERTEXATTRIBI4IVPROC glVertexAttribI4iv;
  PFNGLVERTEXATTRIBI4UIPROC glVertexAttribI4ui;
  PFNGLVERTEXATTRIBI4UIVPROC glVertexAttribI4uiv;
  PFNGLVERTEXATTRIBIPOINTERPROC glVertexAttribIPointer;
  PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer;
  PFNGLVIEWPORTPROC glViewport;
  PFNGLWAITSYNCPROC glWaitSync;

  // ANGLE specific
  PFNGLREQUESTEXTENSIONANGLEPROC glRequestExtensionANGLE;

  // Refreshes extensions list:
  void RefreshGLExtensions();

 private:
  EGLContextWrapper(napi_env env, const GLContextOptions& context_options);

  void InitEGL(napi_env env, const GLContextOptions& context_options);
  void BindProcAddresses();
  void RefreshGLVersion();

  bool display_ref_retained_;
};

}  // namespace nodejsgl

#endif  // NODEJS_GL_EGL_CONTEXT_H_
