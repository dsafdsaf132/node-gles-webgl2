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

#include "utils.h"

namespace nodejsgl {

static void ReleaseGLSyncHandle(napi_env env, GLSyncHandle* handle,
                                bool delete_sync) {
  if (handle == nullptr) {
    return;
  }
  if (delete_sync && handle->sync != nullptr &&
      handle->egl_context_wrapper != nullptr &&
      handle->egl_context_wrapper->glDeleteSync != nullptr) {
    handle->egl_context_wrapper->glDeleteSync(handle->sync);
  }
  handle->sync = nullptr;
  if (handle->context_ref != nullptr) {
    napi_delete_reference(env, handle->context_ref);
    handle->context_ref = nullptr;
  }
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
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nstatus);

  napi_ref context_ref = nullptr;
  nstatus = napi_create_reference(env, context_value, 1, &context_ref);
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nstatus);

  GLSyncHandle* handle =
      new GLSyncHandle{sync, egl_context_wrapper, context_ref};
  nstatus = napi_wrap(env, *wrapped_value, handle, Cleanup, nullptr,
                      nullptr);
  if (nstatus != napi_ok) {
    ReleaseGLSyncHandle(env, handle, false);
  }
  ENSURE_NAPI_OK_RETVAL(env, nstatus, nstatus);

  return napi_ok;
}

}  // namespace nodejsgl
