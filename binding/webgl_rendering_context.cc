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

#include "webgl_rendering_context.h"

#include "utils.h"
#include "webgl_extensions.h"
#include "webgl_sync.h"

#include "angle/include/GLES2/gl2.h"
#include "angle/include/GLES3/gl3.h"
#include "angle/include/GLES3/gl32.h"

#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace nodejsgl {

// Basic type to control what byte-width the ArrayLike buffer is for cleanup.
enum NodeJSGLArrayType {
  kInt32 = 0,
  kFloat32 = 1,
  kUint32 = 2,
};

// Class to automatically handle V8 buffers (TypedArrays/Arrays) with
// auto-cleanup. Specify array type to automatically allocate a different byte
// width (defaults to float).
class ArrayLikeBuffer {
 public:
  ArrayLikeBuffer()
      : data(nullptr), length(0), should_delete(false), array_type(kFloat32) {}

  ArrayLikeBuffer(NodeJSGLArrayType array_type)
      : data(nullptr),
        length(0),
        should_delete(false),
        array_type(array_type) {}

  ~ArrayLikeBuffer() {
    if (should_delete && data != nullptr) {
      free(data);
    }
  }

  size_t size() {
    switch (array_type) {
      case kInt32:
        return length / sizeof(int32_t);
      case kFloat32:
        return length / sizeof(float);
      case kUint32:
        return length / sizeof(uint32_t);
      default:
        fprintf(
            stderr,
            "WARNING: Cannot determine size of unknown array buffer type\n");
        return 0;
    }
  }

  void *data;
  size_t length;
  bool should_delete;

  NodeJSGLArrayType array_type;
};

bool WebGLRenderingContext::CheckForErrors() {
  GLenum error;
  bool had_error = false;
  while ((error = eglContextWrapper_->glGetError()) != GL_NO_ERROR) {
    fprintf(stderr, "HAS ERRORS()\n");
    switch (error) {
      case GL_INVALID_ENUM:
        fprintf(stderr, "Found unchecked GL error: GL_INVALID_ENUM\n");
        break;
      case GL_INVALID_VALUE:
        fprintf(stderr, "Found unchecked GL error: GL_INVALID_VALUE\n");
        break;
      case GL_INVALID_OPERATION:
        fprintf(stderr, "Found unchecked GL error: GL_INVALID_OPERATION\n");
        break;
      case GL_INVALID_FRAMEBUFFER_OPERATION:
        fprintf(stderr,
                "Found unchecked GL error: GL_INVALID_FRAMEBUFFER_OPERATION\n");
        break;
      case GL_OUT_OF_MEMORY:
        fprintf(stderr, "Found unchecked GL error: GL_OUT_OF_MEMORY\n");
        break;
      default:
        fprintf(stderr, "Found unchecked GL error: UNKNOWN ERROR\n");
        break;
    }
  }
  return had_error;
}

#define GL_BROWSER_DEFAULT_WEBGL 0x9244
#define GL_CONTEXT_LOST_WEBGL 0x9242
#define GL_UNPACK_COLORSPACE_CONVERSION_WEBGL 0x9243
#define GL_UNPACK_FLIP_Y_WEBGL 0x9240
#define GL_UNPACK_PREMULTIPLY_ALPHA_WEBGL 0x9241

// Returns wrapped context pointer only.
static napi_status GetContext(napi_env env, napi_callback_info info,
                              WebGLRenderingContext **context) {
  napi_status nstatus;

  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, nullptr, nullptr, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nstatus);

  ENSURE_VALUE_IS_OBJECT_RETVAL(env, js_this, napi_invalid_arg);

  nstatus = napi_unwrap(env, js_this, reinterpret_cast<void **>(context));
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nstatus);

  return napi_ok;
}

static napi_status UnwrapContext(napi_env env, napi_value js_this,
                                 WebGLRenderingContext **context) {
  ENSURE_VALUE_IS_OBJECT_RETVAL(env, js_this, napi_invalid_arg);
  return napi_unwrap(env, js_this, reinterpret_cast<void **>(context));
}

// TODO(cleanup and refactor) all of these helpers!
static napi_status GetContextBoolParams(napi_env env, napi_callback_info info,
                                        WebGLRenderingContext **context,
                                        size_t param_length, bool *params) {
  napi_status nstatus;

  size_t argc = param_length;
  std::vector<napi_value> args;
  args.resize(param_length);
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args.data(), &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nstatus);

  ENSURE_ARGC_RETVAL(env, argc, param_length, napi_invalid_arg);

  nstatus = UnwrapContext(env, js_this, context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nstatus);

  for (size_t i = 0; i < param_length; ++i) {
    ENSURE_VALUE_IS_BOOLEAN_RETVAL(env, args[i], napi_invalid_arg);

    nstatus = napi_get_value_bool(env, args[i], &params[i]);
    ENSURE_NAPI_OK_RETVAL(env, nstatus, nstatus);
  }

  return napi_ok;
}

// Returns wrapped context pointer and uint32_t params.
static napi_status GetContextUint32Params(napi_env env, napi_callback_info info,
                                          WebGLRenderingContext **context,
                                          size_t param_length,
                                          uint32_t *params) {
  napi_status nstatus;

  size_t argc = param_length;
  std::vector<napi_value> args;
  args.resize(param_length);
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args.data(), &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nstatus);

  ENSURE_ARGC_RETVAL(env, argc, param_length, napi_invalid_arg);

  nstatus = UnwrapContext(env, js_this, context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nstatus);

  for (size_t i = 0; i < param_length; ++i) {
    // Null-params get set to 0 in GL world.
    napi_valuetype value_type;
    nstatus = napi_typeof(env, args[i], &value_type);
    ENSURE_NAPI_OK_RETVAL(env, nstatus, nstatus);

    if (value_type == napi_null || value_type == napi_undefined) {
      params[i] = 0;
    } else if (value_type == napi_number) {
      nstatus = napi_get_value_uint32(env, args[i], &params[i]);
      ENSURE_NAPI_OK_RETVAL(env, nstatus, nstatus);
    } else {
      ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[i], nstatus);
    }
  }

  return napi_ok;
}

// Returns wrapped context pointer and uint32_t params.
static napi_status GetContextInt32Params(napi_env env, napi_callback_info info,
                                         WebGLRenderingContext **context,
                                         size_t param_length, int32_t *params) {
  napi_status nstatus;

  size_t argc = param_length;
  std::vector<napi_value> args;
  args.resize(param_length);
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args.data(), &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nstatus);

  ENSURE_ARGC_RETVAL(env, argc, param_length, napi_invalid_arg);

  nstatus = UnwrapContext(env, js_this, context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nstatus);

  for (size_t i = 0; i < param_length; ++i) {
    // Null-params get set to 0 in GL world.
    napi_valuetype value_type;
    nstatus = napi_typeof(env, args[i], &value_type);
    ENSURE_NAPI_OK_RETVAL(env, nstatus, nstatus);

    if (value_type == napi_null || value_type == napi_undefined) {
      params[i] = 0;
    } else if (value_type == napi_number) {
      nstatus = napi_get_value_int32(env, args[i], &params[i]);
      ENSURE_NAPI_OK_RETVAL(env, nstatus, nstatus);
    } else {
      ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[i], nstatus);
    }
  }

  return napi_ok;
}

static napi_status GetContextDoubleParams(napi_env env, napi_callback_info info,
                                          WebGLRenderingContext **context,
                                          size_t param_length, double *params) {
  napi_status nstatus;

  size_t argc = param_length;
  std::vector<napi_value> args;
  args.resize(param_length);
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args.data(), &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nstatus);

  ENSURE_ARGC_RETVAL(env, argc, param_length, napi_invalid_arg);

  nstatus = UnwrapContext(env, js_this, context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nstatus);

  for (size_t i = 0; i < param_length; ++i) {
    // Null-params get set to 0 in GL world.
    napi_valuetype value_type;
    nstatus = napi_typeof(env, args[i], &value_type);
    ENSURE_NAPI_OK_RETVAL(env, nstatus, nstatus);

    if (value_type == napi_null || value_type == napi_undefined) {
      params[i] = 0;
    } else if (value_type == napi_number) {
      nstatus = napi_get_value_double(env, args[i], &params[i]);
      ENSURE_NAPI_OK_RETVAL(env, nstatus, nstatus);
    } else {
      ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[i], nstatus);
    }
  }

  return napi_ok;
}

static napi_status GetStringParam(napi_env env, napi_value string_value,
                                  std::string &string) {
  ENSURE_VALUE_IS_STRING_RETVAL(env, string_value, napi_invalid_arg);

  napi_status nstatus;

  size_t str_length;
  nstatus =
      napi_get_value_string_utf8(env, string_value, nullptr, 0, &str_length);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nstatus);

  AutoBuffer<char> buffer(str_length + 1);
  nstatus = napi_get_value_string_utf8(env, string_value, buffer.get(),
                                       str_length + 1, &str_length);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nstatus);

  string.assign(buffer.get(), str_length);
  return napi_ok;
}

static size_t TypedArrayElementSize(napi_typedarray_type type) {
  switch (type) {
    case napi_int8_array:
    case napi_uint8_array:
    case napi_uint8_clamped_array:
      return 1;
    case napi_int16_array:
    case napi_uint16_array:
      return 2;
    case napi_int32_array:
    case napi_uint32_array:
    case napi_float32_array:
      return 4;
    case napi_float64_array:
      return 8;
    default:
      return 1;
  }
}

static napi_status CreateUint32(napi_env env, uint32_t value,
                                napi_value *result) {
  return napi_create_uint32(env, value, result);
}

static napi_status CreateInt32(napi_env env, int32_t value,
                               napi_value *result) {
  return napi_create_int32(env, value, result);
}

template <typename T>
static napi_status CreateNumericArray(napi_env env, const T *values,
                                      size_t length,
                                      napi_status (*create_value)(napi_env, T,
                                                                  napi_value *),
                                      napi_value *result) {
  napi_status nstatus = napi_create_array_with_length(env, length, result);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nstatus);
  for (size_t i = 0; i < length; ++i) {
    napi_value item;
    nstatus = create_value(env, values[i], &item);
    ENSURE_NAPI_OK_RETVAL(env, nstatus, nstatus);
    nstatus = napi_set_element(env, *result, i, item);
    ENSURE_NAPI_OK_RETVAL(env, nstatus, nstatus);
  }
  return napi_ok;
}

static napi_status GetUint32AllowNull(napi_env env, napi_value value,
                                      GLuint *result) {
  napi_status nstatus;
  napi_valuetype value_type;
  nstatus = napi_typeof(env, value, &value_type);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nstatus);
  if (value_type == napi_null || value_type == napi_undefined) {
    *result = 0;
    return napi_ok;
  }
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, value, napi_invalid_arg);
  nstatus = napi_get_value_uint32(env, value, result);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nstatus);
  return napi_ok;
}

static napi_status GetSyncParam(napi_env env, napi_value value,
                                GLsync *result) {
  napi_status nstatus;
  napi_valuetype value_type;
  nstatus = napi_typeof(env, value, &value_type);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nstatus);
  if (value_type == napi_null || value_type == napi_undefined) {
    *result = nullptr;
    return napi_ok;
  }
  ENSURE_VALUE_IS_OBJECT_RETVAL(env, value, napi_invalid_arg);
  nstatus = napi_unwrap(env, value, reinterpret_cast<void **>(result));
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nstatus);
  return napi_ok;
}

#define ENSURE_GL_PROC_RETVAL(env, context, name, retval) \
  if ((context)->eglContextWrapper_->name == nullptr) {   \
    NAPI_THROW_ERROR(env, #name " is not available");     \
    return retval;                                        \
  }

static napi_status GetArrayLikeBuffer(napi_env env, napi_value array_like_value,
                                      ArrayLikeBuffer *alb);

static napi_status GetUniformMatrixParams(napi_env env, napi_callback_info info,
                                          WebGLRenderingContext **context,
                                          GLint *location, GLboolean *transpose,
                                          ArrayLikeBuffer *alb) {
  napi_status nstatus;
  size_t argc = 3;
  napi_value args[3];
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nstatus);
  ENSURE_ARGC_RETVAL(env, argc, 3, napi_invalid_arg);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[0], napi_invalid_arg);
  ENSURE_VALUE_IS_BOOLEAN_RETVAL(env, args[1], napi_invalid_arg);

  nstatus = napi_get_value_int32(env, args[0], location);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nstatus);
  bool transpose_bool;
  nstatus = napi_get_value_bool(env, args[1], &transpose_bool);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nstatus);
  *transpose = static_cast<GLboolean>(transpose_bool);
  nstatus = GetArrayLikeBuffer(env, args[2], alb);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nstatus);
  nstatus = UnwrapContext(env, js_this, context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nstatus);
  return napi_ok;
}

// Returns a pointer to JS array-like objects. This method should be used when
// accessing underlying datastores for all JS-Array-like objects.
static napi_status GetArrayLikeBuffer(napi_env env, napi_value array_like_value,
                                      ArrayLikeBuffer *alb) {
  ENSURE_VALUE_IS_ARRAY_LIKE_RETVAL(env, array_like_value, napi_invalid_arg);

  bool is_typed_array = false;
  napi_status nstatus =
      napi_is_typedarray(env, array_like_value, &is_typed_array);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nstatus);
  if (is_typed_array) {
    napi_typedarray_type array_type;
    size_t element_count;
    napi_value arraybuffer_value;
    nstatus = napi_get_typedarray_info(env, array_like_value, &array_type,
                                       &element_count, &alb->data,
                                       &arraybuffer_value, nullptr);
    ENSURE_NAPI_OK_RETVAL(env, nstatus, nstatus);

    alb->length = element_count * TypedArrayElementSize(array_type);

    return napi_ok;
  }

  bool is_array = false;
  nstatus = napi_is_array(env, array_like_value, &is_array);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nstatus);
  if (is_array) {
    uint32_t length;
    nstatus = napi_get_array_length(env, array_like_value, &length);
    ENSURE_NAPI_OK_RETVAL(env, nstatus, nstatus);

    // Allocate a buffer based on the value set in ArrayLikeBuffer.
    switch (alb->array_type) {
      case kFloat32:
        alb->data = malloc(sizeof(float) * length);
        alb->length = sizeof(float) * length;
        break;
      case kInt32:
        alb->data = malloc(sizeof(int32_t) * length);
        alb->length = sizeof(int32_t) * length;
        break;
      case kUint32:
        alb->data = malloc(sizeof(uint32_t) * length);
        alb->length = sizeof(uint32_t) * length;
        break;
      default:
        NAPI_THROW_ERROR(env, "Unsupported array type for generic arrays!");
        return napi_invalid_arg;
    }

    // Notify ArrayLikeBuffer to cleanup buffer on deconstruction:
    alb->should_delete = true;

    // Place values in buffer:
    for (uint32_t i = 0; i < length; i++) {
      napi_value cur_value;
      nstatus = napi_get_element(env, array_like_value, i, &cur_value);
      ENSURE_NAPI_OK_RETVAL(env, nstatus, nstatus);

      switch (alb->array_type) {
        case kFloat32: {
          double value;
          nstatus = napi_get_value_double(env, cur_value, &value);
          ENSURE_NAPI_OK_RETVAL(env, nstatus, nstatus);

          static_cast<float *>(alb->data)[i] = static_cast<float>(value);
          break;
        }
        case kInt32: {
          int32_t value;
          nstatus = napi_get_value_int32(env, cur_value, &value);
          ENSURE_NAPI_OK_RETVAL(env, nstatus, nstatus);

          static_cast<int32_t *>(alb->data)[i] = value;
          break;
        }
        case kUint32: {
          uint32_t value;
          nstatus = napi_get_value_uint32(env, cur_value, &value);
          ENSURE_NAPI_OK_RETVAL(env, nstatus, nstatus);

          static_cast<uint32_t *>(alb->data)[i] = value;
          break;
        }
        default:
          NAPI_THROW_ERROR(env, "Unsupported array type for generic arrays!");
          return napi_invalid_arg;
      }
    }

    return napi_ok;
  }

  NAPI_THROW_ERROR(env, "Invalid data type.");
  return napi_invalid_arg;
}

napi_ref WebGLRenderingContext::constructor_ref_;

WebGLRenderingContext::WebGLRenderingContext(napi_env env,
                                             GLContextOptions opts)
    : env_(env), ref_(nullptr) {
  eglContextWrapper_ = EGLContextWrapper::Create(env, opts);
  if (!eglContextWrapper_) {
    NAPI_THROW_ERROR(env, "Could not create EGL context");
    return;
  }
  alloc_count_ = 0;
}

WebGLRenderingContext::~WebGLRenderingContext() {
  if (eglContextWrapper_) {
    delete eglContextWrapper_;
  }

  napi_delete_reference(env_, ref_);
}

/* static */
napi_status WebGLRenderingContext::Register(napi_env env, napi_value exports) {
  napi_status nstatus;

  napi_property_descriptor properties[] = {
      // WebGL methods:
      // clang-format off
      NAPI_DEFINE_METHOD("attachShader", AttachShader),
      NAPI_DEFINE_METHOD("bindAttribLocation", BindAttribLocation),
      NAPI_DEFINE_METHOD("bindBuffer", BindBuffer),
      NAPI_DEFINE_METHOD("bindBufferBase", BindBufferBase),
      NAPI_DEFINE_METHOD("bindBufferRange", BindBufferRange),
      NAPI_DEFINE_METHOD("bindFramebuffer", BindFramebuffer),
      NAPI_DEFINE_METHOD("bindRenderbuffer", BindRenderbuffer),
      NAPI_DEFINE_METHOD("bindSampler", BindSampler),
      NAPI_DEFINE_METHOD("bindTexture", BindTexture),
      NAPI_DEFINE_METHOD("bindTransformFeedback", BindTransformFeedback),
      NAPI_DEFINE_METHOD("bindVertexArray", BindVertexArray),
      NAPI_DEFINE_METHOD("beginQuery", BeginQuery),
      NAPI_DEFINE_METHOD("beginTransformFeedback", BeginTransformFeedback),
      NAPI_DEFINE_METHOD("blitFramebuffer", BlitFramebuffer),
      NAPI_DEFINE_METHOD("blendColor", BlendColor),
      NAPI_DEFINE_METHOD("blendEquation", BlendEquation),
      NAPI_DEFINE_METHOD("blendEquationSeparate", BlendEquationSeparate),
      NAPI_DEFINE_METHOD("blendFunc", BlendFunc),
      NAPI_DEFINE_METHOD("blendFuncSeparate", BlendFuncSeparate),
      NAPI_DEFINE_METHOD("bufferData", BufferData),
      NAPI_DEFINE_METHOD("bufferSubData", BufferSubData),
      NAPI_DEFINE_METHOD("checkFramebufferStatus", CheckFramebufferStatus),
      NAPI_DEFINE_METHOD("clear", Clear),
      NAPI_DEFINE_METHOD("clearColor", ClearColor),
      NAPI_DEFINE_METHOD("clearDepth", ClearDepth),
      NAPI_DEFINE_METHOD("clearStencil", ClearStencil),
      NAPI_DEFINE_METHOD("clientWaitSync", ClientWaitSync),
      NAPI_DEFINE_METHOD("colorMask", ColorMask),
      NAPI_DEFINE_METHOD("compileShader", CompileShader),
      NAPI_DEFINE_METHOD("compressedTexImage2D", CompressedTexImage2D),
      NAPI_DEFINE_METHOD("compressedTexImage3D", CompressedTexImage3D),
      NAPI_DEFINE_METHOD("compressedTexSubImage2D", CompressedTexSubImage2D),
      NAPI_DEFINE_METHOD("compressedTexSubImage3D", CompressedTexSubImage3D),
      NAPI_DEFINE_METHOD("copyBufferSubData", CopyBufferSubData),
      NAPI_DEFINE_METHOD("copyTexImage2D", CopyTexImage2D),
      NAPI_DEFINE_METHOD("copyTexSubImage2D", CopyTexSubImage2D),
      NAPI_DEFINE_METHOD("copyTexSubImage3D", CopyTexSubImage3D),
      NAPI_DEFINE_METHOD("createBuffer", CreateBuffer),
      NAPI_DEFINE_METHOD("createFramebuffer", CreateFramebuffer),
      NAPI_DEFINE_METHOD("createProgram", CreateProgram),
      NAPI_DEFINE_METHOD("createQuery", CreateQuery),
      NAPI_DEFINE_METHOD("createRenderbuffer", CreateRenderbuffer),
      NAPI_DEFINE_METHOD("createSampler", CreateSampler),
      NAPI_DEFINE_METHOD("createShader", CreateShader),
      NAPI_DEFINE_METHOD("createTexture", CreateTexture),
      NAPI_DEFINE_METHOD("createTransformFeedback", CreateTransformFeedback),
      NAPI_DEFINE_METHOD("createVertexArray", CreateVertexArray),
      NAPI_DEFINE_METHOD("cullFace", CullFace),
      NAPI_DEFINE_METHOD("deleteBuffer", DeleteBuffer),
      NAPI_DEFINE_METHOD("deleteFramebuffer", DeleteFramebuffer),
      NAPI_DEFINE_METHOD("deleteProgram", DeleteProgram),
      NAPI_DEFINE_METHOD("deleteQuery", DeleteQuery),
      NAPI_DEFINE_METHOD("deleteRenderbuffer", DeleteRenderbuffer),
      NAPI_DEFINE_METHOD("deleteSampler", DeleteSampler),
      NAPI_DEFINE_METHOD("deleteShader", DeleteShader),
      NAPI_DEFINE_METHOD("deleteSync", DeleteSync),
      NAPI_DEFINE_METHOD("deleteTexture", DeleteTexture),
      NAPI_DEFINE_METHOD("deleteTransformFeedback", DeleteTransformFeedback),
      NAPI_DEFINE_METHOD("deleteVertexArray", DeleteVertexArray),
      NAPI_DEFINE_METHOD("depthFunc", DepthFunc),
      NAPI_DEFINE_METHOD("depthMask", DepthMask),
      NAPI_DEFINE_METHOD("depthRange", DepthRange),
      NAPI_DEFINE_METHOD("detachShader", DetachShader),
      NAPI_DEFINE_METHOD("disable", Disable),
      NAPI_DEFINE_METHOD("disableVertexAttribArray", DisableVertexAttribArray),
      NAPI_DEFINE_METHOD("drawArrays", DrawArrays),
      NAPI_DEFINE_METHOD("drawArraysInstanced", DrawArraysInstanced),
      NAPI_DEFINE_METHOD("drawBuffers", DrawBuffers),
      NAPI_DEFINE_METHOD("drawElements", DrawElements),
      NAPI_DEFINE_METHOD("drawElementsInstanced", DrawElementsInstanced),
      NAPI_DEFINE_METHOD("enable", Enable),
      NAPI_DEFINE_METHOD("enableVertexAttribArray", EnableVertexAttribArray),
      NAPI_DEFINE_METHOD("endQuery", EndQuery),
      NAPI_DEFINE_METHOD("endTransformFeedback", EndTransformFeedback),
      NAPI_DEFINE_METHOD("fenceSync", FenceSynce),
      NAPI_DEFINE_METHOD("finish", Finish),
      NAPI_DEFINE_METHOD("flush", Flush),
      NAPI_DEFINE_METHOD("framebufferRenderbuffer", FramebufferRenderbuffer),
      NAPI_DEFINE_METHOD("framebufferTexture2D", FramebufferTexture2D),
      NAPI_DEFINE_METHOD("framebufferTextureLayer", FramebufferTextureLayer),
      NAPI_DEFINE_METHOD("frontFace", FrontFace),
      NAPI_DEFINE_METHOD("generateMipmap", GenerateMipmap),
      NAPI_DEFINE_METHOD("getActiveAttrib", GetActiveAttrib),
      NAPI_DEFINE_METHOD("getActiveUniform", GetActiveUniform),
      NAPI_DEFINE_METHOD("getAttachedShaders", GetAttachedShaders),
      NAPI_DEFINE_METHOD("getAttribLocation", GetAttribLocation),
      NAPI_DEFINE_METHOD("getBufferParameter", GetBufferParameter),
      NAPI_DEFINE_METHOD("getBufferSubData", GetBufferSubData),
      NAPI_DEFINE_METHOD("getContextAttributes", GetContextAttributes),
      NAPI_DEFINE_METHOD("getError", GetError),
// getExtension(extensionName: "OES_vertex_array_object"): OES_vertex_array_object | null;
// getExtension(extensionName: "WEBGL_compressed_texture_astc"): WEBGL_compressed_texture_astc | null;
// getExtension(extensionName: "WEBGL_compressed_texture_s3tc_srgb"): WEBGL_compressed_texture_s3tc_srgb | null;
// getExtension(extensionName: "WEBGL_debug_shaders"): WEBGL_debug_shaders | null;
// getExtension(extensionName: "WEBGL_draw_buffers"): WEBGL_draw_buffers | null;
// getExtension(extensionName: "WEBGL_compressed_texture_s3tc"): WEBGL_compressed_texture_s3tc | null;
// getExtension(extensionName: "ANGLE_instanced_arrays"): ANGLE_instanced_arrays | null;
      NAPI_DEFINE_METHOD("getFramebufferAttachmentParameter", GetFramebufferAttachmentParameter),
      NAPI_DEFINE_METHOD("getFragDataLocation", GetFragDataLocation),
      NAPI_DEFINE_METHOD("getIndexedParameter", GetIndexedParameter),
      NAPI_DEFINE_METHOD("getInternalformatParameter", GetInternalformatParameter),
      NAPI_DEFINE_METHOD("getExtension", GetExtension),
      NAPI_DEFINE_METHOD("getParameter", GetParameter),
      NAPI_DEFINE_METHOD("getProgramInfoLog", GetProgramInfoLog),
      NAPI_DEFINE_METHOD("getProgramParameter", GetProgramParameter),
      NAPI_DEFINE_METHOD("getQuery", GetQuery),
      NAPI_DEFINE_METHOD("getQueryParameter", GetQueryParameter),
      NAPI_DEFINE_METHOD("getRenderbufferParameter", GetRenderbufferParameter),
      NAPI_DEFINE_METHOD("getSamplerParameter", GetSamplerParameter),
      NAPI_DEFINE_METHOD("getShaderInfoLog", GetShaderInfoLog),
      NAPI_DEFINE_METHOD("getShaderParameter", GetShaderParameter),
      NAPI_DEFINE_METHOD("getShaderPrecisionFormat", GetShaderPrecisionFormat),
      NAPI_DEFINE_METHOD("getSyncParameter", GetSyncParameter),
      NAPI_DEFINE_METHOD("getShaderSource", ShaderSource),
      NAPI_DEFINE_METHOD("getSupportedExtensions", GetSupportedExtensions),
      NAPI_DEFINE_METHOD("getTexParameter", GetTexParameter),
      NAPI_DEFINE_METHOD("getTransformFeedbackVarying", GetTransformFeedbackVarying),
// getUniform(program: WebGLProgram | null, location: WebGLUniformLocation | null): any;
      NAPI_DEFINE_METHOD("getUniformLocation", GetUniformLocation),
      NAPI_DEFINE_METHOD("getVertexAttribIiv", GetVertexAttribIiv),
      NAPI_DEFINE_METHOD("getVertexAttribIuiv", GetVertexAttribIuiv),
// getVertexAttrib(index: number, pname: number): any;
// getVertexuniform1iAttribOffset(index: number, pname: number): number;
      NAPI_DEFINE_METHOD("hint", Hint),
      NAPI_DEFINE_METHOD("isBuffer", IsBuffer),
      NAPI_DEFINE_METHOD("isContextLost", IsContextLost),
      NAPI_DEFINE_METHOD("isEnabled", IsEnabled),
      NAPI_DEFINE_METHOD("isFramebuffer", IsFramebuffer),
      NAPI_DEFINE_METHOD("isProgram", IsProgram),
      NAPI_DEFINE_METHOD("isQuery", IsQuery),
      NAPI_DEFINE_METHOD("isRenderbuffer", IsRenderbuffer),
      NAPI_DEFINE_METHOD("isSampler", IsSampler),
      NAPI_DEFINE_METHOD("isShader", IsShader),
      NAPI_DEFINE_METHOD("isSync", IsSync),
      NAPI_DEFINE_METHOD("isTexture", IsTexture),
      NAPI_DEFINE_METHOD("isTransformFeedback", IsTransformFeedback),
      NAPI_DEFINE_METHOD("isVertexArray", IsVertexArray),
      NAPI_DEFINE_METHOD("invalidateFramebuffer", InvalidateFramebuffer),
      NAPI_DEFINE_METHOD("invalidateSubFramebuffer", InvalidateSubFramebuffer),
      NAPI_DEFINE_METHOD("lineWidth", LineWidth),
      NAPI_DEFINE_METHOD("linkProgram", LinkProgram),
      NAPI_DEFINE_METHOD("pixelStorei", PixelStorei),
      NAPI_DEFINE_METHOD("polygonOffset", PolygonOffset),
      NAPI_DEFINE_METHOD("readPixels", ReadPixels),
      NAPI_DEFINE_METHOD("readBuffer", ReadBuffer),
      NAPI_DEFINE_METHOD("renderbufferStorage", RenderbufferStorage),
      NAPI_DEFINE_METHOD("renderbufferStorageMultisample", RenderbufferStorageMultisample),
      NAPI_DEFINE_METHOD("sampleCoverage", SampleCoverage),
      NAPI_DEFINE_METHOD("samplerParameterf", SamplerParameterf),
      NAPI_DEFINE_METHOD("samplerParameteri", SamplerParameteri),
      NAPI_DEFINE_METHOD("scissor", Scissor),
      NAPI_DEFINE_METHOD("shaderSource", ShaderSource),
      NAPI_DEFINE_METHOD("stencilFunc", StencilFunc),
      NAPI_DEFINE_METHOD("stencilFuncSeparate", StencilFuncSeparate),
      NAPI_DEFINE_METHOD("stencilMask", StencilMask),
      NAPI_DEFINE_METHOD("stencilMaskSeparate", StencilMaskSeparate),
      NAPI_DEFINE_METHOD("stencilOp", StencilOp),
      NAPI_DEFINE_METHOD("stencilOpSeparate", StencilOpSeparate),
      NAPI_DEFINE_METHOD("texImage2D", TexImage2D),
      NAPI_DEFINE_METHOD("texImage3D", TexImage3D),
      NAPI_DEFINE_METHOD("texParameteri", TexParameteri),
      NAPI_DEFINE_METHOD("texParameterf", TexParameterf),
      NAPI_DEFINE_METHOD("texSubImage2D", TexSubImage2D),
      NAPI_DEFINE_METHOD("texSubImage3D", TexSubImage3D),
      NAPI_DEFINE_METHOD("transformFeedbackVaryings", TransformFeedbackVaryings),
      NAPI_DEFINE_METHOD("uniform1f", Uniform1f),
      NAPI_DEFINE_METHOD("uniform1fv", Uniform1fv),
      NAPI_DEFINE_METHOD("uniform1i", Uniform1i),
      NAPI_DEFINE_METHOD("uniform1iv", Uniform1iv),
      NAPI_DEFINE_METHOD("uniform1ui", Uniform1ui),
      NAPI_DEFINE_METHOD("uniform1uiv", Uniform1uiv),
      NAPI_DEFINE_METHOD("uniform2f", Uniform2f),
      NAPI_DEFINE_METHOD("uniform2fv", Uniform2fv),
      NAPI_DEFINE_METHOD("uniform2i", Uniform2i),
      NAPI_DEFINE_METHOD("uniform2iv", Uniform2iv),
      NAPI_DEFINE_METHOD("uniform2ui", Uniform2ui),
      NAPI_DEFINE_METHOD("uniform2uiv", Uniform2uiv),
      NAPI_DEFINE_METHOD("uniform3i", Uniform3i),
      NAPI_DEFINE_METHOD("uniform3iv", Uniform3iv),
      NAPI_DEFINE_METHOD("uniform3f", Uniform3f),
      NAPI_DEFINE_METHOD("uniform3fv", Uniform3fv),
      NAPI_DEFINE_METHOD("uniform3ui", Uniform3ui),
      NAPI_DEFINE_METHOD("uniform3uiv", Uniform3uiv),
      NAPI_DEFINE_METHOD("uniform4f", Uniform4f),
      NAPI_DEFINE_METHOD("uniform4fv", Uniform4fv),
      NAPI_DEFINE_METHOD("uniform4i", Uniform4i),
      NAPI_DEFINE_METHOD("uniform4iv", Uniform4iv),
      NAPI_DEFINE_METHOD("uniform4ui", Uniform4ui),
      NAPI_DEFINE_METHOD("uniform4uiv", Uniform4uiv),
      NAPI_DEFINE_METHOD("uniformMatrix2fv", UniformMatrix2fv),
      NAPI_DEFINE_METHOD("uniformMatrix2x3fv", UniformMatrix2x3fv),
      NAPI_DEFINE_METHOD("uniformMatrix2x4fv", UniformMatrix2x4fv),
      NAPI_DEFINE_METHOD("uniformMatrix3fv", UniformMatrix3fv),
      NAPI_DEFINE_METHOD("uniformMatrix3x2fv", UniformMatrix3x2fv),
      NAPI_DEFINE_METHOD("uniformMatrix3x4fv", UniformMatrix3x4fv),
      NAPI_DEFINE_METHOD("uniformMatrix4fv", UniformMatrix4fv),
      NAPI_DEFINE_METHOD("uniformMatrix4x2fv", UniformMatrix4x2fv),
      NAPI_DEFINE_METHOD("uniformMatrix4x3fv", UniformMatrix4x3fv),
      NAPI_DEFINE_METHOD("useProgram", UseProgram),
      NAPI_DEFINE_METHOD("validateProgram", ValidateProgram),
      NAPI_DEFINE_METHOD("vertexAttrib1f", VertexAttrib1f),
      NAPI_DEFINE_METHOD("vertexAttrib1fv", VertexAttrib1fv),
      NAPI_DEFINE_METHOD("vertexAttrib2f", VertexAttrib2f),
      NAPI_DEFINE_METHOD("vertexAttrib2fv", VertexAttrib2fv),
      NAPI_DEFINE_METHOD("vertexAttrib3f", VertexAttrib3f),
      NAPI_DEFINE_METHOD("vertexAttrib3fv", VertexAttrib3fv),
      NAPI_DEFINE_METHOD("vertexAttrib4f", VertexAttrib4f),
      NAPI_DEFINE_METHOD("vertexAttrib4fv", VertexAttrib4fv),
      NAPI_DEFINE_METHOD("vertexAttribDivisor", VertexAttribDivisor),
      NAPI_DEFINE_METHOD("vertexAttribI4i", VertexAttribI4i),
      NAPI_DEFINE_METHOD("vertexAttribI4iv", VertexAttribI4iv),
      NAPI_DEFINE_METHOD("vertexAttribI4ui", VertexAttribI4ui),
      NAPI_DEFINE_METHOD("vertexAttribI4uiv", VertexAttribI4uiv),
      NAPI_DEFINE_METHOD("vertexAttribIPointer", VertexAttribIPointer),
      NAPI_DEFINE_METHOD("vertexAttribPointer", VertexAttribPointer),
      NAPI_DEFINE_METHOD("viewport", Viewport),
      NAPI_DEFINE_METHOD("waitSync", WaitSync),
      // clang-format on

      // WebGL attributes:
      NapiDefineIntProperty(env, GL_ACTIVE_ATTRIBUTES, "ACTIVE_ATTRIBUTES"),
      NapiDefineIntProperty(env, GL_ACTIVE_TEXTURE, "ACTIVE_TEXTURE"),
      NapiDefineIntProperty(env, GL_ACTIVE_UNIFORMS, "ACTIVE_UNIFORMS"),
      NapiDefineIntProperty(env, GL_ALIASED_LINE_WIDTH_RANGE,
                            "ALIASED_LINE_WIDTH_RANGE"),
      NapiDefineIntProperty(env, GL_ALIASED_POINT_SIZE_RANGE,
                            "ALIASED_POINT_SIZE_RANGE"),
      NapiDefineIntProperty(env, GL_ALPHA, "ALPHA"),
      NapiDefineIntProperty(env, GL_ALPHA_BITS, "ALPHA_BITS"),
      NapiDefineIntProperty(env, GL_ALWAYS, "ALWAYS"),
      NapiDefineIntProperty(env, GL_ARRAY_BUFFER, "ARRAY_BUFFER"),
      NapiDefineIntProperty(env, GL_ARRAY_BUFFER_BINDING,
                            "ARRAY_BUFFER_BINDING"),
      NapiDefineIntProperty(env, GL_ATTACHED_SHADERS, "ATTACHED_SHADERS"),
      NapiDefineIntProperty(env, GL_BACK, "BACK"),
      NapiDefineIntProperty(env, GL_BLEND, "BLEND"),
      NapiDefineIntProperty(env, GL_BLEND_COLOR, "BLEND_COLOR"),
      NapiDefineIntProperty(env, GL_BLEND_DST_ALPHA, "BLEND_DST_ALPHA"),
      NapiDefineIntProperty(env, GL_BLEND_DST_RGB, "BLEND_DST_RGB"),
      NapiDefineIntProperty(env, GL_BLEND_EQUATION, "BLEND_EQUATION"),
      NapiDefineIntProperty(env, GL_BLEND_EQUATION_ALPHA,
                            "BLEND_EQUATION_ALPHA"),
      NapiDefineIntProperty(env, GL_BLEND_EQUATION_RGB, "BLEND_EQUATION_RGB"),
      NapiDefineIntProperty(env, GL_BLEND_SRC_ALPHA, "BLEND_SRC_ALPHA"),
      NapiDefineIntProperty(env, GL_BLEND_SRC_RGB, "BLEND_SRC_RGB"),
      NapiDefineIntProperty(env, GL_BLUE_BITS, "BLUE_BITS"),
      NapiDefineIntProperty(env, GL_BOOL, "BOOL"),
      NapiDefineIntProperty(env, GL_BOOL_VEC2, "BOOL_VEC2"),
      NapiDefineIntProperty(env, GL_BOOL_VEC3, "BOOL_VEC3"),
      NapiDefineIntProperty(env, GL_BOOL_VEC4, "BOOL_VEC4"),
      NapiDefineIntProperty(env, GL_BROWSER_DEFAULT_WEBGL,
                            "BROWSER_DEFAULT_WEBGL"),
      NapiDefineIntProperty(env, GL_BUFFER_SIZE, "BUFFER_SIZE"),
      NapiDefineIntProperty(env, GL_BUFFER_USAGE, "BUFFER_USAGE"),
      NapiDefineIntProperty(env, GL_BYTE, "BYTE"),
      NapiDefineIntProperty(env, GL_CCW, "CCW"),
      NapiDefineIntProperty(env, GL_CLAMP_TO_EDGE, "CLAMP_TO_EDGE"),
      NapiDefineIntProperty(env, GL_COLOR_ATTACHMENT0, "COLOR_ATTACHMENT0"),
      NapiDefineIntProperty(env, GL_COLOR_BUFFER_BIT, "COLOR_BUFFER_BIT"),
      NapiDefineIntProperty(env, GL_COLOR_CLEAR_VALUE, "COLOR_CLEAR_VALUE"),
      NapiDefineIntProperty(env, GL_COLOR_WRITEMASK, "COLOR_WRITEMASK"),
      NapiDefineIntProperty(env, GL_COMPILE_STATUS, "COMPILE_STATUS"),
      NapiDefineIntProperty(env, GL_COMPRESSED_TEXTURE_FORMATS,
                            "COMPRESSED_TEXTURE_FORMATS"),
      NapiDefineIntProperty(env, GL_CONSTANT_ALPHA, "CONSTANT_ALPHA"),
      NapiDefineIntProperty(env, GL_CONSTANT_COLOR, "CONSTANT_COLOR"),
      NapiDefineIntProperty(env, GL_CONTEXT_LOST_WEBGL, "CONTEXT_LOST_WEBGL"),
      NapiDefineIntProperty(env, GL_CULL_FACE, "CULL_FACE"),
      NapiDefineIntProperty(env, GL_CULL_FACE_MODE, "CULL_FACE_MODE"),
      NapiDefineIntProperty(env, GL_CURRENT_PROGRAM, "CURRENT_PROGRAM"),
      NapiDefineIntProperty(env, GL_CURRENT_VERTEX_ATTRIB,
                            "CURRENT_VERTEX_ATTRIB"),
      NapiDefineIntProperty(env, GL_CW, "CW"),
      NapiDefineIntProperty(env, GL_DECR, "DECR"),
      NapiDefineIntProperty(env, GL_DECR_WRAP, "DECR_WRAP"),
      NapiDefineIntProperty(env, GL_DELETE_STATUS, "DELETE_STATUS"),
      NapiDefineIntProperty(env, GL_DEPTH_ATTACHMENT, "DEPTH_ATTACHMENT"),
      NapiDefineIntProperty(env, GL_DEPTH_BITS, "DEPTH_BITS"),
      NapiDefineIntProperty(env, GL_DEPTH_BUFFER_BIT, "DEPTH_BUFFER_BIT"),
      NapiDefineIntProperty(env, GL_DEPTH_CLEAR_VALUE, "DEPTH_CLEAR_VALUE"),
      NapiDefineIntProperty(env, GL_DEPTH_COMPONENT, "DEPTH_COMPONENT"),
      NapiDefineIntProperty(env, GL_DEPTH_COMPONENT16, "DEPTH_COMPONENT16"),
      NapiDefineIntProperty(env, GL_DEPTH_FUNC, "DEPTH_FUNC"),
      NapiDefineIntProperty(env, GL_DEPTH_RANGE, "DEPTH_RANGE"),
      NapiDefineIntProperty(env, GL_DEPTH_STENCIL, "DEPTH_STENCIL"),
      NapiDefineIntProperty(env, GL_DEPTH_STENCIL_ATTACHMENT,
                            "DEPTH_STENCIL_ATTACHMENT"),
      NapiDefineIntProperty(env, GL_DEPTH_TEST, "DEPTH_TEST"),
      NapiDefineIntProperty(env, GL_DEPTH_WRITEMASK, "DEPTH_WRITEMASK"),
      NapiDefineIntProperty(env, GL_DITHER, "DITHER"),
      NapiDefineIntProperty(env, GL_DONT_CARE, "DONT_CARE"),
      NapiDefineIntProperty(env, GL_DST_ALPHA, "DST_ALPHA"),
      NapiDefineIntProperty(env, GL_DST_COLOR, "DST_COLOR"),
      NapiDefineIntProperty(env, GL_DYNAMIC_DRAW, "DYNAMIC_DRAW"),
      NapiDefineIntProperty(env, GL_ELEMENT_ARRAY_BUFFER,
                            "ELEMENT_ARRAY_BUFFER"),
      NapiDefineIntProperty(env, GL_ELEMENT_ARRAY_BUFFER_BINDING,
                            "ELEMENT_ARRAY_BUFFER_BINDING"),
      NapiDefineIntProperty(env, GL_EQUAL, "EQUAL"),
      NapiDefineIntProperty(env, GL_FASTEST, "FASTEST"),
      NapiDefineIntProperty(env, GL_FLOAT, "FLOAT"),
      NapiDefineIntProperty(env, GL_FLOAT_MAT2, "FLOAT_MAT2"),
      NapiDefineIntProperty(env, GL_FLOAT_MAT3, "FLOAT_MAT3"),
      NapiDefineIntProperty(env, GL_FLOAT_MAT4, "FLOAT_MAT4"),
      NapiDefineIntProperty(env, GL_FLOAT_VEC2, "FLOAT_VEC2"),
      NapiDefineIntProperty(env, GL_FLOAT_VEC3, "FLOAT_VEC3"),
      NapiDefineIntProperty(env, GL_FLOAT_VEC4, "FLOAT_VEC4"),
      NapiDefineIntProperty(env, GL_FRAGMENT_SHADER, "FRAGMENT_SHADER"),
      NapiDefineIntProperty(env, GL_FRAMEBUFFER, "FRAMEBUFFER"),
      NapiDefineIntProperty(env, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME,
                            "FRAMEBUFFER_ATTACHMENT_OBJECT_NAME"),
      NapiDefineIntProperty(env, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE,
                            "FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE"),
      NapiDefineIntProperty(env,
                            GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_CUBE_MAP_FACE,
                            "FRAMEBUFFER_ATTACHMENT_TEXTURE_CUBE_MAP_FACE"),
      NapiDefineIntProperty(env, GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL,
                            "FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL"),
      NapiDefineIntProperty(env, GL_FRAMEBUFFER_BINDING, "FRAMEBUFFER_BINDING"),
      NapiDefineIntProperty(env, GL_FRAMEBUFFER_COMPLETE,
                            "FRAMEBUFFER_COMPLETE"),
      NapiDefineIntProperty(env, GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT,
                            "FRAMEBUFFER_INCOMPLETE_ATTACHMENT"),
      NapiDefineIntProperty(env, GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS,
                            "FRAMEBUFFER_INCOMPLETE_DIMENSIONS"),
      NapiDefineIntProperty(env, GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT,
                            "FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT"),
      NapiDefineIntProperty(env, GL_FRAMEBUFFER_UNSUPPORTED,
                            "FRAMEBUFFER_UNSUPPORTED"),
      NapiDefineIntProperty(env, GL_FRONT, "FRONT"),
      NapiDefineIntProperty(env, GL_FRONT_AND_BACK, "FRONT_AND_BACK"),
      NapiDefineIntProperty(env, GL_FRONT_FACE, "FRONT_FACE"),
      NapiDefineIntProperty(env, GL_FUNC_ADD, "FUNC_ADD"),
      NapiDefineIntProperty(env, GL_FUNC_REVERSE_SUBTRACT,
                            "FUNC_REVERSE_SUBTRACT"),
      NapiDefineIntProperty(env, GL_FUNC_SUBTRACT, "FUNC_SUBTRACT"),
      NapiDefineIntProperty(env, GL_GENERATE_MIPMAP_HINT,
                            "GENERATE_MIPMAP_HINT"),
      NapiDefineIntProperty(env, GL_GEQUAL, "GEQUAL"),
      NapiDefineIntProperty(env, GL_GREATER, "GREATER"),
      NapiDefineIntProperty(env, GL_GREEN_BITS, "GREEN_BITS"),
      NapiDefineIntProperty(env, GL_HIGH_FLOAT, "HIGH_FLOAT"),
      NapiDefineIntProperty(env, GL_HIGH_INT, "HIGH_INT"),
      NapiDefineIntProperty(env, GL_IMPLEMENTATION_COLOR_READ_FORMAT,
                            "IMPLEMENTATION_COLOR_READ_FORMAT"),
      NapiDefineIntProperty(env, GL_IMPLEMENTATION_COLOR_READ_TYPE,
                            "IMPLEMENTATION_COLOR_READ_TYPE"),
      NapiDefineIntProperty(env, GL_INCR, "INCR"),
      NapiDefineIntProperty(env, GL_INCR_WRAP, "INCR_WRAP"),
      NapiDefineIntProperty(env, GL_INT, "INT"),
      NapiDefineIntProperty(env, GL_INT_VEC2, "INT_VEC2"),
      NapiDefineIntProperty(env, GL_INT_VEC3, "INT_VEC3"),
      NapiDefineIntProperty(env, GL_INT_VEC4, "INT_VEC4"),
      NapiDefineIntProperty(env, GL_INVALID_ENUM, "INVALID_ENUM"),
      NapiDefineIntProperty(env, GL_INVALID_FRAMEBUFFER_OPERATION,
                            "INVALID_FRAMEBUFFER_OPERATION"),
      NapiDefineIntProperty(env, GL_INVALID_OPERATION, "INVALID_OPERATION"),
      NapiDefineIntProperty(env, GL_INVALID_VALUE, "INVALID_VALUE"),
      NapiDefineIntProperty(env, GL_INVERT, "INVERT"),
      NapiDefineIntProperty(env, GL_KEEP, "KEEP"),
      NapiDefineIntProperty(env, GL_LEQUAL, "LEQUAL"),
      NapiDefineIntProperty(env, GL_LESS, "LESS"),
      NapiDefineIntProperty(env, GL_LINEAR, "LINEAR"),
      NapiDefineIntProperty(env, GL_LINEAR_MIPMAP_LINEAR,
                            "LINEAR_MIPMAP_LINEAR"),
      NapiDefineIntProperty(env, GL_LINEAR_MIPMAP_NEAREST,
                            "LINEAR_MIPMAP_NEAREST"),
      NapiDefineIntProperty(env, GL_LINES, "LINES"),
      NapiDefineIntProperty(env, GL_LINE_LOOP, "LINE_LOOP"),
      NapiDefineIntProperty(env, GL_LINE_STRIP, "LINE_STRIP"),
      NapiDefineIntProperty(env, GL_LINE_WIDTH, "LINE_WIDTH"),
      NapiDefineIntProperty(env, GL_LINK_STATUS, "LINK_STATUS"),
      NapiDefineIntProperty(env, GL_LOW_FLOAT, "LOW_FLOAT"),
      NapiDefineIntProperty(env, GL_LOW_INT, "LOW_INT"),
      NapiDefineIntProperty(env, GL_LUMINANCE, "LUMINANCE"),
      NapiDefineIntProperty(env, GL_LUMINANCE_ALPHA, "LUMINANCE_ALPHA"),
      NapiDefineIntProperty(env, GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS,
                            "MAX_COMBINED_TEXTURE_IMAGE_UNITS"),
      NapiDefineIntProperty(env, GL_MAX_CUBE_MAP_TEXTURE_SIZE,
                            "MAX_CUBE_MAP_TEXTURE_SIZE"),
      NapiDefineIntProperty(env, GL_MAX_FRAGMENT_UNIFORM_VECTORS,
                            "MAX_FRAGMENT_UNIFORM_VECTORS"),
      NapiDefineIntProperty(env, GL_MAX_RENDERBUFFER_SIZE,
                            "MAX_RENDERBUFFER_SIZE"),
      NapiDefineIntProperty(env, GL_MAX_TEXTURE_IMAGE_UNITS,
                            "MAX_TEXTURE_IMAGE_UNITS"),
      NapiDefineIntProperty(env, GL_MAX_TEXTURE_SIZE, "MAX_TEXTURE_SIZE"),
      NapiDefineIntProperty(env, GL_MAX_VARYING_VECTORS, "MAX_VARYING_VECTORS"),
      NapiDefineIntProperty(env, GL_MAX_VERTEX_ATTRIBS, "MAX_VERTEX_ATTRIBS"),
      NapiDefineIntProperty(env, GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS,
                            "MAX_VERTEX_TEXTURE_IMAGE_UNITS"),
      NapiDefineIntProperty(env, GL_MAX_VERTEX_UNIFORM_VECTORS,
                            "MAX_VERTEX_UNIFORM_VECTORS"),
      NapiDefineIntProperty(env, GL_MAX_VIEWPORT_DIMS, "MAX_VIEWPORT_DIMS"),
      NapiDefineIntProperty(env, GL_MEDIUM_FLOAT, "MEDIUM_FLOAT"),
      NapiDefineIntProperty(env, GL_MEDIUM_INT, "MEDIUM_INT"),
      NapiDefineIntProperty(env, GL_MIRRORED_REPEAT, "MIRRORED_REPEAT"),
      NapiDefineIntProperty(env, GL_NEAREST, "NEAREST"),
      NapiDefineIntProperty(env, GL_NEAREST_MIPMAP_LINEAR,
                            "NEAREST_MIPMAP_LINEAR"),
      NapiDefineIntProperty(env, GL_NEAREST_MIPMAP_NEAREST,
                            "NEAREST_MIPMAP_NEAREST"),
      NapiDefineIntProperty(env, GL_NEVER, "NEVER"),
      NapiDefineIntProperty(env, GL_NICEST, "NICEST"),
      NapiDefineIntProperty(env, GL_NONE, "NONE"),
      NapiDefineIntProperty(env, GL_NOTEQUAL, "NOTEQUAL"),
      NapiDefineIntProperty(env, GL_NO_ERROR, "NO_ERROR"),
      NapiDefineIntProperty(env, GL_ONE, "ONE"),
      NapiDefineIntProperty(env, GL_ONE_MINUS_CONSTANT_ALPHA,
                            "ONE_MINUS_CONSTANT_ALPHA"),
      NapiDefineIntProperty(env, GL_ONE_MINUS_CONSTANT_COLOR,
                            "ONE_MINUS_CONSTANT_COLOR"),
      NapiDefineIntProperty(env, GL_ONE_MINUS_DST_ALPHA, "ONE_MINUS_DST_ALPHA"),
      NapiDefineIntProperty(env, GL_ONE_MINUS_DST_COLOR, "ONE_MINUS_DST_COLOR"),
      NapiDefineIntProperty(env, GL_ONE_MINUS_SRC_ALPHA, "ONE_MINUS_SRC_ALPHA"),
      NapiDefineIntProperty(env, GL_ONE_MINUS_SRC_COLOR, "ONE_MINUS_SRC_COLOR"),
      NapiDefineIntProperty(env, GL_OUT_OF_MEMORY, "OUT_OF_MEMORY"),
      NapiDefineIntProperty(env, GL_PACK_ALIGNMENT, "PACK_ALIGNMENT"),
      NapiDefineIntProperty(env, GL_POINTS, "POINTS"),
      NapiDefineIntProperty(env, GL_POLYGON_OFFSET_FACTOR,
                            "POLYGON_OFFSET_FACTOR"),
      NapiDefineIntProperty(env, GL_POLYGON_OFFSET_FILL, "POLYGON_OFFSET_FILL"),
      NapiDefineIntProperty(env, GL_POLYGON_OFFSET_UNITS,
                            "POLYGON_OFFSET_UNITS"),
      NapiDefineIntProperty(env, GL_RED_BITS, "RED_BITS"),
      NapiDefineIntProperty(env, GL_RENDERBUFFER, "RENDERBUFFER"),
      NapiDefineIntProperty(env, GL_RENDERBUFFER_ALPHA_SIZE,
                            "RENDERBUFFER_ALPHA_SIZE"),
      NapiDefineIntProperty(env, GL_RENDERBUFFER_BINDING,
                            "RENDERBUFFER_BINDING"),
      NapiDefineIntProperty(env, GL_RENDERBUFFER_BLUE_SIZE,
                            "RENDERBUFFER_BLUE_SIZE"),
      NapiDefineIntProperty(env, GL_RENDERBUFFER_DEPTH_SIZE,
                            "RENDERBUFFER_DEPTH_SIZE"),
      NapiDefineIntProperty(env, GL_RENDERBUFFER_GREEN_SIZE,
                            "RENDERBUFFER_GREEN_SIZE"),
      NapiDefineIntProperty(env, GL_RENDERBUFFER_HEIGHT, "RENDERBUFFER_HEIGHT"),
      NapiDefineIntProperty(env, GL_RENDERBUFFER_INTERNAL_FORMAT,
                            "RENDERBUFFER_INTERNAL_FORMAT"),
      NapiDefineIntProperty(env, GL_RENDERBUFFER_RED_SIZE,
                            "RENDERBUFFER_RED_SIZE"),
      NapiDefineIntProperty(env, GL_RENDERBUFFER_STENCIL_SIZE,
                            "RENDERBUFFER_STENCIL_SIZE"),
      NapiDefineIntProperty(env, GL_RENDERBUFFER_WIDTH, "RENDERBUFFER_WIDTH"),
      NapiDefineIntProperty(env, GL_RENDERER, "RENDERER"),
      NapiDefineIntProperty(env, GL_REPEAT, "REPEAT"),
      NapiDefineIntProperty(env, GL_REPLACE, "REPLACE"),
      NapiDefineIntProperty(env, GL_RGB, "RGB"),
      NapiDefineIntProperty(env, GL_RGB565, "RGB565"),
      NapiDefineIntProperty(env, GL_RGB5_A1, "RGB5_A1"),
      NapiDefineIntProperty(env, GL_RGBA, "RGBA"),
      NapiDefineIntProperty(env, GL_RGBA4, "RGBA4"),
      NapiDefineIntProperty(env, GL_SAMPLER_2D, "SAMPLER_2D"),
      NapiDefineIntProperty(env, GL_SAMPLER_CUBE, "SAMPLER_CUBE"),
      NapiDefineIntProperty(env, GL_SAMPLES, "SAMPLES"),
      NapiDefineIntProperty(env, GL_SAMPLE_ALPHA_TO_COVERAGE,
                            "SAMPLE_ALPHA_TO_COVERAGE"),
      NapiDefineIntProperty(env, GL_SAMPLE_BUFFERS, "SAMPLE_BUFFERS"),
      NapiDefineIntProperty(env, GL_SAMPLE_COVERAGE, "SAMPLE_COVERAGE"),
      NapiDefineIntProperty(env, GL_SAMPLE_COVERAGE_INVERT,
                            "SAMPLE_COVERAGE_INVERT"),
      NapiDefineIntProperty(env, GL_SAMPLE_COVERAGE_VALUE,
                            "SAMPLE_COVERAGE_VALUE"),
      NapiDefineIntProperty(env, GL_SCISSOR_BOX, "SCISSOR_BOX"),
      NapiDefineIntProperty(env, GL_SCISSOR_TEST, "SCISSOR_TEST"),
      NapiDefineIntProperty(env, GL_SHADER_TYPE, "SHADER_TYPE"),
      NapiDefineIntProperty(env, GL_SHADING_LANGUAGE_VERSION,
                            "SHADING_LANGUAGE_VERSION"),
      NapiDefineIntProperty(env, GL_SHORT, "SHORT"),
      NapiDefineIntProperty(env, GL_SRC_ALPHA, "SRC_ALPHA"),
      NapiDefineIntProperty(env, GL_SRC_ALPHA_SATURATE, "SRC_ALPHA_SATURATE"),
      NapiDefineIntProperty(env, GL_SRC_COLOR, "SRC_COLOR"),
      NapiDefineIntProperty(env, GL_STATIC_DRAW, "STATIC_DRAW"),
      NapiDefineIntProperty(env, GL_STENCIL_ATTACHMENT, "STENCIL_ATTACHMENT"),
      NapiDefineIntProperty(env, GL_STENCIL_BACK_FAIL, "STENCIL_BACK_FAIL"),
      NapiDefineIntProperty(env, GL_STENCIL_BACK_FUNC, "STENCIL_BACK_FUNC"),
      NapiDefineIntProperty(env, GL_STENCIL_BACK_PASS_DEPTH_FAIL,
                            "STENCIL_BACK_PASS_DEPTH_FAIL"),
      NapiDefineIntProperty(env, GL_STENCIL_BACK_PASS_DEPTH_PASS,
                            "STENCIL_BACK_PASS_DEPTH_PASS"),
      NapiDefineIntProperty(env, GL_STENCIL_BACK_REF, "STENCIL_BACK_REF"),
      NapiDefineIntProperty(env, GL_STENCIL_BACK_VALUE_MASK,
                            "STENCIL_BACK_VALUE_MASK"),
      NapiDefineIntProperty(env, GL_STENCIL_BACK_WRITEMASK,
                            "STENCIL_BACK_WRITEMASK"),
      NapiDefineIntProperty(env, GL_STENCIL_BITS, "STENCIL_BITS"),
      NapiDefineIntProperty(env, GL_STENCIL_BUFFER_BIT, "STENCIL_BUFFER_BIT"),
      NapiDefineIntProperty(env, GL_STENCIL_CLEAR_VALUE, "STENCIL_CLEAR_VALUE"),
      NapiDefineIntProperty(env, GL_STENCIL_FAIL, "STENCIL_FAIL"),
      NapiDefineIntProperty(env, GL_STENCIL_FUNC, "STENCIL_FUNC"),
      NapiDefineIntProperty(env, GL_STENCIL_INDEX, "STENCIL_INDEX"),
      NapiDefineIntProperty(env, GL_STENCIL_INDEX8, "STENCIL_INDEX8"),
      NapiDefineIntProperty(env, GL_STENCIL_PASS_DEPTH_FAIL,
                            "STENCIL_PASS_DEPTH_FAIL"),
      NapiDefineIntProperty(env, GL_STENCIL_PASS_DEPTH_PASS,
                            "STENCIL_PASS_DEPTH_PASS"),
      NapiDefineIntProperty(env, GL_STENCIL_REF, "STENCIL_REF"),
      NapiDefineIntProperty(env, GL_STENCIL_TEST, "STENCIL_TEST"),
      NapiDefineIntProperty(env, GL_STENCIL_VALUE_MASK, "STENCIL_VALUE_MASK"),
      NapiDefineIntProperty(env, GL_STENCIL_WRITEMASK, "STENCIL_WRITEMASK"),
      NapiDefineIntProperty(env, GL_STREAM_DRAW, "STREAM_DRAW"),
      NapiDefineIntProperty(env, GL_STREAM_READ, "STREAM_READ"),
      NapiDefineIntProperty(env, GL_SUBPIXEL_BITS, "SUBPIXEL_BITS"),
      NapiDefineIntProperty(env, GL_TEXTURE, "TEXTURE"),
      NapiDefineIntProperty(env, GL_TEXTURE0, "TEXTURE0"),
      NapiDefineIntProperty(env, GL_TEXTURE1, "TEXTURE1"),
      NapiDefineIntProperty(env, GL_TEXTURE10, "TEXTURE10"),
      NapiDefineIntProperty(env, GL_TEXTURE11, "TEXTURE11"),
      NapiDefineIntProperty(env, GL_TEXTURE12, "TEXTURE12"),
      NapiDefineIntProperty(env, GL_TEXTURE13, "TEXTURE13"),
      NapiDefineIntProperty(env, GL_TEXTURE14, "TEXTURE14"),
      NapiDefineIntProperty(env, GL_TEXTURE15, "TEXTURE15"),
      NapiDefineIntProperty(env, GL_TEXTURE16, "TEXTURE16"),
      NapiDefineIntProperty(env, GL_TEXTURE17, "TEXTURE17"),
      NapiDefineIntProperty(env, GL_TEXTURE18, "TEXTURE1"),
      NapiDefineIntProperty(env, GL_TEXTURE19, "TEXTURE19"),
      NapiDefineIntProperty(env, GL_TEXTURE2, "TEXTURE2"),
      NapiDefineIntProperty(env, GL_TEXTURE20, "TEXTURE20"),
      NapiDefineIntProperty(env, GL_TEXTURE21, "TEXTURE21"),
      NapiDefineIntProperty(env, GL_TEXTURE22, "TEXTURE22"),
      NapiDefineIntProperty(env, GL_TEXTURE23, "TEXTURE23"),
      NapiDefineIntProperty(env, GL_TEXTURE24, "TEXTURE24"),
      NapiDefineIntProperty(env, GL_TEXTURE25, "TEXTURE25"),
      NapiDefineIntProperty(env, GL_TEXTURE26, "TEXTURE26"),
      NapiDefineIntProperty(env, GL_TEXTURE27, "TEXTURE27"),
      NapiDefineIntProperty(env, GL_TEXTURE28, "TEXTURE28"),
      NapiDefineIntProperty(env, GL_TEXTURE29, "TEXTURE29"),
      NapiDefineIntProperty(env, GL_TEXTURE3, "TEXTURE3"),
      NapiDefineIntProperty(env, GL_TEXTURE30, "TEXTURE30"),
      NapiDefineIntProperty(env, GL_TEXTURE31, "TEXTURE31"),
      NapiDefineIntProperty(env, GL_TEXTURE4, "TEXTURE4"),
      NapiDefineIntProperty(env, GL_TEXTURE5, "TEXTURE5"),
      NapiDefineIntProperty(env, GL_TEXTURE6, "TEXTURE6"),
      NapiDefineIntProperty(env, GL_TEXTURE7, "TEXTURE7"),
      NapiDefineIntProperty(env, GL_TEXTURE8, "TEXTURE8"),
      NapiDefineIntProperty(env, GL_TEXTURE9, "TEXTURE9"),
      NapiDefineIntProperty(env, GL_TEXTURE_2D, "TEXTURE_2D"),
      NapiDefineIntProperty(env, GL_TEXTURE_BINDING_2D, "TEXTURE_BINDING_2D"),
      NapiDefineIntProperty(env, GL_TEXTURE_BINDING_CUBE_MAP,
                            "TEXTURE_BINDING_CUBE_MAP"),
      NapiDefineIntProperty(env, GL_TEXTURE_CUBE_MAP, "TEXTURE_CUBE_MAP"),
      NapiDefineIntProperty(env, GL_TEXTURE_CUBE_MAP_NEGATIVE_X,
                            "TEXTURE_CUBE_MAP_NEGATIVE_X"),
      NapiDefineIntProperty(env, GL_TEXTURE_CUBE_MAP_NEGATIVE_Y,
                            "TEXTURE_CUBE_MAP_NEGATIVE_Y"),
      NapiDefineIntProperty(env, GL_TEXTURE_CUBE_MAP_NEGATIVE_Z,
                            "TEXTURE_CUBE_MAP_NEGATIVE_Z"),
      NapiDefineIntProperty(env, GL_TEXTURE_CUBE_MAP_POSITIVE_X,
                            "TEXTURE_CUBE_MAP_POSITIVE_X"),
      NapiDefineIntProperty(env, GL_TEXTURE_CUBE_MAP_POSITIVE_Y,
                            "TEXTURE_CUBE_MAP_POSITIVE_Y"),
      NapiDefineIntProperty(env, GL_TEXTURE_CUBE_MAP_POSITIVE_Z,
                            "TEXTURE_CUBE_MAP_POSITIVE_Z"),
      NapiDefineIntProperty(env, GL_TEXTURE_MAG_FILTER, "TEXTURE_MAG_FILTER"),
      NapiDefineIntProperty(env, GL_TEXTURE_MIN_FILTER, "TEXTURE_MIN_FILTER"),
      NapiDefineIntProperty(env, GL_TEXTURE_WRAP_S, "TEXTURE_WRAP_S"),
      NapiDefineIntProperty(env, GL_TEXTURE_WRAP_T, "TEXTURE_WRAP_T"),
      NapiDefineIntProperty(env, GL_TRIANGLES, "TRIANGLES"),
      NapiDefineIntProperty(env, GL_TRIANGLE_FAN, "TRIANGLE_FAN"),
      NapiDefineIntProperty(env, GL_TRIANGLE_STRIP, "TRIANGLE_STRIP"),
      NapiDefineIntProperty(env, GL_UNPACK_ALIGNMENT, "UNPACK_ALIGNMENT"),
      NapiDefineIntProperty(env, GL_UNPACK_COLORSPACE_CONVERSION_WEBGL,
                            "UNPACK_COLORSPACE_CONVERSION_WEBGL"),
      NapiDefineIntProperty(env, GL_UNPACK_FLIP_Y_WEBGL, "UNPACK_FLIP_Y_WEBGL"),
      NapiDefineIntProperty(env, GL_UNPACK_PREMULTIPLY_ALPHA_WEBGL,
                            "UNPACK_PREMULTIPLY_ALPHA_WEBGL"),
      NapiDefineIntProperty(env, GL_UNSIGNED_BYTE, "UNSIGNED_BYTE"),
      NapiDefineIntProperty(env, GL_UNSIGNED_INT, "UNSIGNED_INT"),
      NapiDefineIntProperty(env, GL_UNSIGNED_SHORT, "UNSIGNED_SHORT"),
      NapiDefineIntProperty(env, GL_UNSIGNED_SHORT_4_4_4_4,
                            "UNSIGNED_SHORT_4_4_4_4"),
      NapiDefineIntProperty(env, GL_UNSIGNED_SHORT_5_5_5_1,
                            "UNSIGNED_SHORT_5_5_5_1"),
      NapiDefineIntProperty(env, GL_UNSIGNED_SHORT_5_6_5,
                            "UNSIGNED_SHORT_5_6_5"),
      NapiDefineIntProperty(env, GL_VALIDATE_STATUS, "VALIDATE_STATUS"),
      NapiDefineIntProperty(env, GL_VENDOR, "VENDOR"),
      NapiDefineIntProperty(env, GL_VERSION, "VERSION"),
      NapiDefineIntProperty(env, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING,
                            "VERTEX_ATTRIB_ARRAY_BUFFER_BINDING"),
      NapiDefineIntProperty(env, GL_VERTEX_ATTRIB_ARRAY_ENABLED,
                            "VERTEX_ATTRIB_ARRAY_ENABLED"),
      NapiDefineIntProperty(env, GL_VERTEX_ATTRIB_ARRAY_NORMALIZED,
                            "VERTEX_ATTRIB_ARRAY_NORMALIZED"),
      NapiDefineIntProperty(env, GL_VERTEX_ATTRIB_ARRAY_POINTER,
                            "VERTEX_ATTRIB_ARRAY_POINTER"),
      NapiDefineIntProperty(env, GL_VERTEX_ATTRIB_ARRAY_SIZE,
                            "VERTEX_ATTRIB_ARRAY_SIZE"),
      NapiDefineIntProperty(env, GL_VERTEX_ATTRIB_ARRAY_STRIDE,
                            "VERTEX_ATTRIB_ARRAY_STRIDE"),
      NapiDefineIntProperty(env, GL_VERTEX_ATTRIB_ARRAY_TYPE,
                            "VERTEX_ATTRIB_ARRAY_TYPE"),
      NapiDefineIntProperty(env, GL_VERTEX_SHADER, "VERTEX_SHADER"),
      NapiDefineIntProperty(env, GL_VIEWPORT, "VIEWPORT"),
      NapiDefineIntProperty(env, GL_ZERO, "ZERO"),

      // WebGL2 methods:
      NAPI_DEFINE_METHOD("activeTexture", ActiveTexture),

      // WebGL2 attributes:
      NapiDefineIntProperty(env, GL_CONDITION_SATISFIED, "CONDITION_SATISFIED"),
      NapiDefineIntProperty(env, GL_ALREADY_SIGNALED, "ALREADY_SIGNALED"),
      NapiDefineIntProperty(env, GL_HALF_FLOAT, "HALF_FLOAT"),
      NapiDefineIntProperty(env, GL_PIXEL_PACK_BUFFER, "PIXEL_PACK_BUFFER"),
      NapiDefineIntProperty(env, GL_PIXEL_UNPACK_BUFFER, "PIXEL_UNPACK_BUFFER"),
      NapiDefineIntProperty(env, GL_R16F, "R16F"),
      NapiDefineIntProperty(env, GL_R32F, "R32F"),
      NapiDefineIntProperty(env, GL_RGBA16F, "RGBA16F"),
      NapiDefineIntProperty(env, GL_RGBA32F, "RGBA32F"),
      NapiDefineIntProperty(env, GL_RGBA8, "RGBA8"),
      NapiDefineIntProperty(env, GL_RED, "RED"),
      NapiDefineIntProperty(env, GL_SYNC_GPU_COMMANDS_COMPLETE,
                            "SYNC_GPU_COMMANDS_COMPLETE"),
      NapiDefineIntProperty(env, GL_TIMEOUT_EXPIRED, "TIMEOUT_EXPIRED"),
      NapiDefineIntProperty(env, GL_WAIT_FAILED, "WAIT_FAILED"),
      NapiDefineIntProperty(env, GL_SYNC_STATUS, "SYNC_STATUS"),
      NapiDefineIntProperty(env, GL_SYNC_CONDITION, "SYNC_CONDITION"),
      NapiDefineIntProperty(env, GL_SYNC_FLAGS, "SYNC_FLAGS"),
      NapiDefineIntProperty(env, GL_SIGNALED, "SIGNALED"),
      NapiDefineIntProperty(env, GL_UNSIGNALED, "UNSIGNALED"),
      NapiDefineIntProperty(env, GL_VERTEX_ARRAY_BINDING,
                            "VERTEX_ARRAY_BINDING"),
      NapiDefineIntProperty(env, GL_VERTEX_ATTRIB_ARRAY_DIVISOR,
                            "VERTEX_ATTRIB_ARRAY_DIVISOR"),
      NapiDefineIntProperty(env, GL_DRAW_BUFFER0, "DRAW_BUFFER0"),
      NapiDefineIntProperty(env, GL_DRAW_BUFFER1, "DRAW_BUFFER1"),
      NapiDefineIntProperty(env, GL_DRAW_BUFFER2, "DRAW_BUFFER2"),
      NapiDefineIntProperty(env, GL_DRAW_BUFFER3, "DRAW_BUFFER3"),
      NapiDefineIntProperty(env, GL_DRAW_BUFFER4, "DRAW_BUFFER4"),
      NapiDefineIntProperty(env, GL_DRAW_BUFFER5, "DRAW_BUFFER5"),
      NapiDefineIntProperty(env, GL_DRAW_BUFFER6, "DRAW_BUFFER6"),
      NapiDefineIntProperty(env, GL_DRAW_BUFFER7, "DRAW_BUFFER7"),
      NapiDefineIntProperty(env, GL_DRAW_BUFFER8, "DRAW_BUFFER8"),
      NapiDefineIntProperty(env, GL_DRAW_BUFFER9, "DRAW_BUFFER9"),
      NapiDefineIntProperty(env, GL_DRAW_BUFFER10, "DRAW_BUFFER10"),
      NapiDefineIntProperty(env, GL_DRAW_BUFFER11, "DRAW_BUFFER11"),
      NapiDefineIntProperty(env, GL_DRAW_BUFFER12, "DRAW_BUFFER12"),
      NapiDefineIntProperty(env, GL_DRAW_BUFFER13, "DRAW_BUFFER13"),
      NapiDefineIntProperty(env, GL_DRAW_BUFFER14, "DRAW_BUFFER14"),
      NapiDefineIntProperty(env, GL_DRAW_BUFFER15, "DRAW_BUFFER15"),
      NapiDefineIntProperty(env, GL_MAX_DRAW_BUFFERS, "MAX_DRAW_BUFFERS"),
      NapiDefineIntProperty(env, GL_MAX_COLOR_ATTACHMENTS,
                            "MAX_COLOR_ATTACHMENTS"),
      NapiDefineIntProperty(env, GL_READ_BUFFER, "READ_BUFFER"),
      NapiDefineIntProperty(env, GL_READ_FRAMEBUFFER, "READ_FRAMEBUFFER"),
      NapiDefineIntProperty(env, GL_DRAW_FRAMEBUFFER, "DRAW_FRAMEBUFFER"),
      NapiDefineIntProperty(env, GL_READ_FRAMEBUFFER_BINDING,
                            "READ_FRAMEBUFFER_BINDING"),
      NapiDefineIntProperty(env, GL_DRAW_FRAMEBUFFER_BINDING,
                            "DRAW_FRAMEBUFFER_BINDING"),
      NapiDefineIntProperty(env, GL_COPY_READ_BUFFER, "COPY_READ_BUFFER"),
      NapiDefineIntProperty(env, GL_COPY_WRITE_BUFFER, "COPY_WRITE_BUFFER"),
      NapiDefineIntProperty(env, GL_UNIFORM_BUFFER, "UNIFORM_BUFFER"),
      NapiDefineIntProperty(env, GL_UNIFORM_BUFFER_BINDING,
                            "UNIFORM_BUFFER_BINDING"),
      NapiDefineIntProperty(env, GL_UNIFORM_BUFFER_START,
                            "UNIFORM_BUFFER_START"),
      NapiDefineIntProperty(env, GL_UNIFORM_BUFFER_SIZE, "UNIFORM_BUFFER_SIZE"),
      NapiDefineIntProperty(env, GL_TRANSFORM_FEEDBACK, "TRANSFORM_FEEDBACK"),
      NapiDefineIntProperty(env, GL_TRANSFORM_FEEDBACK_BUFFER,
                            "TRANSFORM_FEEDBACK_BUFFER"),
      NapiDefineIntProperty(env, GL_TRANSFORM_FEEDBACK_BUFFER_BINDING,
                            "TRANSFORM_FEEDBACK_BUFFER_BINDING"),
      NapiDefineIntProperty(env, GL_TRANSFORM_FEEDBACK_BUFFER_START,
                            "TRANSFORM_FEEDBACK_BUFFER_START"),
      NapiDefineIntProperty(env, GL_TRANSFORM_FEEDBACK_BUFFER_SIZE,
                            "TRANSFORM_FEEDBACK_BUFFER_SIZE"),
      NapiDefineIntProperty(env, GL_TRANSFORM_FEEDBACK_ACTIVE,
                            "TRANSFORM_FEEDBACK_ACTIVE"),
      NapiDefineIntProperty(env, GL_TRANSFORM_FEEDBACK_PAUSED,
                            "TRANSFORM_FEEDBACK_PAUSED"),
      NapiDefineIntProperty(env, GL_INTERLEAVED_ATTRIBS, "INTERLEAVED_ATTRIBS"),
      NapiDefineIntProperty(env, GL_SEPARATE_ATTRIBS, "SEPARATE_ATTRIBS"),
      NapiDefineIntProperty(env, GL_TRANSFORM_FEEDBACK_VARYINGS,
                            "TRANSFORM_FEEDBACK_VARYINGS"),
      NapiDefineIntProperty(env, GL_TRANSFORM_FEEDBACK_BUFFER_MODE,
                            "TRANSFORM_FEEDBACK_BUFFER_MODE"),
      NapiDefineIntProperty(env, GL_TRANSFORM_FEEDBACK_VARYING_MAX_LENGTH,
                            "TRANSFORM_FEEDBACK_VARYING_MAX_LENGTH"),
      NapiDefineIntProperty(env, GL_ANY_SAMPLES_PASSED, "ANY_SAMPLES_PASSED"),
      NapiDefineIntProperty(env, GL_ANY_SAMPLES_PASSED_CONSERVATIVE,
                            "ANY_SAMPLES_PASSED_CONSERVATIVE"),
      NapiDefineIntProperty(env, GL_CURRENT_QUERY, "CURRENT_QUERY"),
      NapiDefineIntProperty(env, GL_QUERY_RESULT, "QUERY_RESULT"),
      NapiDefineIntProperty(env, GL_QUERY_RESULT_AVAILABLE,
                            "QUERY_RESULT_AVAILABLE"),
      NapiDefineIntProperty(env, GL_SAMPLER_BINDING, "SAMPLER_BINDING"),
      NapiDefineIntProperty(env, GL_TEXTURE_3D, "TEXTURE_3D"),
      NapiDefineIntProperty(env, GL_TEXTURE_2D_ARRAY, "TEXTURE_2D_ARRAY"),
      NapiDefineIntProperty(env, GL_TEXTURE_BINDING_3D, "TEXTURE_BINDING_3D"),
      NapiDefineIntProperty(env, GL_TEXTURE_BINDING_2D_ARRAY,
                            "TEXTURE_BINDING_2D_ARRAY"),
      NapiDefineIntProperty(env, GL_TEXTURE_WRAP_R, "TEXTURE_WRAP_R"),
      NapiDefineIntProperty(env, GL_MAX_3D_TEXTURE_SIZE, "MAX_3D_TEXTURE_SIZE"),
      NapiDefineIntProperty(env, GL_MAX_ARRAY_TEXTURE_LAYERS,
                            "MAX_ARRAY_TEXTURE_LAYERS"),
      NapiDefineIntProperty(env, GL_TEXTURE_BASE_LEVEL, "TEXTURE_BASE_LEVEL"),
      NapiDefineIntProperty(env, GL_TEXTURE_MAX_LEVEL, "TEXTURE_MAX_LEVEL"),
      NapiDefineIntProperty(env, GL_TEXTURE_COMPARE_FUNC,
                            "TEXTURE_COMPARE_FUNC"),
      NapiDefineIntProperty(env, GL_TEXTURE_COMPARE_MODE,
                            "TEXTURE_COMPARE_MODE"),
      NapiDefineIntProperty(env, GL_COMPARE_REF_TO_TEXTURE,
                            "COMPARE_REF_TO_TEXTURE"),
      NapiDefineIntProperty(env, GL_TEXTURE_IMMUTABLE_FORMAT,
                            "TEXTURE_IMMUTABLE_FORMAT"),
      NapiDefineIntProperty(env, GL_TEXTURE_IMMUTABLE_LEVELS,
                            "TEXTURE_IMMUTABLE_LEVELS"),
      NapiDefineIntProperty(env, GL_RGB8, "RGB8"),
      NapiDefineIntProperty(env, GL_R8, "R8"),
      NapiDefineIntProperty(env, GL_RG, "RG"),
      NapiDefineIntProperty(env, GL_RG8, "RG8"),
      NapiDefineIntProperty(env, GL_R32UI, "R32UI"),
      NapiDefineIntProperty(env, GL_UNSIGNED_INT_VEC2, "UNSIGNED_INT_VEC2"),
      NapiDefineIntProperty(env, GL_UNSIGNED_INT_VEC3, "UNSIGNED_INT_VEC3"),
      NapiDefineIntProperty(env, GL_UNSIGNED_INT_VEC4, "UNSIGNED_INT_VEC4"),
      NapiDefineIntProperty(env, GL_FLOAT_MAT2x3, "FLOAT_MAT2x3"),
      NapiDefineIntProperty(env, GL_FLOAT_MAT2x4, "FLOAT_MAT2x4"),
      NapiDefineIntProperty(env, GL_FLOAT_MAT3x2, "FLOAT_MAT3x2"),
      NapiDefineIntProperty(env, GL_FLOAT_MAT3x4, "FLOAT_MAT3x4"),
      NapiDefineIntProperty(env, GL_FLOAT_MAT4x2, "FLOAT_MAT4x2"),
      NapiDefineIntProperty(env, GL_FLOAT_MAT4x3, "FLOAT_MAT4x3"),
  };

  // Create constructor
  napi_value ctor_value;
  nstatus = napi_define_class(env, "WebGLRenderingContext", NAPI_AUTO_LENGTH,
                              WebGLRenderingContext::InitInternal, nullptr,
                              ARRAY_SIZE(properties), properties, &ctor_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nstatus);

  // Bind reference
  nstatus = napi_create_reference(env, ctor_value, 1, &constructor_ref_);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nstatus);

  return napi_ok;
}

/* static */
napi_status WebGLRenderingContext::NewInstance(napi_env env,
                                               napi_value *instance,
                                               napi_callback_info info) {
  napi_status nstatus;

  napi_value ctor_value;
  nstatus = napi_get_reference_value(env, constructor_ref_, &ctor_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nstatus);

  size_t argc = 5;
  napi_value args[5];
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nstatus);
  ENSURE_ARGC_RETVAL(env, argc, argc, nstatus);

  nstatus = napi_new_instance(env, ctor_value, argc, args, instance);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nstatus);

  return napi_ok;
}

/* static */
napi_value WebGLRenderingContext::InitInternal(napi_env env,
                                               napi_callback_info info) {
  napi_status nstatus;

  ENSURE_CONSTRUCTOR_CALL_RETVAL(env, info, nullptr);

  size_t argc = 5;
  napi_value args[5];
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_ARGC_RETVAL(env, argc, argc, nullptr);

  GLContextOptions opts;
  nstatus = napi_get_value_uint32(env, args[0], &opts.width);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  nstatus = napi_get_value_uint32(env, args[1], &opts.height);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  nstatus = napi_get_value_uint32(env, args[2], &opts.client_major_es_version);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  nstatus = napi_get_value_uint32(env, args[3], &opts.client_minor_es_version);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  nstatus = napi_get_value_bool(env, args[4], &opts.webgl_compatibility);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  WebGLRenderingContext *context = new WebGLRenderingContext(env, opts);
  ENSURE_VALUE_IS_NOT_NULL_RETVAL(env, context, nullptr);

  nstatus = napi_wrap(env, js_this, context, Cleanup, nullptr, &context->ref_);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  return js_this;
}

/* static */
void WebGLRenderingContext::Cleanup(napi_env env, void *native, void *hint) {
  WebGLRenderingContext *context = static_cast<WebGLRenderingContext *>(native);
  delete context;
}

/** Exported WebGL wrapper methods
 * ********************************************/

/* static */
napi_value WebGLRenderingContext::ActiveTexture(napi_env env,
                                                napi_callback_info info) {
  LOG_CALL("ActiveTexture");
  napi_status nstatus;

  WebGLRenderingContext *context = nullptr;

  GLenum texture;
  nstatus = GetContextUint32Params(env, info, &context, 1, &texture);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glActiveTexture(texture);

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::AttachShader(napi_env env,
                                               napi_callback_info info) {
  LOG_CALL("AttachShader");
  napi_status nstatus;

  WebGLRenderingContext *context = nullptr;
  uint32_t args[2];
  nstatus = GetContextUint32Params(env, info, &context, 2, args);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glAttachShader(args[0], args[1]);

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::BindAttribLocation(napi_env env,
                                                     napi_callback_info info) {
  LOG_CALL("BindAttribLocation");
  napi_status nstatus;

  size_t argc = 3;
  napi_value args[3];
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_ARGC_RETVAL(env, argc, 3, nullptr);

  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[0], nullptr);
  GLuint program;
  nstatus = napi_get_value_uint32(env, args[0], &program);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[1], nullptr);
  GLuint index;
  nstatus = napi_get_value_uint32(env, args[1], &index);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  ENSURE_VALUE_IS_STRING_RETVAL(env, args[2], nullptr);
  std::string name;
  nstatus = GetStringParam(env, args[2], name);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glBindAttribLocation(program, index,
                                                    name.c_str());

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::BindBuffer(napi_env env,
                                             napi_callback_info info) {
  LOG_CALL("BindBuffer");
  napi_status nstatus;

  WebGLRenderingContext *context = nullptr;
  uint32_t args[2];
  nstatus = GetContextUint32Params(env, info, &context, 2, args);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glBindBuffer(args[0], args[1]);

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::BindBufferBase(napi_env env,
                                                 napi_callback_info info) {
  LOG_CALL("BindBufferBase");
  WebGLRenderingContext *context = nullptr;
  uint32_t args[3];
  napi_status nstatus = GetContextUint32Params(env, info, &context, 3, args);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_GL_PROC_RETVAL(env, context, glBindBufferBase, nullptr);
  context->eglContextWrapper_->glBindBufferBase(args[0], args[1], args[2]);
#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::BindBufferRange(napi_env env,
                                                  napi_callback_info info) {
  LOG_CALL("BindBufferRange");
  WebGLRenderingContext *context = nullptr;
  uint32_t args[5];
  napi_status nstatus = GetContextUint32Params(env, info, &context, 5, args);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_GL_PROC_RETVAL(env, context, glBindBufferRange, nullptr);
  context->eglContextWrapper_->glBindBufferRange(args[0], args[1], args[2],
                                                 args[3], args[4]);
#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::BindFramebuffer(napi_env env,
                                                  napi_callback_info info) {
  LOG_CALL("BindFramebuffer");
  napi_status nstatus;

  WebGLRenderingContext *context = nullptr;
  uint32_t args[2];
  nstatus = GetContextUint32Params(env, info, &context, 2, args);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glBindFramebuffer(args[0], args[1]);

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::BindRenderbuffer(napi_env env,
                                                   napi_callback_info info) {
  LOG_CALL("BindRenderbuffer");
  napi_status nstatus;

  WebGLRenderingContext *context = nullptr;
  uint32_t args[2];
  nstatus = GetContextUint32Params(env, info, &context, 2, args);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glBindRenderbuffer(args[0], args[1]);

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::BindSampler(napi_env env,
                                              napi_callback_info info) {
  LOG_CALL("BindSampler");
  WebGLRenderingContext *context = nullptr;
  uint32_t args[2];
  napi_status nstatus = GetContextUint32Params(env, info, &context, 2, args);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_GL_PROC_RETVAL(env, context, glBindSampler, nullptr);
  context->eglContextWrapper_->glBindSampler(args[0], args[1]);
#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::BindTransformFeedback(
    napi_env env, napi_callback_info info) {
  LOG_CALL("BindTransformFeedback");
  WebGLRenderingContext *context = nullptr;
  uint32_t args[2];
  napi_status nstatus = GetContextUint32Params(env, info, &context, 2, args);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_GL_PROC_RETVAL(env, context, glBindTransformFeedback, nullptr);
  context->eglContextWrapper_->glBindTransformFeedback(args[0], args[1]);
#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::BindVertexArray(napi_env env,
                                                  napi_callback_info info) {
  LOG_CALL("BindVertexArray");
  napi_status nstatus;

  size_t argc = 1;
  napi_value args[1];
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_ARGC_RETVAL(env, argc, 1, nullptr);

  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_GL_PROC_RETVAL(env, context, glBindVertexArray, nullptr);

  GLuint vertex_array;
  nstatus = GetUint32AllowNull(env, args[0], &vertex_array);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glBindVertexArray(vertex_array);
#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::BeginQuery(napi_env env,
                                             napi_callback_info info) {
  LOG_CALL("BeginQuery");
  WebGLRenderingContext *context = nullptr;
  uint32_t args[2];
  napi_status nstatus = GetContextUint32Params(env, info, &context, 2, args);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_GL_PROC_RETVAL(env, context, glBeginQuery, nullptr);
  context->eglContextWrapper_->glBeginQuery(args[0], args[1]);
#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::BeginTransformFeedback(
    napi_env env, napi_callback_info info) {
  LOG_CALL("BeginTransformFeedback");
  WebGLRenderingContext *context = nullptr;
  uint32_t primitive_mode;
  napi_status nstatus =
      GetContextUint32Params(env, info, &context, 1, &primitive_mode);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_GL_PROC_RETVAL(env, context, glBeginTransformFeedback, nullptr);
  context->eglContextWrapper_->glBeginTransformFeedback(primitive_mode);
#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::BlitFramebuffer(napi_env env,
                                                  napi_callback_info info) {
  LOG_CALL("BlitFramebuffer");
  WebGLRenderingContext *context = nullptr;
  int32_t args[10];
  napi_status nstatus = GetContextInt32Params(env, info, &context, 10, args);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_GL_PROC_RETVAL(env, context, glBlitFramebuffer, nullptr);
  context->eglContextWrapper_->glBlitFramebuffer(
      args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7],
      static_cast<GLbitfield>(args[8]), static_cast<GLenum>(args[9]));
#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::BlendColor(napi_env env,
                                             napi_callback_info info) {
  LOG_CALL("BlendColor");
  napi_status nstatus;

  double values[4];
  WebGLRenderingContext *context = nullptr;
  nstatus = GetContextDoubleParams(env, info, &context, 4, values);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glBlendColor(
      static_cast<GLclampf>(values[0]), static_cast<GLclampf>(values[1]),
      static_cast<GLclampf>(values[2]), static_cast<GLclampf>(values[3]));

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::BlendEquation(napi_env env,
                                                napi_callback_info info) {
  LOG_CALL("BlendEquation");
  napi_status nstatus;

  WebGLRenderingContext *context = nullptr;
  GLuint mode;
  nstatus = GetContextUint32Params(env, info, &context, 1, &mode);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glBlendEquation(mode);

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::BlendEquationSeparate(
    napi_env env, napi_callback_info info) {
  LOG_CALL("BlendEquationSeparate");
  napi_status nstatus;

  WebGLRenderingContext *context = nullptr;
  GLenum args[2];
  nstatus = GetContextUint32Params(env, info, &context, 2, args);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glBlendEquationSeparate(args[0], args[1]);

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::BlendFunc(napi_env env,
                                            napi_callback_info info) {
  LOG_CALL("BlendFunc");
  napi_status nstatus;

  WebGLRenderingContext *context = nullptr;
  GLenum args[2];
  nstatus = GetContextUint32Params(env, info, &context, 2, args);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glBlendFunc(args[0], args[1]);

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::BlendFuncSeparate(napi_env env,
                                                    napi_callback_info info) {
  LOG_CALL("BlendFuncSeparate");
  napi_status nstatus;

  WebGLRenderingContext *context = nullptr;
  GLenum args[4];
  nstatus = GetContextUint32Params(env, info, &context, 4, args);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glBlendFuncSeparate(args[0], args[1], args[2],
                                                   args[3]);

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::BufferData(napi_env env,
                                             napi_callback_info info) {
  LOG_CALL("BufferData");
  napi_status nstatus;

  size_t argc = 3;
  napi_value args[3];
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_ARGC_RETVAL(env, argc, 3, nullptr);

  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[0], nullptr);
  GLenum target;
  nstatus = napi_get_value_uint32(env, args[0], &target);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  // WebGL1 allows for an option of target, size, and usage w/o supplying
  // call of data.
  // Validate arg 1 type:
  napi_valuetype arg_type;
  nstatus = napi_typeof(env, args[1], &arg_type);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  ArrayLikeBuffer alb;
  uint32_t length;
  if (arg_type == napi_number) {
    nstatus = napi_get_value_uint32(env, args[1], &length);
    ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  } else {
    nstatus = GetArrayLikeBuffer(env, args[1], &alb);
    ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

    length = alb.length;
  }

  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[2], nullptr);
  GLenum usage;
  nstatus = napi_get_value_uint32(env, args[2], &usage);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glBufferData(target, length, alb.data, usage);

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::BufferSubData(napi_env env,
                                                napi_callback_info info) {
  LOG_CALL("BufferSubData");
  napi_status nstatus;

  size_t argc = 3;
  napi_value args[3];
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_ARGC_RETVAL(env, argc, 3, nullptr);

  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[0], nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[1], nullptr);

  GLenum target;
  nstatus = napi_get_value_uint32(env, args[0], &target);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  uint32_t offset;
  nstatus = napi_get_value_uint32(env, args[1], &offset);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  ArrayLikeBuffer alb;
  nstatus = GetArrayLikeBuffer(env, args[2], &alb);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  context->eglContextWrapper_->glBufferSubData(target, offset, alb.length,
                                               alb.data);

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::BindTexture(napi_env env,
                                              napi_callback_info info) {
  LOG_CALL("BindTexture");
  napi_status nstatus;

  WebGLRenderingContext *context = nullptr;

  uint32_t args[2];
  nstatus = GetContextUint32Params(env, info, &context, 2, args);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glBindTexture(args[0], args[1]);

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::CheckFramebufferStatus(
    napi_env env, napi_callback_info info) {
  LOG_CALL("CheckFramebufferStatus");
  napi_status nstatus;

  WebGLRenderingContext *context = nullptr;
  uint32_t arg_value;
  nstatus = GetContextUint32Params(env, info, &context, 1, &arg_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLuint status =
      context->eglContextWrapper_->glCheckFramebufferStatus(arg_value);

  napi_value status_value;
  nstatus = napi_create_uint32(env, status, &status_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

#if DEBUG
  context->CheckForErrors();
#endif
  return status_value;
}

/* static */
napi_value WebGLRenderingContext::Clear(napi_env env, napi_callback_info info) {
  LOG_CALL("Clear");

  WebGLRenderingContext *context = nullptr;
  GLbitfield mask;
  napi_status nstatus = GetContextUint32Params(env, info, &context, 1, &mask);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glClear(mask);

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::ClearColor(napi_env env,
                                             napi_callback_info info) {
  LOG_CALL("ClearColor");

  napi_status nstatus;

  double values[4];
  WebGLRenderingContext *context = nullptr;
  nstatus = GetContextDoubleParams(env, info, &context, 4, values);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glClearColor(values[0], values[1], values[2],
                                            values[3]);

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::ClearDepth(napi_env env,
                                             napi_callback_info info) {
  LOG_CALL("ClearDepth");

  napi_status nstatus;

  double depth;
  WebGLRenderingContext *context = nullptr;
  nstatus = GetContextDoubleParams(env, info, &context, 1, &depth);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glClearDepthf(static_cast<GLclampf>(depth));

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::ClearStencil(napi_env env,
                                               napi_callback_info info) {
  LOG_CALL("ClearStencil");

  napi_status nstatus;

  GLint s;
  WebGLRenderingContext *context = nullptr;
  nstatus = GetContextInt32Params(env, info, &context, 1, &s);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glClearStencil(s);

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::ClientWaitSync(napi_env env,
                                                 napi_callback_info info) {
  LOG_CALL("ClientWaitSync");

  napi_status nstatus;

  size_t argc = 3;
  napi_value args[3];
  napi_value js_this;

  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_ARGC_RETVAL(env, argc, 3, nullptr);

  ENSURE_VALUE_IS_OBJECT_RETVAL(env, args[0], nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[1], nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[2], nullptr);

  GLsync sync;
  nstatus = napi_unwrap(env, args[0], reinterpret_cast<void **>(&sync));
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLbitfield flags;
  nstatus = napi_get_value_uint32(env, args[1], &flags);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  // TODO(kreeger): Note that N-API doesn't have uint64 support.
  uint32_t timeout;
  nstatus = napi_get_value_uint32(env, args[2], &timeout);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLenum result =
      context->eglContextWrapper_->glClientWaitSync(sync, flags, timeout);

  napi_value result_value;
  nstatus = napi_create_uint32(env, result, &result_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

#if DEBUG
  context->CheckForErrors();
#endif
  return result_value;
}

/* static */
napi_value WebGLRenderingContext::ColorMask(napi_env env,
                                            napi_callback_info info) {
  LOG_CALL("ColorMask");

  napi_status nstatus;

  bool args[4];
  WebGLRenderingContext *context = nullptr;
  nstatus = GetContextBoolParams(env, info, &context, 4, args);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glColorMask(
      static_cast<GLboolean>(args[0]), static_cast<GLboolean>(args[1]),
      static_cast<GLboolean>(args[2]), static_cast<GLboolean>(args[3]));

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::CompileShader(napi_env env,
                                                napi_callback_info info) {
  LOG_CALL("CompileShader");
  WebGLRenderingContext *context = nullptr;
  GLuint shader;
  napi_status nstatus = GetContextUint32Params(env, info, &context, 1, &shader);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glCompileShader(shader);

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::CompressedTexImage2D(
    napi_env env, napi_callback_info info) {
  LOG_CALL("CompressedTexImage2D");

  napi_status nstatus;

  size_t argc = 7;
  napi_value args[7];
  napi_value js_this;

  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_ARGC_RETVAL(env, argc, 7, nullptr);

  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[0], nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[1], nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[2], nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[3], nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[4], nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[5], nullptr);

  GLenum target;
  nstatus = napi_get_value_uint32(env, args[0], &target);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLint level;
  nstatus = napi_get_value_int32(env, args[1], &level);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLenum internal_format;
  nstatus = napi_get_value_uint32(env, args[2], &internal_format);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLsizei width;
  nstatus = napi_get_value_int32(env, args[3], &width);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLsizei height;
  nstatus = napi_get_value_int32(env, args[4], &height);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLint border;
  nstatus = napi_get_value_int32(env, args[5], &border);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  ArrayLikeBuffer alb;
  nstatus = GetArrayLikeBuffer(env, args[6], &alb);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glCompressedTexImage2D(
      target, level, internal_format, width, height, border,
      static_cast<GLsizei>(alb.length), alb.data);

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::CompressedTexImage3D(
    napi_env env, napi_callback_info info) {
  LOG_CALL("CompressedTexImage3D");
  napi_status nstatus;

  size_t argc = 9;
  napi_value args[9];
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  if (argc != 8 && argc != 9) {
    NAPI_THROW_ERROR(env, "compressedTexImage3D expects 8 or 9 arguments");
    return nullptr;
  }

  for (size_t i = 0; i < 7; ++i) {
    ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[i], nullptr);
  }

  GLenum target;
  GLint level;
  GLenum internal_format;
  GLsizei width;
  GLsizei height;
  GLsizei depth;
  GLint border;
  nstatus = napi_get_value_uint32(env, args[0], &target);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  nstatus = napi_get_value_int32(env, args[1], &level);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  nstatus = napi_get_value_uint32(env, args[2], &internal_format);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  nstatus = napi_get_value_int32(env, args[3], &width);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  nstatus = napi_get_value_int32(env, args[4], &height);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  nstatus = napi_get_value_int32(env, args[5], &depth);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  nstatus = napi_get_value_int32(env, args[6], &border);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLsizei image_size = 0;
  const void *data = nullptr;
  ArrayLikeBuffer alb;
  if (argc == 8) {
    nstatus = GetArrayLikeBuffer(env, args[7], &alb);
    ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
    image_size = static_cast<GLsizei>(alb.length);
    data = alb.data;
  } else {
    uint32_t offset;
    nstatus = napi_get_value_int32(env, args[7], &image_size);
    ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
    nstatus = napi_get_value_uint32(env, args[8], &offset);
    ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
    data = reinterpret_cast<const void *>(static_cast<uintptr_t>(offset));
  }

  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_GL_PROC_RETVAL(env, context, glCompressedTexImage3D, nullptr);
  context->eglContextWrapper_->glCompressedTexImage3D(
      target, level, internal_format, width, height, depth, border, image_size,
      data);
#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::CompressedTexSubImage2D(
    napi_env env, napi_callback_info info) {
  LOG_CALL("CompressedTexSubImage2D");

  napi_status nstatus;

  size_t argc = 8;
  napi_value args[8];
  napi_value js_this;

  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_ARGC_RETVAL(env, argc, 8, nullptr);

  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[0], nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[1], nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[2], nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[3], nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[4], nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[5], nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[6], nullptr);

  GLenum target;
  nstatus = napi_get_value_uint32(env, args[0], &target);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLint level;
  nstatus = napi_get_value_int32(env, args[1], &level);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLint xoffset;
  nstatus = napi_get_value_int32(env, args[2], &xoffset);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLint yoffset;
  nstatus = napi_get_value_int32(env, args[3], &yoffset);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLsizei width;
  nstatus = napi_get_value_int32(env, args[4], &width);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLsizei height;
  nstatus = napi_get_value_int32(env, args[5], &height);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLenum format;
  nstatus = napi_get_value_uint32(env, args[6], &format);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  ArrayLikeBuffer alb;
  nstatus = GetArrayLikeBuffer(env, args[7], &alb);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glCompressedTexSubImage2D(
      target, level, xoffset, yoffset, width, height, format,
      static_cast<GLsizei>(alb.length), alb.data);

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::CompressedTexSubImage3D(
    napi_env env, napi_callback_info info) {
  LOG_CALL("CompressedTexSubImage3D");
  napi_status nstatus;

  size_t argc = 11;
  napi_value args[11];
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  if (argc != 10 && argc != 11) {
    NAPI_THROW_ERROR(env, "compressedTexSubImage3D expects 10 or 11 arguments");
    return nullptr;
  }

  for (size_t i = 0; i < 9; ++i) {
    ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[i], nullptr);
  }

  GLenum target;
  GLint level;
  GLint xoffset;
  GLint yoffset;
  GLint zoffset;
  GLsizei width;
  GLsizei height;
  GLsizei depth;
  GLenum format;
  nstatus = napi_get_value_uint32(env, args[0], &target);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  nstatus = napi_get_value_int32(env, args[1], &level);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  nstatus = napi_get_value_int32(env, args[2], &xoffset);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  nstatus = napi_get_value_int32(env, args[3], &yoffset);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  nstatus = napi_get_value_int32(env, args[4], &zoffset);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  nstatus = napi_get_value_int32(env, args[5], &width);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  nstatus = napi_get_value_int32(env, args[6], &height);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  nstatus = napi_get_value_int32(env, args[7], &depth);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  nstatus = napi_get_value_uint32(env, args[8], &format);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLsizei image_size = 0;
  const void *data = nullptr;
  ArrayLikeBuffer alb;
  if (argc == 10) {
    nstatus = GetArrayLikeBuffer(env, args[9], &alb);
    ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
    image_size = static_cast<GLsizei>(alb.length);
    data = alb.data;
  } else {
    uint32_t offset;
    nstatus = napi_get_value_int32(env, args[9], &image_size);
    ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
    nstatus = napi_get_value_uint32(env, args[10], &offset);
    ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
    data = reinterpret_cast<const void *>(static_cast<uintptr_t>(offset));
  }

  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_GL_PROC_RETVAL(env, context, glCompressedTexSubImage3D, nullptr);
  context->eglContextWrapper_->glCompressedTexSubImage3D(
      target, level, xoffset, yoffset, zoffset, width, height, depth, format,
      image_size, data);
#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::CopyBufferSubData(napi_env env,
                                                    napi_callback_info info) {
  LOG_CALL("CopyBufferSubData");
  WebGLRenderingContext *context = nullptr;
  uint32_t args[5];
  napi_status nstatus = GetContextUint32Params(env, info, &context, 5, args);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_GL_PROC_RETVAL(env, context, glCopyBufferSubData, nullptr);
  context->eglContextWrapper_->glCopyBufferSubData(args[0], args[1], args[2],
                                                   args[3], args[4]);
#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::CopyTexImage2D(napi_env env,
                                                 napi_callback_info info) {
  LOG_CALL("CopyTexImage2D");

  napi_status nstatus;

  size_t argc = 8;
  napi_value args[8];
  napi_value js_this;

  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_ARGC_RETVAL(env, argc, 8, nullptr);

  for (size_t i = 0; i < 8; i++) {
    ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[i], nullptr);
  }

  GLenum target;
  nstatus = napi_get_value_uint32(env, args[0], &target);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLint level;
  nstatus = napi_get_value_int32(env, args[1], &level);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLenum internalformat;
  nstatus = napi_get_value_uint32(env, args[2], &internalformat);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLint x;
  nstatus = napi_get_value_int32(env, args[3], &x);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLint y;
  nstatus = napi_get_value_int32(env, args[4], &y);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLsizei width;
  nstatus = napi_get_value_int32(env, args[5], &width);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLsizei height;
  nstatus = napi_get_value_int32(env, args[6], &height);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLint border;
  nstatus = napi_get_value_int32(env, args[7], &border);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glCopyTexImage2D(target, level, internalformat,
                                                x, y, width, height, border);

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::CopyTexSubImage2D(napi_env env,
                                                    napi_callback_info info) {
  LOG_CALL("CopyTexSubImage2D");

  napi_status nstatus;

  size_t argc = 8;
  napi_value args[8];
  napi_value js_this;

  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_ARGC_RETVAL(env, argc, 8, nullptr);

  for (size_t i = 0; i < 8; i++) {
    ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[i], nullptr);
  }

  GLenum target;
  nstatus = napi_get_value_uint32(env, args[0], &target);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLint level;
  nstatus = napi_get_value_int32(env, args[1], &level);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLint xoffset;
  nstatus = napi_get_value_int32(env, args[2], &xoffset);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLint yoffset;
  nstatus = napi_get_value_int32(env, args[3], &yoffset);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLint x;
  nstatus = napi_get_value_int32(env, args[4], &x);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLint y;
  nstatus = napi_get_value_int32(env, args[5], &y);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLsizei width;
  nstatus = napi_get_value_int32(env, args[6], &width);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLsizei height;
  nstatus = napi_get_value_int32(env, args[7], &height);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glCopyTexSubImage2D(
      target, level, xoffset, yoffset, x, y, width, height);

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::CopyTexSubImage3D(napi_env env,
                                                    napi_callback_info info) {
  LOG_CALL("CopyTexSubImage3D");
  WebGLRenderingContext *context = nullptr;
  int32_t args[9];
  napi_status nstatus = GetContextInt32Params(env, info, &context, 9, args);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_GL_PROC_RETVAL(env, context, glCopyTexSubImage3D, nullptr);
  context->eglContextWrapper_->glCopyTexSubImage3D(
      static_cast<GLenum>(args[0]), args[1], args[2], args[3], args[4], args[5],
      args[6], args[7], args[8]);
#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::CreateBuffer(napi_env env,
                                               napi_callback_info info) {
  LOG_CALL("CreateBuffer");
  napi_status nstatus;

  WebGLRenderingContext *context = nullptr;
  nstatus = GetContext(env, info, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLuint buffer;
  context->eglContextWrapper_->glGenBuffers(1, &buffer);

  // TODO(kreeger): Keep track of global objects.
  context->alloc_count_++;

  napi_value buffer_value;
  nstatus = napi_create_uint32(env, buffer, &buffer_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

#if DEBUG
  context->CheckForErrors();
#endif
  return buffer_value;
}

/* static */
napi_value WebGLRenderingContext::CreateFramebuffer(napi_env env,
                                                    napi_callback_info info) {
  LOG_CALL("CreateFrameBuffer");
  napi_status nstatus;

  WebGLRenderingContext *context = nullptr;
  nstatus = GetContext(env, info, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLuint buffer;
  context->eglContextWrapper_->glGenFramebuffers(1, &buffer);

  // TODO(kreeger): Keep track of global objects.
  context->alloc_count_++;

  napi_value frame_buffer_value;
  nstatus = napi_create_uint32(env, buffer, &frame_buffer_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

#if DEBUG
  context->CheckForErrors();
#endif
  return frame_buffer_value;
}

/* static */
napi_value WebGLRenderingContext::CreateProgram(napi_env env,
                                                napi_callback_info info) {
  LOG_CALL("CreateProgram");
  WebGLRenderingContext *context = nullptr;
  napi_status nstatus = GetContext(env, info, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLuint program = context->eglContextWrapper_->glCreateProgram();

  // TODO(kreeger): Keep track of global objects.
  context->alloc_count_++;

  napi_value program_value;
  nstatus = napi_create_uint32(env, program, &program_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

#if DEBUG
  context->CheckForErrors();
#endif
  return program_value;
}

/* static */
napi_value WebGLRenderingContext::CreateQuery(napi_env env,
                                              napi_callback_info info) {
  LOG_CALL("CreateQuery");
  WebGLRenderingContext *context = nullptr;
  napi_status nstatus = GetContext(env, info, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_GL_PROC_RETVAL(env, context, glGenQueries, nullptr);

  GLuint query;
  context->eglContextWrapper_->glGenQueries(1, &query);
  context->alloc_count_++;

  napi_value query_value;
  nstatus = napi_create_uint32(env, query, &query_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
#if DEBUG
  context->CheckForErrors();
#endif
  return query_value;
}

/* static */
napi_value WebGLRenderingContext::CreateRenderbuffer(napi_env env,
                                                     napi_callback_info info) {
  LOG_CALL("CreateRenderBuffer");

  WebGLRenderingContext *context = nullptr;
  napi_status nstatus = GetContext(env, info, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLuint renderbuffer;
  context->eglContextWrapper_->glGenRenderbuffers(1, &renderbuffer);

  napi_value renderbuffer_value;
  nstatus = napi_create_uint32(env, renderbuffer, &renderbuffer_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

#if DEBUG
  context->CheckForErrors();
#endif
  return renderbuffer_value;
}

/* static */
napi_value WebGLRenderingContext::CreateSampler(napi_env env,
                                                napi_callback_info info) {
  LOG_CALL("CreateSampler");
  WebGLRenderingContext *context = nullptr;
  napi_status nstatus = GetContext(env, info, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_GL_PROC_RETVAL(env, context, glGenSamplers, nullptr);

  GLuint sampler;
  context->eglContextWrapper_->glGenSamplers(1, &sampler);
  context->alloc_count_++;

  napi_value sampler_value;
  nstatus = napi_create_uint32(env, sampler, &sampler_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
#if DEBUG
  context->CheckForErrors();
#endif
  return sampler_value;
}

/* static */
napi_value WebGLRenderingContext::CreateShader(napi_env env,
                                               napi_callback_info info) {
  LOG_CALL("CreateShader");
  napi_status nstatus;

  WebGLRenderingContext *context = nullptr;
  GLenum shader_type;
  nstatus = GetContextUint32Params(env, info, &context, 1, &shader_type);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLuint shader = context->eglContextWrapper_->glCreateShader(shader_type);

  // TODO(kreeger): Keep track of global objects.
  context->alloc_count_++;

  napi_value shader_value;
  nstatus = napi_create_uint32(env, shader, &shader_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

#if DEBUG
  context->CheckForErrors();
#endif
  return shader_value;
}

/* static */
napi_value WebGLRenderingContext::CreateTexture(napi_env env,
                                                napi_callback_info info) {
  LOG_CALL("CreateTexture");

  WebGLRenderingContext *context = nullptr;
  napi_status nstatus = GetContext(env, info, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLuint texture;
  context->eglContextWrapper_->glGenTextures(1, &texture);

  // TODO(kreeger): Keep track of global objects.
  context->alloc_count_++;

  napi_value texture_value;
  nstatus = napi_create_uint32(env, texture, &texture_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

#if DEBUG
  context->CheckForErrors();
#endif
  return texture_value;
}

/* static */
napi_value WebGLRenderingContext::CreateTransformFeedback(
    napi_env env, napi_callback_info info) {
  LOG_CALL("CreateTransformFeedback");
  WebGLRenderingContext *context = nullptr;
  napi_status nstatus = GetContext(env, info, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_GL_PROC_RETVAL(env, context, glGenTransformFeedbacks, nullptr);

  GLuint transform_feedback;
  context->eglContextWrapper_->glGenTransformFeedbacks(1, &transform_feedback);
  context->alloc_count_++;

  napi_value transform_feedback_value;
  nstatus =
      napi_create_uint32(env, transform_feedback, &transform_feedback_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
#if DEBUG
  context->CheckForErrors();
#endif
  return transform_feedback_value;
}

/* static */
napi_value WebGLRenderingContext::CreateVertexArray(napi_env env,
                                                    napi_callback_info info) {
  LOG_CALL("CreateVertexArray");
  WebGLRenderingContext *context = nullptr;
  napi_status nstatus = GetContext(env, info, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_GL_PROC_RETVAL(env, context, glGenVertexArrays, nullptr);

  GLuint vertex_array;
  context->eglContextWrapper_->glGenVertexArrays(1, &vertex_array);
  context->alloc_count_++;

  napi_value vertex_array_value;
  nstatus = napi_create_uint32(env, vertex_array, &vertex_array_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
#if DEBUG
  context->CheckForErrors();
#endif
  return vertex_array_value;
}

/* static */
napi_value WebGLRenderingContext::CullFace(napi_env env,
                                           napi_callback_info info) {
  LOG_CALL("CullFace");

  WebGLRenderingContext *context = nullptr;
  GLenum mode;
  napi_status nstatus = GetContextUint32Params(env, info, &context, 1, &mode);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glCullFace(mode);

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::DeleteBuffer(napi_env env,
                                               napi_callback_info info) {
  LOG_CALL("DeleteBuffer");

  WebGLRenderingContext *context = nullptr;
  GLuint buffer;
  napi_status nstatus = GetContextUint32Params(env, info, &context, 1, &buffer);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glDeleteBuffers(1, &buffer);

  // TODO(kreeger): Keep track of global objects.
  context->alloc_count_--;
#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::DeleteFramebuffer(napi_env env,
                                                    napi_callback_info info) {
  LOG_CALL("DeleteFramebuffer");

  WebGLRenderingContext *context = nullptr;
  GLuint frame_buffer;
  napi_status nstatus =
      GetContextUint32Params(env, info, &context, 1, &frame_buffer);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glDeleteFramebuffers(1, &frame_buffer);

  // TODO(kreeger): Keep track of global objects.
  context->alloc_count_--;
#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::DeleteProgram(napi_env env,
                                                napi_callback_info info) {
  LOG_CALL("DeleteProgram");

  WebGLRenderingContext *context = nullptr;
  GLuint program;
  napi_status nstatus =
      GetContextUint32Params(env, info, &context, 1, &program);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glDeleteProgram(program);

  // TODO(kreeger): Keep track of global objects.
  context->alloc_count_--;
#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::DeleteQuery(napi_env env,
                                              napi_callback_info info) {
  LOG_CALL("DeleteQuery");
  WebGLRenderingContext *context = nullptr;
  GLuint query;
  napi_status nstatus = GetContextUint32Params(env, info, &context, 1, &query);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_GL_PROC_RETVAL(env, context, glDeleteQueries, nullptr);
  context->eglContextWrapper_->glDeleteQueries(1, &query);
  context->alloc_count_--;
#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::DeleteRenderbuffer(napi_env env,
                                                     napi_callback_info info) {
  LOG_CALL("DeleteRenderbuffer");

  WebGLRenderingContext *context = nullptr;
  GLuint renderbuffer;
  napi_status nstatus =
      GetContextUint32Params(env, info, &context, 1, &renderbuffer);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glDeleteRenderbuffers(1, &renderbuffer);

  // TODO(kreeger): Keep track of global objects.
  context->alloc_count_--;
#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::DeleteSampler(napi_env env,
                                                napi_callback_info info) {
  LOG_CALL("DeleteSampler");
  WebGLRenderingContext *context = nullptr;
  GLuint sampler;
  napi_status nstatus =
      GetContextUint32Params(env, info, &context, 1, &sampler);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_GL_PROC_RETVAL(env, context, glDeleteSamplers, nullptr);
  context->eglContextWrapper_->glDeleteSamplers(1, &sampler);
  context->alloc_count_--;
#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::DeleteShader(napi_env env,
                                               napi_callback_info info) {
  LOG_CALL("DeleteShader");

  WebGLRenderingContext *context = nullptr;
  GLuint shader;
  napi_status nstatus = GetContextUint32Params(env, info, &context, 1, &shader);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glDeleteShader(shader);

  // TODO(kreeger): Keep track of global objects.
  context->alloc_count_--;
#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::DeleteSync(napi_env env,
                                             napi_callback_info info) {
  LOG_CALL("DeleteSync");
  napi_status nstatus;

  size_t argc = 1;
  napi_value args[1];
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_ARGC_RETVAL(env, argc, 1, nullptr);

  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_GL_PROC_RETVAL(env, context, glDeleteSync, nullptr);

  GLsync sync = nullptr;
  nstatus = GetSyncParam(env, args[0], &sync);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  if (sync != nullptr) {
    context->eglContextWrapper_->glDeleteSync(sync);
    void *removed = nullptr;
    napi_remove_wrap(env, args[0], &removed);
  }
#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::DeleteTexture(napi_env env,
                                                napi_callback_info info) {
  LOG_CALL("DeleteTexture");

  WebGLRenderingContext *context = nullptr;
  GLuint texture;
  napi_status nstatus =
      GetContextUint32Params(env, info, &context, 1, &texture);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glDeleteTextures(1, &texture);

  // TODO(kreeger): Keep track of global objects.
  context->alloc_count_--;
#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::DeleteTransformFeedback(
    napi_env env, napi_callback_info info) {
  LOG_CALL("DeleteTransformFeedback");
  WebGLRenderingContext *context = nullptr;
  GLuint transform_feedback;
  napi_status nstatus =
      GetContextUint32Params(env, info, &context, 1, &transform_feedback);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_GL_PROC_RETVAL(env, context, glDeleteTransformFeedbacks, nullptr);
  context->eglContextWrapper_->glDeleteTransformFeedbacks(1,
                                                          &transform_feedback);
  context->alloc_count_--;
#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::DeleteVertexArray(napi_env env,
                                                    napi_callback_info info) {
  LOG_CALL("DeleteVertexArray");
  napi_status nstatus;

  size_t argc = 1;
  napi_value args[1];
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_ARGC_RETVAL(env, argc, 1, nullptr);

  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_GL_PROC_RETVAL(env, context, glDeleteVertexArrays, nullptr);

  GLuint vertex_array;
  nstatus = GetUint32AllowNull(env, args[0], &vertex_array);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  if (vertex_array != 0) {
    context->eglContextWrapper_->glDeleteVertexArrays(1, &vertex_array);
    context->alloc_count_--;
  }
#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::DepthFunc(napi_env env,
                                            napi_callback_info info) {
  LOG_CALL("DepthFunc");

  WebGLRenderingContext *context = nullptr;
  GLenum func;
  napi_status nstatus = GetContextUint32Params(env, info, &context, 1, &func);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glDepthFunc(func);

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::DepthMask(napi_env env,
                                            napi_callback_info info) {
  LOG_CALL("DepthMask");

  WebGLRenderingContext *context = nullptr;
  bool flag;
  napi_status nstatus = GetContextBoolParams(env, info, &context, 1, &flag);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glDepthMask(static_cast<GLboolean>(flag));

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::DepthRange(napi_env env,
                                             napi_callback_info info) {
  LOG_CALL("DepthRange");

  WebGLRenderingContext *context = nullptr;
  double args[2];
  napi_status nstatus = GetContextDoubleParams(env, info, &context, 2, args);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glDepthRangef(static_cast<GLfloat>(args[0]),
                                             static_cast<GLfloat>(args[1]));

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::DetachShader(napi_env env,
                                               napi_callback_info info) {
  LOG_CALL("DetachShader");

  WebGLRenderingContext *context = nullptr;
  GLuint args[2];
  napi_status nstatus = GetContextUint32Params(env, info, &context, 2, args);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glDetachShader(args[0], args[1]);

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::Disable(napi_env env,
                                          napi_callback_info info) {
  LOG_CALL("Disable");

  WebGLRenderingContext *context = nullptr;
  GLenum cap;
  napi_status nstatus = GetContextUint32Params(env, info, &context, 1, &cap);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glDisable(cap);

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::DisableVertexAttribArray(
    napi_env env, napi_callback_info info) {
  LOG_CALL("DisableVertexAttribArray");

  WebGLRenderingContext *context = nullptr;
  GLuint index;
  napi_status nstatus = GetContextUint32Params(env, info, &context, 1, &index);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glDisableVertexAttribArray(index);

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::DrawArrays(napi_env env,
                                             napi_callback_info info) {
  LOG_CALL("DrawArrays");

  napi_status nstatus;

  size_t argc = 3;
  napi_value args[3];
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_ARGC_RETVAL(env, argc, 3, nullptr);

  for (size_t i = 0; i < 3; i++) {
    ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[i], nullptr);
  }

  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLenum mode;
  nstatus = napi_get_value_uint32(env, args[0], &mode);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLint first;
  nstatus = napi_get_value_int32(env, args[1], &first);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLsizei count;
  nstatus = napi_get_value_int32(env, args[2], &count);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glDrawArrays(mode, first, count);

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::DrawArraysInstanced(napi_env env,
                                                      napi_callback_info info) {
  LOG_CALL("DrawArraysInstanced");
  WebGLRenderingContext *context = nullptr;
  int32_t args[4];
  napi_status nstatus = GetContextInt32Params(env, info, &context, 4, args);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_GL_PROC_RETVAL(env, context, glDrawArraysInstanced, nullptr);
  context->eglContextWrapper_->glDrawArraysInstanced(
      static_cast<GLenum>(args[0]), args[1], args[2], args[3]);
#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::DrawBuffers(napi_env env,
                                              napi_callback_info info) {
  LOG_CALL("DrawBuffers");
  napi_status nstatus;

  size_t argc = 1;
  napi_value args[1];
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_ARGC_RETVAL(env, argc, 1, nullptr);

  ArrayLikeBuffer alb(kUint32);
  nstatus = GetArrayLikeBuffer(env, args[0], &alb);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_GL_PROC_RETVAL(env, context, glDrawBuffers, nullptr);
  context->eglContextWrapper_->glDrawBuffers(static_cast<GLsizei>(alb.size()),
                                             static_cast<GLenum *>(alb.data));
#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::DrawElements(napi_env env,
                                               napi_callback_info info) {
  LOG_CALL("DrawElements");

  napi_status nstatus;

  size_t argc = 4;
  napi_value args[4];
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_ARGC_RETVAL(env, argc, 4, nullptr);

  for (size_t i = 0; i < 4; i++) {
    ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[i], nullptr);
  }

  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLenum mode;
  nstatus = napi_get_value_uint32(env, args[0], &mode);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLsizei count;
  nstatus = napi_get_value_int32(env, args[1], &count);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLenum type;
  nstatus = napi_get_value_uint32(env, args[2], &type);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  uint32_t offset;
  nstatus = napi_get_value_uint32(env, args[3], &offset);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glDrawElements(
      mode, count, type, reinterpret_cast<GLvoid *>(offset));

#if DEBUG
  context->CheckForErrors();
#endif

  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::DrawElementsInstanced(
    napi_env env, napi_callback_info info) {
  LOG_CALL("DrawElementsInstanced");
  napi_status nstatus;

  size_t argc = 5;
  napi_value args[5];
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_ARGC_RETVAL(env, argc, 5, nullptr);

  for (size_t i = 0; i < 5; i++) {
    ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[i], nullptr);
  }

  GLenum mode;
  GLsizei count;
  GLenum type;
  uint32_t offset;
  GLsizei instance_count;
  nstatus = napi_get_value_uint32(env, args[0], &mode);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  nstatus = napi_get_value_int32(env, args[1], &count);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  nstatus = napi_get_value_uint32(env, args[2], &type);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  nstatus = napi_get_value_uint32(env, args[3], &offset);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  nstatus = napi_get_value_int32(env, args[4], &instance_count);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_GL_PROC_RETVAL(env, context, glDrawElementsInstanced, nullptr);
  context->eglContextWrapper_->glDrawElementsInstanced(
      mode, count, type,
      reinterpret_cast<GLvoid *>(static_cast<uintptr_t>(offset)),
      instance_count);
#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::Enable(napi_env env,
                                         napi_callback_info info) {
  LOG_CALL("Enable");

  WebGLRenderingContext *context = nullptr;
  GLenum cap;
  napi_status nstatus = GetContextUint32Params(env, info, &context, 1, &cap);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glEnable(cap);

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::EnableVertexAttribArray(
    napi_env env, napi_callback_info info) {
  LOG_CALL("EnableVertexAttribArray");

  WebGLRenderingContext *context = nullptr;
  GLenum index;
  napi_status nstatus = GetContextUint32Params(env, info, &context, 1, &index);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glEnableVertexAttribArray(index);

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::EndQuery(napi_env env,
                                           napi_callback_info info) {
  LOG_CALL("EndQuery");
  WebGLRenderingContext *context = nullptr;
  GLenum target;
  napi_status nstatus = GetContextUint32Params(env, info, &context, 1, &target);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_GL_PROC_RETVAL(env, context, glEndQuery, nullptr);
  context->eglContextWrapper_->glEndQuery(target);
#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::EndTransformFeedback(
    napi_env env, napi_callback_info info) {
  LOG_CALL("EndTransformFeedback");
  WebGLRenderingContext *context = nullptr;
  napi_status nstatus = GetContext(env, info, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_GL_PROC_RETVAL(env, context, glEndTransformFeedback, nullptr);
  context->eglContextWrapper_->glEndTransformFeedback();
#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::FenceSynce(napi_env env,
                                             napi_callback_info info) {
  LOG_CALL("FenceSync");

  WebGLRenderingContext *context = nullptr;

  uint32_t args[2];
  napi_status nstatus = GetContextUint32Params(env, info, &context, 2, args);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLsync sync = context->eglContextWrapper_->glFenceSync(args[0], args[1]);

  napi_value sync_value;
  nstatus = WrapGLsync(env, sync, context->eglContextWrapper_, &sync_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

#if DEBUG
  context->CheckForErrors();
#endif
  return sync_value;
}

/* static */
napi_value WebGLRenderingContext::Finish(napi_env env,
                                         napi_callback_info info) {
  LOG_CALL("Finish");

  WebGLRenderingContext *context = nullptr;
  napi_status nstatus = GetContext(env, info, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glFinish();

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::GetError(napi_env env,
                                           napi_callback_info info) {
  LOG_CALL("GetError");
  napi_status nstatus;

  WebGLRenderingContext *context = nullptr;
  nstatus = GetContext(env, info, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLenum error = context->eglContextWrapper_->glGetError();

  napi_value error_value;
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  nstatus = napi_create_uint32(env, error, &error_value);

#if DEBUG
  context->CheckForErrors();
#endif
  return error_value;
}

/* static */
napi_value WebGLRenderingContext::GetFramebufferAttachmentParameter(
    napi_env env, napi_callback_info info) {
  LOG_CALL("GetFramebufferAttachmentParameter");
  napi_status nstatus;

  GLenum args[3];
  WebGLRenderingContext *context = nullptr;
  nstatus = GetContextUint32Params(env, info, &context, 3, args);

  GLint params;
  context->eglContextWrapper_->glGetFramebufferAttachmentParameteriv(
      args[0], args[1], args[2], &params);
#if DEBUG
  context->CheckForErrors();
#endif

  napi_value params_value;
  nstatus = napi_create_int32(env, params, &params_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  return params_value;
}

/* static */
napi_value WebGLRenderingContext::GetFragDataLocation(napi_env env,
                                                      napi_callback_info info) {
  LOG_CALL("GetFragDataLocation");
  napi_status nstatus;

  size_t argc = 2;
  napi_value args[2];
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_ARGC_RETVAL(env, argc, 2, nullptr);

  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[0], nullptr);
  GLuint program;
  nstatus = napi_get_value_uint32(env, args[0], &program);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  std::string name;
  nstatus = GetStringParam(env, args[1], name);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_GL_PROC_RETVAL(env, context, glGetFragDataLocation, nullptr);
  GLint location =
      context->eglContextWrapper_->glGetFragDataLocation(program, name.c_str());

  napi_value location_value;
  nstatus = napi_create_int32(env, location, &location_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  return location_value;
}

/* static */
napi_value WebGLRenderingContext::GetIndexedParameter(napi_env env,
                                                      napi_callback_info info) {
  LOG_CALL("GetIndexedParameter");
  WebGLRenderingContext *context = nullptr;
  uint32_t args[2];
  napi_status nstatus = GetContextUint32Params(env, info, &context, 2, args);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_GL_PROC_RETVAL(env, context, glGetIntegeri_v, nullptr);

  GLint param = 0;
  context->eglContextWrapper_->glGetIntegeri_v(args[0], args[1], &param);
  napi_value param_value;
  nstatus = napi_create_int32(env, param, &param_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  return param_value;
}

/* static */
napi_value WebGLRenderingContext::GetInternalformatParameter(
    napi_env env, napi_callback_info info) {
  LOG_CALL("GetInternalformatParameter");
  WebGLRenderingContext *context = nullptr;
  uint32_t args[3];
  napi_status nstatus = GetContextUint32Params(env, info, &context, 3, args);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_GL_PROC_RETVAL(env, context, glGetInternalformativ, nullptr);

  if (args[2] == GL_SAMPLES) {
    GLint count = 0;
    context->eglContextWrapper_->glGetInternalformativ(
        args[0], args[1], GL_NUM_SAMPLE_COUNTS, 1, &count);
    if (count <= 0) {
      napi_value empty;
      nstatus = napi_create_array_with_length(env, 0, &empty);
      ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
      return empty;
    }
    std::vector<GLint> samples(count);
    context->eglContextWrapper_->glGetInternalformativ(
        args[0], args[1], args[2], count, samples.data());
    napi_value sample_values;
    nstatus = CreateNumericArray<int32_t>(env, samples.data(), samples.size(),
                                          CreateInt32, &sample_values);
    ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
    return sample_values;
  }

  GLint param = 0;
  context->eglContextWrapper_->glGetInternalformativ(args[0], args[1], args[2],
                                                     1, &param);
  napi_value param_value;
  nstatus = napi_create_int32(env, param, &param_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  return param_value;
}

/* static */
napi_value WebGLRenderingContext::GetExtension(napi_env env,
                                               napi_callback_info info) {
  LOG_CALL("GetExtension");
  napi_status nstatus;

  size_t argc = 1;
  napi_value extension_value;
  napi_value js_this;
  nstatus =
      napi_get_cb_info(env, info, &argc, &extension_value, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_ARGC_RETVAL(env, argc, 1, nullptr);

  std::string extension_name;
  nstatus = GetStringParam(env, extension_value, extension_name);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  // TODO(kreeger): Extension stuff is super funny w/ WebGL vs. ANGLE. Many
  // different names and matching that needs to be done in this binding.

  const char *name = extension_name.c_str();
  EGLContextWrapper *egl_ctx = context->eglContextWrapper_;

  napi_value webgl_extension = nullptr;
  if (strcmp(name, "ANGLE_instanced_arrays") == 0 &&
      ANGLEInstancedArraysExtension::IsSupported(egl_ctx)) {
    nstatus = ANGLEInstancedArraysExtension::NewInstance(env, &webgl_extension,
                                                         egl_ctx);
  } else if (strcmp(name, "EXT_blend_minmax") == 0 &&
             EXTBlendMinmaxExtension::IsSupported(egl_ctx)) {
    nstatus =
        EXTBlendMinmaxExtension::NewInstance(env, &webgl_extension, egl_ctx);
  } else if ((strcmp(name, "EXT_color_buffer_float") == 0 ||
              strcmp(name, "WEBGL_color_buffer_float") == 0) &&
             EXTColorBufferFloatExtension::IsSupported(egl_ctx)) {
    nstatus = EXTColorBufferFloatExtension::NewInstance(env, &webgl_extension,
                                                        egl_ctx);
  } else if (strcmp(name, "EXT_color_buffer_half_float") == 0 &&
             EXTColorBufferHalfFloatExtension::IsSupported(egl_ctx)) {
    nstatus = EXTColorBufferHalfFloatExtension::NewInstance(
        env, &webgl_extension, egl_ctx);
  } else if (strcmp(name, "EXT_frag_depth") == 0 &&
             EXTFragDepthExtension::IsSupported(egl_ctx)) {
    nstatus =
        EXTFragDepthExtension::NewInstance(env, &webgl_extension, egl_ctx);
  } else if (strcmp(name, "EXT_sRGB") == 0 &&
             EXTSRGBExtension::IsSupported(egl_ctx)) {
    nstatus = EXTSRGBExtension::NewInstance(env, &webgl_extension, egl_ctx);
  } else if (strcmp(name, "EXT_shader_texture_lod") == 0 &&
             EXTShaderTextureLodExtension::IsSupported(egl_ctx)) {
    nstatus = EXTShaderTextureLodExtension::NewInstance(env, &webgl_extension,
                                                        egl_ctx);
  } else if (strcmp(name, "EXT_texture_filter_anisotropic") == 0 &&
             EXTTextureFilterAnisotropicExtension::IsSupported(egl_ctx)) {
    nstatus = EXTTextureFilterAnisotropicExtension::NewInstance(
        env, &webgl_extension, egl_ctx);
  } else if (strcmp(name, "OES_element_index_uint") == 0 &&
             OESElementIndexUintExtension::IsSupported(egl_ctx)) {
    nstatus = OESElementIndexUintExtension::NewInstance(env, &webgl_extension,
                                                        egl_ctx);
  } else if (strcmp(name, "OES_standard_derivatives") == 0 &&
             OESStandardDerivativesExtension::IsSupported(egl_ctx)) {
    nstatus = OESStandardDerivativesExtension::NewInstance(
        env, &webgl_extension, egl_ctx);
  } else if (strcmp(name, "OES_texture_float") == 0 &&
             OESTextureFloatExtension::IsSupported(egl_ctx)) {
    nstatus =
        OESTextureFloatExtension::NewInstance(env, &webgl_extension, egl_ctx);
  } else if (strcmp(name, "OES_texture_float_linear") == 0 &&
             OESTextureFloatLinearExtension::IsSupported(egl_ctx)) {
    nstatus = OESTextureFloatLinearExtension::NewInstance(env, &webgl_extension,
                                                          egl_ctx);
  } else if (strcmp(name, "OES_texture_half_float") == 0 &&
             OESTextureHalfFloatExtension::IsSupported(egl_ctx)) {
    nstatus = OESTextureHalfFloatExtension::NewInstance(env, &webgl_extension,
                                                        egl_ctx);
  } else if (strcmp(name, "OES_texture_half_float_linear") == 0 &&
             OESTextureHalfFloatLinearExtension::IsSupported(egl_ctx)) {
    nstatus = OESTextureHalfFloatLinearExtension::NewInstance(
        env, &webgl_extension, egl_ctx);
  } else if (strcmp(name, "WEBGL_debug_renderer_info") == 0 &&
             WebGLDebugRendererInfoExtension::IsSupported(egl_ctx)) {
    nstatus = WebGLDebugRendererInfoExtension::NewInstance(
        env, &webgl_extension, egl_ctx);
  } else if (strcmp(name, "WEBGL_depth_texture") == 0 &&
             WebGLDepthTextureExtension::IsSupported(egl_ctx)) {
    nstatus =
        WebGLDepthTextureExtension::NewInstance(env, &webgl_extension, egl_ctx);
  } else if (strcmp(name, "WEBGL_lose_context") == 0 &&
             WebGLLoseContextExtension::IsSupported(egl_ctx)) {
    nstatus =
        WebGLLoseContextExtension::NewInstance(env, &webgl_extension, egl_ctx);
  } else {
    fprintf(stderr, "Unsupported extension: %s\n", name);
    nstatus = napi_get_null(env, &webgl_extension);
  }
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

#if DEBUG
  context->CheckForErrors();
#endif
  return webgl_extension;
}

/* static */
napi_value WebGLRenderingContext::GetParameter(napi_env env,
                                               napi_callback_info info) {
  LOG_CALL("GetParameter");
  napi_status nstatus;

  GLenum name;
  WebGLRenderingContext *context = nullptr;
  nstatus = GetContextUint32Params(env, info, &context, 1, &name);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  switch (name) {
    case GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS:
    case GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS:
    case GL_MAX_CUBE_MAP_TEXTURE_SIZE:
    case GL_MAX_VERTEX_ATTRIBS:
    case GL_MAX_VERTEX_UNIFORM_VECTORS:
    case GL_MAX_VARYING_VECTORS:
    case GL_MAX_FRAGMENT_UNIFORM_VECTORS:
    case GL_MAX_TEXTURE_SIZE:
    case GL_MAX_TEXTURE_IMAGE_UNITS:
      GLint params;
      context->eglContextWrapper_->glGetIntegerv(name, &params);

      napi_value params_value;
      nstatus = napi_create_int32(env, params, &params_value);
      ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

      return params_value;

    case GL_VERSION: {
      const GLubyte *str = context->eglContextWrapper_->glGetString(name);
      if (str) {
        const char *str_c_str = reinterpret_cast<const char *>(str);
        napi_value str_value;
        nstatus = napi_create_string_utf8(env, str_c_str, strlen(str_c_str),
                                          &str_value);
        ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

        return str_value;
      }
      break;
    }

    case GL_VENDOR:
    case GL_RENDERER:
    case GL_SHADING_LANGUAGE_VERSION: {
      const GLubyte *str = context->eglContextWrapper_->glGetString(name);
      if (str) {
        const char *str_c_str = reinterpret_cast<const char *>(str);
        napi_value str_value;
        nstatus = napi_create_string_utf8(env, str_c_str, strlen(str_c_str),
                                          &str_value);
        ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

        return str_value;
      }
      break;
    }

    case GL_ARRAY_BUFFER_BINDING: {
      GLint previous_buffer = 0;
      context->eglContextWrapper_->glGetIntegerv(GL_ARRAY_BUFFER_BINDING,
                                                 &previous_buffer);

      napi_value previous_buffer_value;
      nstatus = napi_create_int32(env, previous_buffer, &previous_buffer_value);
      ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

      return previous_buffer_value;
    }

    case GL_BLEND:
    case GL_CULL_FACE:
    case GL_DEPTH_TEST:
    case GL_DITHER:
    case GL_POLYGON_OFFSET_FILL:
    case GL_SAMPLE_ALPHA_TO_COVERAGE:
    case GL_SAMPLE_COVERAGE:
    case GL_SCISSOR_TEST:
    case GL_STENCIL_TEST: {
      GLboolean enabled = context->eglContextWrapper_->glIsEnabled(name);
      napi_value enabled_value;
      nstatus = napi_get_boolean(env, enabled, &enabled_value);
      ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
      return enabled_value;
    }

    default: {
      GLint params;
      context->eglContextWrapper_->glGetIntegerv(name, &params);
      napi_value params_value;
      nstatus = napi_create_int32(env, params, &params_value);
      ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
      return params_value;
    }
  }

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::Flush(napi_env env, napi_callback_info info) {
  LOG_CALL("Flush");

  WebGLRenderingContext *context = nullptr;
  napi_status nstatus = GetContext(env, info, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glFlush();

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::FramebufferRenderbuffer(
    napi_env env, napi_callback_info info) {
  LOG_CALL("FramebufferRenderbuffer");

  napi_status nstatus;

  uint32_t args[4];
  WebGLRenderingContext *context = nullptr;
  nstatus = GetContextUint32Params(env, info, &context, 4, args);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glFramebufferRenderbuffer(args[0], args[1],
                                                         args[2], args[3]);

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::FramebufferTexture2D(
    napi_env env, napi_callback_info info) {
  LOG_CALL("FramebufferTexture2D");
  napi_status nstatus;

  size_t argc = 5;
  napi_value args[5];
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_ARGC_RETVAL(env, argc, 5, nullptr);

  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[0], nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[1], nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[2], nullptr);

  // The texture can be null
  napi_valuetype value_type;
  nstatus = napi_typeof(env, args[3], &value_type);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  if (value_type != napi_null) {
    ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[3], nullptr);
  }

  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[4], nullptr);

  GLenum target;
  nstatus = napi_get_value_uint32(env, args[0], &target);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLenum attachment;
  nstatus = napi_get_value_uint32(env, args[1], &attachment);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLenum textarget;
  nstatus = napi_get_value_uint32(env, args[2], &textarget);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLuint texture = 0;
  if (value_type != napi_null) {
    nstatus = napi_get_value_uint32(env, args[3], &texture);
    ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  }

  GLint level;
  nstatus = napi_get_value_int32(env, args[4], &level);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glFramebufferTexture2D(
      target, attachment, textarget, texture, level);

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::FramebufferTextureLayer(
    napi_env env, napi_callback_info info) {
  LOG_CALL("FramebufferTextureLayer");
  napi_status nstatus;

  size_t argc = 5;
  napi_value args[5];
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_ARGC_RETVAL(env, argc, 5, nullptr);

  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[0], nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[1], nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[3], nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[4], nullptr);

  GLenum target;
  GLenum attachment;
  GLuint texture;
  GLint level;
  GLint layer;
  nstatus = napi_get_value_uint32(env, args[0], &target);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  nstatus = napi_get_value_uint32(env, args[1], &attachment);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  nstatus = GetUint32AllowNull(env, args[2], &texture);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  nstatus = napi_get_value_int32(env, args[3], &level);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  nstatus = napi_get_value_int32(env, args[4], &layer);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_GL_PROC_RETVAL(env, context, glFramebufferTextureLayer, nullptr);
  context->eglContextWrapper_->glFramebufferTextureLayer(target, attachment,
                                                         texture, level, layer);
#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::FrontFace(napi_env env,
                                            napi_callback_info info) {
  LOG_CALL("FrontFace");

  WebGLRenderingContext *context = nullptr;
  GLenum mode;
  napi_status nstatus = GetContextUint32Params(env, info, &context, 1, &mode);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glFrontFace(mode);

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::GenerateMipmap(napi_env env,
                                                 napi_callback_info info) {
  LOG_CALL("GenerateMipmap");

  WebGLRenderingContext *context = nullptr;
  GLenum target;
  napi_status nstatus = GetContextUint32Params(env, info, &context, 1, &target);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glGenerateMipmap(target);

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::GetAttachedShaders(napi_env env,
                                                     napi_callback_info info) {
  LOG_CALL("GetAttachedShaders");

  WebGLRenderingContext *context = nullptr;
  GLenum program;
  napi_status nstatus =
      GetContextUint32Params(env, info, &context, 1, &program);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLint attached_shader_count;
  context->eglContextWrapper_->glGetProgramiv(program, GL_ATTACHED_SHADERS,
                                              &attached_shader_count);
#if DEBUG
  context->CheckForErrors();
#endif

  GLsizei count;
  std::vector<GLuint> shaders;
  shaders.resize(attached_shader_count);
  context->eglContextWrapper_->glGetAttachedShaders(
      program, attached_shader_count, &count, shaders.data());
#if DEBUG
  context->CheckForErrors();
#endif

  napi_value shaders_array_value;
  nstatus = napi_create_array_with_length(env, count, &shaders_array_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  for (GLsizei i = 0; i < count; i++) {
    napi_value shader_value;
    nstatus = napi_create_uint32(env, shaders[i], &shader_value);
    ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

    nstatus = napi_set_element(env, shaders_array_value, i, shader_value);
    ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  }

  return shaders_array_value;
}

/* static */
napi_value WebGLRenderingContext::GetActiveAttrib(napi_env env,
                                                  napi_callback_info info) {
  LOG_CALL("GetActiveAttrib");

  napi_status nstatus;

  WebGLRenderingContext *context = nullptr;
  GLuint args[2];
  nstatus = GetContextUint32Params(env, info, &context, 2, args);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLint max_attr_length;
  context->eglContextWrapper_->glGetProgramiv(
      args[0], GL_ACTIVE_ATTRIBUTE_MAX_LENGTH, &max_attr_length);

  GLsizei length = 0;
  GLsizei size;
  GLenum type;

  AutoBuffer<char> buffer(max_attr_length);
  context->eglContextWrapper_->glGetActiveAttrib(
      args[0], args[1], max_attr_length, &length, &size, &type, buffer.get());

#if DEBUG
  context->CheckForErrors();
#endif

  if (length <= 0) {
    // Attribute not found - return nullptr.
    return nullptr;
  }

  napi_value name_value;
  nstatus = napi_create_string_utf8(env, buffer.get(), length, &name_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  napi_value size_value;
  nstatus = napi_create_int32(env, size, &size_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  napi_value type_value;
  nstatus = napi_create_uint32(env, type, &type_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  napi_value active_info_value;
  nstatus = napi_create_object(env, &active_info_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  nstatus = napi_set_named_property(env, active_info_value, "name", name_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  nstatus = napi_set_named_property(env, active_info_value, "size", size_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  nstatus = napi_set_named_property(env, active_info_value, "type", type_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  return active_info_value;
}

/* static */
napi_value WebGLRenderingContext::GetAttribLocation(napi_env env,
                                                    napi_callback_info info) {
  LOG_CALL("GetAttribLocation");
  napi_status nstatus;

  size_t argc = 2;
  napi_value args[2];
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_ARGC_RETVAL(env, argc, 2, nullptr);

  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[0], nullptr);
  GLuint program;
  nstatus = napi_get_value_uint32(env, args[0], &program);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  std::string attrib_name;
  nstatus = GetStringParam(env, args[1], attrib_name);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLint location = context->eglContextWrapper_->glGetAttribLocation(
      program, attrib_name.c_str());

  napi_value location_value;
  nstatus = napi_create_int32(env, location, &location_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

#if DEBUG
  context->CheckForErrors();
#endif
  return location_value;
}

/* static */
napi_value WebGLRenderingContext::GetActiveUniform(napi_env env,
                                                   napi_callback_info info) {
  LOG_CALL("GetActiveUniform");

  napi_status nstatus;

  WebGLRenderingContext *context = nullptr;
  GLuint args[2];
  nstatus = GetContextUint32Params(env, info, &context, 2, args);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLint max_uniform_length;
  context->eglContextWrapper_->glGetProgramiv(
      args[0], GL_ACTIVE_UNIFORM_MAX_LENGTH, &max_uniform_length);

  GLsizei length = 0;
  GLsizei size;
  GLenum type;

  AutoBuffer<char> buffer(max_uniform_length);
  context->eglContextWrapper_->glGetActiveUniform(args[0], args[1],
                                                  max_uniform_length, &length,
                                                  &size, &type, buffer.get());

#if DEBUG
  context->CheckForErrors();
#endif

  if (length <= 0) {
    // Attribute not found - return nullptr.
    return nullptr;
  }

  napi_value name_value;
  nstatus = napi_create_string_utf8(env, buffer.get(), length, &name_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  napi_value size_value;
  nstatus = napi_create_int32(env, size, &size_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  napi_value type_value;
  nstatus = napi_create_uint32(env, type, &type_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  napi_value active_info_value;
  nstatus = napi_create_object(env, &active_info_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  nstatus = napi_set_named_property(env, active_info_value, "name", name_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  nstatus = napi_set_named_property(env, active_info_value, "size", size_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  nstatus = napi_set_named_property(env, active_info_value, "type", type_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  return active_info_value;
}

/* static */
napi_value WebGLRenderingContext::GetBufferParameter(napi_env env,
                                                     napi_callback_info info) {
  LOG_CALL("GetBufferParameter");
  napi_status nstatus;

  GLenum args[2];
  WebGLRenderingContext *context = nullptr;
  nstatus = GetContextUint32Params(env, info, &context, 2, args);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLint params;
  context->eglContextWrapper_->glGetBufferParameteriv(args[0], args[1],
                                                      &params);
#if DEBUG
  context->CheckForErrors();
#endif

  napi_value params_value;
  nstatus = napi_create_int32(env, params, &params_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  return params_value;
}

/* static */
napi_value WebGLRenderingContext::GetBufferSubData(napi_env env,
                                                   napi_callback_info info) {
  napi_status nstatus;

  // TODO(kreeger): This method requires 3 - but takes upto 5 args.
  size_t argc = 3;
  napi_value args[3];
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_ARGC_RETVAL(env, argc, 3, nullptr);

  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[0], nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[1], nullptr);

  GLenum target;
  nstatus = napi_get_value_uint32(env, args[0], &target);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  // N-API does not support signed long ints:
  uint32_t offset;
  nstatus = napi_get_value_uint32(env, args[1], &offset);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  ArrayLikeBuffer alb;
  nstatus = GetArrayLikeBuffer(env, args[2], &alb);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  void *buffer = context->eglContextWrapper_->glMapBufferRange(
      target, offset, alb.length, GL_MAP_READ_BIT);
#if DEBUG
  context->CheckForErrors();
#endif
  memcpy(alb.data, buffer, alb.length);

  context->eglContextWrapper_->glUnmapBuffer(target);
#if DEBUG
  context->CheckForErrors();
#endif

  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::GetContextAttributes(
    napi_env env, napi_callback_info info) {
  LOG_CALL("GetContextAttributes");

  napi_status nstatus;

  napi_value context_attr_value;
  nstatus = napi_create_object(env, &context_attr_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  // TODO(kreeger): These context values should be stored at creation time.
  napi_value alpha_value;
  nstatus = napi_get_boolean(env, true, &alpha_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  nstatus =
      napi_set_named_property(env, context_attr_value, "alpha", alpha_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  napi_value antialias_value;
  nstatus = napi_get_boolean(env, true, &antialias_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  nstatus = napi_set_named_property(env, context_attr_value, "antialias",
                                    antialias_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  napi_value depth_value;
  nstatus = napi_get_boolean(env, true, &depth_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  nstatus =
      napi_set_named_property(env, context_attr_value, "depth", depth_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  napi_value fail_if_major_performance_caveat_value;
  nstatus =
      napi_get_boolean(env, false, &fail_if_major_performance_caveat_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  nstatus = napi_set_named_property(env, context_attr_value,
                                    "failIfMajorPerformanceCaveat",
                                    fail_if_major_performance_caveat_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  const char *default_value = "default";
  napi_value power_pref_value;
  nstatus = napi_create_string_utf8(env, default_value,
                                    strnlen(default_value, NAPI_STRING_SIZE),
                                    &power_pref_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  nstatus = napi_set_named_property(env, context_attr_value, "powerPreference",
                                    power_pref_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  napi_value premultiplied_alpha_value;
  nstatus = napi_get_boolean(env, true, &premultiplied_alpha_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  nstatus = napi_set_named_property(
      env, context_attr_value, "premultipliedAlpha", premultiplied_alpha_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  napi_value preserve_drawing_buffer_value;
  nstatus = napi_get_boolean(env, true, &preserve_drawing_buffer_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  nstatus =
      napi_set_named_property(env, context_attr_value, "preserveDrawingBuffer",
                              preserve_drawing_buffer_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  napi_value stencil_value;
  nstatus = napi_get_boolean(env, true, &stencil_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  nstatus = napi_set_named_property(env, context_attr_value, "stencil",
                                    stencil_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  return context_attr_value;
}

/* static */
napi_value WebGLRenderingContext::GetProgramInfoLog(napi_env env,
                                                    napi_callback_info info) {
  LOG_CALL("GetProgramInfoLog");
  napi_status nstatus;

  WebGLRenderingContext *context = nullptr;
  GLuint program;
  nstatus = GetContextUint32Params(env, info, &context, 1, &program);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLint log_length;
  context->eglContextWrapper_->glGetProgramiv(program, GL_INFO_LOG_LENGTH,
                                              &log_length);

  char *error = new char[log_length];
  context->eglContextWrapper_->glGetProgramInfoLog(program, log_length,
                                                   &log_length, error);

  napi_value error_value;
  nstatus = napi_create_string_utf8(env, error, log_length, &error_value);
  delete[] error;
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

#if DEBUG
  context->CheckForErrors();
#endif
  return error_value;
}

/* static */
napi_value WebGLRenderingContext::GetProgramParameter(napi_env env,
                                                      napi_callback_info info) {
  LOG_CALL("GetProgramParameter");
  napi_status nstatus;

  WebGLRenderingContext *context = nullptr;
  uint32_t args[2];
  nstatus = GetContextUint32Params(env, info, &context, 2, args);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLint param;
  context->eglContextWrapper_->glGetProgramiv(args[0], args[1], &param);

  napi_value param_value;

  switch (args[1]) {
    case GL_DELETE_STATUS:
    case GL_LINK_STATUS:
    case GL_VALIDATE_STATUS:
      nstatus = napi_get_boolean(env, param, &param_value);
      break;
    default:
      nstatus = napi_create_int32(env, param, &param_value);
      break;
  }

  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

#if DEBUG
  context->CheckForErrors();
#endif
  return param_value;
}

/* static */
napi_value WebGLRenderingContext::GetQuery(napi_env env,
                                           napi_callback_info info) {
  LOG_CALL("GetQuery");
  WebGLRenderingContext *context = nullptr;
  uint32_t args[2];
  napi_status nstatus = GetContextUint32Params(env, info, &context, 2, args);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_GL_PROC_RETVAL(env, context, glGetQueryiv, nullptr);

  GLint param = 0;
  context->eglContextWrapper_->glGetQueryiv(args[0], args[1], &param);
  napi_value param_value;
  nstatus = napi_create_int32(env, param, &param_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  return param_value;
}

/* static */
napi_value WebGLRenderingContext::GetQueryParameter(napi_env env,
                                                    napi_callback_info info) {
  LOG_CALL("GetQueryParameter");
  WebGLRenderingContext *context = nullptr;
  uint32_t args[2];
  napi_status nstatus = GetContextUint32Params(env, info, &context, 2, args);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_GL_PROC_RETVAL(env, context, glGetQueryObjectuiv, nullptr);

  GLuint param = 0;
  context->eglContextWrapper_->glGetQueryObjectuiv(args[0], args[1], &param);
  napi_value param_value;
  if (args[1] == GL_QUERY_RESULT_AVAILABLE) {
    nstatus = napi_get_boolean(env, param != 0, &param_value);
  } else {
    nstatus = napi_create_uint32(env, param, &param_value);
  }
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  return param_value;
}

/* static */
napi_value WebGLRenderingContext::GetRenderbufferParameter(
    napi_env env, napi_callback_info info) {
  LOG_CALL("GetRenderbufferParameter");
  napi_status nstatus;

  WebGLRenderingContext *context = nullptr;
  GLenum args[2];
  nstatus = GetContextUint32Params(env, info, &context, 2, args);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLint params;
  context->eglContextWrapper_->glGetRenderbufferParameteriv(args[0], args[1],
                                                            &params);

  napi_value params_value;
  nstatus = napi_create_int32(env, params, &params_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

#if DEBUG
  context->CheckForErrors();
#endif
  return params_value;
}

/* static */
napi_value WebGLRenderingContext::GetSamplerParameter(napi_env env,
                                                      napi_callback_info info) {
  LOG_CALL("GetSamplerParameter");
  WebGLRenderingContext *context = nullptr;
  uint32_t args[2];
  napi_status nstatus = GetContextUint32Params(env, info, &context, 2, args);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_GL_PROC_RETVAL(env, context, glGetSamplerParameteriv, nullptr);

  napi_value params_value;
  switch (args[1]) {
    case GL_TEXTURE_MAX_LOD:
    case GL_TEXTURE_MIN_LOD: {
      ENSURE_GL_PROC_RETVAL(env, context, glGetSamplerParameterfv, nullptr);
      GLfloat param;
      context->eglContextWrapper_->glGetSamplerParameterfv(args[0], args[1],
                                                           &param);
      nstatus = napi_create_double(env, param, &params_value);
      break;
    }
    default: {
      GLint param;
      context->eglContextWrapper_->glGetSamplerParameteriv(args[0], args[1],
                                                           &param);
      nstatus = napi_create_int32(env, param, &params_value);
      break;
    }
  }
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  return params_value;
}

/* static */
napi_value WebGLRenderingContext::GetShaderPrecisionFormat(
    napi_env env, napi_callback_info info) {
  LOG_CALL("GetShaderPrecisionFormat");
  napi_status nstatus;

  WebGLRenderingContext *context = nullptr;
  GLenum args[2];
  nstatus = GetContextUint32Params(env, info, &context, 2, args);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLint range[2];
  GLint precision;
  context->eglContextWrapper_->glGetShaderPrecisionFormat(args[0], args[1],
                                                          range, &precision);
#if DEBUG
  context->CheckForErrors();
#endif

  napi_value precision_value;
  nstatus = napi_create_int32(env, precision, &precision_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  napi_value range_min_value;
  nstatus = napi_create_int32(env, range[0], &range_min_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  napi_value range_max_value;
  nstatus = napi_create_int32(env, range[1], &range_max_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  napi_value precision_format_value;
  nstatus = napi_create_object(env, &precision_format_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  nstatus = napi_set_named_property(env, precision_format_value, "precision",
                                    precision_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  nstatus = napi_set_named_property(env, precision_format_value, "rangeMin",
                                    range_min_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  nstatus = napi_set_named_property(env, precision_format_value, "rangeMax",
                                    range_max_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  return precision_format_value;
}

/* static */
napi_value WebGLRenderingContext::GetShaderInfoLog(napi_env env,
                                                   napi_callback_info info) {
  LOG_CALL("GetShaderInfoLog");
  napi_status nstatus;

  WebGLRenderingContext *context = nullptr;
  GLuint shader;
  nstatus = GetContextUint32Params(env, info, &context, 1, &shader);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLint log_length;
  context->eglContextWrapper_->glGetShaderiv(shader, GL_INFO_LOG_LENGTH,
                                             &log_length);

  char *error = new char[log_length];
  context->eglContextWrapper_->glGetShaderInfoLog(shader, log_length,
                                                  &log_length, error);

  napi_value error_value;
  nstatus = napi_create_string_utf8(env, error, log_length, &error_value);
  delete[] error;
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

#if DEBUG
  context->CheckForErrors();
#endif
  return error_value;
}

/* static */
napi_value WebGLRenderingContext::GetShaderParameter(napi_env env,
                                                     napi_callback_info info) {
  LOG_CALL("GetShaderParameter");
  napi_status nstatus;

  WebGLRenderingContext *context = nullptr;
  uint32_t arg_values[2];
  nstatus = GetContextUint32Params(env, info, &context, 2, arg_values);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLint param;
  context->eglContextWrapper_->glGetShaderiv(arg_values[0], arg_values[1],
                                             &param);

  napi_value param_value;

  switch (arg_values[1]) {
    case GL_DELETE_STATUS:
    case GL_COMPILE_STATUS:
      nstatus = napi_get_boolean(env, param, &param_value);
      break;
    default:
      nstatus = napi_create_int32(env, param, &param_value);
      break;
  }

  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

#if DEBUG
  context->CheckForErrors();
#endif
  return param_value;
}

/* static */
napi_value WebGLRenderingContext::GetSyncParameter(napi_env env,
                                                   napi_callback_info info) {
  LOG_CALL("GetSyncParameter");
  napi_status nstatus;

  size_t argc = 2;
  napi_value args[2];
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_ARGC_RETVAL(env, argc, 2, nullptr);

  GLsync sync = nullptr;
  nstatus = GetSyncParam(env, args[0], &sync);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[1], nullptr);
  GLenum pname;
  nstatus = napi_get_value_uint32(env, args[1], &pname);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_GL_PROC_RETVAL(env, context, glGetSynciv, nullptr);

  GLint param = 0;
  GLsizei length = 0;
  context->eglContextWrapper_->glGetSynciv(sync, pname, 1, &length, &param);
  napi_value param_value;
  nstatus = napi_create_int32(env, param, &param_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  return param_value;
}

/* static */
napi_value WebGLRenderingContext::GetSupportedExtensions(
    napi_env env, napi_callback_info info) {
  LOG_CALL("GetSupportedExtensions");

  WebGLRenderingContext *context = nullptr;
  napi_status nstatus;
  nstatus = GetContext(env, info, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  napi_value extensions_value;
  nstatus = napi_create_array(env, &extensions_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->RefreshGLExtensions();

  std::string s(context->eglContextWrapper_->angle_requestable_extensions
                    ->GetExtensions());

  std::string delim = " ";
  size_t pos = 0;
  uint32_t index = 0;
  std::string token;
  while ((pos = s.find(delim)) != std::string::npos) {
    token = s.substr(0, pos);
    s.erase(0, pos + delim.length());

    napi_value str_value;
    nstatus =
        napi_create_string_utf8(env, token.c_str(), token.size(), &str_value);
    ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

    nstatus = napi_set_element(env, extensions_value, index++, str_value);
    ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  }

  return extensions_value;
}

/* static */
napi_value WebGLRenderingContext::GetTexParameter(napi_env env,
                                                  napi_callback_info info) {
  LOG_CALL("GetTexParameter");

  napi_status nstatus;

  WebGLRenderingContext *context = nullptr;
  GLenum args[2];
  nstatus = GetContextUint32Params(env, info, &context, 2, args);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  napi_value params_value;

  switch (args[1]) {
    case GL_TEXTURE_MAX_ANISOTROPY_EXT:
    case GL_TEXTURE_MAX_LOD:
    case GL_TEXTURE_MIN_LOD: {
      GLfloat params;
      context->eglContextWrapper_->glGetTexParameterfv(args[0], args[1],
                                                       &params);
#if DEBUG
      context->CheckForErrors();
#endif

      nstatus = napi_create_double(env, params, &params_value);
      ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
      break;
    }
    case GL_TEXTURE_MAG_FILTER:
    case GL_TEXTURE_MIN_FILTER:
    case GL_TEXTURE_WRAP_S:
    case GL_TEXTURE_WRAP_T:
    case GL_TEXTURE_COMPARE_FUNC:
    case GL_TEXTURE_COMPARE_MODE:
    case GL_TEXTURE_WRAP_R:
    case GL_TEXTURE_IMMUTABLE_LEVELS: {
      GLint params;
      context->eglContextWrapper_->glGetTexParameteriv(args[0], args[1],
                                                       &params);
#if DEBUG
      context->CheckForErrors();
#endif

      nstatus = napi_create_uint32(env, params, &params_value);
      ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
      break;
    }
    case GL_TEXTURE_BASE_LEVEL:
    case GL_TEXTURE_MAX_LEVEL: {
      GLint params;
      context->eglContextWrapper_->glGetTexParameteriv(args[0], args[1],
                                                       &params);
#if DEBUG
      context->CheckForErrors();
#endif

      nstatus = napi_create_int32(env, params, &params_value);
      ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
      break;
    }
    case GL_TEXTURE_IMMUTABLE_FORMAT: {
      GLint params;
      context->eglContextWrapper_->glGetTexParameteriv(args[0], args[1],
                                                       &params);
#if DEBUG
      context->CheckForErrors();
#endif

      nstatus = napi_get_boolean(env, params, &params_value);
      ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
      break;
    }
    default:
      NAPI_THROW_ERROR(env, "Invalid argument");
      return nullptr;
  }

  return params_value;
}

/* static */
napi_value WebGLRenderingContext::GetTransformFeedbackVarying(
    napi_env env, napi_callback_info info) {
  LOG_CALL("GetTransformFeedbackVarying");
  napi_status nstatus;

  WebGLRenderingContext *context = nullptr;
  GLuint args[2];
  nstatus = GetContextUint32Params(env, info, &context, 2, args);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_GL_PROC_RETVAL(env, context, glGetTransformFeedbackVarying, nullptr);

  GLint max_length = 0;
  context->eglContextWrapper_->glGetProgramiv(
      args[0], GL_TRANSFORM_FEEDBACK_VARYING_MAX_LENGTH, &max_length);

  GLsizei length = 0;
  GLsizei size = 0;
  GLenum type = 0;
  AutoBuffer<char> buffer(max_length > 0 ? max_length : 1);
  context->eglContextWrapper_->glGetTransformFeedbackVarying(
      args[0], args[1], max_length, &length, &size, &type, buffer.get());

  if (length <= 0) {
    return nullptr;
  }

  napi_value name_value;
  nstatus = napi_create_string_utf8(env, buffer.get(), length, &name_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  napi_value size_value;
  nstatus = napi_create_int32(env, size, &size_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  napi_value type_value;
  nstatus = napi_create_uint32(env, type, &type_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  napi_value active_info_value;
  nstatus = napi_create_object(env, &active_info_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  nstatus = napi_set_named_property(env, active_info_value, "name", name_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  nstatus = napi_set_named_property(env, active_info_value, "size", size_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  nstatus = napi_set_named_property(env, active_info_value, "type", type_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  return active_info_value;
}

/* static */
napi_value WebGLRenderingContext::GetUniformLocation(napi_env env,
                                                     napi_callback_info info) {
  LOG_CALL("GetUniformLocation");
  napi_status nstatus;

  size_t argc = 2;
  napi_value args[2];
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_ARGC_RETVAL(env, argc, 2, nullptr);

  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[0], nullptr);
  GLuint program;
  nstatus = napi_get_value_uint32(env, args[0], &program);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  std::string uniform_name;
  nstatus = GetStringParam(env, args[1], uniform_name);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLint location = context->eglContextWrapper_->glGetUniformLocation(
      program, uniform_name.c_str());

  napi_value location_value;
  nstatus = napi_create_int32(env, location, &location_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

#if DEBUG
  context->CheckForErrors();
#endif
  return location_value;
}

/* static */
napi_value WebGLRenderingContext::GetVertexAttribIiv(napi_env env,
                                                     napi_callback_info info) {
  LOG_CALL("GetVertexAttribIiv");
  WebGLRenderingContext *context = nullptr;
  uint32_t args[2];
  napi_status nstatus = GetContextUint32Params(env, info, &context, 2, args);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_GL_PROC_RETVAL(env, context, glGetVertexAttribIiv, nullptr);

  GLint params[4] = {0, 0, 0, 0};
  context->eglContextWrapper_->glGetVertexAttribIiv(args[0], args[1], params);
  napi_value params_value;
  nstatus =
      CreateNumericArray<int32_t>(env, params, 4, CreateInt32, &params_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  return params_value;
}

/* static */
napi_value WebGLRenderingContext::GetVertexAttribIuiv(napi_env env,
                                                      napi_callback_info info) {
  LOG_CALL("GetVertexAttribIuiv");
  WebGLRenderingContext *context = nullptr;
  uint32_t args[2];
  napi_status nstatus = GetContextUint32Params(env, info, &context, 2, args);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_GL_PROC_RETVAL(env, context, glGetVertexAttribIuiv, nullptr);

  GLuint params[4] = {0, 0, 0, 0};
  context->eglContextWrapper_->glGetVertexAttribIuiv(args[0], args[1], params);
  napi_value params_value;
  nstatus =
      CreateNumericArray<uint32_t>(env, params, 4, CreateUint32, &params_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  return params_value;
}

/* static */
napi_value WebGLRenderingContext::Hint(napi_env env, napi_callback_info info) {
  LOG_CALL("Hint");
  napi_status nstatus;

  WebGLRenderingContext *context = nullptr;
  GLenum args[2];
  nstatus = GetContextUint32Params(env, info, &context, 2, args);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glHint(args[0], args[1]);

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::IsBuffer(napi_env env,
                                           napi_callback_info info) {
  LOG_CALL("IsBuffer");
  napi_status nstatus;

  WebGLRenderingContext *context = nullptr;
  GLuint buffer;
  nstatus = GetContextUint32Params(env, info, &context, 1, &buffer);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLboolean is_buffer = context->eglContextWrapper_->glIsBuffer(buffer);

  napi_value result_value;
  nstatus = napi_get_boolean(env, is_buffer, &result_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

#if DEBUG
  context->CheckForErrors();
#endif
  return result_value;
}

/* static */
napi_value WebGLRenderingContext::IsContextLost(napi_env env,
                                                napi_callback_info info) {
  LOG_CALL("IsContextLost");
  napi_status nstatus;

  // Headless bindings never lose context:
  napi_value result_value;
  nstatus = napi_get_boolean(env, false, &result_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  return result_value;
}

/* static */
napi_value WebGLRenderingContext::IsEnabled(napi_env env,
                                            napi_callback_info info) {
  LOG_CALL("IsEnabled");
  napi_status nstatus;

  WebGLRenderingContext *context = nullptr;
  GLenum cap;
  nstatus = GetContextUint32Params(env, info, &context, 1, &cap);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLboolean is_enabled = context->eglContextWrapper_->glIsEnabled(cap);

  napi_value result_value;
  nstatus = napi_get_boolean(env, is_enabled, &result_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

#if DEBUG
  context->CheckForErrors();
#endif
  return result_value;
}

/* static */
napi_value WebGLRenderingContext::IsFramebuffer(napi_env env,
                                                napi_callback_info info) {
  LOG_CALL("IsFramebuffer");
  napi_status nstatus;

  WebGLRenderingContext *context = nullptr;
  GLuint framebuffer;
  nstatus = GetContextUint32Params(env, info, &context, 1, &framebuffer);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLboolean is_framebuffer =
      context->eglContextWrapper_->glIsFramebuffer(framebuffer);

  napi_value result_value;
  nstatus = napi_get_boolean(env, is_framebuffer, &result_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

#if DEBUG
  context->CheckForErrors();
#endif
  return result_value;
}

/* static */
napi_value WebGLRenderingContext::IsProgram(napi_env env,
                                            napi_callback_info info) {
  LOG_CALL("IsProgram");
  napi_status nstatus;

  WebGLRenderingContext *context = nullptr;
  GLuint program;
  nstatus = GetContextUint32Params(env, info, &context, 1, &program);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLboolean is_program = context->eglContextWrapper_->glIsProgram(program);

  napi_value result_value;
  nstatus = napi_get_boolean(env, is_program, &result_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

#if DEBUG
  context->CheckForErrors();
#endif
  return result_value;
}

/* static */
napi_value WebGLRenderingContext::IsQuery(napi_env env,
                                          napi_callback_info info) {
  LOG_CALL("IsQuery");
  WebGLRenderingContext *context = nullptr;
  GLuint query;
  napi_status nstatus = GetContextUint32Params(env, info, &context, 1, &query);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_GL_PROC_RETVAL(env, context, glIsQuery, nullptr);
  GLboolean result = context->eglContextWrapper_->glIsQuery(query);
  napi_value result_value;
  nstatus = napi_get_boolean(env, result, &result_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  return result_value;
}

/* static */
napi_value WebGLRenderingContext::IsRenderbuffer(napi_env env,
                                                 napi_callback_info info) {
  LOG_CALL("IsRenderbuffer");
  napi_status nstatus;

  WebGLRenderingContext *context = nullptr;
  GLuint render_buffer;
  nstatus = GetContextUint32Params(env, info, &context, 1, &render_buffer);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLboolean is_renderbuffer =
      context->eglContextWrapper_->glIsRenderbuffer(render_buffer);

  napi_value result_value;
  nstatus = napi_get_boolean(env, is_renderbuffer, &result_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

#if DEBUG
  context->CheckForErrors();
#endif
  return result_value;
}

/* static */
napi_value WebGLRenderingContext::IsSampler(napi_env env,
                                            napi_callback_info info) {
  LOG_CALL("IsSampler");
  WebGLRenderingContext *context = nullptr;
  GLuint sampler;
  napi_status nstatus =
      GetContextUint32Params(env, info, &context, 1, &sampler);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_GL_PROC_RETVAL(env, context, glIsSampler, nullptr);
  GLboolean result = context->eglContextWrapper_->glIsSampler(sampler);
  napi_value result_value;
  nstatus = napi_get_boolean(env, result, &result_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  return result_value;
}

/* static */
napi_value WebGLRenderingContext::IsShader(napi_env env,
                                           napi_callback_info info) {
  LOG_CALL("IsShader");
  napi_status nstatus;

  WebGLRenderingContext *context = nullptr;
  GLuint shader;
  nstatus = GetContextUint32Params(env, info, &context, 1, &shader);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLboolean is_shader = context->eglContextWrapper_->glIsShader(shader);

  napi_value result_value;
  nstatus = napi_get_boolean(env, is_shader, &result_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

#if DEBUG
  context->CheckForErrors();
#endif
  return result_value;
}

/* static */
napi_value WebGLRenderingContext::IsSync(napi_env env,
                                         napi_callback_info info) {
  LOG_CALL("IsSync");
  napi_status nstatus;

  size_t argc = 1;
  napi_value args[1];
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_ARGC_RETVAL(env, argc, 1, nullptr);

  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_GL_PROC_RETVAL(env, context, glIsSync, nullptr);

  GLsync sync = nullptr;
  nstatus = GetSyncParam(env, args[0], &sync);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  GLboolean result =
      sync ? context->eglContextWrapper_->glIsSync(sync) : GL_FALSE;
  napi_value result_value;
  nstatus = napi_get_boolean(env, result, &result_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  return result_value;
}

/* static */
napi_value WebGLRenderingContext::IsTexture(napi_env env,
                                            napi_callback_info info) {
  LOG_CALL("IsTexture");
  napi_status nstatus;

  WebGLRenderingContext *context = nullptr;
  GLuint texture;
  nstatus = GetContextUint32Params(env, info, &context, 1, &texture);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLboolean is_texture = context->eglContextWrapper_->glIsTexture(texture);

  napi_value result_value;
  nstatus = napi_get_boolean(env, is_texture, &result_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

#if DEBUG
  context->CheckForErrors();
#endif
  return result_value;
}

/* static */
napi_value WebGLRenderingContext::IsTransformFeedback(napi_env env,
                                                      napi_callback_info info) {
  LOG_CALL("IsTransformFeedback");
  WebGLRenderingContext *context = nullptr;
  GLuint transform_feedback;
  napi_status nstatus =
      GetContextUint32Params(env, info, &context, 1, &transform_feedback);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_GL_PROC_RETVAL(env, context, glIsTransformFeedback, nullptr);
  GLboolean result =
      context->eglContextWrapper_->glIsTransformFeedback(transform_feedback);
  napi_value result_value;
  nstatus = napi_get_boolean(env, result, &result_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  return result_value;
}

/* static */
napi_value WebGLRenderingContext::IsVertexArray(napi_env env,
                                                napi_callback_info info) {
  LOG_CALL("IsVertexArray");
  napi_status nstatus;

  size_t argc = 1;
  napi_value args[1];
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_ARGC_RETVAL(env, argc, 1, nullptr);

  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_GL_PROC_RETVAL(env, context, glIsVertexArray, nullptr);

  GLuint vertex_array;
  nstatus = GetUint32AllowNull(env, args[0], &vertex_array);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  GLboolean result =
      vertex_array ? context->eglContextWrapper_->glIsVertexArray(vertex_array)
                   : GL_FALSE;
  napi_value result_value;
  nstatus = napi_get_boolean(env, result, &result_value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  return result_value;
}

/* static */
napi_value WebGLRenderingContext::InvalidateFramebuffer(
    napi_env env, napi_callback_info info) {
  LOG_CALL("InvalidateFramebuffer");
  napi_status nstatus;

  size_t argc = 2;
  napi_value args[2];
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_ARGC_RETVAL(env, argc, 2, nullptr);

  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[0], nullptr);
  GLenum target;
  nstatus = napi_get_value_uint32(env, args[0], &target);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  ArrayLikeBuffer alb(kUint32);
  nstatus = GetArrayLikeBuffer(env, args[1], &alb);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_GL_PROC_RETVAL(env, context, glInvalidateFramebuffer, nullptr);
  context->eglContextWrapper_->glInvalidateFramebuffer(
      target, static_cast<GLsizei>(alb.size()),
      static_cast<const GLenum *>(alb.data));
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::InvalidateSubFramebuffer(
    napi_env env, napi_callback_info info) {
  LOG_CALL("InvalidateSubFramebuffer");
  napi_status nstatus;

  size_t argc = 6;
  napi_value args[6];
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_ARGC_RETVAL(env, argc, 6, nullptr);

  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[0], nullptr);
  GLenum target;
  nstatus = napi_get_value_uint32(env, args[0], &target);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ArrayLikeBuffer alb(kUint32);
  nstatus = GetArrayLikeBuffer(env, args[1], &alb);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  int32_t rect[4];
  for (size_t i = 0; i < 4; ++i) {
    ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[i + 2], nullptr);
    nstatus = napi_get_value_int32(env, args[i + 2], &rect[i]);
    ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  }

  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_GL_PROC_RETVAL(env, context, glInvalidateSubFramebuffer, nullptr);
  context->eglContextWrapper_->glInvalidateSubFramebuffer(
      target, static_cast<GLsizei>(alb.size()),
      static_cast<const GLenum *>(alb.data), rect[0], rect[1], rect[2],
      rect[3]);
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::LineWidth(napi_env env,
                                            napi_callback_info info) {
  LOG_CALL("LineWidth");

  WebGLRenderingContext *context = nullptr;
  double width;
  napi_status nstatus = GetContextDoubleParams(env, info, &context, 1, &width);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glLineWidth(static_cast<GLfloat>(width));

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::LinkProgram(napi_env env,
                                              napi_callback_info info) {
  LOG_CALL("LinkProgram");

  WebGLRenderingContext *context = nullptr;
  GLuint program;
  napi_status nstatus =
      GetContextUint32Params(env, info, &context, 1, &program);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glLinkProgram(program);

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::PixelStorei(napi_env env,
                                              napi_callback_info info) {
  LOG_CALL("PixelStorei");

  napi_status nstatus;
  size_t argc = 2;
  napi_value args[2];
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_ARGC_RETVAL(env, argc, 2, nullptr);

  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[0], nullptr);
  napi_valuetype result;
  nstatus = napi_typeof(env, args[1], &result);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  if (result == napi_boolean) {
    ENSURE_VALUE_IS_BOOLEAN_RETVAL(env, args[1], nullptr);
  } else {
    ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[1], nullptr);
  }

  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLenum pname;
  nstatus = napi_get_value_uint32(env, args[0], &pname);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLint param;
  if (result == napi_boolean) {
    nstatus = napi_get_value_bool(env, args[1], (bool *)&param);
    ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  } else {
    nstatus = napi_get_value_int32(env, args[1], &param);
    ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  }

  context->eglContextWrapper_->glPixelStorei(pname, param);

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::PolygonOffset(napi_env env,
                                                napi_callback_info info) {
  LOG_CALL("PolygonOffset");

  WebGLRenderingContext *context = nullptr;
  double args[2];
  napi_status nstatus = GetContextDoubleParams(env, info, &context, 2, args);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glPolygonOffset(static_cast<GLfloat>(args[0]),
                                               static_cast<GLfloat>(args[1]));

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::ReadPixels(napi_env env,
                                             napi_callback_info info) {
  LOG_CALL("ReadPixels");

  napi_status nstatus;

  size_t argc = 7;
  napi_value args[7];
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_ARGC_RETVAL(env, argc, 7, nullptr);

  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[0], nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[1], nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[2], nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[3], nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[4], nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[5], nullptr);

  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLint x;
  nstatus = napi_get_value_int32(env, args[0], &x);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLint y;
  nstatus = napi_get_value_int32(env, args[1], &y);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLsizei width;
  nstatus = napi_get_value_int32(env, args[2], &width);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLsizei height;
  nstatus = napi_get_value_int32(env, args[3], &height);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLenum format;
  nstatus = napi_get_value_uint32(env, args[4], &format);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLenum type;
  nstatus = napi_get_value_uint32(env, args[5], &type);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  ArrayLikeBuffer alb;
  nstatus = GetArrayLikeBuffer(env, args[6], &alb);
  if (nstatus != napi_ok) {
    napi_valuetype value_type;
    nstatus = napi_typeof(env, args[6], &value_type);
    ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

    if (value_type != napi_number) {
      NAPI_THROW_ERROR(env, "Invalid value passed for data buffer");
      return nullptr;
    }
  }

  context->eglContextWrapper_->glReadPixels(x, y, width, height, format, type,
                                            alb.data);

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::ReadBuffer(napi_env env,
                                             napi_callback_info info) {
  LOG_CALL("ReadBuffer");
  WebGLRenderingContext *context = nullptr;
  GLenum src;
  napi_status nstatus = GetContextUint32Params(env, info, &context, 1, &src);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_GL_PROC_RETVAL(env, context, glReadBuffer, nullptr);
  context->eglContextWrapper_->glReadBuffer(src);
#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::RenderbufferStorage(napi_env env,
                                                      napi_callback_info info) {
  LOG_CALL("RenderbufferStorage");

  napi_status nstatus;

  size_t argc = 4;
  napi_value args[4];
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_ARGC_RETVAL(env, argc, 4, nullptr);

  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[0], nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[1], nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[2], nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[3], nullptr);

  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLenum target;
  nstatus = napi_get_value_uint32(env, args[0], &target);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLenum internal_format;
  nstatus = napi_get_value_uint32(env, args[1], &internal_format);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLsizei width;
  nstatus = napi_get_value_int32(env, args[2], &width);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLsizei height;
  nstatus = napi_get_value_int32(env, args[3], &height);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glRenderbufferStorage(target, internal_format,
                                                     width, height);

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::RenderbufferStorageMultisample(
    napi_env env, napi_callback_info info) {
  LOG_CALL("RenderbufferStorageMultisample");
  WebGLRenderingContext *context = nullptr;
  uint32_t args[5];
  napi_status nstatus = GetContextUint32Params(env, info, &context, 5, args);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_GL_PROC_RETVAL(env, context, glRenderbufferStorageMultisample,
                        nullptr);
  context->eglContextWrapper_->glRenderbufferStorageMultisample(
      args[0], args[1], args[2], args[3], args[4]);
#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::SampleCoverage(napi_env env,
                                                 napi_callback_info info) {
  LOG_CALL("Scissor");
  napi_status nstatus;

  size_t argc = 2;
  napi_value args[2];
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_ARGC_RETVAL(env, argc, 2, nullptr);

  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[0], nullptr);
  ENSURE_VALUE_IS_BOOLEAN_RETVAL(env, args[1], nullptr);

  double value;
  nstatus = napi_get_value_double(env, args[0], &value);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  bool invert;
  nstatus = napi_get_value_bool(env, args[1], &invert);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glSampleCoverage(static_cast<GLclampf>(value),
                                                static_cast<GLboolean>(invert));

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::SamplerParameterf(napi_env env,
                                                    napi_callback_info info) {
  LOG_CALL("SamplerParameterf");
  napi_status nstatus;
  size_t argc = 3;
  napi_value args[3];
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_ARGC_RETVAL(env, argc, 3, nullptr);

  uint32_t sampler;
  uint32_t pname;
  double param;
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[0], nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[1], nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[2], nullptr);
  nstatus = napi_get_value_uint32(env, args[0], &sampler);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  nstatus = napi_get_value_uint32(env, args[1], &pname);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  nstatus = napi_get_value_double(env, args[2], &param);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_GL_PROC_RETVAL(env, context, glSamplerParameterf, nullptr);
  context->eglContextWrapper_->glSamplerParameterf(sampler, pname,
                                                   static_cast<GLfloat>(param));
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::SamplerParameteri(napi_env env,
                                                    napi_callback_info info) {
  LOG_CALL("SamplerParameteri");
  WebGLRenderingContext *context = nullptr;
  uint32_t args[3];
  napi_status nstatus = GetContextUint32Params(env, info, &context, 3, args);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_GL_PROC_RETVAL(env, context, glSamplerParameteri, nullptr);
  context->eglContextWrapper_->glSamplerParameteri(args[0], args[1], args[2]);
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::Scissor(napi_env env,
                                          napi_callback_info info) {
  LOG_CALL("Scissor");
  napi_status nstatus;

  size_t argc = 4;
  napi_value args[4];
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_ARGC_RETVAL(env, argc, 4, nullptr);

  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[0], nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[1], nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[2], nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[3], nullptr);

  GLint x;
  nstatus = napi_get_value_int32(env, args[0], &x);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLint y;
  nstatus = napi_get_value_int32(env, args[1], &y);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLsizei width;
  nstatus = napi_get_value_int32(env, args[2], &width);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLsizei height;
  nstatus = napi_get_value_int32(env, args[3], &height);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glScissor(x, y, width, height);

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::TexImage2D(napi_env env,
                                             napi_callback_info info) {
  LOG_CALL("TexImage2D");

  napi_status nstatus;

  size_t argc = 9;
  napi_value args[9];
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLsizei width;
  GLsizei height;
  GLsizei border;
  GLenum format;
  GLint type;
  ArrayLikeBuffer alb;

  // texImage2D has a WebGL1 API that only takes 6 args intead of 9. This
  // argument is in place to allow the user to pass an HTML element. Handle
  // the only types that are available to get the required properties.
  if (argc == 6) {
    ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[0], nullptr);
    ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[1], nullptr);
    ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[2], nullptr);
    ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[3], nullptr);
    ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[4], nullptr);
    ENSURE_VALUE_IS_OBJECT_RETVAL(env, args[5], nullptr);

    nstatus = napi_get_value_uint32(env, args[3], &format);
    ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

    nstatus = napi_get_value_int32(env, args[4], &type);
    ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

    napi_value width_value;
    nstatus = napi_get_named_property(env, args[5], "width", &width_value);
    ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

    nstatus = napi_get_value_int32(env, width_value, &width);
    ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

    napi_value height_value;
    nstatus = napi_get_named_property(env, args[5], "height", &height_value);
    ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

    nstatus = napi_get_value_int32(env, height_value, &height);
    ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

    // Default border to 0
    // TODO(kreeger): Consider looking this up if a property exists.
    border = 0;

    // Ensure that the object has at least a field named 'data'. All other
    // objects are not supported at this time.
    bool has_data_property = false;
    nstatus = napi_has_named_property(env, args[5], "data", &has_data_property);
    ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

    if (!has_data_property) {
      NAPI_THROW_ERROR(env, "Image types must have a property named 'data'!");
      return nullptr;
    }

    napi_value data_value;
    nstatus = napi_get_named_property(env, args[5], "data", &data_value);
    ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

    nstatus = GetArrayLikeBuffer(env, data_value, &alb);
    ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  } else {
    // If argc is not 6, it should match arguments for OpenGL ES API.
    ENSURE_ARGC_RETVAL(env, argc, 9, nullptr);

    ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[0], nullptr);
    ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[1], nullptr);
    ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[2], nullptr);
    ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[3], nullptr);
    ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[4], nullptr);
    ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[5], nullptr);
    ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[6], nullptr);
    ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[7], nullptr);

    nstatus = napi_get_value_int32(env, args[3], &width);
    ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

    nstatus = napi_get_value_int32(env, args[4], &height);
    ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

    nstatus = napi_get_value_int32(env, args[5], &border);
    ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

    nstatus = napi_get_value_uint32(env, args[6], &format);
    ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

    nstatus = napi_get_value_int32(env, args[7], &type);
    ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

    napi_valuetype value_type;
    nstatus = napi_typeof(env, args[8], &value_type);
    ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

    if (value_type != napi_null && value_type != napi_undefined) {
      nstatus = GetArrayLikeBuffer(env, args[8], &alb);
      ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
    }
  }

  GLenum target;
  nstatus = napi_get_value_uint32(env, args[0], &target);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLint level;
  nstatus = napi_get_value_int32(env, args[1], &level);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLenum internal_format;
  nstatus = napi_get_value_uint32(env, args[2], &internal_format);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glTexImage2D(target, level, internal_format,
                                            width, height, border, format, type,
                                            alb.data);

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::TexImage3D(napi_env env,
                                             napi_callback_info info) {
  LOG_CALL("TexImage3D");
  napi_status nstatus;

  size_t argc = 10;
  napi_value args[10];
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_ARGC_RETVAL(env, argc, 10, nullptr);

  for (size_t i = 0; i < 9; ++i) {
    ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[i], nullptr);
  }

  GLenum target;
  GLint level;
  GLenum internal_format;
  GLsizei width;
  GLsizei height;
  GLsizei depth;
  GLint border;
  GLenum format;
  GLenum type;
  nstatus = napi_get_value_uint32(env, args[0], &target);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  nstatus = napi_get_value_int32(env, args[1], &level);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  nstatus = napi_get_value_uint32(env, args[2], &internal_format);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  nstatus = napi_get_value_int32(env, args[3], &width);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  nstatus = napi_get_value_int32(env, args[4], &height);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  nstatus = napi_get_value_int32(env, args[5], &depth);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  nstatus = napi_get_value_int32(env, args[6], &border);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  nstatus = napi_get_value_uint32(env, args[7], &format);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  nstatus = napi_get_value_uint32(env, args[8], &type);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  const void *data = nullptr;
  ArrayLikeBuffer alb;
  napi_valuetype value_type;
  nstatus = napi_typeof(env, args[9], &value_type);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  if (value_type == napi_number) {
    uint32_t offset;
    nstatus = napi_get_value_uint32(env, args[9], &offset);
    ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
    data = reinterpret_cast<const void *>(static_cast<uintptr_t>(offset));
  } else if (value_type != napi_null) {
    nstatus = GetArrayLikeBuffer(env, args[9], &alb);
    ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
    data = alb.data;
  }

  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_GL_PROC_RETVAL(env, context, glTexImage3D, nullptr);
  context->eglContextWrapper_->glTexImage3D(target, level, internal_format,
                                            width, height, depth, border,
                                            format, type, data);
#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::ShaderSource(napi_env env,
                                               napi_callback_info info) {
  LOG_CALL("ShaderSource");
  napi_status nstatus;

  size_t argc = 2;
  napi_value args[2];
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_ARGC_RETVAL(env, argc, 2, nullptr);

  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[0], nullptr);
  GLuint shader;
  nstatus = napi_get_value_uint32(env, args[0], &shader);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  std::string source;
  nstatus = GetStringParam(env, args[1], source);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLint length = source.size();
  const char *codes[] = {source.c_str()};
  context->eglContextWrapper_->glShaderSource(shader, 1, codes, &length);

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::StencilFunc(napi_env env,
                                              napi_callback_info info) {
  LOG_CALL("StencilFunc");
  napi_status nstatus;

  size_t argc = 3;
  napi_value args[3];
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_ARGC_RETVAL(env, argc, 3, nullptr);

  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[0], nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[1], nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[2], nullptr);

  GLenum func;
  nstatus = napi_get_value_uint32(env, args[0], &func);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLint ref;
  nstatus = napi_get_value_int32(env, args[1], &ref);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLuint mask;
  nstatus = napi_get_value_uint32(env, args[2], &mask);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glStencilFunc(func, ref, mask);

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::StencilFuncSeparate(napi_env env,
                                                      napi_callback_info info) {
  LOG_CALL("StencilFuncSeparate");
  napi_status nstatus;

  size_t argc = 4;
  napi_value args[4];
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_ARGC_RETVAL(env, argc, 4, nullptr);

  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[0], nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[1], nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[2], nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[3], nullptr);

  GLenum face;
  nstatus = napi_get_value_uint32(env, args[0], &face);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLenum func;
  nstatus = napi_get_value_uint32(env, args[1], &func);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLint ref;
  nstatus = napi_get_value_int32(env, args[2], &ref);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLuint mask;
  nstatus = napi_get_value_uint32(env, args[3], &mask);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glStencilFuncSeparate(face, func, ref, mask);

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::StencilMask(napi_env env,
                                              napi_callback_info info) {
  LOG_CALL("StencilMask");
  GLuint mask;
  WebGLRenderingContext *context = nullptr;
  napi_status nstatus = GetContextUint32Params(env, info, &context, 1, &mask);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glStencilMask(mask);

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::StencilMaskSeparate(napi_env env,
                                                      napi_callback_info info) {
  LOG_CALL("StencilMaskSeparate");

  uint32_t args[2];
  WebGLRenderingContext *context = nullptr;
  napi_status nstatus = GetContextUint32Params(env, info, &context, 2, args);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glStencilMaskSeparate(
      static_cast<GLenum>(args[0]), static_cast<GLuint>(args[1]));

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::StencilOp(napi_env env,
                                            napi_callback_info info) {
  LOG_CALL("StencilOp");

  GLenum args[3];
  WebGLRenderingContext *context = nullptr;
  napi_status nstatus = GetContextUint32Params(env, info, &context, 3, args);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glStencilOp(args[0], args[1], args[2]);

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::StencilOpSeparate(napi_env env,
                                                    napi_callback_info info) {
  LOG_CALL("StencilOpSeparate");

  GLenum args[4];
  WebGLRenderingContext *context = nullptr;
  napi_status nstatus = GetContextUint32Params(env, info, &context, 4, args);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glStencilOpSeparate(args[0], args[1], args[2],
                                                   args[3]);

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::TexParameteri(napi_env env,
                                                napi_callback_info info) {
  LOG_CALL("TexParameteri");
  napi_status nstatus;

  size_t argc = 3;
  napi_value args[3];
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_ARGC_RETVAL(env, argc, 3, nullptr);

  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[0], nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[1], nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[2], nullptr);

  GLenum target;
  nstatus = napi_get_value_uint32(env, args[0], &target);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLenum pname;
  nstatus = napi_get_value_uint32(env, args[1], &pname);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLint param;
  nstatus = napi_get_value_int32(env, args[2], &param);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glTexParameteri(target, pname, param);

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::TexParameterf(napi_env env,
                                                napi_callback_info info) {
  LOG_CALL("TexParameterf");
  napi_status nstatus;

  size_t argc = 3;
  napi_value args[3];
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_ARGC_RETVAL(env, argc, 3, nullptr);

  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[0], nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[1], nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[2], nullptr);

  GLenum target;
  nstatus = napi_get_value_uint32(env, args[0], &target);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLenum pname;
  nstatus = napi_get_value_uint32(env, args[1], &pname);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  double param;
  nstatus = napi_get_value_double(env, args[2], &param);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glTexParameterf(target, pname,
                                               static_cast<GLfloat>(param));

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

napi_value WebGLRenderingContext::TexSubImage2D(napi_env env,
                                                napi_callback_info info) {
  LOG_CALL("TexSubImage2D");
  napi_status nstatus;

  size_t argc = 9;
  napi_value args[9];
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_ARGC_RETVAL(env, argc, 9, nullptr);

  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[0], nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[1], nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[2], nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[3], nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[4], nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[5], nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[6], nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[7], nullptr);

  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLenum target;
  nstatus = napi_get_value_uint32(env, args[0], &target);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLint level;
  nstatus = napi_get_value_int32(env, args[1], &level);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLint xoffset;
  nstatus = napi_get_value_int32(env, args[2], &xoffset);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLint yoffset;
  nstatus = napi_get_value_int32(env, args[3], &yoffset);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLsizei width;
  nstatus = napi_get_value_int32(env, args[4], &width);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLsizei height;
  nstatus = napi_get_value_int32(env, args[5], &height);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLenum format;
  nstatus = napi_get_value_uint32(env, args[6], &format);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLenum type;
  nstatus = napi_get_value_uint32(env, args[7], &type);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  ArrayLikeBuffer alb;
  nstatus = GetArrayLikeBuffer(env, args[8], &alb);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glTexSubImage2D(
      target, level, xoffset, yoffset, width, height, format, type, alb.data);

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::TexSubImage3D(napi_env env,
                                                napi_callback_info info) {
  LOG_CALL("TexSubImage3D");
  napi_status nstatus;

  size_t argc = 11;
  napi_value args[11];
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_ARGC_RETVAL(env, argc, 11, nullptr);

  for (size_t i = 0; i < 10; ++i) {
    ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[i], nullptr);
  }

  GLenum target;
  GLint level;
  GLint xoffset;
  GLint yoffset;
  GLint zoffset;
  GLsizei width;
  GLsizei height;
  GLsizei depth;
  GLenum format;
  GLenum type;
  nstatus = napi_get_value_uint32(env, args[0], &target);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  nstatus = napi_get_value_int32(env, args[1], &level);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  nstatus = napi_get_value_int32(env, args[2], &xoffset);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  nstatus = napi_get_value_int32(env, args[3], &yoffset);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  nstatus = napi_get_value_int32(env, args[4], &zoffset);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  nstatus = napi_get_value_int32(env, args[5], &width);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  nstatus = napi_get_value_int32(env, args[6], &height);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  nstatus = napi_get_value_int32(env, args[7], &depth);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  nstatus = napi_get_value_uint32(env, args[8], &format);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  nstatus = napi_get_value_uint32(env, args[9], &type);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  const void *data = nullptr;
  ArrayLikeBuffer alb;
  napi_valuetype value_type;
  nstatus = napi_typeof(env, args[10], &value_type);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  if (value_type == napi_number) {
    uint32_t offset;
    nstatus = napi_get_value_uint32(env, args[10], &offset);
    ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
    data = reinterpret_cast<const void *>(static_cast<uintptr_t>(offset));
  } else {
    nstatus = GetArrayLikeBuffer(env, args[10], &alb);
    ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
    data = alb.data;
  }

  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_GL_PROC_RETVAL(env, context, glTexSubImage3D, nullptr);
  context->eglContextWrapper_->glTexSubImage3D(target, level, xoffset, yoffset,
                                               zoffset, width, height, depth,
                                               format, type, data);
#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::TransformFeedbackVaryings(
    napi_env env, napi_callback_info info) {
  LOG_CALL("TransformFeedbackVaryings");
  napi_status nstatus;

  size_t argc = 3;
  napi_value args[3];
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_ARGC_RETVAL(env, argc, 3, nullptr);

  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[0], nullptr);
  ENSURE_VALUE_IS_ARRAY_RETVAL(env, args[1], nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[2], nullptr);

  GLuint program;
  GLenum buffer_mode;
  nstatus = napi_get_value_uint32(env, args[0], &program);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  nstatus = napi_get_value_uint32(env, args[2], &buffer_mode);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  uint32_t length;
  nstatus = napi_get_array_length(env, args[1], &length);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  std::vector<std::string> names;
  std::vector<const char *> name_ptrs;
  names.reserve(length);
  name_ptrs.reserve(length);
  for (uint32_t i = 0; i < length; ++i) {
    napi_value item;
    nstatus = napi_get_element(env, args[1], i, &item);
    ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
    names.emplace_back();
    nstatus = GetStringParam(env, item, names.back());
    ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
    name_ptrs.push_back(names.back().c_str());
  }

  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_GL_PROC_RETVAL(env, context, glTransformFeedbackVaryings, nullptr);
  context->eglContextWrapper_->glTransformFeedbackVaryings(
      program, static_cast<GLsizei>(name_ptrs.size()), name_ptrs.data(),
      buffer_mode);
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::Uniform1i(napi_env env,
                                            napi_callback_info info) {
  LOG_CALL("Uniform1i");
  napi_status nstatus;

  size_t argc = 2;
  napi_value args[2];
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_ARGC_RETVAL(env, argc, 2, nullptr);

  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[0], nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[1], nullptr);

  GLint location;
  nstatus = napi_get_value_int32(env, args[0], &location);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLint v0;
  nstatus = napi_get_value_int32(env, args[1], &v0);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glUniform1i(location, v0);

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::Uniform1iv(napi_env env,
                                             napi_callback_info info) {
  LOG_CALL("Uniform1iv");
  napi_status nstatus;

  size_t argc = 2;
  napi_value args[2];
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_ARGC_RETVAL(env, argc, 2, nullptr);

  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[0], nullptr);

  GLint location;
  nstatus = napi_get_value_int32(env, args[0], &location);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  ArrayLikeBuffer alb(kInt32);
  nstatus = GetArrayLikeBuffer(env, args[1], &alb);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glUniform1iv(location,
                                            static_cast<GLsizei>(alb.size()),
                                            static_cast<GLint *>(alb.data));

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::Uniform1ui(napi_env env,
                                             napi_callback_info info) {
  LOG_CALL("Uniform1ui");
  WebGLRenderingContext *context = nullptr;
  uint32_t args[2];
  napi_status nstatus = GetContextUint32Params(env, info, &context, 2, args);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_GL_PROC_RETVAL(env, context, glUniform1ui, nullptr);
  context->eglContextWrapper_->glUniform1ui(args[0], args[1]);
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::Uniform1uiv(napi_env env,
                                              napi_callback_info info) {
  LOG_CALL("Uniform1uiv");
  napi_status nstatus;
  size_t argc = 2;
  napi_value args[2];
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_ARGC_RETVAL(env, argc, 2, nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[0], nullptr);

  GLint location;
  nstatus = napi_get_value_int32(env, args[0], &location);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ArrayLikeBuffer alb(kUint32);
  nstatus = GetArrayLikeBuffer(env, args[1], &alb);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_GL_PROC_RETVAL(env, context, glUniform1uiv, nullptr);
  context->eglContextWrapper_->glUniform1uiv(
      location, static_cast<GLsizei>(alb.size()),
      reinterpret_cast<const GLuint *>(alb.data));
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::Uniform1f(napi_env env,
                                            napi_callback_info info) {
  LOG_CALL("Uniform1f");
  napi_status nstatus;

  size_t argc = 2;
  napi_value args[2];
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_ARGC_RETVAL(env, argc, 2, nullptr);

  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[0], nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[1], nullptr);

  GLint location;
  nstatus = napi_get_value_int32(env, args[0], &location);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  double v0;
  nstatus = napi_get_value_double(env, args[1], &v0);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glUniform1f(location, static_cast<GLfloat>(v0));

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::Uniform1fv(napi_env env,
                                             napi_callback_info info) {
  LOG_CALL("Uniform1fv");
  napi_status nstatus;

  size_t argc = 2;
  napi_value args[2];
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_ARGC_RETVAL(env, argc, 2, nullptr);

  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[0], nullptr);

  GLint location;
  nstatus = napi_get_value_int32(env, args[0], &location);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  ArrayLikeBuffer alb;
  nstatus = GetArrayLikeBuffer(env, args[1], &alb);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glUniform1fv(
      location, alb.size(), reinterpret_cast<GLfloat *>(alb.data));

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::Uniform2f(napi_env env,
                                            napi_callback_info info) {
  LOG_CALL("Uniform2f");
  napi_status nstatus;

  size_t argc = 3;
  napi_value args[3];
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_ARGC_RETVAL(env, argc, 3, nullptr);

  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[0], nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[1], nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[2], nullptr);

  GLint location;
  nstatus = napi_get_value_int32(env, args[0], &location);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  double v0;
  nstatus = napi_get_value_double(env, args[1], &v0);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  double v1;
  nstatus = napi_get_value_double(env, args[2], &v1);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glUniform2f(location, static_cast<GLfloat>(v0),
                                           static_cast<GLfloat>(v1));

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::Uniform2fv(napi_env env,
                                             napi_callback_info info) {
  LOG_CALL("Uniform2fv");
  napi_status nstatus;

  size_t argc = 2;
  napi_value args[2];
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_ARGC_RETVAL(env, argc, 2, nullptr);

  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[0], nullptr);

  GLint location;
  nstatus = napi_get_value_int32(env, args[0], &location);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  ArrayLikeBuffer alb;
  nstatus = GetArrayLikeBuffer(env, args[1], &alb);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glUniform2fv(
      location, static_cast<GLsizei>(alb.size() >> 1),
      static_cast<GLfloat *>(alb.data));

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::Uniform2i(napi_env env,
                                            napi_callback_info info) {
  LOG_CALL("Uniform2i");
  napi_status nstatus;

  WebGLRenderingContext *context = nullptr;
  int32_t args[3];
  nstatus = GetContextInt32Params(env, info, &context, 3, args);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glUniform2i(args[0], args[1], args[2]);

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::Uniform2iv(napi_env env,
                                             napi_callback_info info) {
  LOG_CALL("Uniform2iv");
  napi_status nstatus;

  size_t argc = 2;
  napi_value args[2];
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_ARGC_RETVAL(env, argc, 2, nullptr);

  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[0], nullptr);

  GLint location;
  nstatus = napi_get_value_int32(env, args[0], &location);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  ArrayLikeBuffer alb(kInt32);
  nstatus = GetArrayLikeBuffer(env, args[1], &alb);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glUniform2iv(
      location, static_cast<GLsizei>(alb.size() >> 1),
      reinterpret_cast<GLint *>(alb.data));

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::Uniform2ui(napi_env env,
                                             napi_callback_info info) {
  LOG_CALL("Uniform2ui");
  WebGLRenderingContext *context = nullptr;
  uint32_t args[3];
  napi_status nstatus = GetContextUint32Params(env, info, &context, 3, args);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_GL_PROC_RETVAL(env, context, glUniform2ui, nullptr);
  context->eglContextWrapper_->glUniform2ui(args[0], args[1], args[2]);
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::Uniform2uiv(napi_env env,
                                              napi_callback_info info) {
  LOG_CALL("Uniform2uiv");
  napi_status nstatus;
  size_t argc = 2;
  napi_value args[2];
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_ARGC_RETVAL(env, argc, 2, nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[0], nullptr);
  GLint location;
  nstatus = napi_get_value_int32(env, args[0], &location);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ArrayLikeBuffer alb(kUint32);
  nstatus = GetArrayLikeBuffer(env, args[1], &alb);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_GL_PROC_RETVAL(env, context, glUniform2uiv, nullptr);
  context->eglContextWrapper_->glUniform2uiv(
      location, static_cast<GLsizei>(alb.size() >> 1),
      reinterpret_cast<const GLuint *>(alb.data));
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::Uniform3i(napi_env env,
                                            napi_callback_info info) {
  LOG_CALL("Uniform3i");

  GLint args[4];
  WebGLRenderingContext *context = nullptr;
  napi_status nstatus = GetContextInt32Params(env, info, &context, 4, args);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glUniform3i(args[0], args[1], args[2], args[3]);

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::Uniform3iv(napi_env env,
                                             napi_callback_info info) {
  LOG_CALL("Uniform3iv");
  napi_status nstatus;

  size_t argc = 2;
  napi_value args[2];
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_ARGC_RETVAL(env, argc, 2, nullptr);

  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[0], nullptr);

  GLint location;
  nstatus = napi_get_value_int32(env, args[0], &location);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  ArrayLikeBuffer alb(kInt32);
  nstatus = GetArrayLikeBuffer(env, args[1], &alb);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glUniform3iv(
      location, static_cast<GLsizei>(alb.size() / 3),
      reinterpret_cast<GLint *>(alb.data));

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::Uniform3f(napi_env env,
                                            napi_callback_info info) {
  LOG_CALL("Uniform3f");
  napi_status nstatus;

  size_t argc = 4;
  napi_value args[4];
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_ARGC_RETVAL(env, argc, 4, nullptr);

  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[0], nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[1], nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[2], nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[3], nullptr);

  GLint location;
  nstatus = napi_get_value_int32(env, args[0], &location);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  double v0;
  nstatus = napi_get_value_double(env, args[1], &v0);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  double v1;
  nstatus = napi_get_value_double(env, args[2], &v1);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  double v2;
  nstatus = napi_get_value_double(env, args[3], &v2);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glUniform3f(location, static_cast<GLfloat>(v0),
                                           static_cast<GLfloat>(v1),
                                           static_cast<GLfloat>(v2));

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::Uniform3fv(napi_env env,
                                             napi_callback_info info) {
  LOG_CALL("Uniform3fv");
  napi_status nstatus;

  size_t argc = 2;
  napi_value args[2];
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_ARGC_RETVAL(env, argc, 2, nullptr);

  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[0], nullptr);

  GLint location;
  nstatus = napi_get_value_int32(env, args[0], &location);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  ArrayLikeBuffer alb;
  nstatus = GetArrayLikeBuffer(env, args[1], &alb);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glUniform3fv(
      location, static_cast<GLsizei>(alb.size() / 3),
      reinterpret_cast<GLfloat *>(alb.data));

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::Uniform3ui(napi_env env,
                                             napi_callback_info info) {
  LOG_CALL("Uniform3ui");
  WebGLRenderingContext *context = nullptr;
  uint32_t args[4];
  napi_status nstatus = GetContextUint32Params(env, info, &context, 4, args);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_GL_PROC_RETVAL(env, context, glUniform3ui, nullptr);
  context->eglContextWrapper_->glUniform3ui(args[0], args[1], args[2], args[3]);
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::Uniform3uiv(napi_env env,
                                              napi_callback_info info) {
  LOG_CALL("Uniform3uiv");
  napi_status nstatus;
  size_t argc = 2;
  napi_value args[2];
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_ARGC_RETVAL(env, argc, 2, nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[0], nullptr);
  GLint location;
  nstatus = napi_get_value_int32(env, args[0], &location);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ArrayLikeBuffer alb(kUint32);
  nstatus = GetArrayLikeBuffer(env, args[1], &alb);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_GL_PROC_RETVAL(env, context, glUniform3uiv, nullptr);
  context->eglContextWrapper_->glUniform3uiv(
      location, static_cast<GLsizei>(alb.size() / 3),
      reinterpret_cast<const GLuint *>(alb.data));
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::Uniform4fv(napi_env env,
                                             napi_callback_info info) {
  LOG_CALL("Uniform4fv");
  napi_status nstatus;

  size_t argc = 2;
  napi_value args[2];
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_ARGC_RETVAL(env, argc, 2, nullptr);

  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[0], nullptr);

  GLint location;
  nstatus = napi_get_value_int32(env, args[0], &location);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  ArrayLikeBuffer alb;
  nstatus = GetArrayLikeBuffer(env, args[1], &alb);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glUniform4fv(
      location, static_cast<GLsizei>(alb.size() >> 2),
      reinterpret_cast<GLfloat *>(alb.data));

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::Uniform4i(napi_env env,
                                            napi_callback_info info) {
  LOG_CALL("Uniform4i");
  napi_status nstatus;

  WebGLRenderingContext *context = nullptr;
  int32_t args[5];
  nstatus = GetContextInt32Params(env, info, &context, 5, args);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glUniform4i(args[0], args[1], args[2], args[3],
                                           args[4]);

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::Uniform4iv(napi_env env,
                                             napi_callback_info info) {
  LOG_CALL("Uniform4iv");
  napi_status nstatus;

  size_t argc = 2;
  napi_value args[2];
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_ARGC_RETVAL(env, argc, 2, nullptr);

  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[0], nullptr);

  GLint location;
  nstatus = napi_get_value_int32(env, args[0], &location);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  ArrayLikeBuffer alb(kInt32);
  nstatus = GetArrayLikeBuffer(env, args[1], &alb);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glUniform4iv(
      location, static_cast<GLsizei>(alb.size() >> 2),
      static_cast<GLint *>(alb.data));

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::Uniform4f(napi_env env,
                                            napi_callback_info info) {
  LOG_CALL("Uniform4f");
  napi_status nstatus;

  size_t argc = 5;
  napi_value args[5];
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_ARGC_RETVAL(env, argc, 5, nullptr);

  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[0], nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[1], nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[2], nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[3], nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[4], nullptr);

  GLint location;
  nstatus = napi_get_value_int32(env, args[0], &location);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  double v0;
  nstatus = napi_get_value_double(env, args[1], &v0);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  double v1;
  nstatus = napi_get_value_double(env, args[2], &v1);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  double v2;
  nstatus = napi_get_value_double(env, args[3], &v2);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  double v3;
  nstatus = napi_get_value_double(env, args[4], &v3);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glUniform4f(
      location, static_cast<GLfloat>(v0), static_cast<GLfloat>(v1),
      static_cast<GLfloat>(v2), static_cast<GLfloat>(v3));

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::Uniform4ui(napi_env env,
                                             napi_callback_info info) {
  LOG_CALL("Uniform4ui");
  WebGLRenderingContext *context = nullptr;
  uint32_t args[5];
  napi_status nstatus = GetContextUint32Params(env, info, &context, 5, args);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_GL_PROC_RETVAL(env, context, glUniform4ui, nullptr);
  context->eglContextWrapper_->glUniform4ui(args[0], args[1], args[2], args[3],
                                            args[4]);
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::Uniform4uiv(napi_env env,
                                              napi_callback_info info) {
  LOG_CALL("Uniform4uiv");
  napi_status nstatus;
  size_t argc = 2;
  napi_value args[2];
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_ARGC_RETVAL(env, argc, 2, nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[0], nullptr);
  GLint location;
  nstatus = napi_get_value_int32(env, args[0], &location);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ArrayLikeBuffer alb(kUint32);
  nstatus = GetArrayLikeBuffer(env, args[1], &alb);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_GL_PROC_RETVAL(env, context, glUniform4uiv, nullptr);
  context->eglContextWrapper_->glUniform4uiv(
      location, static_cast<GLsizei>(alb.size() >> 2),
      reinterpret_cast<const GLuint *>(alb.data));
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::UniformMatrix2fv(napi_env env,
                                                   napi_callback_info info) {
  LOG_CALL("UniformMatrix2fv");

  napi_status nstatus;

  size_t argc = 3;
  napi_value args[3];
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[0], nullptr);
  ENSURE_VALUE_IS_BOOLEAN_RETVAL(env, args[1], nullptr);

  GLint location;
  nstatus = napi_get_value_int32(env, args[0], &location);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  bool transpose;
  nstatus = napi_get_value_bool(env, args[1], &transpose);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  ArrayLikeBuffer alb;
  nstatus = GetArrayLikeBuffer(env, args[2], &alb);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glUniformMatrix2fv(
      location, static_cast<GLsizei>(alb.size() >> 2),
      static_cast<GLboolean>(transpose),
      static_cast<const GLfloat *>(alb.data));

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::UniformMatrix2x3fv(napi_env env,
                                                     napi_callback_info info) {
  LOG_CALL("UniformMatrix2x3fv");
  GLint location;
  GLboolean transpose;
  ArrayLikeBuffer alb;
  WebGLRenderingContext *context = nullptr;
  napi_status nstatus =
      GetUniformMatrixParams(env, info, &context, &location, &transpose, &alb);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_GL_PROC_RETVAL(env, context, glUniformMatrix2x3fv, nullptr);
  context->eglContextWrapper_->glUniformMatrix2x3fv(
      location, static_cast<GLsizei>(alb.size() / 6), transpose,
      static_cast<const GLfloat *>(alb.data));
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::UniformMatrix2x4fv(napi_env env,
                                                     napi_callback_info info) {
  LOG_CALL("UniformMatrix2x4fv");
  GLint location;
  GLboolean transpose;
  ArrayLikeBuffer alb;
  WebGLRenderingContext *context = nullptr;
  napi_status nstatus =
      GetUniformMatrixParams(env, info, &context, &location, &transpose, &alb);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_GL_PROC_RETVAL(env, context, glUniformMatrix2x4fv, nullptr);
  context->eglContextWrapper_->glUniformMatrix2x4fv(
      location, static_cast<GLsizei>(alb.size() / 8), transpose,
      static_cast<const GLfloat *>(alb.data));
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::UniformMatrix3fv(napi_env env,
                                                   napi_callback_info info) {
  LOG_CALL("UniformMatrix3fv");

  napi_status nstatus;

  size_t argc = 3;
  napi_value args[3];
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[0], nullptr);
  ENSURE_VALUE_IS_BOOLEAN_RETVAL(env, args[1], nullptr);

  GLint location;
  nstatus = napi_get_value_int32(env, args[0], &location);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  bool transpose;
  nstatus = napi_get_value_bool(env, args[1], &transpose);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  ArrayLikeBuffer alb;
  nstatus = GetArrayLikeBuffer(env, args[2], &alb);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glUniformMatrix3fv(
      location, static_cast<GLsizei>(alb.size() / 9),
      static_cast<GLboolean>(transpose),
      static_cast<const GLfloat *>(alb.data));

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::UniformMatrix3x2fv(napi_env env,
                                                     napi_callback_info info) {
  LOG_CALL("UniformMatrix3x2fv");
  GLint location;
  GLboolean transpose;
  ArrayLikeBuffer alb;
  WebGLRenderingContext *context = nullptr;
  napi_status nstatus =
      GetUniformMatrixParams(env, info, &context, &location, &transpose, &alb);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_GL_PROC_RETVAL(env, context, glUniformMatrix3x2fv, nullptr);
  context->eglContextWrapper_->glUniformMatrix3x2fv(
      location, static_cast<GLsizei>(alb.size() / 6), transpose,
      static_cast<const GLfloat *>(alb.data));
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::UniformMatrix3x4fv(napi_env env,
                                                     napi_callback_info info) {
  LOG_CALL("UniformMatrix3x4fv");
  GLint location;
  GLboolean transpose;
  ArrayLikeBuffer alb;
  WebGLRenderingContext *context = nullptr;
  napi_status nstatus =
      GetUniformMatrixParams(env, info, &context, &location, &transpose, &alb);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_GL_PROC_RETVAL(env, context, glUniformMatrix3x4fv, nullptr);
  context->eglContextWrapper_->glUniformMatrix3x4fv(
      location, static_cast<GLsizei>(alb.size() / 12), transpose,
      static_cast<const GLfloat *>(alb.data));
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::UniformMatrix4fv(napi_env env,
                                                   napi_callback_info info) {
  LOG_CALL("UniformMatrix4fv");

  napi_status nstatus;

  size_t argc = 3;
  napi_value args[3];
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[0], nullptr);
  ENSURE_VALUE_IS_BOOLEAN_RETVAL(env, args[1], nullptr);

  GLint location;
  nstatus = napi_get_value_int32(env, args[0], &location);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  bool transpose;
  nstatus = napi_get_value_bool(env, args[1], &transpose);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  ArrayLikeBuffer alb;
  nstatus = GetArrayLikeBuffer(env, args[2], &alb);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glUniformMatrix4fv(
      location, static_cast<GLsizei>(alb.size() >> 4),
      static_cast<GLboolean>(transpose),
      static_cast<const GLfloat *>(alb.data));

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::UniformMatrix4x2fv(napi_env env,
                                                     napi_callback_info info) {
  LOG_CALL("UniformMatrix4x2fv");
  GLint location;
  GLboolean transpose;
  ArrayLikeBuffer alb;
  WebGLRenderingContext *context = nullptr;
  napi_status nstatus =
      GetUniformMatrixParams(env, info, &context, &location, &transpose, &alb);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_GL_PROC_RETVAL(env, context, glUniformMatrix4x2fv, nullptr);
  context->eglContextWrapper_->glUniformMatrix4x2fv(
      location, static_cast<GLsizei>(alb.size() / 8), transpose,
      static_cast<const GLfloat *>(alb.data));
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::UniformMatrix4x3fv(napi_env env,
                                                     napi_callback_info info) {
  LOG_CALL("UniformMatrix4x3fv");
  GLint location;
  GLboolean transpose;
  ArrayLikeBuffer alb;
  WebGLRenderingContext *context = nullptr;
  napi_status nstatus =
      GetUniformMatrixParams(env, info, &context, &location, &transpose, &alb);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_GL_PROC_RETVAL(env, context, glUniformMatrix4x3fv, nullptr);
  context->eglContextWrapper_->glUniformMatrix4x3fv(
      location, static_cast<GLsizei>(alb.size() / 12), transpose,
      static_cast<const GLfloat *>(alb.data));
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::UseProgram(napi_env env,
                                             napi_callback_info info) {
  LOG_CALL("UseProgram");

  WebGLRenderingContext *context = nullptr;
  GLuint program;
  napi_status nstatus =
      GetContextUint32Params(env, info, &context, 1, &program);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glUseProgram(program);

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::ValidateProgram(napi_env env,
                                                  napi_callback_info info) {
  LOG_CALL("ValidateProgram");

  WebGLRenderingContext *context = nullptr;
  GLuint program;
  napi_status nstatus =
      GetContextUint32Params(env, info, &context, 1, &program);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glValidateProgram(program);

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::VertexAttrib1f(napi_env env,
                                                 napi_callback_info info) {
  LOG_CALL("VertexAttrib1f");

  napi_status nstatus;

  size_t argc = 2;
  napi_value args[2];
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[0], nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[1], nullptr);

  GLuint index;
  nstatus = napi_get_value_uint32(env, args[0], &index);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  double v0;
  nstatus = napi_get_value_double(env, args[1], &v0);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glVertexAttrib1f(index, v0);

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::VertexAttrib1fv(napi_env env,
                                                  napi_callback_info info) {
  LOG_CALL("VertexAttrib1fv");

  napi_status nstatus;

  size_t argc = 2;
  napi_value args[2];
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[0], nullptr);

  GLuint index;
  nstatus = napi_get_value_uint32(env, args[0], &index);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  ArrayLikeBuffer alb;
  nstatus = GetArrayLikeBuffer(env, args[1], &alb);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glVertexAttrib1fv(
      index, static_cast<GLfloat *>(alb.data));

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}
/* static */
napi_value WebGLRenderingContext::VertexAttrib2f(napi_env env,
                                                 napi_callback_info info) {
  LOG_CALL("VertexAttrib2f");

  napi_status nstatus;

  size_t argc = 3;
  napi_value args[3];
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[0], nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[1], nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[2], nullptr);

  GLuint index;
  nstatus = napi_get_value_uint32(env, args[0], &index);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  double v0;
  nstatus = napi_get_value_double(env, args[1], &v0);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  double v1;
  nstatus = napi_get_value_double(env, args[2], &v1);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glVertexAttrib2f(index, v0, v1);

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::VertexAttrib2fv(napi_env env,
                                                  napi_callback_info info) {
  LOG_CALL("VertexAttrib2fv");

  napi_status nstatus;

  size_t argc = 2;
  napi_value args[2];
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[0], nullptr);

  GLuint index;
  nstatus = napi_get_value_uint32(env, args[0], &index);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  ArrayLikeBuffer alb;
  nstatus = GetArrayLikeBuffer(env, args[1], &alb);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glVertexAttrib2fv(
      index, static_cast<GLfloat *>(alb.data));

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::VertexAttrib3f(napi_env env,
                                                 napi_callback_info info) {
  LOG_CALL("VertexAttrib3f");

  napi_status nstatus;

  size_t argc = 4;
  napi_value args[4];
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[0], nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[1], nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[2], nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[3], nullptr);

  GLuint index;
  nstatus = napi_get_value_uint32(env, args[0], &index);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  double v0;
  nstatus = napi_get_value_double(env, args[1], &v0);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  double v1;
  nstatus = napi_get_value_double(env, args[1], &v1);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  double v2;
  nstatus = napi_get_value_double(env, args[1], &v2);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glVertexAttrib3f(index, v0, v1, v2);

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::VertexAttrib3fv(napi_env env,
                                                  napi_callback_info info) {
  LOG_CALL("VertexAttrib1fv");

  napi_status nstatus;

  size_t argc = 2;
  napi_value args[2];
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[0], nullptr);

  GLuint index;
  nstatus = napi_get_value_uint32(env, args[0], &index);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  ArrayLikeBuffer alb;
  nstatus = GetArrayLikeBuffer(env, args[1], &alb);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glVertexAttrib3fv(
      index, static_cast<GLfloat *>(alb.data));

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::VertexAttrib4f(napi_env env,
                                                 napi_callback_info info) {
  LOG_CALL("VertexAttrib4f");

  napi_status nstatus;

  size_t argc = 5;
  napi_value args[5];
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[0], nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[1], nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[2], nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[3], nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[4], nullptr);

  GLuint index;
  nstatus = napi_get_value_uint32(env, args[0], &index);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  double v0;
  nstatus = napi_get_value_double(env, args[1], &v0);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  double v1;
  nstatus = napi_get_value_double(env, args[1], &v1);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  double v2;
  nstatus = napi_get_value_double(env, args[1], &v2);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  double v3;
  nstatus = napi_get_value_double(env, args[1], &v3);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glVertexAttrib4f(index, v0, v1, v2, v3);

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::VertexAttrib4fv(napi_env env,
                                                  napi_callback_info info) {
  LOG_CALL("VertexAttrib4fv");

  napi_status nstatus;

  size_t argc = 2;
  napi_value args[2];
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[0], nullptr);

  GLuint index;
  nstatus = napi_get_value_uint32(env, args[0], &index);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  ArrayLikeBuffer alb;
  nstatus = GetArrayLikeBuffer(env, args[1], &alb);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glVertexAttrib4fv(
      index, static_cast<GLfloat *>(alb.data));

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::VertexAttribDivisor(napi_env env,
                                                      napi_callback_info info) {
  LOG_CALL("VertexAttribDivisor");
  WebGLRenderingContext *context = nullptr;
  uint32_t args[2];
  napi_status nstatus = GetContextUint32Params(env, info, &context, 2, args);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_GL_PROC_RETVAL(env, context, glVertexAttribDivisor, nullptr);
  context->eglContextWrapper_->glVertexAttribDivisor(args[0], args[1]);
#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::VertexAttribI4i(napi_env env,
                                                  napi_callback_info info) {
  LOG_CALL("VertexAttribI4i");
  WebGLRenderingContext *context = nullptr;
  int32_t args[5];
  napi_status nstatus = GetContextInt32Params(env, info, &context, 5, args);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_GL_PROC_RETVAL(env, context, glVertexAttribI4i, nullptr);
  context->eglContextWrapper_->glVertexAttribI4i(args[0], args[1], args[2],
                                                 args[3], args[4]);
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::VertexAttribI4iv(napi_env env,
                                                   napi_callback_info info) {
  LOG_CALL("VertexAttribI4iv");
  napi_status nstatus;
  size_t argc = 2;
  napi_value args[2];
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_ARGC_RETVAL(env, argc, 2, nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[0], nullptr);
  GLuint index;
  nstatus = napi_get_value_uint32(env, args[0], &index);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ArrayLikeBuffer alb(kInt32);
  nstatus = GetArrayLikeBuffer(env, args[1], &alb);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_GL_PROC_RETVAL(env, context, glVertexAttribI4iv, nullptr);
  context->eglContextWrapper_->glVertexAttribI4iv(
      index, static_cast<const GLint *>(alb.data));
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::VertexAttribI4ui(napi_env env,
                                                   napi_callback_info info) {
  LOG_CALL("VertexAttribI4ui");
  WebGLRenderingContext *context = nullptr;
  uint32_t args[5];
  napi_status nstatus = GetContextUint32Params(env, info, &context, 5, args);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_GL_PROC_RETVAL(env, context, glVertexAttribI4ui, nullptr);
  context->eglContextWrapper_->glVertexAttribI4ui(args[0], args[1], args[2],
                                                  args[3], args[4]);
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::VertexAttribI4uiv(napi_env env,
                                                    napi_callback_info info) {
  LOG_CALL("VertexAttribI4uiv");
  napi_status nstatus;
  size_t argc = 2;
  napi_value args[2];
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_ARGC_RETVAL(env, argc, 2, nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[0], nullptr);
  GLuint index;
  nstatus = napi_get_value_uint32(env, args[0], &index);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ArrayLikeBuffer alb(kUint32);
  nstatus = GetArrayLikeBuffer(env, args[1], &alb);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_GL_PROC_RETVAL(env, context, glVertexAttribI4uiv, nullptr);
  context->eglContextWrapper_->glVertexAttribI4uiv(
      index, static_cast<const GLuint *>(alb.data));
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::VertexAttribIPointer(
    napi_env env, napi_callback_info info) {
  LOG_CALL("VertexAttribIPointer");
  napi_status nstatus;
  size_t argc = 5;
  napi_value args[5];
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_ARGC_RETVAL(env, argc, 5, nullptr);
  for (size_t i = 0; i < 5; ++i) {
    ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[i], nullptr);
  }
  GLuint index;
  GLint size;
  GLenum type;
  GLsizei stride;
  uint32_t offset;
  nstatus = napi_get_value_uint32(env, args[0], &index);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  nstatus = napi_get_value_int32(env, args[1], &size);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  nstatus = napi_get_value_uint32(env, args[2], &type);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  nstatus = napi_get_value_int32(env, args[3], &stride);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  nstatus = napi_get_value_uint32(env, args[4], &offset);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_GL_PROC_RETVAL(env, context, glVertexAttribIPointer, nullptr);
  context->eglContextWrapper_->glVertexAttribIPointer(
      index, size, type, stride,
      reinterpret_cast<const GLvoid *>(static_cast<uintptr_t>(offset)));
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::VertexAttribPointer(napi_env env,
                                                      napi_callback_info info) {
  LOG_CALL("VertexAttribPointer");
  napi_status nstatus;

  size_t argc = 6;
  napi_value args[6];
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_ARGC_RETVAL(env, argc, 6, nullptr);

  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[0], nullptr);
  uint32_t index;
  nstatus = napi_get_value_uint32(env, args[0], &index);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[1], nullptr);
  GLint size;
  nstatus = napi_get_value_int32(env, args[1], &size);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[2], nullptr);
  GLenum type;
  nstatus = napi_get_value_uint32(env, args[2], &type);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  ENSURE_VALUE_IS_BOOLEAN_RETVAL(env, args[3], nullptr);
  bool normalized;
  nstatus = napi_get_value_bool(env, args[3], &normalized);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[4], nullptr);
  GLsizei stride;
  nstatus = napi_get_value_int32(env, args[4], &stride);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[5], nullptr);
  uint32_t offset;
  nstatus = napi_get_value_uint32(env, args[5], &offset);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glVertexAttribPointer(
      index, size, type, normalized, stride,
      reinterpret_cast<GLvoid *>(offset));

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::WaitSync(napi_env env,
                                           napi_callback_info info) {
  LOG_CALL("WaitSync");
  napi_status nstatus;
  size_t argc = 3;
  napi_value args[3];
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_ARGC_RETVAL(env, argc, 3, nullptr);

  GLsync sync = nullptr;
  nstatus = GetSyncParam(env, args[0], &sync);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[1], nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[2], nullptr);
  GLbitfield flags;
  uint32_t timeout;
  nstatus = napi_get_value_uint32(env, args[1], &flags);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  nstatus = napi_get_value_uint32(env, args[2], &timeout);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_GL_PROC_RETVAL(env, context, glWaitSync, nullptr);
  context->eglContextWrapper_->glWaitSync(sync, flags, timeout);
  return nullptr;
}

/* static */
napi_value WebGLRenderingContext::Viewport(napi_env env,
                                           napi_callback_info info) {
  LOG_CALL("Viewport");
  napi_status nstatus;

  size_t argc = 4;
  napi_value args[4];
  napi_value js_this;
  nstatus = napi_get_cb_info(env, info, &argc, args, &js_this, nullptr);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);
  ENSURE_ARGC_RETVAL(env, argc, 4, nullptr);

  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[0], nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[1], nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[2], nullptr);
  ENSURE_VALUE_IS_NUMBER_RETVAL(env, args[3], nullptr);

  GLint x;
  nstatus = napi_get_value_int32(env, args[0], &x);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLint y;
  nstatus = napi_get_value_int32(env, args[1], &y);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLsizei width;
  nstatus = napi_get_value_int32(env, args[2], &width);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  GLsizei height;
  nstatus = napi_get_value_int32(env, args[3], &height);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  WebGLRenderingContext *context = nullptr;
  nstatus = UnwrapContext(env, js_this, &context);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nullptr);

  context->eglContextWrapper_->glViewport(x, y, width, height);

#if DEBUG
  context->CheckForErrors();
#endif
  return nullptr;
}

}  // namespace nodejsgl
