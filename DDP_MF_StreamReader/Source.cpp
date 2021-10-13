#define WINVER _WIN32_WINNT_WIN10
#define INITGUID
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <stdio.h>
#include <mferror.h>
#include <assert.h>

#include <iostream>
#include <iomanip>

template <class T>
void SafeRelease(T** ppT)
{
    if (*ppT)
    {
        (*ppT)->Release();
        *ppT = NULL;
    }
}

std::string GuidToString(GUID* guid) {
    char guid_string[37]; // 32 hex chars + 4 hyphens + null terminator
    snprintf(
        guid_string, sizeof(guid_string),
        "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        guid->Data1, guid->Data2, guid->Data3,
        guid->Data4[0], guid->Data4[1], guid->Data4[2],
        guid->Data4[3], guid->Data4[4], guid->Data4[5],
        guid->Data4[6], guid->Data4[7]);
    return guid_string;
}


//-------------------------------------------------------------------
// ConfigureAudioStream
//
// Selects an audio stream from the source file, and configures the
// stream to deliver decoded PCM audio.
//-------------------------------------------------------------------
HRESULT ConfigureAudioStream(
    IMFSourceReader* pReader,   // Pointer to the source reader.
    IMFMediaType** ppPCMAudio   // Receives the audio format.
)
{
    IMFMediaType* pUncompressedAudioType = NULL;
    IMFMediaType* pPartialType = NULL;

    // Select the first audio stream, and deselect all other streams.
    HRESULT hr = pReader->SetStreamSelection(
        (DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE);

    if (SUCCEEDED(hr))
    {
        hr = pReader->SetStreamSelection(
            (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE);
    }

    // Create a partial media type that specifies uncompressed PCM audio.
    hr = MFCreateMediaType(&pPartialType);

    if (SUCCEEDED(hr))
    {
        hr = pPartialType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    }

    if (SUCCEEDED(hr))
    {
        hr = pPartialType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float);
    }

    // Set this type on the source reader. The source reader will
    // load the necessary decoder.
    if (SUCCEEDED(hr))
    {
        hr = pReader->SetCurrentMediaType(
            (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM,
            NULL, pPartialType);
    }

    // Get the complete uncompressed format.
    if (SUCCEEDED(hr))
    {
        hr = pReader->GetCurrentMediaType(
            (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM,
            &pUncompressedAudioType);
    }

    // Ensure the stream is selected.
    if (SUCCEEDED(hr))
    {
        hr = pReader->SetStreamSelection(
            (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM,
            TRUE);
    }

    // Return the PCM format to the caller.
    if (SUCCEEDED(hr))
    {
        *ppPCMAudio = pUncompressedAudioType;
        (*ppPCMAudio)->AddRef();
    }

    SafeRelease(&pUncompressedAudioType);
    SafeRelease(&pPartialType);
    return hr;
}

HRESULT WriteToFile(HANDLE hFile, void* p, DWORD cb)
{
    DWORD cbWritten = 0;
    HRESULT hr = S_OK;

    BOOL bResult = WriteFile(hFile, p, cb, &cbWritten, NULL);
    if (!bResult)
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
    }
    return hr;
}

HRESULT WriteWaveData(
    HANDLE hFile,               // Output file.
    IMFSourceReader* pReader,   // Source reader.
    DWORD* pcbDataWritten       // Receives the amount of data written.
)
{
    HRESULT hr = S_OK;
    DWORD cbAudioData = 0;
    DWORD cbBuffer = 0;
    BYTE* pAudioData = NULL;

    IMFSample* pSample = NULL;
    IMFMediaBuffer* pBuffer = NULL;

    // Get audio samples from the source reader.
    while (true)
    {
        DWORD dwFlags = 0;

        // Read the next sample.
        hr = pReader->ReadSample(
            (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM,
            0, NULL, &dwFlags, NULL, &pSample);

        if (FAILED(hr)) { break; }

        if (dwFlags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED)
        {
            printf("Type change - not supported by WAVE file format.\n");
            break;
        }
        if (dwFlags & MF_SOURCE_READERF_ENDOFSTREAM)
        {
            printf("End of input file.\n");
            break;
        }

        if (pSample == NULL)
        {
            printf("No sample\n");
            continue;
        }

        // Get a pointer to the audio data in the sample.

        hr = pSample->ConvertToContiguousBuffer(&pBuffer);

        if (FAILED(hr)) { break; }

        DWORD currentLength = 0;
        pBuffer->GetCurrentLength(&currentLength);
        hr = pBuffer->Lock(&pAudioData, NULL, &cbBuffer);

        currentLength = 0;
        pBuffer->GetCurrentLength(&currentLength);
        if (FAILED(hr)) { break; }

        // Write this data to the output file.
        hr = WriteToFile(hFile, pAudioData, cbBuffer);

        if (FAILED(hr)) { break; }

        // Unlock the buffer.
        hr = pBuffer->Unlock();

        currentLength = 0;
        pBuffer->GetCurrentLength(&currentLength);

        pAudioData = NULL;

        if (FAILED(hr)) { break; }

        // Update running total of audio data.
        cbAudioData += cbBuffer;

        SafeRelease(&pSample);
        SafeRelease(&pBuffer);
    }

    if (SUCCEEDED(hr))
    {
        printf("Wrote %d bytes of audio data.\n", cbAudioData);

        *pcbDataWritten = cbAudioData;
    }

    if (pAudioData)
    {
        pBuffer->Unlock();
    }

    SafeRelease(&pBuffer);
    SafeRelease(&pSample);
    return hr;
}

HRESULT WriteRawFile(
    IMFSourceReader* pReader,   // Pointer to the source reader.
    HANDLE hFile
)
{
    HRESULT hr = S_OK;

    DWORD cbHeader = 0;         // Size of the WAVE file header, in bytes.
    DWORD cbAudioData = 0;      // Total bytes of PCM audio data written to the file.
    DWORD cbMaxAudioData = 0;

    IMFMediaType* pAudioType = NULL;    // Represents the PCM audio format.

    // Configure the source reader to get uncompressed PCM audio from the source file.
    hr = ConfigureAudioStream(pReader, &pAudioType);

    // Convert the PCM audio format into a WAVEFORMATEX structure.
    WAVEFORMATEX* wavFormat;
    UINT32 wavFormatSize = 0;
    hr = MFCreateWaveFormatExFromMFMediaType(pAudioType, &wavFormat, &wavFormatSize);
    std::cout << std::setfill(' ') << std::setw(20) << "wFormatTag" << ": " << wavFormat->wFormatTag << std::endl;
    std::cout << std::setfill(' ') << std::setw(20) << "nChannels" << ": " << wavFormat->nChannels << std::endl;
    std::cout << std::setfill(' ') << std::setw(20) << "nSamplesPerSec" << ": " << wavFormat->nSamplesPerSec << std::endl;
    std::cout << std::setfill(' ') << std::setw(20) << "nAvgBytesPerSec" << ": " << wavFormat->nAvgBytesPerSec << std::endl;
    std::cout << std::setfill(' ') << std::setw(20) << "nBlockAlign" << ": " << wavFormat->nBlockAlign << std::endl;
    std::cout << std::setfill(' ') << std::setw(20) << "wBitsPerSample" << ": " << wavFormat->wBitsPerSample << std::endl;

    if (sizeof(WAVEFORMATEX) < wavFormatSize)
    {
        auto extensible = (WAVEFORMATEXTENSIBLE*)wavFormat;
        
        std::cout << std::setfill(' ') << std::setw(20) << "SubFormat" << ": " << GuidToString(&(extensible->SubFormat)).c_str()<< std::endl;
    }
    CoTaskMemFree(wavFormat);

    SafeRelease(&pAudioType);

    // Decode audio data to the file.
    hr = WriteWaveData(hFile, pReader, &cbAudioData);
    return hr;
}

void DecodeAudio(const WCHAR* sourceFile, const WCHAR* targetFile)
{
    HRESULT hr = S_OK;

    IMFSourceReader* pReader = NULL;
    HANDLE hFile = INVALID_HANDLE_VALUE;

    // Initialize the COM library.
    hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    assert(SUCCEEDED(hr));
    // Initialize the Media Foundation platform.
    hr = MFStartup(MF_VERSION);
    assert(SUCCEEDED(hr));

    // Create the source reader to read the input file.
    hr = MFCreateSourceReaderFromURL(sourceFile, NULL, &pReader);
    assert(SUCCEEDED(hr));

    // Open the output file for writing.
    hFile = CreateFile(targetFile, GENERIC_WRITE, FILE_SHARE_READ, NULL,
        CREATE_ALWAYS, 0, NULL);
    assert(hFile != INVALID_HANDLE_VALUE);

    // Read duration of current audio
    IMFPresentationDescriptor* pPD = NULL;
    MFTIME pDuration = 0;
    PROPVARIANT prop;
    PropVariantInit(&prop);
    hr = pReader->GetPresentationAttribute(MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &prop);
    assert(SUCCEEDED(hr));
    if (prop.vt == VT_UI8)
    {
        pDuration = prop.ulVal;
    }
    LONG MAX_AUDIO_DURATION_MSEC = pDuration / 10000;

    // Write the WAVE file.
    hr = WriteRawFile(pReader, hFile);
    assert(SUCCEEDED(hr));

    // Clean up.
    if (hFile != INVALID_HANDLE_VALUE)
    {
        CloseHandle(hFile);
    }

    SafeRelease(&pReader);
    MFShutdown();
    CoUninitialize();
};

int main()
{
    HeapSetInformation(NULL, HeapEnableTerminationOnCorruption, NULL, 0);
    const WCHAR* sourceFile = L"C:\\Users\\xx\\Desktop\\SpatialSoundContent\\Amaze_DD+JOC.mp4";
    const WCHAR* targetFile = L"C:\\Users\\xx\\Desktop\\decoded\\MFStreamReader_output.raw";
    DecodeAudio(sourceFile, targetFile);
    return 0;
}