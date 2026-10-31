# Host API reference

Every public function, type and macro of `wrench.h`, grouped by topic, with
a link to the page that explains it. Build flags are listed in
[Build flags](../reference/build-flags.md), error codes in
[Error codes](../reference/error-codes.md).

## States

| Declaration | Description | See |
|---|---|---|
| `WRState* wr_newState( int stackSize =WRENCH_DEFAULT_STACK_SIZE )` | create a state; `stackSize` values per context stack | [running](running.md#the-state) |
| `void wr_destroyState( WRState* w )` | destroy the state, its contexts and everything they allocated | [running](running.md#the-state) |
| `void wr_setStateContext( WRState* w, void* ctx )` | set the state's user pointer (`w->ctx`) | [data](data.md#user-pointers) |
| `void* wr_getStateContext( WRState* w )` | get the state's user pointer | [data](data.md#user-pointers) |
| `void wr_setLocalContext( WRContext* c, void* ctx )` | set a context's user pointer (`c->ctxLocal`) | [data](data.md#user-pointers) |
| `void* wr_getLocalContext( WRContext* c )` | get a context's user pointer | [data](data.md#user-pointers) |
| `void wr_setAllocatedMemoryGCHint( WRState* w, const uint32_t bytes )` | bytes a context allocates before its garbage collector runs | [small targets](small-targets.md#garbage-collection) |
| `WRENCH_DEFAULT_ALLOCATED_MEMORY_GC_HINT` | the default of the above, 4000 | [small targets](small-targets.md#garbage-collection) |

## Memory

| Declaration | Description | See |
|---|---|---|
| `typedef void* (*WR_ALLOC)( size_t size )` | allocator function type | [small targets](small-targets.md#the-allocator) |
| `typedef void (*WR_FREE)( void* ptr )` | deallocator function type | [small targets](small-targets.md#the-allocator) |
| `void wr_setGlobalAllocator( WR_ALLOC wralloc, WR_FREE wrfree )` | replace `malloc()` / `free()` for all of wrench; call first | [small targets](small-targets.md#the-allocator) |
| `void* wr_malloc( size_t size )` | allocate with wrench's allocator | [small targets](small-targets.md#the-allocator) |
| `void wr_free( void* ptr )` | free memory from wrench's allocator (bytecode, `asMallocString()` text, serialized buffers) | [small targets](small-targets.md#the-allocator) |
| `extern WR_ALLOC g_malloc`, `extern WR_FREE g_free` | the current allocator pair | [small targets](small-targets.md#the-allocator) |
| `extern WRENCH_THREAD_LOCAL bool g_mallocFailed` | set when an allocation failed, cleared at the start of each call into the VM; with `WRENCH_HANDLE_MALLOC_FAIL` only | [errors](errors.md#recovering-after-an-error) |

## Hashing

| Declaration | Description | See |
|---|---|---|
| `uint32_t wr_hash( const void* dat, const int len, uint32_t serial=0 )` | wrench's hash of a block of bytes | [calling](calling.md#the-calls) |
| `uint32_t wr_hashStr( const char* dat, uint32_t serial=0 )` | hash of a zero terminated string: function names, global names, hash table keys | [calling](calling.md#the-calls) |
| `WRENCH_NULL_HASH` | `0xABABABAB`, the empty slot marker of hash tables | [build flags](../reference/build-flags.md#defined-by-wrench) |

## Compiling

| Declaration | Description | See |
|---|---|---|
| `enum WrenchCompilerFlags` | `WR_INCLUDE_GLOBALS`, `WR_EMBED_DEBUG_CODE`, `WR_EMBED_SOURCE_CODE`, `WR_NON_STRICT_VAR`, `WR_READS_KEEP_VALUES` | [compiling](compiling.md#compiler-flags) |
| `WRError wr_compile( const char* source, const int size, unsigned char** out, int* outLen, WRstr* errMsg =0, const uint8_t compilerOptionFlags = WR_INCLUDE_GLOBALS )` | compile source to a bytecode block from `wr_malloc()` | [compiling](compiling.md#the-compile-functions) |
| `WRError wr_compileEx( ..., WRstr* errMsg, const uint8_t compilerOptionFlags, int* errorLine, int* errorColumn )` | the same, reporting the line and column of an error | [compiling](compiling.md#the-compile-functions) |
| `typedef bool (*WR_INCLUDE_CALLBACK)( const char* name, const char* includer, WRstr& source, WRstr& resolved, void* usr )` | finds the text of an included file | [compiling](compiling.md#includes-and-the-include-callback) |
| `struct WRCompileOptions` | `flags`, `name`, `include`, `includeUser` | [compiling](compiling.md#includes-and-the-include-callback) |
| `WRError wr_compileWithOptions( const char* source, const int size, unsigned char** out, int* outLen, const WRCompileOptions& options, WRstr* errMsg =0, int* errorLine =0, int* errorColumn =0, WRstr* errorFile =0 )` | compile with a source name and includes | [compiling](compiling.md#includes-and-the-include-callback) |
| `bool wr_isBytecodeValid( const uint8_t* bytecode, const unsigned int len, uint32_t* hash =0 )` | check an image's checksum and version | [compiling](compiling.md#checking-and-reading-images) |
| `void wr_disassemble( const uint8_t* bytecode, const unsigned int len, WRstr& out, const bool includeComments =true )` | readable listing of an image | [compiling](compiling.md#checking-and-reading-images) |
| `void wr_disassemble( const uint8_t* bytecode, const unsigned int len, char** out, unsigned int* outLen =0 )` | the same into a buffer from `wr_malloc()` | [compiling](compiling.md#checking-and-reading-images) |

## Loading and running

| Declaration | Description | See |
|---|---|---|
| `WRContext* wr_run( WRState* w, const unsigned char* block, const int blockSize, const bool takeOwnershipOfBlock =false, const bool destroyContext =false )` | load an image and run its global code; null on failure | [running](running.md#contexts) |
| `wr_runOnce( w, block, blockSize )` | macro: run the global code, destroy the context | [running](running.md#contexts) |
| `bool wr_runCommand( WRState* w, const char* sourceCode, const int size =-1 )` | compile and run source once; false if it does not compile | [running](running.md#contexts) |
| `WRContext* wr_newContext( WRState* w, const unsigned char* block, const int blockSize, bool takeOwnership =false )` | load an image without running anything | [running](running.md#contexts) |
| `WRValue* wr_executeContext( WRContext* context )` | run a new context's global code | [running](running.md#contexts) |
| `bool wr_resetContext( WRContext* context )` | clear globals and imports so the global code can run again | [running](running.md#contexts) |
| `void wr_destroyContext( WRContext* context )` | free a context, its imports, its objects and an owned image | [running](running.md#destroying-a-context) |
| `WRContext* wr_import( WRContext* context, const unsigned char* block, const int blockSize, bool takeOwnership =false )` | load more code into a context as a module | [running](running.md#imports) |
| `WRValue* wr_returnValueFromLastCall( WRContext* context )` | the return value of the last call into the context | [calling](calling.md#return-values) |

## Calling

| Declaration | Description | See |
|---|---|---|
| `WRValue* wr_callFunction( WRContext* context, const char* functionName, const WRValue* argv =0, const int argn =0 )` | call a script or library function by name | [calling](calling.md) |
| `WRValue* wr_callFunction( WRContext* context, const int32_t hash, const WRValue* argv =0, const int argn =0 )` | the same by `wr_hashStr()` of the name | [calling](calling.md) |
| `WRValue* wr_callFunction( WRContext* context, WRFunction* function, const WRValue* argv =0, const int argn =0 )` | the same through a pointer from `wr_getFunction()`; null after the global code ended fails with `WR_ERR_execute_function_zero_called_more_than_once` | [calling](calling.md) |
| `WRFunction* wr_getFunction( WRContext* context, const char* functionName )` | look up a script function once | [calling](calling.md#the-calls) |
| `WRFunction* wr_getFunction( WRContext* context, const uint32_t functionHash )` | the same by hash | [calling](calling.md#the-calls) |
| `struct WRFunction` | opaque handle of a script function | [calling](calling.md#the-calls) |
| `WRValue* wr_continue( WRContext* context )` | resume a yielded context | [scheduling](scheduling.md#yield-from-a-script) |
| `bool wr_getYieldInfo( WRContext* context, WRValue** args =0, int* argnum =0, WRValue** returnValue =0 )` | is the context yielded, with what values | [scheduling](scheduling.md#yield-from-a-script) |

## Errors

| Declaration | Description | See |
|---|---|---|
| `enum WRError` | all compile and runtime error codes; `WR_USER` (100) and up for natives | [errors](errors.md) |
| `WRError wr_getLastError( WRState* w )` | the error of the last call into the VM | [errors](errors.md#how-errors-are-reported) |
| `uint32_t wr_getMissingFunctionHash( WRState* w )` | hash of the name behind the last `*_function_not_found` | [errors](errors.md#how-errors-are-reported) |
| `extern const char* c_errStrings[]` | names of the error codes (builds with the compiler) | [errors](errors.md#how-errors-are-reported) |

## Time slices and the scheduler

All need `WRENCH_TIME_SLICES`.

| Declaration | Description | See |
|---|---|---|
| `void wr_setInstructionsPerSlice( WRState* w, const int instructions )` | jumps and branches per call before a forced yield, 0 for no limit | [scheduling](scheduling.md#time-slices) |
| `void wr_forceYield( WRState* w )` | yield the running script at its next jump | [scheduling](scheduling.md#time-slices) |
| `int wr_slicesUsedLastCall( WRState* w )` | jumps and branches the last call used | [scheduling](scheduling.md#time-slices) |
| `class WrenchScheduler` | round robin runner of several scripts | [scheduling](scheduling.md#the-scheduler) |
| `WrenchScheduler( const int stackSizePerThread = WRENCH_DEFAULT_STACK_SIZE )` | create, with a state of its own | [scheduling](scheduling.md#the-scheduler) |
| `WRState* state() const` | the scheduler's state, to register natives | [scheduling](scheduling.md#the-scheduler) |
| `int addThread( const uint8_t* byteCode, const int size, const int instructionsThisSlice =1000, const bool takeOwnership =false )` | start a task; id, 0 if it finished at once, -1 on failure | [scheduling](scheduling.md#the-scheduler) |
| `void tick( const int instructionsPerSlice =1000 )` | one slice for every task | [scheduling](scheduling.md#the-scheduler) |
| `bool removeTask( const int taskId )` | stop and destroy a task | [scheduling](scheduling.md#the-scheduler) |
| `int lastErr() const`, `int lastErrTaskId() const`, `void clearErr()` | the last task that failed | [scheduling](scheduling.md#the-scheduler) |

## Natives and libraries

| Declaration | Description | See |
|---|---|---|
| `typedef void (*WR_C_CALLBACK)( WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr )` | a native function | [natives](natives.md#natives) |
| `void wr_registerFunction( WRState* w, const char* name, WR_C_CALLBACK function, void* usr =0 )` | register a native for all contexts of the state | [natives](natives.md#natives) |
| `typedef void (*WR_LIB_CALLBACK)( WRValue* stackTop, const int argn, WRContext* context )` | a library function | [natives](natives.md#library-functions) |
| `void wr_registerLibraryFunction( WRState* w, const char* signature, WR_LIB_CALLBACK function )` | register `lib::name` | [natives](natives.md#library-functions) |
| `void wr_registerLibraryConstant( WRState* w, const char* signature, const int32_t i )` | register an int constant `lib::NAME` | [natives](natives.md#library-functions) |
| `void wr_registerLibraryConstant( WRState* w, const char* signature, const float f )` | register a float constant | [natives](natives.md#library-functions) |
| `typedef void (*WR_C_CALLBACK_NOT_FOUND)( const uint32_t Signature, WRContext* c, const WRValue* argv, const int argn, WRValue& retVal, void* usr )` | handler for calls nothing provides | [natives](natives.md#names-nothing-provides) |
| `void wr_setOnCallbackNotFound( WRState* w, WR_C_CALLBACK_NOT_FOUND onCallbackNotFound )` | install it | [natives](natives.md#names-nothing-provides) |
| `typedef void (*WR_LIB_CALLBACK_NOT_FOUND)( const uint32_t Signature, WRValue* stackTop, const int argn, WRContext* context )` | the same for `lib::name` calls | [natives](natives.md#names-nothing-provides) |
| `void wr_setOnLibCallbackNotFound( WRState* w, WR_LIB_CALLBACK_NOT_FOUND onLibCallbackNotFound )` | install it | [natives](natives.md#names-nothing-provides) |
| `void wr_loadAllLibs( WRState* w )` | load every standard library below except the Arduino ones | [natives](natives.md#the-standard-library) |
| `void wr_loadSysLib( WRState* w )` | `sys::` | [natives](natives.md#the-standard-library) |
| `void wr_loadMathLib( WRState* w )` | `math::` | [natives](natives.md#the-standard-library) |
| `void wr_loadStdLib( WRState* w )` | `std::rand`, `std::srand`, `std::time`, `time::ms` | [natives](natives.md#the-standard-library) |
| `void wr_loadIOLib( WRState* w )` | `io::`, with a file I/O flag | [natives](natives.md#the-standard-library) |
| `void wr_loadStringLib( WRState* w )` | `str::` | [natives](natives.md#the-standard-library) |
| `void wr_loadMessageLib( WRState* w )` | `msg::` | [natives](natives.md#the-standard-library) |
| `void wr_loadSerializeLib( WRState* w )` | `std::serialize`, `std::deserialize` | [natives](natives.md#the-standard-library) |
| `void wr_loadDebugLib( WRState* w )` | `debug::print`, `debug::println` | [natives](natives.md#the-standard-library) |
| `void wr_loadTCPLib( WRState* w )` | `tcp::`, with a TCP flag | [natives](natives.md#the-standard-library) |
| `void wr_loadContainerLib( WRState* w )` | `array::`, `hash::`, `list::`, `queue::`, `stack::` | [natives](natives.md#the-standard-library) |
| `void wr_loadAllArduinoLibs( WRState* w )` | the four Arduino libraries below; declared only when `ARDUINO` is defined, as are the four | [natives](natives.md#the-standard-library) |
| `void wr_loadArduinoLib( WRState* w )` | another name for `wr_loadAllArduinoLibs()` | [natives](natives.md#the-standard-library) |
| `void wr_loadArduinoSTDLib( WRState* w )` | `std::delay` | [natives](natives.md#the-standard-library) |
| `void wr_loadArduinoIOLib( WRState* w )` | `io::pinMode`, `io::digitalWrite`, `io::analogRead` | [natives](natives.md#the-standard-library) |
| `void wr_loadArduinoWireLib( WRState* w )` | `wire::`, the Arduino `Wire` library | [natives](natives.md#the-standard-library) |
| `void wr_loadArduinoLCDLib( WRState* w )` | `lcd::begin`, `lcd::setCursor`, `lcd::print` on the display named by `WR_ARDUINO_LCD`; registers nothing without it | [natives](natives.md#the-standard-library) |
| `WR_ARDUINO_LCD` | the name of your display object (LiquidCrystal interface), for `lcd::` | [natives](natives.md#the-standard-library) |
| `void wr_loadFastLEDLib( WRState* w )` | `fastled::`; declared only when `WRENCH_ARDUINO_FASTLED` is defined | [natives](natives.md#the-standard-library) |
| `WR_FASTLED_DATA_PIN` | the LED strip's data pin, default 6 | [build flags](../reference/build-flags.md#devices) |

## Values

| Declaration | Description | See |
|---|---|---|
| `struct WRValue` | a value: int, float, string, array, hash table, struct, reference | [values](values.md) |
| `WRValue()`, `WRValue( int )`, `WRValue( float )` | construct (the default constructor leaves it uninitialized) | [values](values.md#writing-values) |
| `WRValue* init()`, `init( int )`, `init( float )` | set to 0, an int or a float | [values](values.md#writing-values) |
| `int asInt() const`, `float asFloat() const` | read as a number | [values](values.md#reading-values) |
| `void setInt( const int I )`, `void setFloat( const float F )` | write a number | [values](values.md#writing-values) |
| `void setString( WRContext* context, const char* data, const int len =0 )` | make a string in a context | [values](values.md#writing-values) |
| `bool isInt() const`, `bool isFloat() const` | type tests | [values](values.md#what-a-wrvalue-is) |
| `bool isString( int* len =0 ) const` | string test, with length | [values](values.md#what-a-wrvalue-is) |
| `bool isWrenchArray( int* len =0 ) const` | array test, with element count | [values](values.md#what-a-wrvalue-is) |
| `bool isRawArray( int* len =0 ) const` | host byte array test | [values](values.md#what-a-wrvalue-is) |
| `bool isHashTable( int* members =0 ) const` | hash table test, with entry count | [values](values.md#what-a-wrvalue-is) |
| `bool isStruct() const` | struct instance test | [values](values.md#what-a-wrvalue-is) |
| `WRValue* indexArray( WRContext* context, const uint32_t index, const bool create ) const` | element of an array, optionally created | [values](values.md#arrays-and-hash-tables) |
| `WRValue* indexHash( WRContext* context, const uint32_t hash, const bool create ) const` | entry of a hash table, optionally created | [values](values.md#arrays-and-hash-tables) |
| `WRValue* indexStruct( const char* label ) const` | member of a struct instance | [values](values.md#arrays-and-hash-tables) |
| `char* asString( char* string, unsigned int maxLen =0, unsigned int* strLen =0 ) const` | value as text into a buffer | [values](values.md#reading-values) |
| `char* asMallocString( unsigned int* strLen =0 ) const` | value as text in a `wr_malloc()` buffer | [values](values.md#reading-values) |
| `class WRValue::MallocStrScoped` | `asMallocString()` that frees itself; `operator bool`, `operator const char*`, `size()` | [values](values.md#reading-values) |
| `char* technicalAsString( char* string, unsigned int maxLen, bool valuesInHex =false, unsigned int* strLen =0 ) const` | value with contents and type, for logs | [values](values.md#reading-values) |
| `void* array( unsigned int* len =0, char arrayType =SV_CHAR ) const` | characters of a string (or data of an array of that type) | [values](values.md#reading-values) |
| `int arraySize() const` | element count of an array or string, -1 otherwise | [values](values.md#reading-values) |
| `WRValue& deref() const` | the value a reference refers to | [values](values.md#reading-values) |
| `WRValue& singleValue() const` | the value reduced to a single comparable value (internal) | [values](values.md#reading-values) |
| `uint32_t getHash() const`, `getHashEx() const`, `getHashLevel( const int depth ) const` | the value's hash, as used for keys | [values](values.md#reading-values) |
| `class WRValue::Iterator`, `begin() const`, `end() const` | iterate an array, hash table or string | [values](values.md#arrays-and-hash-tables) |
| `struct WRIteratorEntry` | `type`, `key`, `value`, `index`, `character` of one element | [values](values.md#arrays-and-hash-tables) |
| `enum WRValueType` | `WR_INT`, `WR_FLOAT`, `WR_REF`, `WR_EX`: the low type bits | [values](values.md#what-a-wrvalue-is) |
| `enum WRExType` | extended types: raw array, iterator, container member, array, struct, hash table | [values](values.md#what-a-wrvalue-is) |
| `WRValue& wr_makeInt( WRValue* val, int i )`, `WRValue& wr_makeFloat( WRValue* val, float f )` | same as `setInt()`, `setFloat()` | [values](values.md#writing-values) |
| `WRValue& wr_makeString( WRContext* context, WRValue* val, const char* data, const int len =0 )` | same as `setString()` | [values](values.md#writing-values) |
| `class WrenchValue` | converting wrapper of a global or a value: `isValid()`, `Int()`, `Float()`, `operator[]`, `asArrayMember()`, `arraySize()`, `addHashTableValue()`, `getHashTableValue()` | [values](values.md#the-wrenchvalue-helper) |

## Globals, containers and data

| Declaration | Description | See |
|---|---|---|
| `WRValue* wr_getGlobalRef( WRContext* context, const char* label )` | a global variable by name, with `WR_INCLUDE_GLOBALS` | [data](data.md#globals) |
| `WRValue& wr_makeContainer( WRValue* val, const uint16_t sizeHint =0 )` | a host owned hash table | [data](data.md#containers-built-by-the-host) |
| `void wr_destroyContainer( WRValue* val )` | free it | [data](data.md#containers-built-by-the-host) |
| `void wr_addValueToContainer( WRValue* container, const char* name, WRValue* value )` | member referring to a host `WRValue` | [data](data.md#containers-built-by-the-host) |
| `void wr_addArrayToContainer( WRValue* container, const char* name, char* array, const uint32_t size )` | member referring to host bytes | [data](data.md#containers-built-by-the-host) |
| `void wr_addFloatToContainer( WRValue* container, const char* name, const float f )` | float member | [data](data.md#containers-built-by-the-host) |
| `void wr_addIntToContainer( WRValue* container, const char* name, const int i )` | int member | [data](data.md#containers-built-by-the-host) |
| `WRValue* wr_getValueFromContainer( WRValue const& container, const char* name )` | a member by name, null when there is none | [data](data.md#containers-built-by-the-host) |
| `bool wr_keepValue( const WRValue& value )` | keep a string, array, hash table or struct instance the host holds alive across collections | [values](values.md#strings-and-their-lifetime) |
| `bool wr_releaseValue( const WRValue& value )` | let a kept value be collected again | [values](values.md#strings-and-their-lifetime) |
| `WRValue* wr_instanceStruct( WRValue* val, WRContext* context, const char* name, const WRValue* argv =0, const int argn =0 )` | `new name( ... )` from the host | [data](data.md#structs-made-by-the-host) |
| `bool wr_copyValue( WRContext* context, WRValue& dest, const WRValue& src )` | deep copy into another context | [data](data.md#values-from-another-context) |
| `bool wr_serialize( char** buf, int* len, const WRValue& value )` | value to bytes, buffer from `wr_malloc()` | [data](data.md#serialization) |
| `bool wr_deserialize( WRContext* context, WRValue& value, const char* buf, const int len )` | bytes to a value in a context | [data](data.md#serialization) |
| `bool wr_msgWrite( WRState* w, const char* key, const WRValue& value )` | store an int, float or string in the `msg::` mailbox | [data](data.md#the-msg-mailbox) |
| `bool wr_msgWriteString( WRState* w, const char* key, const char* data, const int len =-1 )` | store a string in the mailbox | [data](data.md#the-msg-mailbox) |
| `const WRValue* wr_msgPeek( WRState* w, const char* key )` | a stored message, or null | [data](data.md#the-msg-mailbox) |
| `bool wr_msgRead( WRContext* context, const char* key, WRValue& value, const bool clear =false )` | a message as a value of a context, optionally removing it | [data](data.md#the-msg-mailbox) |
| `bool wr_msgClear( WRState* w, const char* key )` | remove a message | [data](data.md#the-msg-mailbox) |

## Random numbers

| Declaration | Description | See |
|---|---|---|
| `int32_t wr_randEx( WRState* w, const int32_t from, const int32_t to )` | a random number from `from` to `to`, both included, from the state's seed (the one `std::rand` and `std::srand` use) | [threads](threads.md#the-rule-one-state-one-thread) |
| `extern int32_t wr_Seed` | the seed of the host's own generator, and the seed `wr_loadStdLib()` gives a state | [threads](threads.md#the-rule-one-state-one-thread) |
| `const int32_t wr_randEx( const int32_t from, const int32_t to )` | a random number from the host's generator, not used by any state and not thread safe | [threads](threads.md#the-rule-one-state-one-thread) |

## The engine's structures

`WRState` and `WRContext` are declared in `wrench.h` so natives and library
functions can reach their members. Use the functions above where they
exist; the members are the implementation.

| Member | Meaning |
|---|---|
| `WRState::err` | the current error; natives set it to fail a call |
| `WRState::ctx` | the state's user pointer |
| `WRState::stackSize` | the stack size of new contexts |
| `WRState::allocatedMemoryLimit` | the garbage collection threshold |
| `WRState::missingFunction` | see `wr_getMissingFunctionHash()` |
| `WRState::globalRegistry`, `WRState::mailbox` | registered functions and constants; the `msg::` table |
| `WRState::randSeed` | the seed of `std::rand` in this state, see `wr_randEx( w, ... )` |
| `WRState::contextList`, `libCleanupFunctions`, `onCallbackNotFound`, `onLibCallbackNotFound` | internal lists and the not-found handlers |
| `WRState::instructionsPerSlice`, `sliceInstructionCount`, `yieldEnabled`, `lastSlicesUsed` | time slice counters, with `WRENCH_TIME_SLICES` |
| `WRContext::w` | the context's state |
| `WRContext::ctxLocal` | the context's user pointer |
| `WRContext::globals`, `stack`, `bottom`, `bottomSize`, `codeStart`, `stopLocation` | globals count, value stack, the image and where execution stopped |
| `WRContext::yield_pc` and the other `yield_` members | the yield state (non-null `yield_pc` means yielded) |
| `WRContext::imported`, `localFunctions`, `numLocalFunctions`, `registry`, `svAllocated`, `allocatedMemoryHint`, `flags`, `stackOffset`, `nextStateContextLink` | internal |
| `WRContext::debugServer` | the attached debug server, with `WRENCH_INCLUDE_DEBUG_CODE` |
| `class WRGCBase`, `class WRGCObject` | the garbage collected objects behind strings, arrays and hash tables |
| `enum WRGCObjectType` | `SV_VOID_HASH_TABLE`, `SV_HASH_TABLE`, `SV_HASH_ENTRY`, `SV_HASH_INTERNAL`, `SV_VALUE`, `SV_CHAR`; `SV_CHAR` and `SV_VALUE` are the `arrayType` values of `WRValue::array()` |
| `enum WRGCFlags` | `GCFlag_NoContext`, `GCFlag_Marked`, `GCFlag_Perm`, `GCFlag_Slack`: collector bookkeeping |
| `struct WRLibraryCleanup`, `struct WrenchScheduledTask`, `struct WrenchDebugServerPrivate` | internal, declared for the members that point to them |

## Strings: WRstr

`class WRstr` is the small string class wrench uses internally and in its
API (error messages, the include callback). It has the usual operations:
construction from `const char*`, `c_str()`, `size()`, `set()`, `append()`,
`+=`, `format()` and `appendFormat()` (printf style), `find()`, `substr()`,
`trim()`, comparisons. Its memory comes from wrench's allocator. Short
strings live inside the object.

## Debugging

With `WRENCH_INCLUDE_DEBUG_CODE` (and the transport flags), described in
[Adding the debugger to your firmware](../debugging/firmware.md).

| Declaration | Description |
|---|---|
| `class WrenchDebugTransport` | byte link interface: `write()`, `read()`, `millis()`, optional `idle()` and `connected()` |
| `class WrenchDebugTcpServerTransport` | TCP listener on Windows and Linux: `listen( port, loopbackOnly )`, `listenOn( address, port )` (one IPv4 address, `"0.0.0.0"` for every interface), `waitForClient( timeoutMs )` |
| `class WrenchDebugTcpClientTransport` | TCP connection on Windows and Linux: `connect( host, port, timeoutMs )`, `close()` |
| `class WrenchDebugSerialTransport` | serial port on Windows and Linux: `open( port, baud )`, `close()` |
| `class WrenchDebugStreamTransport` | any Arduino `Stream`: `WrenchDebugStreamTransport( Stream& )`, `setStream()` |
| `enum WrenchDebugTickFlags` | `WRD_TickClientAttached`, `WRD_TickProgramUploaded`, `WRD_TickRunRequested` |
| `class WrenchDebugServer` | the target side of the debugger |
| `WrenchDebugServer( WRState* w, WrenchDebugTransport* transport )` | create a server for a state |
| `int tick()` | service the link from the host loop; returns `WrenchDebugTickFlags` |
| `void poll()` | service the link from inside a native that blocks |
| `int attach( WRContext* context, const char* name =0 )` | debug a context; returns its program number |
| `void detach( WRContext* context )`, `void detach()` | stop debugging one or all contexts |
| `void setLaunchable( const bool launchable )` | the firmware carries out launches and uploads |
| `bool takeUpload( uint8_t*& bytes, int& size )` | take an uploaded image (from `wr_malloc()`) |
| `void setHostDriven( const bool hostDriven )` | the firmware keeps calling into the script after its global code |
| `void breakOnEntry( const uint32_t waitMs = 0 )` | halt the next script code on its first line |
| `void setLinkTimeout( const uint32_t ms )` | release a halted target whose debugger went silent |
| `void setAllowUpload( const bool allow )` | refuse uploaded programs |
| `void print( const char* text, const int len =-1 )` | text to the debugger's console |
| `bool clientAttached() const` | a debugger completed the handshake |
| `void setAbortOnDisconnect( const bool abort )` | terminate running code when the debugger leaves |
| `onDebugInfo()`, `onError()`, `onReturn()`, `onImport()`, `onImportCall()`, `onImportReturn()`, `onGlobalCodeEnd()`, `onContextReset()`, `onContextDestroyed()` | hooks the VM calls; not for the host |

## Version

| Macro | Value |
|---|---|
| `WRENCH_VERSION_MAJOR`, `WRENCH_VERSION_MINOR`, `WRENCH_VERSION_BUILD` | 8, 0, 0 |
| `WRENCH_PREVIOUS_IMAGE_VERSION` | 7, the major version whose images this VM also runs ([compiling](compiling.md#images-from-other-versions)) |
