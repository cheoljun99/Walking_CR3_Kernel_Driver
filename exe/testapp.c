/*++

Copyright (c) 1990-98  Microsoft Corporation All Rights Reserved

Module Name:

    testapp.c

Abstract:

Environment:

    Win32 console multi-threaded application

--*/
#include <windows.h>
#include <winioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strsafe.h>
#include <sys\sioctl.h>



void dump(const void* mem, size_t length) {
    const unsigned char* data = (const unsigned char*)mem;
    size_t i, j;

    for (i = 0; i < length; i += 16) {
        // Print offset
        printf("%016p  ", (((char*)mem + i)));

        // Print hex bytes
        for (j = 0; j < 16; ++j) {
            if (i + j < length) {
                printf("%02x ", data[i + j]);
            }
            else {
                printf("   ");
            }
        }

        // Print ASCII characters
        printf(" ");
        for (j = 0; j < 16; ++j) {
            if (i + j < length) {
                unsigned char ch = data[i + j];
                printf("%c", isprint(ch) ? ch : '.');
            }
            else {
                printf(" ");
            }
        }

        printf("\n");
    }
}


BOOLEAN
ManageDriver(
    _In_ LPCTSTR  DriverName,
    _In_ LPCTSTR  ServiceName,
    _In_ USHORT   Function
);

BOOLEAN
SetupDriverName(
    _Inout_updates_bytes_all_(BufferLength) PCHAR DriverLocation,
    _In_ ULONG BufferLength
);

char OutputBuffer[100];
char InputBuffer[100];



VOID __cdecl
main(
    _In_ ULONG argc,
    _In_reads_(argc) PCHAR argv[]
)
{
    HANDLE hDevice;
    BOOL bRc;
    ULONG bytesReturned;
    DWORD errNum = 0;
    TCHAR driverLocation[MAX_PATH];

    UNREFERENCED_PARAMETER(argc);
    UNREFERENCED_PARAMETER(argv);

    // open the device

    if ((hDevice = CreateFile("\\\\.\\IoctlTest",
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL)) == INVALID_HANDLE_VALUE) {

        errNum = GetLastError();

        if (errNum != ERROR_FILE_NOT_FOUND) {

            printf("CreateFile failed : %d\n", errNum);

            return;
        }

        // The driver is not started yet so let us the install the driver.
        // First setup full path to driver name.

        if (!SetupDriverName(driverLocation, sizeof(driverLocation))) {

            return;
        }

        if (!ManageDriver(DRIVER_NAME,
            driverLocation,
            DRIVER_FUNC_INSTALL
        )) {

            printf("Unable to install driver.\n");

            // Error - remove driver.

            ManageDriver(DRIVER_NAME,
                driverLocation,
                DRIVER_FUNC_REMOVE
            );

            return;
        }

        hDevice = CreateFile("\\\\.\\IoctlTest",
            GENERIC_READ | GENERIC_WRITE,
            0,
            NULL,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            NULL);

        if (hDevice == INVALID_HANDLE_VALUE) {
            printf("Error: CreatFile Failed : %d\n", GetLastError());
            return;
        }

    }

    // Printing Input & Output buffer pointers and size

    printf("InputBuffer Pointer = %p, BufLength = %Iu\n", InputBuffer,
        sizeof(InputBuffer));
    printf("OutputBuffer Pointer = %p BufLength = %Iu\n", OutputBuffer,
        sizeof(OutputBuffer));

    // Performing METHOD_BUFFERED

    StringCbCopy(InputBuffer, sizeof(InputBuffer),
        "This String is from User Application; using METHOD_BUFFERED");

    printf("\nCalling DeviceIoControl METHOD_BUFFERED:\n");

    char cInput;
    char* pMem = NULL;
    unsigned int unPid;
    virt_addr_t a = { 0 };

    while (1) {
        cInput = (char)getchar();
        switch (cInput)
        {
            case '1':
              
                pMem = (char*)malloc(160);
                memcpy(pMem, "mallocTest", 11);
                a.value = (QWORD)pMem;
                printf("VA : %p \nPML4 index : %lld \nPDPT index : %lld \nPD index : %lld \nPT index : %lld \nOffset : %lld\n",
                    pMem, a.a.pml4_index, a.a.pdpt_index, a.a.pd_index, a.a.pt_index, a.a.offset_4kb);
                break;
            case '2':
                dump(pMem, 160);
                printf("%p\n", pMem);
                break;
            case '3':
                unPid = GetCurrentProcessId(); //현재 프로세스 id
                printf("현재 프로세스 PID : %d\n", unPid);
                QWORD cr3 = 0;
                bRc = DeviceIoControl(hDevice,
                    (DWORD)IOCTL_SIOCTL_GET_CR3,
                    &unPid, //inbuf
                    sizeof(unPid),
                    &cr3,
                    sizeof(cr3),
                    &bytesReturned,
                    NULL
                );
                printf("지금부터 출력하는 주소는 모두 물리주소입니다!!!\n");
                printf("현재 프로세스 CR3 : %016llx\n", cr3);
                QWORD aqwTable[512] = { 0 };
                bRc = DeviceIoControl(hDevice,
                    (DWORD)IOCTL_SIOCTL_GET_PAGE,
                    &cr3,
                    sizeof(cr3),
                    &aqwTable,
                    sizeof(aqwTable),
                    &bytesReturned,
                    NULL
                );
                printf("==================== PML4T ====================\n");
                for (int i = 0; i < 512; i++) {
                    if (aqwTable[i] != 0)
                        printf("%d - %016llx\n", i, aqwTable[i]);
                }
                printf("PML4 Index : ");
                int index;
                scanf_s("%d", &index);
                QWORD qwPfn = PFN_MASK(aqwTable[index]);
                printf("%d - %016llx\n ", index, aqwTable[index]);
                bRc = DeviceIoControl(hDevice,
                    (DWORD)IOCTL_SIOCTL_GET_PAGE,
                    &qwPfn,
                    sizeof(qwPfn),
                    &aqwTable,
                    sizeof(aqwTable),
                    &bytesReturned,
                    NULL
                );
                printf("=================== PDPT ===================\n");
                for (int i = 0; i < 512; i++) {
                    if (aqwTable[i] != 0)
                        printf("%d - %016llx\n", i, aqwTable[i]);
                }
                printf("PDPT Index : ");
                scanf_s("%d", &index);
                qwPfn = PFN_MASK(aqwTable[index]);
                printf("%d - %016llx\n ", index, aqwTable[index]);
                bRc = DeviceIoControl(hDevice,
                    (DWORD)IOCTL_SIOCTL_GET_PAGE,
                    &qwPfn,
                    sizeof(qwPfn),
                    &aqwTable,
                    sizeof(aqwTable),
                    &bytesReturned,
                    NULL
                );
                printf("=================== PD ===================\n");
                for (int i = 0; i < 512; i++) {
                    if (aqwTable[i] != 0)
                        printf("%d - %016llx\n", i, aqwTable[i]);
                }
                printf("PD Index : ");
                scanf_s("%d", &index);
                qwPfn = PFN_MASK(aqwTable[index]);
                printf("%d - %016llx\n ", index, aqwTable[index]);
                bRc = DeviceIoControl(hDevice,
                    (DWORD)IOCTL_SIOCTL_GET_PAGE,
                    &qwPfn,
                    sizeof(qwPfn),
                    &aqwTable,
                    sizeof(aqwTable),
                    &bytesReturned,
                    NULL
                );
                printf("=================== PT ===================\n");
                for (int i = 0; i < 512; i++) {
                    if (aqwTable[i] != 0)
                        printf("%d - %016llx\n", i, aqwTable[i]);
                }
                printf("PT Index : ");
                scanf_s("%d", &index);
                qwPfn = PFN_MASK(aqwTable[index]);
                printf("%d - %016llx\n ", index, aqwTable[index]);
                bRc = DeviceIoControl(hDevice,
                    (DWORD)IOCTL_SIOCTL_GET_PAGE,
                    &qwPfn,
                    sizeof(qwPfn),
                    &aqwTable,
                    sizeof(aqwTable),
                    &bytesReturned,
                    NULL
                );
                //dump(aqwTable, 4096);
                //printf("%p\n", aqwTable);

                // aqwTable에서 4096 크기의 데이터를 저장할 메모리 할당
                void* buffer = malloc(4096);
                if (buffer == NULL) {
                    // 메모리 할당 실패 시 처리
                    fprintf(stderr, "Memory allocation failed\n");
                    exit(1);
                }
                // aqwTable의 데이터를 buffer로 복사
                memcpy(buffer, aqwTable, 4096);
                // a.a.offset_4kb를 고려하여 원하는 범위의 데이터를 출력
                QWORD start = a.a.offset_4kb;
                int strLen = 10;
                for (int i = 0; i < strLen; i++) {
                    printf("%c", ((unsigned char*)buffer)[start + i]);
                }
                printf("\n");
                // 메모리 해제
                free(buffer);
                break;
            case 'x':
                break;
        }
    }
    CloseHandle(hDevice);
    // Unload the driver.  Ignore any errors.
    ManageDriver(DRIVER_NAME,
        driverLocation,
        DRIVER_FUNC_REMOVE
    );
    // close the handle to the device.
}

