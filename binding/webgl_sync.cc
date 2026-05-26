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

#include <mutex>
#include <new>
#include <vector>

#include "utils.h"

namespace nodejsgl {

namespace {

std::mutex sync_handle_mutex;
GLSyncHandle* sync_handle_head = nullptr;

void RegisterGLSyncHandle(GLSyncHandle* handle) {
  std::lock_guard<std::mutex> lock(sync_handle_mutex);
  handle->previous = nullptr;
  handle->next = sync_handle_head;
  if (sync_handle_head != nullptr) {
    sync_handle_head->previous = handle;
  }
  sync_handle_head = handle;
}

void UnregisterGLSyncHandle(GLSyncHandle* handle) {
  std::lock_guard<std::mutex> lock(sync_handle_mutex);
  if (handle->previous != nullptr) {
    handle->previous->next = handle->next;
  } else if (sync_handle_head == handle) {
    sync_handle_head = handle->next;
  }
  if (handle->next != nullptr) {
    handle->next->previous = handle->previous;
  }
  handle->previous = nullptr;
  handle->next = nullptr;
}

bool IsRegisteredGLSyncHandle(GLSyncHandle* handle) {
  std::lock_guard<std::mutex> lock(sync_handle_mutex);
  for (GLSyncHandle* current = sync_handle_head; current != nullptr;
       current = current->next) {
    if (current == handle) {
      return true;
    }
  }
  return false;
}

struct EGLCurrentBinding {
  EGLDisplay display;
  EGLSurface draw_surface;
  EGLSurface read_surface;
  EGLContext context;
};

EGLCurrentBinding GetCurrentEGLBinding() {
  return EGLCurrentBinding{
      eglGetCurrentDisplay(), eglGetCurrentSurface(EGL_DRAW),
      eglGetCurrentSurface(EGL_READ), eglGetCurrentContext()};
}

bool RestoreEGLBinding(const EGLCurrentBinding& binding,
                       EGLDisplay fallback_display) {
  const EGLDisplay display =
      binding.display != EGL_NO_DISPLAY ? binding.display : fallback_display;
  const EGLSurface draw_surface =
      binding.context != EGL_NO_CONTEXT ? binding.draw_surface : EGL_NO_SURFACE;
  const EGLSurface read_surface =
      binding.context != EGL_NO_CONTEXT ? binding.read_surface : EGL_NO_SURFACE;
  return display != EGL_NO_DISPLAY &&
         eglMakeCurrent(display, draw_surface, read_surface, binding.context);
}

}  // namespace

static void DeleteGLsync(EGLContextWrapper* egl_context_wrapper, GLsync* sync) {
  if (sync == nullptr || *sync == nullptr || egl_context_wrapper == nullptr ||
      egl_context_wrapper->glDeleteSync == nullptr) {
    return;
  }
  if (!egl_context_wrapper->IsCurrent()) {
    const EGLCurrentBinding previous_binding = GetCurrentEGLBinding();
    if (egl_context_wrapper->MakeCurrent()) {
      egl_context_wrapper->glDeleteSync(*sync);
      *sync = nullptr;
      if (!RestoreEGLBinding(previous_binding, egl_context_wrapper->display)) {
        eglMakeCurrent(egl_context_wrapper->display, EGL_NO_SURFACE,
                       EGL_NO_SURFACE, EGL_NO_CONTEXT);
      }
      return;
    }

    // Last-resort fallback: if the owner context cannot be rebound, the EGL
    // context teardown will release this sync after flushing queued handles.
    egl_context_wrapper->pending_sync_deletes.push_back(*sync);
    *sync = nullptr;
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
  handle->egl_context_wrapper = nullptr;
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
  UnregisterGLSyncHandle(handle);
  ReleaseGLSyncResources(env, handle, delete_sync);
  delete handle;
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

  GLSyncHandle* handle = new (std::nothrow)
      GLSyncHandle{sync, egl_context_wrapper, context_ref, nullptr, nullptr};
  if (handle == nullptr) {
    napi_delete_reference(env, context_ref);
    DeleteGLsync(egl_context_wrapper, &sync);
    NAPI_THROW_ERROR(env, "Could not allocate WebGLSync");
    return napi_generic_failure;
  }
  sync = nullptr;

  nstatus = napi_wrap(env, *wrapped_value, handle, Cleanup, nullptr, nullptr);
  if (nstatus != napi_ok) {
    ReleaseGLSyncHandle(env, handle, true);
  }
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nstatus);

  RegisterGLSyncHandle(handle);

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

  GLSyncHandle* candidate = static_cast<GLSyncHandle*>(wrapped_handle);
  if (!IsRegisteredGLSyncHandle(candidate)) {
    NAPI_THROW_ERROR(env, "sync must be a WebGLSync object");
    return napi_invalid_arg;
  }

  *handle = candidate;
  return napi_ok;
}

void InvalidateGLSyncHandlesForContext(napi_env env,
                                       EGLContextWrapper* egl_context_wrapper) {
  if (egl_context_wrapper == nullptr) {
    return;
  }

  std::vector<GLSyncHandle*> handles;
  {
    std::lock_guard<std::mutex> lock(sync_handle_mutex);
    for (GLSyncHandle* current = sync_handle_head; current != nullptr;
         current = current->next) {
      if (current->egl_context_wrapper == egl_context_wrapper) {
        handles.push_back(current);
      }
    }
  }

  for (GLSyncHandle* handle : handles) {
    ReleaseGLSyncResources(env, handle, false);
  }
}

}  // namespace nodejsgl
