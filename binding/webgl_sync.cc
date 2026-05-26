/** * @license
 * Copyright 2019 Google Inc. All Rights Reserved.
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

#include "webgl_sync.h"

#include <new>

#include "utils.h"

namespace nodejsgl {

namespace {

constexpr const char kGLSyncHandleProperty[] = "__node_gles_webgl_sync_handle";

}  // namespace

static void DeleteGLsync(EGLContextWrapper* egl_context_wrapper, GLsync* sync) {
  if (sync == nullptr || *sync == nullptr || egl_context_wrapper == nullptr ||
      egl_context_wrapper->glDeleteSync == nullptr) {
    return;
  }
  egl_context_wrapper->glDeleteSync(*sync);
  *sync = nullptr;
}

static void ReleaseGLSyncResources(napi_env env, GLSyncHandle* handle,
                                   bool delete_sync) {
  if (handle == nullptr) {
    return;
  }
  if (delete_sync) {
    DeleteGLsync(handle->egl_context_wrapper, &handle->sync);
  } else {
    handle->sync = nullptr;
  }
  if (handle->context_ref != nullptr) {
    napi_delete_reference(env, handle->context_ref);
    handle->context_ref = nullptr;
  }
}

static void ReleaseGLSyncHandle(napi_env env, GLSyncHandle* handle,
                                bool delete_sync) {
  if (handle == nullptr) {
    return;
  }
  ReleaseGLSyncResources(env, handle, delete_sync);
  delete handle;
}

static void DetachAndReleaseWrappedGLSyncHandle(napi_env env,
                                                napi_value wrapped_value,
                                                GLSyncHandle* handle) {
  void* removed = nullptr;
  napi_status remove_status = napi_remove_wrap(env, wrapped_value, &removed);
  if (remove_status == napi_ok && removed != nullptr) {
    ReleaseGLSyncHandle(env, static_cast<GLSyncHandle*>(removed), true);
    return;
  }

  // If napi_remove_wrap fails, the object still owns the native pointer and its
  // finalizer will delete the handle. Release GL resources now, but leave the
  // handle allocation for the already-registered cleanup callback.
  ReleaseGLSyncResources(env, handle, true);
}

void Cleanup(napi_env env, void* native, void* hint) {
  GLSyncHandle* handle = static_cast<GLSyncHandle*>(native);
  ReleaseGLSyncHandle(env, handle, true);
}

napi_status WrapGLsync(napi_env env, GLsync& sync,
                       EGLContextWrapper* egl_context_wrapper,
                       napi_value context_value, napi_value* wrapped_value) {
  napi_status nstatus;

  nstatus = napi_create_object(env, wrapped_value);
  if (nstatus != napi_ok) {
    DeleteGLsync(egl_context_wrapper, &sync);
  }
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nstatus);

  napi_ref context_ref = nullptr;
  nstatus = napi_create_reference(env, context_value, 1, &context_ref);
  if (nstatus != napi_ok) {
    DeleteGLsync(egl_context_wrapper, &sync);
  }
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nstatus);

  GLSyncHandle* handle =
      new (std::nothrow) GLSyncHandle{sync, egl_context_wrapper, context_ref};
  if (handle == nullptr) {
    napi_delete_reference(env, context_ref);
    DeleteGLsync(egl_context_wrapper, &sync);
    NAPI_THROW_ERROR(env, "Could not allocate WebGLSync");
    return napi_generic_failure;
  }
  sync = nullptr;

  nstatus = napi_wrap(env, *wrapped_value, handle, Cleanup, nullptr,
                      nullptr);
  if (nstatus != napi_ok) {
    ReleaseGLSyncHandle(env, handle, true);
  }
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nstatus);

  napi_value handle_external;
  nstatus =
      napi_create_external(env, handle, nullptr, nullptr, &handle_external);
  if (nstatus != napi_ok) {
    DetachAndReleaseWrappedGLSyncHandle(env, *wrapped_value, handle);
    ENSURE_NAPI_OK_RETVAL(env, nstatus, nstatus);
  }

  napi_property_descriptor descriptor = {
      kGLSyncHandleProperty, nullptr,      nullptr, nullptr, nullptr,
      handle_external,       napi_default, nullptr};
  nstatus = napi_define_properties(env, *wrapped_value, 1, &descriptor);
  if (nstatus != napi_ok) {
    DetachAndReleaseWrappedGLSyncHandle(env, *wrapped_value, handle);
  }
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nstatus);

  return napi_ok;
}

napi_status GetGLsyncHandle(napi_env env, napi_value value,
                            GLSyncHandle** handle) {
  void* wrapped_handle = nullptr;
  napi_status nstatus = napi_unwrap(env, value, &wrapped_handle);
  if (nstatus != napi_ok || wrapped_handle == nullptr) {
    NAPI_THROW_ERROR(env, "sync must be a WebGLSync object");
    return napi_invalid_arg;
  }

  napi_value property_key;
  nstatus = napi_create_string_utf8(
      env, kGLSyncHandleProperty, NAPI_AUTO_LENGTH, &property_key);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nstatus);

  bool has_handle = false;
  nstatus = napi_has_own_property(env, value, property_key, &has_handle);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nstatus);
  if (!has_handle) {
    NAPI_THROW_ERROR(env, "sync must be a WebGLSync object");
    return napi_invalid_arg;
  }

  napi_value handle_external;
  nstatus = napi_get_property(env, value, property_key, &handle_external);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nstatus);

  void* native_handle = nullptr;
  nstatus = napi_get_value_external(env, handle_external, &native_handle);
  if (nstatus != napi_ok || native_handle == nullptr ||
      native_handle != wrapped_handle) {
    NAPI_THROW_ERROR(env, "sync must be a WebGLSync object");
    return napi_invalid_arg;
  }

  *handle = static_cast<GLSyncHandle*>(wrapped_handle);
  return napi_ok;
}

}  // namespace nodejsgl
