//
// Created by Anthony Shaw on 30/10/21.
//

#include <coreclr_delegates.h>
#include <hostfxr.h>


#ifndef SRC_PYJION_COREHOST_H
#define SRC_PYJION_COREHOST_H

// Forward declarations
bool load_hostfxr();
load_assembly_and_get_function_pointer_fn get_dotnet_load_assembly();

#endif//SRC_PYJION_COREHOST_H
