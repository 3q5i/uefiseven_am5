/** @file

  Copyright (c) 2020, Seungjoo Kim
  Copyright (c) 2016, Dawid Ciecierski

  This program and the accompanying materials
  are licensed and made available under the terms and conditions of the BSD License
  which accompanies this distribution.  The full text of the license may be found at
  http://opensource.org/licenses/bsd-license.php

  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
  WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/

#include "Display.h"
#include "Util.h"
#include <Guid/EventGroup.h>

STATIC
CONST CHAR16 *
PixelFormatToStr (
  IN EFI_GRAPHICS_PIXEL_FORMAT PixelFormat
  )
{
  switch (PixelFormat) {
    case PixelRedGreenBlueReserved8BitPerColor:
      return L"RGBR";
    case PixelBlueGreenRedReserved8BitPerColor:
      return L"BGRR";
    case PixelBitMask:
      return L"BitMask";
    case PixelBltOnly:
      return L"BltOnly";
    default:
      return L"Unknown";
  }
}

typedef struct {
  BOOLEAN                       Saved;
  EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop;
  UINT32                        HorizontalResolution;
  UINT32                        VerticalResolution;
  EFI_GRAPHICS_PIXEL_FORMAT     PixelFormat;
  EFI_PIXEL_BITMASK             PixelInformation;
  UINT32                        PixelsPerScanLine;
  UINTN                         FrameBufferSize;
} SAVED_GOP_MODE;

STATIC SAVED_GOP_MODE  mSavedGopMode = { 0 };
STATIC EFI_EVENT       mExitBootServicesEvent = NULL;

STATIC
VOID
RestoreGopModeInfoIfSaved (
  VOID
  )
{
  if (!mSavedGopMode.Saved || (mSavedGopMode.Gop == NULL) || (mSavedGopMode.Gop->Mode == NULL)) {
    return;
  }

  PrintDebug (L"Restoring GOP mode info before ExitBootServices\n");
  mSavedGopMode.Gop->Mode->Info->HorizontalResolution = mSavedGopMode.HorizontalResolution;
  mSavedGopMode.Gop->Mode->Info->VerticalResolution   = mSavedGopMode.VerticalResolution;
  mSavedGopMode.Gop->Mode->Info->PixelFormat          = mSavedGopMode.PixelFormat;
  mSavedGopMode.Gop->Mode->Info->PixelInformation     = mSavedGopMode.PixelInformation;
  mSavedGopMode.Gop->Mode->Info->PixelsPerScanLine    = mSavedGopMode.PixelsPerScanLine;
  mSavedGopMode.Gop->Mode->FrameBufferSize            = mSavedGopMode.FrameBufferSize;
}

STATIC
VOID
EFIAPI
OnExitBootServices (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  RestoreGopModeInfoIfSaved ();
}

EFI_STATUS
InstallDisplayExitBootServicesNotifications (
  VOID
  )
{
  EFI_STATUS  Status;

  if (mExitBootServicesEvent != NULL) {
    return EFI_SUCCESS;
  }

  Status = gBS->CreateEventEx (
                  EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  OnExitBootServices,
                  NULL,
                  &gEfiEventExitBootServicesGuid,
                  &mExitBootServicesEvent
                  );
  if (EFI_ERROR (Status)) {
    PrintDebug (L"Failed to register ExitBootServices notification (error: %r)\n", Status);
    mExitBootServicesEvent = NULL;
    return Status;
  }

  PrintDebug (L"Registered ExitBootServices notification\n");
  return EFI_SUCCESS;
}


/**
  -----------------------------------------------------------------------------
  Local method implementations.
  -----------------------------------------------------------------------------
**/


/**
  Scans the system for Graphics Output Protocol (GOP) and
  Universal Graphic Adapter (UGA) compatible adapters/GPUs.
  If one is found, vital information about its video mode is
  retrieved and stored for later use.

  @retval EFI_SUCCESS     An adapter was found and its current
                          mode parameters stored in mDisplayInfo
                          global variable.
  @retval other           No compatible adapters were found or
                          their mode parameters could not be
                          retrieved.

**/
EFI_STATUS
InitializeDisplay (
  VOID
  )
{
  EFI_STATUS    Status;

  // Sets AdapterFound = FALSE and Protocol = NONE
  ZeroMem (&mDisplayInfo, sizeof (DISPLAY_INFO));

  //
  // Try a GOP adapter first.
  //
  Status = gBS->HandleProtocol (gST->ConsoleOutHandle, &gEfiGraphicsOutputProtocolGuid, (VOID **)&mDisplayInfo.GOP);
  if (EFI_ERROR (Status)) {
    Status = gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&mDisplayInfo.GOP);
  }
  if (!EFI_ERROR (Status)) {
    PrintDebug (L"Found a GOP display adapter\n");

    if (mDisplayInfo.GOP->Mode->Info->PixelFormat == PixelBltOnly) {
      UINT32                                MaxMode;
      UINT32                                ModeIndex;
      EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  *ModeInfo;
      UINTN                                 SizeOfInfo;

      PrintDebug (L"Current GOP mode is PixelBltOnly; trying to switch to a linear framebuffer mode\n");
      MaxMode = mDisplayInfo.GOP->Mode->MaxMode;
      for (ModeIndex = 0; ModeIndex < MaxMode; ModeIndex++) {
        Status = mDisplayInfo.GOP->QueryMode (mDisplayInfo.GOP, ModeIndex, &SizeOfInfo, &ModeInfo);
        if (EFI_ERROR (Status)) {
          continue;
        }

        if ((ModeInfo->PixelFormat == PixelBlueGreenRedReserved8BitPerColor)
          || (ModeInfo->PixelFormat == PixelRedGreenBlueReserved8BitPerColor)
          || (ModeInfo->PixelFormat == PixelBitMask)
          )
        {
          Status = mDisplayInfo.GOP->SetMode (mDisplayInfo.GOP, ModeIndex);
          if (!EFI_ERROR (Status)) {
            if (mDisplayInfo.GOP->Mode->FrameBufferBase > MAX_UINT32) {
              PrintDebug (L"Mode %u framebuffer base above 4GB (%lx); trying other modes\n", ModeIndex, mDisplayInfo.GOP->Mode->FrameBufferBase);
              continue;
            }

            PrintDebug (L"Switched GOP to mode %u (%ux%u, PixelFormat=%u)\n",
              ModeIndex, ModeInfo->HorizontalResolution, ModeInfo->VerticalResolution, ModeInfo->PixelFormat);
            break;
          }
        }
      }
    }

    mDisplayInfo.HorizontalResolution  = mDisplayInfo.GOP->Mode->Info->HorizontalResolution;
    mDisplayInfo.VerticalResolution    = mDisplayInfo.GOP->Mode->Info->VerticalResolution;
    mDisplayInfo.PixelFormat           = mDisplayInfo.GOP->Mode->Info->PixelFormat;
    mDisplayInfo.PixelInformation      = mDisplayInfo.GOP->Mode->Info->PixelInformation;
    mDisplayInfo.PixelsPerScanLine     = mDisplayInfo.GOP->Mode->Info->PixelsPerScanLine;
    mDisplayInfo.FrameBufferBase       = mDisplayInfo.GOP->Mode->FrameBufferBase;
    // usually = PixelsPerScanLine * VerticalResolution * BytesPerPixel
    // for MacBookAir7,2: 1536 * 900 * 4 = 5,529,600 bytes
    mDisplayInfo.FrameBufferSize       = mDisplayInfo.GOP->Mode->FrameBufferSize;

    mDisplayInfo.Protocol              = GOP;
    mDisplayInfo.AdapterFound          = TRUE;
    goto Exit;
  } else {
    PrintDebug (L"GOP display adapter not found\n");
  }

  Exit:

  if (!mDisplayInfo.AdapterFound) {
    PrintError (L"No display adapters found\n", Status);
  }

  mDisplayInfo.Initialized    = TRUE;

  return Status;
}


/**
  Calculates the x and y coordinates so that the given image
  would be displayed in screen center at the current resolution.
  Image width and height are given explicitly to allow for arbitrary
  calculations useful for sprites.

  @param[in] ImageWidth   Image width.
  @param[in] ImageHeight  Image height.
  @param[out] PositionX   Screen X coordinate of the top left corner
                          of the centered image.
  @param[out] PositionX   Screen Y coordinate of the top left corner
                          of the centered image.

  @retval EFI_SUCCESS     Screen center values were successfully
                          calculated for the current resolution
                          and specified image.
  @retval other           Either no graphics adapter was found,
                          the image was too big to fit on the
                          screen at current resolution or some
                          other problem was encountered.

**/
EFI_STATUS
CalculatePositionForCenter (
  IN  UINTN   ImageWidth,
  IN  UINTN   ImageHeight,
  OUT UINTN   *PositionX,
  OUT UINTN   *PositionY
  )
{
  if (EFI_ERROR (EnsureDisplayAvailable ())) {
    PrintDebug (L"No display adapters found, unable to calculate centered position\n");
    return EFI_DEVICE_ERROR;
  }

  if ((ImageWidth == 0) || (ImageHeight == 0)
    || (ImageWidth > mDisplayInfo.HorizontalResolution)
    || (ImageHeight > mDisplayInfo.VerticalResolution)
    )
  {
    PrintDebug (L"Wrong image size (%ux%u) for this screen resolution (%ux%u)\n",
      ImageWidth, ImageHeight, mDisplayInfo.HorizontalResolution, mDisplayInfo.VerticalResolution);
    return EFI_INVALID_PARAMETER;
  }

  *PositionX = (mDisplayInfo.HorizontalResolution / 2) - (ImageWidth / 2);
  *PositionY = (mDisplayInfo.VerticalResolution / 2) - (ImageHeight / 2);

  if ((*PositionX + ImageWidth) > mDisplayInfo.HorizontalResolution) {
    *PositionX = mDisplayInfo.HorizontalResolution - ImageWidth;
  }
  if ((*PositionY + ImageHeight) > mDisplayInfo.VerticalResolution) {
    *PositionY = mDisplayInfo.VerticalResolution - ImageHeight;
  }

  //PrintDebug (L"Top left corner position for centered image: %u,%u\n", *PositionX, *PositionY);

  return EFI_SUCCESS;
}


/**
  -----------------------------------------------------------------------------
  Exported method implementations.
  -----------------------------------------------------------------------------
**/

/**
  Performs the initial scan for graphics adapters if one
  has not been performed yet and returns a simple TRUE/FALSE
  information if one has been found and information about
  it is ready for use in the global mDisplayInfo variable.

  @retval TRUE    An adapter has been found and its current
                  mode parameters stored in mDisplayInfo
                  global variable.
  @retval other   No compatible adapters were found or
                  their mode parameters could not be
                  retrieved.

**/
EFI_STATUS
EnsureDisplayAvailable (
  VOID
  )
{
  if (!mDisplayInfo.Initialized) {
    InitializeDisplay ();
  }
  return (mDisplayInfo.AdapterFound && (mDisplayInfo.Protocol != NONE)) ? EFI_SUCCESS : EFI_NOT_FOUND;
}


/**
  Switch to a video mode with specified resolution.

  @param[in] Width      Desired screen width.
  @param[in] Height     Desired screen height.

  @retval EFI_SUCCESS   Successfully switched to a desired mode.
  @retval other         Either no graphics adapter available,
                        or the graphics adapter does not support mode switching,
                        or the graphics adapter failed to switch modes.
**/
EFI_STATUS
SwitchVideoMode (
  IN UINTN  Width,
  IN UINTN  Height
  )
{
  EFI_STATUS                              Status = EFI_DEVICE_ERROR;
  UINT32                                  MaxMode;
  UINT32                                  i;
  UINT32                                  OriginalMode;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION    *ModeInfo;
  UINTN                                   SizeOfInfo;
  BOOLEAN                                 MatchFound = FALSE;

  if ((Width == 0) || (Height == 0)) {
    return EFI_INVALID_PARAMETER;
  }

  if (EFI_ERROR (EnsureDisplayAvailable ())) {
    PrintDebug (L"No display adapters found, unable to switch video mode.\n");
    return EFI_DEVICE_ERROR;
  }

  if (mDisplayInfo.Protocol != GOP) {
    PrintError (L"Video mode switching is only supported on GOP.\n");
    return EFI_UNSUPPORTED;
  }

  // Try to switch to a desired resolution
  MaxMode = mDisplayInfo.GOP->Mode->MaxMode;
  OriginalMode = mDisplayInfo.GOP->Mode->Mode;
  for (i = 0; i < MaxMode; i++) {
    Status = mDisplayInfo.GOP->QueryMode (mDisplayInfo.GOP, i, &SizeOfInfo, &ModeInfo);
    if (!EFI_ERROR (Status)) {
      if ((ModeInfo->HorizontalResolution == Width)
        && (ModeInfo->VerticalResolution == Height)
        )
      {
        if ((ModeInfo->PixelFormat == PixelBlueGreenRedReserved8BitPerColor)
          || (ModeInfo->PixelFormat == PixelRedGreenBlueReserved8BitPerColor)
          || (ModeInfo->PixelFormat == PixelBitMask)
          )
        {
          MatchFound = TRUE;
          Status = mDisplayInfo.GOP->SetMode (mDisplayInfo.GOP, i);
          if (EFI_ERROR (Status)) {
            PrintError (L"Failed to switch to Mode %u with desired %ux%u resolution.\n", i, Width, Height);
          } else {
            if (mDisplayInfo.GOP->Mode->FrameBufferBase > MAX_UINT32) {
              PrintDebug (L"Mode %u framebuffer base above 4GB (%lx); trying other modes\n", i, mDisplayInfo.GOP->Mode->FrameBufferBase);
              continue;
            }

            PrintDebug (L"Set mode %u with desired %ux%u resolution.\n", i, Width, Height);
            goto Exit;
          }
        }
      }
    }
  }

  if (MatchFound) {
    // Candidates existed but were not usable; restore original mode.
    (VOID)mDisplayInfo.GOP->SetMode (mDisplayInfo.GOP, OriginalMode);
  }

Exit:
  // Refresh mDisplayInfo
  mDisplayInfo.HorizontalResolution  = mDisplayInfo.GOP->Mode->Info->HorizontalResolution;
  mDisplayInfo.VerticalResolution    = mDisplayInfo.GOP->Mode->Info->VerticalResolution;
  mDisplayInfo.PixelFormat           = mDisplayInfo.GOP->Mode->Info->PixelFormat;
  mDisplayInfo.PixelInformation      = mDisplayInfo.GOP->Mode->Info->PixelInformation;
  mDisplayInfo.PixelsPerScanLine     = mDisplayInfo.GOP->Mode->Info->PixelsPerScanLine;
  mDisplayInfo.FrameBufferBase       = mDisplayInfo.GOP->Mode->FrameBufferBase;
  mDisplayInfo.FrameBufferSize       = mDisplayInfo.GOP->Mode->FrameBufferSize;

  gST->ConOut->ClearScreen (gST->ConOut);

  if (!MatchFound) {
    PrintError (L"Resolution %ux%u not supported.\n", Width, Height);
  }

  return Status;
}


EFI_STATUS
ForceVideoModeHack (
  IN UINTN  Width,
  IN UINTN  Height
  )
{
  EFI_STATUS    Status = EFI_DEVICE_ERROR;
  UINT32        OrigHorizontalResolution;
  UINT32        OrigVerticalResolution;
  UINT32        OrigPixelsPerScanLine;
  UINT32        OrigFrameBufferSize;
  UINT32        NewHorizontalResolution;
  UINT32        NewVerticalResolution;
  UINT32        NewPixelsPerScanLine;
  UINT32        NewFrameBufferSize;
  UINT32        ScanlineScale = 1;

  if ((Width == 0) || (Height == 0)) {
    return EFI_INVALID_PARAMETER;
  }

  if (EFI_ERROR (EnsureDisplayAvailable ())) {
    PrintDebug (L"No display adapters found, unable to switch video mode.\n");
    return EFI_DEVICE_ERROR;
  }

  if (mDisplayInfo.Protocol != GOP) {
    PrintError (L"Video mode switching is only supported on GOP.\n");
    return EFI_UNSUPPORTED;
  }

  // Save old settings
  OrigHorizontalResolution  = mDisplayInfo.GOP->Mode->Info->HorizontalResolution;
  OrigVerticalResolution    = mDisplayInfo.GOP->Mode->Info->VerticalResolution;
  OrigPixelsPerScanLine     = mDisplayInfo.GOP->Mode->Info->PixelsPerScanLine;
  OrigFrameBufferSize       = (UINT32)mDisplayInfo.GOP->Mode->FrameBufferSize;

  if (!mSavedGopMode.Saved) {
    mSavedGopMode.Saved                = TRUE;
    mSavedGopMode.Gop                  = mDisplayInfo.GOP;
    mSavedGopMode.HorizontalResolution = OrigHorizontalResolution;
    mSavedGopMode.VerticalResolution   = OrigVerticalResolution;
    mSavedGopMode.PixelFormat          = mDisplayInfo.GOP->Mode->Info->PixelFormat;
    mSavedGopMode.PixelInformation     = mDisplayInfo.GOP->Mode->Info->PixelInformation;
    mSavedGopMode.PixelsPerScanLine    = OrigPixelsPerScanLine;
    mSavedGopMode.FrameBufferSize      = mDisplayInfo.GOP->Mode->FrameBufferSize;
  }

  NewHorizontalResolution   = (UINT32)Width;
  NewVerticalResolution     = (UINT32)Height;

  while ((OrigPixelsPerScanLine * ScanlineScale) < Width) {
    ScanlineScale++;
  }

  NewPixelsPerScanLine      = OrigPixelsPerScanLine * ScanlineScale; // Should be bigger than HorizontalResolution
  NewFrameBufferSize        = NewPixelsPerScanLine * NewVerticalResolution * 4; // PixelsPerScanLine * VerticalResolution * 4

  if (NewFrameBufferSize > OrigFrameBufferSize) {
    PrintError (L"ForceVideoModeHack aborted: computed framebuffer size %u exceeds reported size %u\n",
      NewFrameBufferSize, OrigFrameBufferSize);
    return EFI_UNSUPPORTED;
  }

  mDisplayInfo.GOP->Mode->Info->HorizontalResolution = NewHorizontalResolution;
  mDisplayInfo.GOP->Mode->Info->VerticalResolution   = NewVerticalResolution;
  //mDisplayInfo.GOP->Mode->Info->PixelFormat = 1;
  mDisplayInfo.GOP->Mode->Info->PixelsPerScanLine    = NewPixelsPerScanLine;

  mDisplayInfo.GOP->Mode->FrameBufferSize = NewFrameBufferSize;

  // Refresh mDisplayInfo
  mDisplayInfo.HorizontalResolution  = mDisplayInfo.GOP->Mode->Info->HorizontalResolution;
  mDisplayInfo.VerticalResolution    = mDisplayInfo.GOP->Mode->Info->VerticalResolution;
  mDisplayInfo.PixelFormat           = mDisplayInfo.GOP->Mode->Info->PixelFormat;
  mDisplayInfo.PixelInformation      = mDisplayInfo.GOP->Mode->Info->PixelInformation;
  mDisplayInfo.PixelsPerScanLine     = mDisplayInfo.GOP->Mode->Info->PixelsPerScanLine;
  mDisplayInfo.FrameBufferBase       = mDisplayInfo.GOP->Mode->FrameBufferBase;
  mDisplayInfo.FrameBufferSize       = mDisplayInfo.GOP->Mode->FrameBufferSize;

  gST->ConOut->ClearScreen (gST->ConOut);

  return Status;
}


/**
  Prints important information about the currently running video
  mode. Initializes adapters if they have not yet been detected.

**/
VOID
PrintVideoInfo (
  VOID
  )
{
  UINT32                                MaxMode;
  UINT32                                i;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  *ModeInfo;
  UINTN                                 SizeOfInfo;
  EFI_STATUS                            Status;

  if (EFI_ERROR (EnsureDisplayAvailable ())) {
    PrintDebug (L"No display adapters found, unable to print display information\n");
    return;
  }

  PrintDebug (L"Current mode:\n");
  PrintDebug (L"  HorizontalResolution = %u\n", mDisplayInfo.HorizontalResolution);
  PrintDebug (L"  VerticalResolution = %u\n", mDisplayInfo.VerticalResolution);
  PrintDebug (L"  PixelFormat = %u (%s)\n", mDisplayInfo.PixelFormat, PixelFormatToStr (mDisplayInfo.PixelFormat));
  if (mDisplayInfo.PixelFormat == PixelBitMask) {
    PrintDebug (L"  PixelMask R=%08x G=%08x B=%08x X=%08x\n",
      mDisplayInfo.PixelInformation.RedMask,
      mDisplayInfo.PixelInformation.GreenMask,
      mDisplayInfo.PixelInformation.BlueMask,
      mDisplayInfo.PixelInformation.ReservedMask);
  }
  PrintDebug (L"  PixelsPerScanLine = %u\n", mDisplayInfo.PixelsPerScanLine);
  PrintDebug (L"  FrameBufferBase = %x\n", mDisplayInfo.FrameBufferBase);
  PrintDebug (L"  FrameBufferSize = %u\n", mDisplayInfo.FrameBufferSize);

  // Query available modes.
  if (mDisplayInfo.Protocol == GOP) {
    MaxMode = mDisplayInfo.GOP->Mode->MaxMode;
    PrintDebug (L"Available modes (MaxMode = %u):\n", MaxMode);
    for (i = 0; i < MaxMode; i++) {
      Status = mDisplayInfo.GOP->QueryMode (mDisplayInfo.GOP, i, &SizeOfInfo, &ModeInfo);
      if (!EFI_ERROR (Status)) {
        PrintDebug (L"  Mode%u: %ux%u PixelFormat=%u (%s)\n",
          i,
          ModeInfo->HorizontalResolution,
          ModeInfo->VerticalResolution,
          ModeInfo->PixelFormat,
          PixelFormatToStr (ModeInfo->PixelFormat));
        if (ModeInfo->PixelFormat == PixelBitMask) {
          PrintDebug (L"    Mask R=%08x G=%08x B=%08x X=%08x\n",
            ModeInfo->PixelInformation.RedMask,
            ModeInfo->PixelInformation.GreenMask,
            ModeInfo->PixelInformation.BlueMask,
            ModeInfo->PixelInformation.ReservedMask);
        }
      }
    }
  }
}

/**
  Prints important information about the currently running video
  mode. Initializes adapters if they have not yet been detected.

**/
BOOLEAN
MatchCurrentResolution (
  IN UINTN  Width,
  IN UINTN  Height
  )
{
  if ((Width != 0) && (Height != 0)) {
    if (EFI_ERROR (EnsureDisplayAvailable ())) {
      PrintDebug (L"No display adapters found, unable to print display information\n");
    } else if ((mDisplayInfo.HorizontalResolution == Width)
      && (mDisplayInfo.VerticalResolution == Height)
      )
    {
      return TRUE;
    }
  }

  return FALSE;
}


/**
  Allocates resources for a new in-memory image of the specified
  width and height.

  @param[in] Width        Desired image width.
  @param[in] Height       Desired image height.

  @retval IMAGE *         Pointer to a zero-initialized memory
                          location ready to receive pixel data
                          up to the width and height specified.

**/
IMAGE *
CreateImage (
  IN  UINTN   Width,
  IN  UINTN   Height
  )
{
  IMAGE   *Image;

  if ((Width == 0) || (Height == 0)) {
    return NULL;
  }

  Image = (IMAGE *)AllocatePool (sizeof (IMAGE));
  if (Image == NULL) {
    return NULL;
  }

  Image->Width      = Width;
  Image->Height     = Height;
  Image->PixelData  = (EFI_GRAPHICS_OUTPUT_BLT_PIXEL *)AllocateZeroPool (Width * Height * sizeof (EFI_GRAPHICS_OUTPUT_BLT_PIXEL));
  if (Image->PixelData == NULL) {
    DestroyImage (Image);
    return NULL;
  }

  return Image;
}


/**
  Releases all resouces held by the specified image.

  @param[in] Image        Image whose memory resources are to
                          be released.

**/
VOID
DestroyImage (
  IN  IMAGE   *Image
  )
{
  if (Image != NULL) {
    if (Image->PixelData != NULL) {
      FreePool (Image->PixelData);
    }
    FreePool (Image);
  }
}


/**
  Converts bytes of a bitmap file into a memory representation
  useful for other graphics in-memory operations.

  Any error messages will only be printed on the debug console
  and only the error code returned to caller.

  @param[in] FileData       Pointer to the first byte of file contents.
  @param[in] FileSizeBytes  Total number of bytes available at the
                            specified location.
  @param[out] Result        Pointer to a mermory location holding
                            the address of the image structure
                            and data representing the specified bmp file.

  @retval EFI_SUCCESS       File data was interpreted successfully.
  @retval other             Either the file contained no valid or
                            supported image, no memory could be
                            allocated to hold pixel data or some other
                            problem was encountered.

**/
EFI_STATUS
BmpFileToImage (
  IN  UINT8   *FileData,
  IN  UINTN   FileSizeBytes,
  OUT VOID    **Result
  )
{
  BMP_HEADER      *BmpHeader;
  UINT8           *BmpCurrentPixel;
  UINT8           *BmpCurrentLine;
  UINTN           LineSizeBytes;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL   *TargetPixel;
  UINTN           x;
  UINTN           y;

  // Sanity checks.
  if (Result == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  if ((FileData == NULL) || (FileSizeBytes < sizeof (BMP_HEADER))) {
    PrintDebug (L"File too small or does not exist\n");
    return EFI_INVALID_PARAMETER;
  }

  BmpHeader = (BMP_HEADER *)FileData;
  if ((BmpHeader->Signature[0] != 'B')
    || (BmpHeader->Signature[1] != 'M')
    || (BmpHeader->CompressionType != 0)  // only support uncompressed...
    || (BmpHeader->BitPerPixel != 24)     // ...24 bits per pixel images
    || (BmpHeader->Width < 1)
    || (BmpHeader->Height < 1)
    )
  {
    return EFI_INVALID_PARAMETER;
  }

  *Result = CreateImage (BmpHeader->Width, BmpHeader->Height);
  if (*Result == NULL) {
    PrintDebug (L"Unable to allocate enough memory for image size %ux%u\n",
      BmpHeader->Width, BmpHeader->Height);
    return EFI_OUT_OF_RESOURCES;
  }

  // Calculate line size and adjust with padding to multiple of 4 bytes.
  LineSizeBytes = BmpHeader->Width * 3; // 24 bits = 3 bytes
  LineSizeBytes += ((LineSizeBytes % 4) != 0)
                      ? (4 - (LineSizeBytes % 4))
                      : 0;

  // Check if we have enough pixel data.
  if (BmpHeader->PixelDataOffset + BmpHeader->Height * LineSizeBytes > FileSizeBytes) {
    PrintDebug (L"Not enough pixel data (%u bytes, expected %u)\n",
      FileSizeBytes, BmpHeader->PixelDataOffset + BmpHeader->Height * LineSizeBytes);
    DestroyImage ((IMAGE *)*Result);
    return EFI_INVALID_PARAMETER;
  }

  // Fill in pixel values.
  BmpCurrentLine = FileData + BmpHeader->PixelDataOffset;
  for (y = 0; y < BmpHeader->Height; y++) {
    BmpCurrentPixel = BmpCurrentLine;
    BmpCurrentLine += LineSizeBytes;
    // jump to the right pixel line; BMP PixelArray is bottom-to-top...
    TargetPixel = ((IMAGE *)*Result)->PixelData + BmpHeader->Width * (BmpHeader->Height - y - 1);
    // ...but thankfully left-to-right
    for (x = 0; x < BmpHeader->Width; x++) {
      TargetPixel->Blue     = *BmpCurrentPixel++;
      TargetPixel->Green    = *BmpCurrentPixel++;
      TargetPixel->Red      = *BmpCurrentPixel++;
      TargetPixel->Reserved = 0;
      TargetPixel++;
    }
  }

  PrintDebug (L"Successfully imported image size %ux%u from bmp file\n",
    ((IMAGE *)*Result)->Width, ((IMAGE *)*Result)->Height);

  return EFI_SUCCESS;
}


/**
  Clears screen in both text and graphics modes.

**/
VOID
ClearScreen (
  VOID
  )
{
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL   FillColor;

  ZeroMem (&FillColor, sizeof (EFI_GRAPHICS_OUTPUT_BLT_PIXEL));

  if (EFI_ERROR (EnsureDisplayAvailable ())) {
    PrintDebug (L"No display adapters found, unable to clear screen\n");
    return;
  }

  SwitchToGraphics (FALSE);

  mDisplayInfo.GOP->Blt (
                      mDisplayInfo.GOP,
                      &FillColor,
                      EfiBltVideoFill,
                      0, 0, 0, 0,
                      mDisplayInfo.HorizontalResolution, mDisplayInfo.VerticalResolution, 0
                      );
}


VOID
DrawImage (
  IN  IMAGE   *Image,
  IN  UINTN   DrawWidth,
  IN  UINTN   DrawHeight,
  IN  UINTN   ScreenX,
  IN  UINTN   ScreenY,
  IN  UINTN   SpriteX,
  IN  UINTN   SpriteY
  )
{
  if (EFI_ERROR (EnsureDisplayAvailable ())) {
    PrintDebug (L"No display adapters found, unable to draw image\n");
    return;
  }

  if ((Image == NULL) || (DrawWidth == 0) || (DrawHeight == 0)) {
    PrintDebug (L"No image to draw\n");
    return;
  }
  if (((ScreenX + DrawWidth) > mDisplayInfo.HorizontalResolution)
    || ((ScreenY + DrawHeight) > mDisplayInfo.VerticalResolution)
    )
  {
    PrintDebug (L"Image too big to draw on screen\n");
    return;
  }

  SwitchToGraphics (FALSE);

  mDisplayInfo.GOP->Blt (
                      mDisplayInfo.GOP,
                      (EFI_GRAPHICS_OUTPUT_BLT_PIXEL *)Image->PixelData,
                      EfiBltBufferToVideo,
                      SpriteX, SpriteY, ScreenX, ScreenY,
                      DrawWidth, DrawHeight, 0
                      );
}


VOID
DrawImageCentered (
  IN  IMAGE   *Image
  )
{
  EFI_STATUS  Status;
  UINTN       PositionX;
  UINTN       PositionY;

  if (Image == NULL) {
    return;
  }

  if (EFI_ERROR (EnsureDisplayAvailable ())) {
    PrintDebug (L"No display adapters found, unable to draw centered image\n");
    return;
  }

  Status = CalculatePositionForCenter (Image->Width, Image->Height, &PositionX, &PositionY);
  if (!EFI_ERROR (Status)) {
    DrawImage (Image, Image->Width, Image->Height, PositionX, PositionY, 0, 0);
  }
}


VOID
AnimateImage (
  IN  IMAGE   *Image
  )
{
  EFI_STATUS  Status;
  UINTN       NumFrames;
  UINTN       Frame;
  UINTN       MsPerFrame = 20;
  UINTN       PositionX;
  UINTN       PositionY;

  if (Image == NULL) {
    return;
  }

  if (Image->Width == Image->Height) {
    // animation called by mistake, just show on screen
    DrawImageCentered (Image);
  } else if (Image->Width > Image->Height) {
    // frames are stacked left-to-right
    Status = CalculatePositionForCenter (Image->Height, Image->Height, &PositionX, &PositionY);
    if (EFI_ERROR (Status)) {
      return;
    }
    NumFrames = Image->Width / Image->Height;
    for (Frame = 0; Frame < NumFrames; Frame++) {
      DrawImage (Image, Image->Height, Image->Height, PositionX, PositionY, Frame * Image->Height, 0);
      gBS->Stall (MsPerFrame * 1000);
    }
  } else {
    // frames are stacked top-to-bottom
    Status = CalculatePositionForCenter (Image->Width, Image->Width, &PositionX, &PositionY);
    if (EFI_ERROR (Status)) {
      return;
    }
    NumFrames = Image->Height / Image->Width;
    for (Frame = 0; Frame < NumFrames; Frame++) {
      DrawImage (Image, Image->Width, Image->Width, PositionX, PositionY, 0, Frame * Image->Width);
      gBS->Stall (MsPerFrame * 1000);
    }
  }
}


VOID
SwitchToMode (
  IN  EFI_CONSOLE_CONTROL_SCREEN_MODE   NewMode,
  IN  BOOLEAN                           Force
  )
{
  EFI_CONSOLE_CONTROL_SCREEN_MODE   CurrentMode;
  EFI_STATUS                        Status;
  EFI_CONSOLE_CONTROL_PROTOCOL      *ConsoleControl;

  if ((NewMode != EfiConsoleControlScreenText) && (NewMode != EfiConsoleControlScreenGraphics)) {
    return;
  }

  Status = gBS->LocateProtocol (&gEfiConsoleControlProtocolGuid, NULL, (VOID **)&ConsoleControl);
  if (!EFI_ERROR (Status)) {
    Status = ConsoleControl->GetMode (ConsoleControl, &CurrentMode, NULL, NULL);
    if (Force || (!EFI_ERROR (Status) && (CurrentMode != NewMode))) {
      ConsoleControl->SetMode (ConsoleControl, NewMode);
    }
  }
}


VOID
SwitchToText (
  IN  BOOLEAN   Force
  )
{
  SwitchToMode (EfiConsoleControlScreenText, Force);
}


VOID
SwitchToGraphics (
  IN  BOOLEAN   Force
  )
{
  SwitchToMode (EfiConsoleControlScreenGraphics, Force);
}
