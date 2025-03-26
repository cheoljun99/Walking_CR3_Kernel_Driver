/*++

Copyright (c) 1990-98  Microsoft Corporation All Rights Reserved

Module Name:

    sioctl.c

Abstract:

    Purpose of this driver is to demonstrate how the four different types
    of IOCTLs can be used, and how the I/O manager handles the user I/O
    buffers in each case. This sample also helps to understand the usage of
    some of the memory manager functions.

Environment:

    Kernel mode only.

--*/


//
// Include files.
//

#include <ntddk.h>          // various NT definitions
#include <string.h>

#include "sioctl.h"

typedef unsigned int DWORD;


#define NT_DEVICE_NAME      L"\\Device\\SIOCTL"
#define DOS_DEVICE_NAME     L"\\DosDevices\\IoctlTest"


#if DBG
#define SIOCTL_KDPRINT(_x_) \
                DbgPrint("SIOCTL.SYS: ");\
                DbgPrint _x_;

#else
#define SIOCTL_KDPRINT(_x_)
#endif



// Windows 10 19041 x64
#define PID_OFFSET 0x440
#define PS_ACTIVE_OFFSET 0x448
QWORD FindProcessEPROC(
    _In_ int nPID
)
{
    QWORD eproc = 0x00000000;
    int currentPID = 0;
    int startPID = 0;
    int iCount = 0;
    PLIST_ENTRY plistActiveProcs;

    eproc = (QWORD)PsGetCurrentProcess();
    startPID = (INT) * ((QWORD*)(eproc + (QWORD)PID_OFFSET));
    currentPID = startPID;
    for (;;)
    {
        if (nPID == currentPID)
        {
            return eproc;// found
        }
        else if ((iCount >= 1) && (startPID == currentPID))
        {
            break;
        }
        else {
            plistActiveProcs = (LIST_ENTRY*)(eproc + PS_ACTIVE_OFFSET);
            eproc = (QWORD)plistActiveProcs->Flink - PS_ACTIVE_OFFSET;
            currentPID = (INT) * ((QWORD*)(eproc + (QWORD)PID_OFFSET));
            iCount++;
        }
    }

    return 0;
}

#define DTB_OFFSET 0x028
QWORD GetProcessDirBase(QWORD eproc)
{
    QWORD   directoryTableBase;

    if (eproc == 0x0) {
        return 0x0;
    }

    directoryTableBase = *(QWORD*)(eproc + DTB_OFFSET);
    directoryTableBase = directoryTableBase & 0xfffffffff000;

    return directoryTableBase;
}




#define PFN_MASK(pe)        ((QWORD)((pe) & 0x0000FFFFFFFFF000UL))
#define PFN_SETZERO(pe)    ((QWORD)((pe) & 0xFFFF000000000FFFUL))



NTSTATUS MmReadPhysical(PVOID targetAddress, ULONG64 sourceAddress, size_t size, size_t* bytesRead)
{
    PHYSICAL_ADDRESS address = { 0 };
    MM_COPY_ADDRESS copyInfo = { 0 };
    address.QuadPart = (LONGLONG)sourceAddress;
    copyInfo.PhysicalAddress = address;
    return MmCopyMemory(targetAddress, copyInfo, size, MM_COPY_MEMORY_PHYSICAL, bytesRead);
}


//
// Device driver routine declarations.
//

DRIVER_INITIALIZE DriverEntry;

_Dispatch_type_(IRP_MJ_CREATE)
_Dispatch_type_(IRP_MJ_CLOSE)
DRIVER_DISPATCH SioctlCreateClose;

_Dispatch_type_(IRP_MJ_DEVICE_CONTROL)
DRIVER_DISPATCH SioctlDeviceControl;

DRIVER_UNLOAD SioctlUnloadDriver;

VOID
PrintIrpInfo(
    PIRP Irp
);
VOID
PrintChars(
    _In_reads_(CountChars) PCHAR BufferAddress,
    _In_ size_t CountChars
);

#ifdef ALLOC_PRAGMA
#pragma alloc_text( INIT, DriverEntry )
#pragma alloc_text( PAGE, SioctlCreateClose)
#pragma alloc_text( PAGE, SioctlDeviceControl)
#pragma alloc_text( PAGE, SioctlUnloadDriver)
#pragma alloc_text( PAGE, PrintIrpInfo)
#pragma alloc_text( PAGE, PrintChars)
#endif // ALLOC_PRAGMA


NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT   DriverObject,
    _In_ PUNICODE_STRING      RegistryPath
)
/*++

Routine Description:
    This routine is called by the Operating System to initialize the driver.

    It creates the device object, fills in the dispatch entry points and
    completes the initialization.

Arguments:
    DriverObject - a pointer to the object that represents this device
    driver.

    RegistryPath - a pointer to our Services key in the registry.

Return Value:
    STATUS_SUCCESS if initialized; an error otherwise.

--*/

{
    NTSTATUS        ntStatus;
    UNICODE_STRING  ntUnicodeString;    // NT Device Name "\Device\SIOCTL"
    UNICODE_STRING  ntWin32NameString;    // Win32 Name "\DosDevices\IoctlTest"
    PDEVICE_OBJECT  deviceObject = NULL;    // ptr to device object

    UNREFERENCED_PARAMETER(RegistryPath);

    RtlInitUnicodeString(&ntUnicodeString, NT_DEVICE_NAME);

    ntStatus = IoCreateDevice(
        DriverObject,                   // Our Driver Object
        0,                              // We don't use a device extension
        &ntUnicodeString,               // Device name "\Device\SIOCTL"
        FILE_DEVICE_UNKNOWN,            // Device type
        FILE_DEVICE_SECURE_OPEN,     // Device characteristics
        FALSE,                          // Not an exclusive device
        &deviceObject);                // Returned ptr to Device Object

    if (!NT_SUCCESS(ntStatus))
    {
        SIOCTL_KDPRINT(("Couldn't create the device object\n"));
        return ntStatus;
    }

    //
    // Initialize the driver object with this driver's entry points.
    //

    DriverObject->MajorFunction[IRP_MJ_CREATE] = SioctlCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = SioctlCreateClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = SioctlDeviceControl;
    DriverObject->DriverUnload = SioctlUnloadDriver;

    //
    // Initialize a Unicode String containing the Win32 name
    // for our device.
    //

    RtlInitUnicodeString(&ntWin32NameString, DOS_DEVICE_NAME);

    //
    // Create a symbolic link between our device name  and the Win32 name
    //

    ntStatus = IoCreateSymbolicLink(
        &ntWin32NameString, &ntUnicodeString);

    if (!NT_SUCCESS(ntStatus))
    {
        //
        // Delete everything that this routine has allocated.
        //
        SIOCTL_KDPRINT(("Couldn't create symbolic link\n"));
        IoDeleteDevice(deviceObject);
    }


    return ntStatus;
}


NTSTATUS
SioctlCreateClose(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp
)
/*++

Routine Description:

    This routine is called by the I/O system when the SIOCTL is opened or
    closed.

    No action is performed other than completing the request successfully.

Arguments:

    DeviceObject - a pointer to the object that represents the device
    that I/O is to be done on.

    Irp - a pointer to the I/O Request Packet for this request.

Return Value:

    NT status code

--*/

{
    UNREFERENCED_PARAMETER(DeviceObject);

    PAGED_CODE();

    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;

    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return STATUS_SUCCESS;
}

VOID
SioctlUnloadDriver(
    _In_ PDRIVER_OBJECT DriverObject
)
/*++

Routine Description:

    This routine is called by the I/O system to unload the driver.

    Any resources previously allocated must be freed.

Arguments:

    DriverObject - a pointer to the object that represents our driver.

Return Value:

    None
--*/

{
    PDEVICE_OBJECT deviceObject = DriverObject->DeviceObject;
    UNICODE_STRING uniWin32NameString;

    PAGED_CODE();

    //
    // Create counted string version of our Win32 device name.
    //

    RtlInitUnicodeString(&uniWin32NameString, DOS_DEVICE_NAME);


    //
    // Delete the link from our device name to a name in the Win32 namespace.
    //

    IoDeleteSymbolicLink(&uniWin32NameString);

    if (deviceObject != NULL)
    {
        IoDeleteDevice(deviceObject);
    }



}

QWORD test = 0x77;

NTSTATUS
SioctlDeviceControl(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp
)

/*++

Routine Description:

    This routine is called by the I/O system to perform a device I/O
    control function.

Arguments:

    DeviceObject - a pointer to the object that represents the device
        that I/O is to be done on.

    Irp - a pointer to the I/O Request Packet for this request.

Return Value:

    NT status code

--*/

{
    PIO_STACK_LOCATION  irpSp;// Pointer to current stack location
    NTSTATUS            ntStatus = STATUS_SUCCESS;// Assume success
    ULONG               inBufLength; // Input buffer length
    ULONG               outBufLength; // Output buffer length
    PCHAR               inBuf, outBuf; // pointer to Input and output buffer

    UNREFERENCED_PARAMETER(DeviceObject);

    PAGED_CODE();

    irpSp = IoGetCurrentIrpStackLocation(Irp);
    inBufLength = irpSp->Parameters.DeviceIoControl.InputBufferLength;
    outBufLength = irpSp->Parameters.DeviceIoControl.OutputBufferLength;

    if (!inBufLength || !outBufLength)
    {
        ntStatus = STATUS_INVALID_PARAMETER;
        goto End;
    }

    //
    // Determine which I/O control code was specified.
    //

    switch (irpSp->Parameters.DeviceIoControl.IoControlCode)
    {

    case IOCTL_SIOCTL_GET_CR3:
        //getcurrentprocessid()
        inBuf = Irp->AssociatedIrp.SystemBuffer;
        //QWORD VA를 얻어냈음
        //cr3는 pid만 있으면 얻어낼 수 있음.
        DWORD dwPid = *(DWORD*)inBuf; //변수 새로 선언
        //QWORD eproc = 0;
        QWORD eproc = FindProcessEPROC(dwPid);
        //QWORD qwCr3 = 0;
        QWORD qwCr3 = GetProcessDirBase(eproc);

        DbgPrintEx(DPFLTR_IHVDRIVER_ID, 0, "qwCr3 : %llx\n", qwCr3);

        /*
            outBuf = Irp->AssociatedIrp.SystemBuffer;
            RtlCopyBytes(outBuf, &qwPfn, sizeof(qwPfn));
            Irp->IoStatus.Information = sizeof(qwPfn);

            아웃버퍼에다가 CR3 사이즈만큼을 태운다.(덮어씌운다.)
        */
        outBuf = Irp->AssociatedIrp.SystemBuffer;
        RtlCopyBytes(outBuf, &qwCr3, sizeof(qwCr3));
        Irp->IoStatus.Information = sizeof(qwCr3);
        break;

    case IOCTL_SIOCTL_GET_PAGE:
        inBuf = Irp->AssociatedIrp.SystemBuffer;
        QWORD qwPfn = *(QWORD*)inBuf;

        size_t copySize = PAGE_SIZE;
        //Int3();
        PVOID buffer = ExAllocatePool(NonPagedPool, copySize);
        size_t dummy;
        //copy pml4
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, 0, "Pfn : %llx\n", qwPfn);
        MmReadPhysical(buffer, qwPfn, copySize, &dummy);

        outBuf = Irp->AssociatedIrp.SystemBuffer;
        RtlCopyBytes(outBuf, buffer, copySize);
        Irp->IoStatus.Information = copySize;

        break;

    default:

        //
        // The specified I/O control code is unrecognized by this driver.
        //

        ntStatus = STATUS_INVALID_DEVICE_REQUEST;
        SIOCTL_KDPRINT(("ERROR: unrecognized IOCTL %x\n",
            irpSp->Parameters.DeviceIoControl.IoControlCode));
        break;
    }

End:
    //
    // Finish the I/O operation by simply completing the packet and returning
    // the same status as in the packet itself.
    //

    Irp->IoStatus.Status = ntStatus;

    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return ntStatus;
}

VOID
PrintIrpInfo(
    PIRP Irp)
{
    PIO_STACK_LOCATION  irpSp;
    irpSp = IoGetCurrentIrpStackLocation(Irp);

    PAGED_CODE();

    SIOCTL_KDPRINT(("\tIrp->AssociatedIrp.SystemBuffer = 0x%p\n",
        Irp->AssociatedIrp.SystemBuffer));
    SIOCTL_KDPRINT(("\tIrp->UserBuffer = 0x%p\n", Irp->UserBuffer));
    SIOCTL_KDPRINT(("\tirpSp->Parameters.DeviceIoControl.Type3InputBuffer = 0x%p\n",
        irpSp->Parameters.DeviceIoControl.Type3InputBuffer));
    SIOCTL_KDPRINT(("\tirpSp->Parameters.DeviceIoControl.InputBufferLength = %d\n",
        irpSp->Parameters.DeviceIoControl.InputBufferLength));
    SIOCTL_KDPRINT(("\tirpSp->Parameters.DeviceIoControl.OutputBufferLength = %d\n",
        irpSp->Parameters.DeviceIoControl.OutputBufferLength));
    return;
}

VOID
PrintChars(
    _In_reads_(CountChars) PCHAR BufferAddress,
    _In_ size_t CountChars
)
{
    PAGED_CODE();

    if (CountChars) {

        while (CountChars--) {

            if (*BufferAddress > 31
                && *BufferAddress != 127) {

                KdPrint(("%c", *BufferAddress));

            }
            else {

                KdPrint(("."));

            }
            BufferAddress++;
        }
        KdPrint(("\n"));
    }
    return;
}


