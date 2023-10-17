/*
 * Copyright (c) 2009-2021, Google LLC
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Google LLC nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL Google LLC BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef PYUPB_DESCRIPTOR_CONTAINERS_H__
#define PYUPB_DESCRIPTOR_CONTAINERS_H__

// This file defines immutable Python containiner types whose data comes from
// an underlying descriptor (def).
//
// Because there are many instances of these types that vend different kinds of
// data (fields, oneofs, enums, etc) these types accept a "vtable" of function
// pointers. This saves us from having to define numerous distinct Python types
// for each kind of data we want to vend.
//
// The underlying upb APIs follow a consistent pattern that allows us to use
// those functions directly inside these vtables, greatly reducing the amount of
// "adaptor" code we need to write.

#include <stdbool.h>

#include "protobuf.h"
#include "upb/reflection/def.h"

// -----------------------------------------------------------------------------
// PyUpb_GenericSequence
// -----------------------------------------------------------------------------

// A Python object that vends a sequence of descriptors.

typedef struct {
  // Returns the number of elements in the map.
  int (*get_elem_count)(const void* parent);
  // Returns an element by index.
  const void* (*index)(const void* parent, int idx);
  // Returns a Python object wrapping this element, caller owns a ref.
  PyObject* (*get_elem_wrapper)(const void* elem);
} PyUpb_GenericSequence_Funcs;

// Returns a new GenericSequence.  The vtable `funcs` must outlive this object
// (generally it should be static).  The GenericSequence will take a ref on
// `parent_obj`, which must be sufficient to keep `parent` alive.  The object
// `parent` will be passed as an argument to the functions in `funcs`.
PyObject* PyUpb_GenericSequence_New(const PyUpb_GenericSequence_Funcs* funcs,
                                    const void* parent, PyObject* parent_obj);

// -----------------------------------------------------------------------------
// PyUpb_ByNameMap
// -----------------------------------------------------------------------------

// A Python object that vends a name->descriptor map.

typedef struct {
  PyUpb_GenericSequence_Funcs base;
  // Looks up by name and returns either a pointer to the element or NULL.
  const void* (*lookup)(const void* parent, const char* key);
  // Returns the name associated with this element.
  const char* (*get_elem_name)(const void* elem);
} PyUpb_ByNameMap_Funcs;

// Returns a new ByNameMap.  The vtable `funcs` must outlive this object
// (generally it should be static).  The ByNameMap will take a ref on
// `parent_obj`, which must be sufficient to keep `parent` alive.  The object
// `parent` will be passed as an argument to the functions in `funcs`.
PyObject* PyUpb_ByNameMap_New(const PyUpb_ByNameMap_Funcs* funcs,
                              const void* parent, PyObject* parent_obj);

// -----------------------------------------------------------------------------
// PyUpb_ByNumberMap
// -----------------------------------------------------------------------------

// A Python object that vends a number->descriptor map.

typedef struct {
  PyUpb_GenericSequence_Funcs base;
  // Looks up by name and returns either a pointer to the element or NULL.
  const void* (*lookup)(const void* parent, int num);
  // Returns the name associated with this element.
  int (*get_elem_num)(const void* elem);
} PyUpb_ByNumberMap_Funcs;

// Returns a new ByNumberMap.  The vtable `funcs` must outlive this object
// (generally it should be static).  The ByNumberMap will take a ref on
// `parent_obj`, which must be sufficient to keep `parent` alive.  The object
// `parent` will be passed as an argument to the functions in `funcs`.
PyObject* PyUpb_ByNumberMap_New(const PyUpb_ByNumberMap_Funcs* funcs,
                                const void* parent, PyObject* parent_obj);

bool PyUpb_InitDescriptorContainers(PyObject* m);

#endif  // PYUPB_DESCRIPTOR_CONTAINERS_H__
