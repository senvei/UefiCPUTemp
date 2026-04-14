#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/ShellCEntryLib.h>
#include <Library/BaseMemoryLib.h>
#include <Protocol/LoadedImage.h>

#include <Library/IoLib.h>
#include <Library/PciCf8Lib.h>
#include "Cpuid.h"

#define ACPI_TABLE_GUID {0xeb9d2d30, 0x2d88, 0x11d3, {0x9a, 0x16, 0x0, 0x90, 0x27, 0x3f, 0xc1, 0x4d }}
#define EFI_ACPI_TABLE_GUID { 0x8868e871, 0xe4f1, 0x11d3, {0xbc, 0x22, 0x0, 0x80, 0xc7, 0x3c, 0x88, 0x81 }}

#define cDebug 0
#define EFI_ACPI_5_0_ROOT_SYSTEM_DESCRIPTION_POINTER_REVEISION 0x02

#define PRINT_BIT_FIELD(Variable, FieldName) \
  Print (L"%5a%42a: %x\n", #Variable, #FieldName, Variable.Bits.FieldName);
  
#define PRINT_VALUE(Variable, Description) \
  Print (L"%5a%42a: %x\n", #Variable, #Description, Variable);
#define _CR(Record, TYPE, Field)  ((TYPE *) ((CHAR8 *) (Record) - (CHAR8 *) &(((TYPE *) 0)->Field)))
#define LOADED_IMAGE_PRIVATE_DATA_FROM_THIS(a) _CR(a, LOADED_IMAGE_PRIVATE_DATA_TEMP, Info)

#define MSR_TEMPERATURE_TARGET 0x1A2
#define IA32_THERM_STATUS 0x19C

extern EFI_BOOT_SERVICES *gBS;
extern EFI_SYSTEM_TABLE *gST;
extern EFI_HANDLE         gImageHandle;


UINT16 CPUTemp=0;
UINT16 CPUTempDec=0;
STATIC CONST UINTN SecondsToNanoSeconds = 10000000; //unit:100ns  10000000*100ns = 1000000000 / 1000 / 1000 / 1000 =1s

#pragma pack(1)
typedef struct {
	EFI_ACPI_DESCRIPTION_HEADER Header;
	UINT64 Reserved1;
	UINT64 PCIEBase;
	UINT16 PCISeg;
	UINT8 StartBusNo;
	UINT8 EndBusNo;
	UINT32 Reserved2;
}EFI_ACPI_MCFG;
#pragma pack()

typedef struct {
	UINTN Signature;
	EFI_HANDLE Handle; // Image handl
	UINTN Type;        //Image Type
	BOOLEAN Started;   //If entrypoint has been called
	EFI_IMAGE_ENTRY_POINT EntryPoint; //The image's entry point
	EFI_LOADED_IMAGE_PROTOCOL Info; //Loaded image protocol
}LOADED_IMAGE_PRIVATE_DATA_TEMP;

	
UINT32  gMaximumBasicFunction    = CPUID_SIGNATURE;

typedef void (*EnterResidentModeFun)();
UINTN CompareGUID(IN EFI_GUID *Guid1, IN EFI_GUID *Guid2);
UINTN GetPCIEBase();
UINT32 CpuidSignature();
UINT32 CpuidVersionInfo ();
UINTN IntelTmp();
UINTN AmdTemp();

VOID EFIAPI TimeOut(IN EFI_EVENT Event, IN VOID *Context)
{
	UINTN x, y,x1,y1;
	UINT32 CPUFLAG;
	x=gST->ConOut->Mode->CursorColumn;
	y=gST->ConOut->Mode->CursorRow;
	gST->ConOut->QueryMode(gST->ConOut,gST->ConOut->Mode->Mode,&x1,&y1);
	gST->ConOut->SetCursorPosition(gST->ConOut, x1-4,y1-1);
	CPUFLAG=CpuidSignature();
	if (0x6C65746E==CPUFLAG){
		IntelTmp();
	}else if (0x444D4163 == CPUFLAG){
		AmdTemp();
	}
	Print(L"%d",CPUTemp);
	gST->ConOut->SetCursorPosition(gST->ConOut,x,y);
}

void EnterResidentMode(){
	EFI_STATUS Status;
	EFI_HANDLE TimerOne = NULL;
	Status = gBS->CreateEvent(
				EVT_NOTIFY_SIGNAL | EVT_TIMER,
				TPL_CALLBACK,
				TimeOut,
				NULL,
				&TimerOne);
	if (EFI_ERROR(Status)){
		Print(L"Create Event Error!\n");
		return;
	}
	Status = gBS->SetTimer(
			TimerOne,
			TimerPeriodic,
			MultU64x32(SecondsToNanoSeconds, 1));
	if(EFI_ERROR(Status)){
		Print(L"Set Timer Error\n");
		return;
	}
}


UINTN CompareGUID(IN EFI_GUID *Guid1, IN EFI_GUID *Guid2){
	if((Guid1==NULL) || (Guid2 == NULL)){
		Print(L"Parameter error!\n");
		return 2;
	}
	if((Guid1->Data1 != Guid2->Data1)||
	   (Guid1->Data2 != Guid2->Data2)||
	   (Guid1->Data3 != Guid2->Data3)||
	   (Guid1->Data4[0] != Guid2->Data4[0])||
	   (Guid1->Data4[1] != Guid2->Data4[1])||
	   (Guid1->Data4[2] != Guid2->Data4[2])||
	   (Guid1->Data4[3] != Guid2->Data4[3])||
	   (Guid1->Data4[4] != Guid2->Data4[4])||
	   (Guid1->Data4[5] != Guid2->Data4[5])||
	   (Guid1->Data4[6] != Guid2->Data4[6])||
	   (Guid1->Data4[7] != Guid2->Data4[7]))
	   {return 1;}
	   return 0;
}

UINTN GetPCIEBase(){
	//EFI_STATUS Status;
	UINTN i,j,EntryCount;
	CHAR16 Sign[20];
	UINT64 *EntryPtr, PCIEBase;
	EFI_GUID AcpiTableGuid = ACPI_TABLE_GUID;
	EFI_GUID Acpi2TableGuid = EFI_ACPI_TABLE_GUID;
	EFI_CONFIGURATION_TABLE *C = NULL;
	EFI_ACPI_DESCRIPTION_HEADER *XSDT, *Entry;
	EFI_ACPI_5_0_ROOT_SYSTEM_DESCRIPTION_POINTER *Root;
	EFI_ACPI_MCFG *MCFG;
	
	C = gST->ConfigurationTable;
	//Step1. Find the table for ACPI
	for(i=0;i<gST->NumberOfTableEntries-1;i++){
		if((CompareGUID(&C->VendorGuid, &AcpiTableGuid)==0 )||(CompareGUID(&C->VendorGuid, &Acpi2TableGuid)==0))
		   {
			   Root = C->VendorTable;
			   if(cDebug){
				   Print(L"Root System Description @[0x%X]\n",Root);
			   }
			   if(cDebug){
				   ZeroMem(Sign, sizeof(Sign));
				   for(j=0;j<8;j++){
					   Sign[j] = (Root->Signature>>(j*8) & 0xFF);
				   }
				   Print(L"Signature [%S]\n",Sign);
				   Print(L"Revision [%d]\n",Root->Revision);
				   ZeroMem(Sign, sizeof(Sign));
				   for(j=0;j<6;j++){
					   Sign[j]=(Root->OemId[j] & 0xFF);
				   }
				   Print(L"OEMID [%s]\n",Sign);
				   Print(L"RSDT address = [0x%X], Length = [0x%X]\n", Root->RsdtAddress, Root->Length);
				   Print(L"XSDT address = [0x%LX]\n",Root->XsdtAddress);
			   }
			   //Step2. Check the Revision, we olny accept Revision >= 2
			   if(Root->Revision >= EFI_ACPI_5_0_ROOT_SYSTEM_DESCRIPTION_POINTER_REVEISION){
				   // Step3. Get XSDT address
				   XSDT=(EFI_ACPI_DESCRIPTION_HEADER *)(UINTN)Root->XsdtAddress;
				   EntryCount = (XSDT->Length - sizeof(EFI_ACPI_DESCRIPTION_HEADER))/sizeof(UINT64);
				   if(cDebug){
					   ZeroMem(Sign, sizeof(Sign));
					   Sign[0]=(XSDT->Signature & 0xFF);
					   Sign[1]=(XSDT->Signature >> 8 & 0xFF);
					   Sign[2]=(XSDT->Signature >> 16 & 0xFF);
					   Sign[3]=(XSDT->Signature >> 24 & 0xFF);
					   Print(L"Sign [%S]\n",Sign);
					   Print(L"Length [%d]\n",XSDT->Length);
					   Print(L"Counter [%d]\n", EntryCount);
				   }
				   //Step4. Check the signature of every entry
				   EntryPtr = (UINT64 *)(XSDT + 1);
				   for(j=0;j<EntryCount;j++,EntryPtr++){
					   Entry = (EFI_ACPI_DESCRIPTION_HEADER *)((UINTN)(*EntryPtr));
					   if(cDebug){
						   ZeroMem(Sign, sizeof(Sign));
						   Sign[0]=(Entry->Signature & 0xFF);
						   Sign[1]=(Entry->Signature >> 8 & 0xFF);
						   Sign[2]=(Entry->Signature >> 16 & 0xFF);
						   Sign[3]=(Entry->Signature >> 24 & 0xFF);		
						   Print(L"%d: [%S] @[%X]\n", j, Sign, Entry);
					   }
					   //Step5. Find the MCFG table
					   if(Entry->Signature == SIGNATURE_32('M','C','F','G')){
							MCFG = (EFI_ACPI_MCFG *)(UINTN)(Entry);
							PCIEBase = (UINTN)( MCFG->PCIEBase);
							if (cDebug){
							   Print(L"MCFG Table @[0x%X]\n",MCFG);
							   Print(L"PCIEBase @[0x%X]\n",PCIEBase);
							}
							return PCIEBase;
					   }

				   }
			   }
		   }
		C++;
	}
	return 0;
}

UINT32 CpuidSignature ( VOID )
{
  UINT32 Eax;
  UINT32 Ebx;
  UINT32 Ecx;
  UINT32 Edx;
  CHAR8  Signature[13];

  AsmCpuid (CPUID_SIGNATURE, &Eax, &Ebx, &Ecx, &Edx); //#define CPUID_SIGNATURE                         0x00

  //Print (L"CPUID_SIGNATURE (Leaf %08x)\n", CPUID_SIGNATURE);
  //Print (L"  EAX:%08x  EBX:%08x  ECX:%08x  EDX:%08x\n", Eax, Ebx, Ecx, Edx);
  //PRINT_VALUE (Eax, MaximumLeaf);
  *(UINT32 *)(Signature + 0) = Ebx;
  *(UINT32 *)(Signature + 4) = Edx;
  *(UINT32 *)(Signature + 8) = Ecx;
  Signature [12] = 0;
  //Print (L"  Signature = %a\n", Signature);

  gMaximumBasicFunction = Eax;
  return Ecx;
}

UINT32 CpuidVersionInfo ( VOID  )
{
  CPUID_VERSION_INFO_EAX  Eax;
  CPUID_VERSION_INFO_EBX  Ebx;
  CPUID_VERSION_INFO_ECX  Ecx;
  CPUID_VERSION_INFO_EDX  Edx;
  UINT32                  DisplayFamily;
  UINT32                  DisplayModel;
  UINT32 FMS=0;
  UINT32 CPUFLAG;
  CPUFLAG=CpuidSignature();
  if (CPUID_VERSION_INFO > gMaximumBasicFunction) {
    return 0;
  }
  

  AsmCpuid (CPUID_VERSION_INFO, &Eax.Uint32, &Ebx.Uint32, &Ecx.Uint32, &Edx.Uint32);

  //Print (L"CPUID_VERSION_INFO (Leaf %08x)\n", CPUID_VERSION_INFO);
  //Print (L"  EAX:%08x  EBX:%08x  ECX:%08x  EDX:%08x\n", Eax.Uint32, Ebx.Uint32, Ecx.Uint32, Edx.Uint32);
  
  FMS = Eax.Bits.SteppingId;
  FMS = Eax.Bits.Model << 4 | FMS;
  FMS = Eax.Bits.FamilyId << 8 | FMS;
  FMS = Eax.Bits.ExtendedModelId << 16 | FMS;
  FMS = Eax.Bits.ExtendedFamilyId << 20 | FMS;
  DisplayFamily = Eax.Bits.FamilyId;
  if (CPUFLAG == 0x444D4163) { //AMDCPU
	  DisplayFamily += Eax.Bits.ExtendedFamilyId;
  }else if (Eax.Bits.FamilyId == 0x0F) {
    DisplayFamily |= (Eax.Bits.ExtendedFamilyId << 4);
  }
  
  DisplayModel = Eax.Bits.Model;
  if (CPUFLAG == 0x444D4163) { //AMDCPU
	  DisplayModel |= (Eax.Bits.ExtendedModelId << 4);
  } else if (Eax.Bits.FamilyId == 0x06 || Eax.Bits.FamilyId == 0x0f) {
    DisplayModel |= (Eax.Bits.ExtendedModelId << 4);
  }

  //Print (L"  Family = %x  Model = %x  Stepping = %x, CPUID1EAX=%08X\n", DisplayFamily, DisplayModel, Eax.Bits.SteppingId, FMS);
  return FMS;
}

UINTN IntelTmp(){
	UINT32 Eax, Ebx, Ecx, Edx;
	UINT64 MTt, ITs;
	UINT32 Tt, Ts;
	AsmCpuid (0x6, &Eax, &Ebx, &Ecx, &Edx);
	if ((Eax & 0x1) == 0){
		Print(L"Intel CPU: Not Support Digital Readout Query\n");
		return 0;
	}
	MTt=AsmReadMsr64(MSR_TEMPERATURE_TARGET);
	Tt = (UINT32)((MTt & 0xFF0000) >> 16) + (UINT32)((MTt & 0x3F000000) >> 24);
	if (Tt < 80) Tt=100;
	ITs=AsmReadMsr32(IA32_THERM_STATUS);
	if ((ITs & 0x80000000) == 0) {
		Print(L"Digital Readout invalid.\n");
		return 0;
	}
	Ts = (UINT32)((ITs & 0x7F0000 )>>16);
	CPUTemp = (UINT16)(Tt - Ts);
	return CPUTemp;
}



UINTN AmdTemp(){
	UINTN PCIEB=0;
	UINTN PBA=0; 
	UINT32 CPUFMS, REGB8, REGBC;
	UINT16 Value, ValueDec;
	CPUFMS=CpuidVersionInfo();
	if(CPUFMS == 0){
		Print(L"Faild to Get CPU Version Info\n");
		return EFI_SUCCESS;
	}
	if (CPUFMS >= 0x660F01 && CPUFMS <= 0x660FF1){ //F=15H, Modes=60-6fH
		REGB8 = PCI_CF8_LIB_ADDRESS(0, 0, 0, 0xB8);
		REGBC = PCI_CF8_LIB_ADDRESS(0, 0, 0, 0xBC);
		PciCf8Write32(REGB8, 0x80000000 | 0xD8200CA4);
		gBS->Stall(200);
		CPUFMS = PciCf8Read32(REGBC);
		Value = (UINT16)(CPUFMS >> 16);
		Value = Value >> 5;
		Value = Value / 8;
		CPUTemp = Value;
		return CPUTemp;
	}
	else if (CPUFMS >= 0x730F01 && CPUFMS <= 0x730FF1){ //F=16H, Modes=30-3fH
		PCIEB=GetPCIEBase();
		PBA=PCIEB + (0 << 20) + (0x18 << 15) + (3 << 12);
		Value = MmioRead16(PBA+0xA6);
		Value = Value >> 5;
		Value = Value / 8;
		CPUTemp = Value;
		return CPUTemp;
	}
	else if (CPUFMS >=0x800F01 && CPUFMS <= 0x820FF1){
		REGB8 = PCI_CF8_LIB_ADDRESS(0, 0, 0, 0x60);
		REGBC = PCI_CF8_LIB_ADDRESS(0, 0, 0, 0x64);
		PciCf8Write32(REGB8, 0x59800);
		gBS->Stall(200);
		CPUFMS = PciCf8Read32(REGBC);
		Value = (UINT16)((CPUFMS >> 21)>>3);
		ValueDec=(UINT16)(((CPUFMS >> 21) & 0x7) * 125);
		CPUTemp=Value;
		CPUTempDec=ValueDec;
		return CPUTemp;
	}
	return 0;
	
}

INTN EFIAPI ShellAppMain ( IN UINTN  Argc, IN CHAR16  **Argv )
{
	UINT32 CPUFLAG;
	EFI_STATUS  Status = EFI_SUCCESS;
	EFI_LOADED_IMAGE_PROTOCOL *ImageInfo = NULL;
	EFI_HANDLE Handle=0;
	LOADED_IMAGE_PRIVATE_DATA_TEMP *private = NULL;
	EnterResidentModeFun fun;
	UINTN FunOffset;
	UINTN FunAddr;
	
	CPUFLAG=CpuidSignature();
	if (Argc == 1){
	    if (0x6C65746E==CPUFLAG){
		    IntelTmp();
	    }else if (0x444D4163 == CPUFLAG){
		    AmdTemp();
	    }
	    Print(L"Current CPU temperature is: %d\n",CPUTemp);
	    return EFI_SUCCESS;
	} else {
      	Status = gBS->HandleProtocol(gImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&ImageInfo);
		//Function offset in the old image
		FunOffset = (UINTN)EnterResidentMode - (UINTN)ImageInfo->ImageBase;
		//Load the image in memory again
		Status = gBS->LoadImage(FALSE, gImageHandle, NULL, ImageInfo->ImageBase, (UINTN)ImageInfo->ImageSize, &Handle);
		//get the newer imageinfo
		Status=gBS->HandleProtocol(Handle,&gEfiLoadedImageProtocolGuid, (VOID **)(&ImageInfo));
		private = LOADED_IMAGE_PRIVATE_DATA_FROM_THIS(ImageInfo);
		FunAddr=(UINTN)FunOffset + (UINTN)ImageInfo->ImageBase;
		fun = (EnterResidentModeFun)(FunAddr);
		fun();
		return EFI_SUCCESS;
	}
}

