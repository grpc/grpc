// Copyright 2019 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// -----------------------------------------------------------------------------
// parentheses.h
// -----------------------------------------------------------------------------
//
// This file contains macros that expand to a left parenthesis and a right
// parenthesis. These are in their own file and are generated from macros
// because otherwise clang-format gets confused and clang-format off directives
// do not help.
//
// The parentheses macros are used when wanting to require a rescan before
// expansion of parenthesized text appearing after a function-style macro name.

#ifndef ABSL_TYPES_INTERNAL_PARENTHESES_H_
#define ABSL_TYPES_INTERNAL_PARENTHESES_H_

#define ABSL_INTERNAL_LPAREN (

#define ABSL_INTERNAL_RPAREN )

#endif  // ABSL_TYPES_INTERNAL_PARENTHESES_H_
