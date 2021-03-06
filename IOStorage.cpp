/*
 * Copyright (c) 1998-2014 Apple Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 * 
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this
 * file.
 * 
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 * 
 * @APPLE_LICENSE_HEADER_END@
 */

#include <IOKit/assert.h>
#include <IOKit/IOLib.h>
#include <IOKit/storage/IOStorage.h>

#define super IOService
OSDefineMetaClassAndAbstractStructors(IOStorage, IOService)

#ifndef __LP64__
#define kIOStorageAttributesUnsupported ( ( IOStorage::ExpansionData * ) 1 )

IOStorageAttributes gIOStorageAttributesUnsupported = { kIOStorageOptionReserved };

extern "C" void _ZN9IOStorage4readEP9IOServiceyP18IOMemoryDescriptor19IOStorageCompletion( IOStorage *, IOService *, UInt64, IOMemoryDescriptor *, IOStorageCompletion );
extern "C" void _ZN9IOStorage5writeEP9IOServiceyP18IOMemoryDescriptor19IOStorageCompletion( IOStorage *, IOService *, UInt64, IOMemoryDescriptor *, IOStorageCompletion );
extern "C" void _ZN9IOStorage4readEP9IOServiceyP18IOMemoryDescriptorP19IOStorageAttributesP19IOStorageCompletion( IOStorage *, IOService *, UInt64, IOMemoryDescriptor *, IOStorageAttributes *, IOStorageCompletion * );
extern "C" void _ZN9IOStorage5writeEP9IOServiceyP18IOMemoryDescriptorP19IOStorageAttributesP19IOStorageCompletion( IOStorage *, IOService *, UInt64, IOMemoryDescriptor *, IOStorageAttributes *, IOStorageCompletion * );

#define storageAttributes( storage ) ( ( OSMemberFunctionCast( void *, storage, ( void ( IOStorage::* )( IOService *, UInt64, IOMemoryDescriptor *, IOStorageCompletion                          ) ) &IOStorage::read  ) == _ZN9IOStorage4readEP9IOServiceyP18IOMemoryDescriptor19IOStorageCompletion                         ) && \
                                       ( OSMemberFunctionCast( void *, storage, ( void ( IOStorage::* )( IOService *, UInt64, IOMemoryDescriptor *, IOStorageCompletion                          ) ) &IOStorage::write ) == _ZN9IOStorage5writeEP9IOServiceyP18IOMemoryDescriptor19IOStorageCompletion                        ) && \
                                       ( OSMemberFunctionCast( void *, storage, ( void ( IOStorage::* )( IOService *, UInt64, IOMemoryDescriptor *, IOStorageAttributes *, IOStorageCompletion * ) ) &IOStorage::read  ) != _ZN9IOStorage4readEP9IOServiceyP18IOMemoryDescriptorP19IOStorageAttributesP19IOStorageCompletion  ) && \
                                       ( OSMemberFunctionCast( void *, storage, ( void ( IOStorage::* )( IOService *, UInt64, IOMemoryDescriptor *, IOStorageAttributes *, IOStorageCompletion * ) ) &IOStorage::write ) != _ZN9IOStorage5writeEP9IOServiceyP18IOMemoryDescriptorP19IOStorageAttributesP19IOStorageCompletion ) )
#endif /* !__LP64__ */

class IOStorageSyncerLock
{
protected:

    IOLock * _lock;

public:

    inline IOStorageSyncerLock( )
    {
        _lock = IOLockAlloc( );
    }

    inline ~IOStorageSyncerLock( )
    {
        if ( _lock ) IOLockFree( _lock );
    }

    inline void lock( )
    {
        IOLockLock( _lock );
    }

    inline void unlock( )
    {
        IOLockUnlock( _lock );
    }

    inline void sleep( void * event )
    {
        IOLockSleep( _lock, event, THREAD_UNINT );
    }

    inline void wakeup( void * event )
    {
        IOLockWakeup( _lock, event, false );
    }
};

static IOStorageSyncerLock gIOStorageSyncerLock;

class IOStorageSyncer
{
protected:

    IOReturn _status;
    bool     _wakeup;

public:

    IOStorageSyncer( )
    {
        _wakeup = false;
    }

    IOReturn wait( )
    {
        gIOStorageSyncerLock.lock( );

        while ( _wakeup == false )
        {
            gIOStorageSyncerLock.sleep( this );
        }

        gIOStorageSyncerLock.unlock( );

        return _status;
    }

    void signal( IOReturn status )
    {
        _status = status;

        gIOStorageSyncerLock.lock( );

        _wakeup = true;

        gIOStorageSyncerLock.wakeup( this );

        gIOStorageSyncerLock.unlock( );
    }
};

static void storageCompletion(void *   target,
                              void *   parameter,
                              IOReturn status,
                              UInt64   actualByteCount)
{
    //
    // Internal completion routine for synchronous versions of read and write.
    //

    if (parameter)  *((UInt64 *)parameter) = actualByteCount;
    ((IOStorageSyncer *)target)->signal(status);
}

#ifndef __LP64__
bool IOStorage::init(OSDictionary * properties)
{
    //
    // Initialize this object's minimal state.
    //

    if ( super::init( properties ) == false )
    {
        return false;
    }

    if ( storageAttributes( this ) == false )
    {
        IOStorage::_expansionData = kIOStorageAttributesUnsupported;
    }

    if ( IOStorage::_expansionData )
    {
        OSDictionary * features;

        features = OSDictionary::withCapacity( 1 );

        if ( features )
        {
            setProperty( kIOStorageFeaturesKey, features );

            features->release( );
        }
    }

    return true;
}

void IOStorage::complete(IOStorageCompletion completion,
                         IOReturn            status,
                         UInt64              actualByteCount)
{
    //
    // Invokes the specified completion action of the read/write request.  If
    // the completion action is unspecified, no action is taken.  This method
    // serves simply as a convenience to storage subclass developers.
    //

    complete( &completion, status, actualByteCount );
}
#endif /* !__LP64__ */

void IOStorage::complete(IOStorageCompletion * completion,
                         IOReturn              status,
                         UInt64                actualByteCount)
{
    //
    // Invokes the specified completion action of the read/write request.  If
    // the completion action is unspecified, no action is taken.  This method
    // serves simply as a convenience to storage subclass developers.
    //

    if ( completion && completion->action )
    {
        ( completion->action )( completion->target, completion->parameter, status, actualByteCount );
    }
}

bool IOStorage::open(IOService *     client,
                     IOOptionBits    options,
                     IOStorageAccess access)
{
    //
    // Ask the storage object for permission to access its contents; the method
    // is equivalent to IOService::open(), but with the correct parameter types.
    //

    return super::open(client, options, (void *) (uintptr_t) access);
}

IOReturn IOStorage::read(IOService *           client,
                         UInt64                byteStart,
                         IOMemoryDescriptor *  buffer,
                         IOStorageAttributes * attributes,
                         UInt64 *              actualByteCount)
{
    //
    // Read data from the storage object at the specified byte offset into the
    // specified buffer, synchronously.   When the read completes, this method
    // will return to the caller.  The actual byte count field is optional.
    //

    IOStorageCompletion	completion;
    IOStorageSyncer     syncer;

    // Fill in the completion information for this request.

    completion.target    = &syncer;
    completion.action    = storageCompletion;
    completion.parameter = actualByteCount;

    // Issue the asynchronous read.

    read(client, byteStart, buffer, attributes, &completion);

    // Wait for the read to complete.

    return syncer.wait();
}

IOReturn IOStorage::write(IOService *           client,
                          UInt64                byteStart,
                          IOMemoryDescriptor *  buffer,
                          IOStorageAttributes * attributes,
                          UInt64 *              actualByteCount)
{
    //
    // Write data into the storage object at the specified byte offset from the
    // specified buffer, synchronously.   When the write completes, this method
    // will return to the caller.  The actual byte count field is optional.
    //

    IOStorageCompletion completion;
    IOStorageSyncer     syncer;

    // Fill in the completion information for this request.

    completion.target    = &syncer;
    completion.action    = storageCompletion;
    completion.parameter = actualByteCount;

    // Issue the asynchronous write.

    write(client, byteStart, buffer, attributes, &completion);

    // Wait for the write to complete.

    return syncer.wait();
}

#ifndef __LP64__
void IOStorage::read(IOService *          client,
                     UInt64               byteStart,
                     IOMemoryDescriptor * buffer,
                     IOStorageCompletion  completion)
{
    //
    // Read data from the storage object at the specified byte offset into the
    // specified buffer, asynchronously.   When the read completes, the caller
    // will be notified via the specified completion action.
    //
    // The buffer will be retained for the duration of the read.
    //

    if ( IOStorage::_expansionData == kIOStorageAttributesUnsupported )
    {
        read( client, byteStart, buffer, &gIOStorageAttributesUnsupported, &completion );
    }
    else
    {
        read( client, byteStart, buffer, NULL, &completion );
    }
}

void IOStorage::write(IOService *          client,
                      UInt64               byteStart,
                      IOMemoryDescriptor * buffer,
                      IOStorageCompletion  completion)
{
    //
    // Write data into the storage object at the specified byte offset from the
    // specified buffer, asynchronously.   When the write completes, the caller
    // will be notified via the specified completion action.
    //
    // The buffer will be retained for the duration of the write.
    //

    if ( IOStorage::_expansionData == kIOStorageAttributesUnsupported )
    {
        write( client, byteStart, buffer, &gIOStorageAttributesUnsupported, &completion );
    }
    else
    {
        write( client, byteStart, buffer, NULL, &completion );
    }
}

void IOStorage::read(IOService *           client,
                     UInt64                byteStart,
                     IOMemoryDescriptor *  buffer,
                     IOStorageAttributes * attributes,
                     IOStorageCompletion * completion)
{
    //
    // Read data from the storage object at the specified byte offset into the
    // specified buffer, asynchronously.   When the read completes, the caller
    // will be notified via the specified completion action.
    //
    // The buffer will be retained for the duration of the read.
    //

    if ( attributes && attributes->options )
    {
        complete( completion, kIOReturnUnsupported );
    }
    else
    {
        read( client, byteStart, buffer, completion ? *completion : ( IOStorageCompletion ) { 0 } );
    }
}

void IOStorage::write(IOService *           client,
                      UInt64                byteStart,
                      IOMemoryDescriptor *  buffer,
                      IOStorageAttributes * attributes,
                      IOStorageCompletion * completion)
{
    //
    // Write data into the storage object at the specified byte offset from the
    // specified buffer, asynchronously.   When the write completes, the caller
    // will be notified via the specified completion action.
    //
    // The buffer will be retained for the duration of the write.
    //

    if ( attributes && attributes->options )
    {
        complete( completion, kIOReturnUnsupported );
    }
    else
    {
        write( client, byteStart, buffer, completion ? *completion : ( IOStorageCompletion ) { 0 } );
    }
}
#endif /* !__LP64__ */

IOReturn IOStorage::discard(IOService * client,
                            UInt64      byteStart,
                            UInt64      byteCount)
{
    //
    // Delete unused data from the storage object at the specified byte offset,
    // synchronously.
    //

    return kIOReturnUnsupported;
}

IOReturn IOStorage::unmap(IOService *       client,
                          IOStorageExtent * extents,
                          UInt32            extentsCount,
                          UInt32            options)
{
    //
    // Delete unused data from the storage object at the specified byte offsets,
    // synchronously.
    //

    return kIOReturnUnsupported;
}

bool IOStorage::lockPhysicalExtents(IOService * client)
{
    //
    // Lock the contents of the storage object against relocation temporarily,
    // for the purpose of getting physical extents.
    //

    return false;
}

IOStorage * IOStorage::copyPhysicalExtent(IOService * client,
                                          UInt64 *    byteStart,
                                          UInt64 *    byteCount)
{
    //
    // Convert the specified byte offset into a physical byte offset, relative
    // to a physical storage object.  This call should only be made within the
    // context of lockPhysicalExtents().
    //

    return NULL;
}

void IOStorage::unlockPhysicalExtents(IOService * client)
{
    //
    // Unlock the contents of the storage object for relocation again.  This
    // call must balance a successful call to lockPhysicalExtents().
    //

    return;
}

IOReturn IOStorage::setPriority(IOService *       client,
                                IOStorageExtent * extents,
                                UInt32            extentsCount,
                                IOStoragePriority priority)

{
    //
    // Reprioritize read or write requests at the specified byte offsets.
    //

    return kIOReturnUnsupported;
}

OSMetaClassDefineReservedUsed(IOStorage,  0);
OSMetaClassDefineReservedUsed(IOStorage,  1);
OSMetaClassDefineReservedUsed(IOStorage,  2);
OSMetaClassDefineReservedUsed(IOStorage,  3);
OSMetaClassDefineReservedUsed(IOStorage,  4);
#ifdef __LP64__
OSMetaClassDefineReservedUnused(IOStorage,  5);
OSMetaClassDefineReservedUnused(IOStorage,  6);
OSMetaClassDefineReservedUnused(IOStorage,  7);
#else /* !__LP64__ */
OSMetaClassDefineReservedUsed(IOStorage,  5);
OSMetaClassDefineReservedUsed(IOStorage,  6);
OSMetaClassDefineReservedUsed(IOStorage,  7);
#endif /* !__LP64__ */
OSMetaClassDefineReservedUnused(IOStorage,  8);
OSMetaClassDefineReservedUnused(IOStorage,  9);
OSMetaClassDefineReservedUnused(IOStorage, 10);
OSMetaClassDefineReservedUnused(IOStorage, 11);
OSMetaClassDefineReservedUnused(IOStorage, 12);
OSMetaClassDefineReservedUnused(IOStorage, 13);
OSMetaClassDefineReservedUnused(IOStorage, 14);
OSMetaClassDefineReservedUnused(IOStorage, 15);
