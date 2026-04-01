// Copyright (c) 2026 Marshall A. Greenblatt. All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//    * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//    * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// ---------------------------------------------------------------------------
//
// Minimal version header for CEF API consumers (e.g. bindgen) when the
// full make_version_header.py output is not present in the tree.
// Values match CHROMIUM_BUILD_COMPATIBILITY.txt (147.0.7727.0).
//

#ifndef CEF_INCLUDE_CEF_VERSION_H_
#define CEF_INCLUDE_CEF_VERSION_H_

#define CEF_VERSION "147.0.0"
#define CEF_VERSION_MAJOR 147
#define CEF_VERSION_MINOR 0
#define CEF_VERSION_PATCH 0
#define CEF_COMMIT_NUMBER 0
#define CEF_COMMIT_HASH "0000000000000000000000000000000000000000"
#define COPYRIGHT_YEAR 2026

#define CHROME_VERSION_MAJOR 147
#define CHROME_VERSION_MINOR 0
#define CHROME_VERSION_BUILD 7727
#define CHROME_VERSION_PATCH 0

#define CEF_SANDBOX_COMPAT_HASH ""

#define DO_MAKE_STRING(p) #p
#define MAKE_STRING(p) DO_MAKE_STRING(p)

#endif  // CEF_INCLUDE_CEF_VERSION_H_
