/* Copyright 2019 by Lane Votapka
 * All rights reserved
 * -------------------------------------------------------------------------- *
 *                                   OpenMM                                   *
 * -------------------------------------------------------------------------- *
 * This is part of the OpenMM molecular simulation toolkit originating from   *
 * Simbios, the NIH National Center for Physics-Based Simulation of           *
 * Biological Structures at Stanford, funded under the NIH Roadmap for        *
 * Medical Research, grant U54 GM072970. See https://simtk.org.               *
 *                                                                            *
 * Portions copyright (c) 2013 Stanford University and the Authors.           *
 * Authors: Peter Eastman                                                     *
 * Contributors:                                                              *
 *                                                                            *
 * Permission is hereby granted, free of charge, to any person obtaining a    *
 * copy of this software and associated documentation files (the "Software"), *
 * to deal in the Software without restriction, including without limitation  *
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,   *
 * and/or sell copies of the Software, and to permit persons to whom the      *
 * Software is furnished to do so, subject to the following conditions:       *
 *                                                                            *
 * The above copyright notice and this permission notice shall be included in *
 * all copies or substantial portions of the Software.                        *
 *                                                                            *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR *
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,   *
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL    *
 * THE AUTHORS, CONTRIBUTORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,    *
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR      *
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE  *
 * USE OR OTHER DEALINGS IN THE SOFTWARE.                                     *
 * -------------------------------------------------------------------------- */

#ifdef WIN32
#include <windows.h>
#include <sstream>
#else
#include <dlfcn.h>
#include <dirent.h>
#include <cstdlib>
#endif

#include "openmm/OpenMMException.h"

#include "MmvtLangevinMiddleIntegrator.h"
#include "ElberLangevinMiddleIntegrator.h"

#include "openmm/serialization/SerializationProxy.h"

#include "MmvtLangevinMiddleIntegratorProxy.h"
#include "ElberLangevinMiddleIntegratorProxy.h"

#if defined(WIN32)
    #include <windows.h>
    extern "C" OPENMM_EXPORT_SEEKR2 void registerMmvtSerializationProxies();
    BOOL WINAPI DllMain(HANDLE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved) {
        if (ul_reason_for_call == DLL_PROCESS_ATTACH)
            registerMmvtSerializationProxies();
        return TRUE;
    }
    extern "C" OPENMM_EXPORT_SEEKR2 void registerElberSerializationProxies();
    BOOL WINAPI DllMain(HANDLE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved) {
        if (ul_reason_for_call == DLL_PROCESS_ATTACH)
            registerElberSerializationProxies();
        return TRUE;
    }
#else
    extern "C" void __attribute__((constructor)) registerMmvtSerializationProxies();
    extern "C" void __attribute__((constructor)) registerElberSerializationProxies();
#endif

using namespace OpenMM;
using namespace Seekr2Plugin;

extern "C" OPENMM_EXPORT_SEEKR2 void registerMmvtSerializationProxies() {
    SerializationProxy::registerProxy(typeid(MmvtLangevinMiddleIntegrator), new MmvtLangevinMiddleIntegratorProxy());
}
extern "C" OPENMM_EXPORT_SEEKR2 void registerElberSerializationProxies() {
    SerializationProxy::registerProxy(typeid(ElberLangevinMiddleIntegrator), new ElberLangevinMiddleIntegratorProxy());
}
