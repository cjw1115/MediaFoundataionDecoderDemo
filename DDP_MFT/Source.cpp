#include "MFDebuggingHelper.h"
#include <vector>
#include <string>
#include <wil/com.h>
#include <wil/win32_helpers.h>
#include <iostream>

template <class T>
void SafeRelease(T** ppT)
{
    if (*ppT)
    {
        (*ppT)->Release();
        *ppT = NULL;
    }
}

//  Create a media source from a URL.
HRESULT CreateMediaSource(PCWSTR sURL, IMFMediaSource** ppSource)
{
    MF_OBJECT_TYPE ObjectType = MF_OBJECT_INVALID;

    IMFSourceResolver* pSourceResolver = NULL;
    IUnknown* pSource = NULL;

    // Create the source resolver.
    HRESULT hr = MFCreateSourceResolver(&pSourceResolver);
    if (FAILED(hr))
    {
        goto done;
    }

    // Use the source resolver to create the media source.

    // Note: For simplicity this sample uses the synchronous method to create 
    // the media source. However, creating a media source can take a noticeable
    // amount of time, especially for a network source. For a more responsive 
    // UI, use the asynchronous BeginCreateObjectFromURL method.

    hr = pSourceResolver->CreateObjectFromURL(
        sURL,                       // URL of the source.
        MF_RESOLUTION_MEDIASOURCE,  // Create a source object.
        NULL,                       // Optional property store.
        &ObjectType,        // Receives the created object type. 
        &pSource            // Receives a pointer to the media source.
    );
    if (FAILED(hr))
    {
        goto done;
    }

    // Get the IMFMediaSource interface from the media source.
    hr = pSource->QueryInterface(IID_PPV_ARGS(ppSource));

done:
    SafeRelease(&pSourceResolver);
    SafeRelease(&pSource);
    return hr;
}

void DemoMediaSource()
{
    HRESULT hr = CoInitializeEx(0, COINIT_MULTITHREADED);
    IMFMediaSource* mfSource;
    hr = MFStartup(MF_VERSION);
    hr = CreateMediaSource(L"C:/Users/cjw11/Desktop/SpatialSoundContent/Amaze_DD+JOC.mp4", &mfSource);
    IMFPresentationDescriptor* descriptor;
    hr = mfSource->CreatePresentationDescriptor(&descriptor);

    DWORD count = 0;
    descriptor->GetStreamDescriptorCount(&count);
    for (size_t i = 0; i < count; i++)
    {
        BOOL selected = false;
        IMFStreamDescriptor* streamDsc;
        hr = descriptor->GetStreamDescriptorByIndex(i, &selected, &streamDsc);
        DWORD identifer;
        streamDsc->GetStreamIdentifier(&identifer);
        IMFMediaTypeHandler* handler;
        streamDsc->GetMediaTypeHandler(&handler);

        GUID majorType;
        handler->GetMajorType(&majorType);
        auto mainName = GetGUIDNameConst(majorType);

        DWORD mediaTypeCount = 0;
        handler->GetMediaTypeCount(&mediaTypeCount);
        for (size_t typeIndex = 0; typeIndex < mediaTypeCount; typeIndex++)
        {
            IMFMediaType* type;
            handler->GetMediaTypeByIndex(typeIndex, &type);
            GUID mediaMajorType;
            type->GetGUID(MF_MT_MAJOR_TYPE, &mediaMajorType);
            auto name = GetGUIDNameConst(mediaMajorType);

            GUID subType;
            type->GetGUID(MF_MT_SUBTYPE, &subType);
            auto subName = GetGUIDNameConst(subType);
            BOOL isCompressed;
            type->IsCompressedFormat(&isCompressed);
            continue;
        }
    }
}

std::vector<byte> getRawBitStream(const char* wavPath)
{
    std::vector<byte> data;
    FILE* file;
    fopen_s(&file, wavPath, "rb");

    fseek(file, 0, SEEK_END);
    auto size = ftell(file);
    fseek(file, 0, SEEK_SET);

    auto wavBuffer = (byte*)malloc(size);
    auto readed = fread(wavBuffer, 1, size, file);

    byte* pData = nullptr;
    int dataSize = 0;
    for (size_t i = 0; i < size - 4; i++)
    {
        if (wavBuffer[i] == 'd'
            && wavBuffer[i + 1] == 'a'
            && wavBuffer[i + 2] == 't'
            && wavBuffer[i + 3] == 'a')
        {
            auto pSize = &wavBuffer[i + 4];
            dataSize = *(int*)pSize;
            pData = (byte*)(pSize + sizeof(int));
            break;
        }
    }

    data = { pData,pData + dataSize };
    fclose(file);
    return data;
}

class PCMWriter
{
public:
    PCMWriter(std::string path)
    {
        fopen_s(&outputPCMFile, path.c_str(), "wb");
    }
    ~PCMWriter()
    {
        Close();
    }
    void Write(byte* buffer, int size)
    {
        fwrite(buffer, 1, size, outputPCMFile);
    }
    void Close()
    {
        if (outputPCMFile != nullptr)
        {
            fclose(outputPCMFile);
            outputPCMFile = nullptr;
        }
    }
private:
    FILE* outputPCMFile = nullptr;
};

#define DDPIN_BUFFER_SIZE 1024


void DecodeAudio(const char* sourceFile, const char* targetFile)
{
    HRESULT hr = CoInitializeEx(0, COINIT_MULTITHREADED);
    hr = MFStartup(MF_VERSION);

    MFT_REGISTER_TYPE_INFO inputType;
    inputType.guidMajorType = MFMediaType_Audio;
    inputType.guidSubtype = MFAudioFormat_Dolby_DDPlus;


    MFT_REGISTER_TYPE_INFO outputType;
    outputType.guidMajorType = MFMediaType_Audio;
    outputType.guidSubtype = MFAudioFormat_Float;

    CLSID* mftTypes = nullptr;
    UINT count = 0;
    hr = MFTEnum(MFT_CATEGORY_AUDIO_DECODER, 0, &inputType, &outputType, 0, &mftTypes, &count);

    INT32 unFlags = MFT_ENUM_FLAG_FIELDOFUSE;
    IMFActivate** ppActivate = NULL;    // Array of activation objects.
    hr = MFTEnumEx(MFT_CATEGORY_AUDIO_DECODER, unFlags, &inputType, &outputType, &ppActivate, &count);

    IMFTransform* mft;
    hr = ppActivate[0]->ActivateObject(IID_PPV_ARGS(&mft));

    DWORD inputStreams, outputStream;
    hr = mft->GetStreamCount(&inputStreams, &outputStream);

    DWORD inputIds[2];
    DWORD outputIds[2];
    hr = mft->GetStreamIDs(inputStreams, inputIds, outputStream, outputIds);
    
#pragma region Set Input Media Type
    wil::com_ptr<IMFMediaType> inputMediaType;
    for (size_t i = 0; i < 10; i++)
    {
        wil::com_ptr<IMFMediaType> mediaType;
        hr = mft->GetInputAvailableType(0, i, &mediaType);
        if (hr != S_OK)
            break;
        WAVEFORMATEX* wavFormat;
        UINT32 wavFormatSize = 0;
        hr = MFCreateWaveFormatExFromMFMediaType(mediaType.get(), &wavFormat, &wavFormatSize);
        
        if (sizeof(WAVEFORMATEX) < wavFormatSize)
        {
            auto extensible = (WAVEFORMATEXTENSIBLE*)wavFormat;
            auto formatName = GetGUIDNameConst(extensible->SubFormat);
            if (MFAudioFormat_Dolby_DDPlus == extensible->SubFormat)
            {
                inputMediaType = mediaType;
            }
            wavFormatSize = 0;
        }
        CoTaskMemFree(wavFormat);
    }

    WAVEFORMATEX* inputWavFormat;
    UINT32 inputWavFormatSize = 0;
    hr = MFCreateWaveFormatExFromMFMediaType(inputMediaType.get(), &inputWavFormat, &inputWavFormatSize);
    if (sizeof(WAVEFORMATEX) < inputWavFormatSize)
    {
        auto extensible = (WAVEFORMATEXTENSIBLE*)inputWavFormat;
        inputWavFormat->nChannels = 6;
        inputWavFormat->nSamplesPerSec = 48000;
        inputWavFormat->nBlockAlign = sizeof(float) * inputWavFormat->nChannels;
        inputWavFormat->wBitsPerSample = sizeof(float) * 8;
        inputWavFormat->nAvgBytesPerSec = inputWavFormat->nSamplesPerSec * inputWavFormat->nBlockAlign;
        hr = MFInitMediaTypeFromWaveFormatEx(inputMediaType.get(), inputWavFormat, inputWavFormatSize);
    }
    CoTaskMemFree(inputWavFormat);
    inputWavFormatSize = 0;
    inputWavFormat = nullptr;
    hr = MFCreateWaveFormatExFromMFMediaType(inputMediaType.get(), &inputWavFormat, &inputWavFormatSize);
    hr = mft->SetInputType(0, inputMediaType.get(), NULL);
#pragma endregion

#pragma region Set Ouput Media Type
    wil::com_ptr<IMFMediaType> outputMediaType;
    for (size_t i = 0; i < 10; i++)
    {
        wil::com_ptr<IMFMediaType> mediaType;
        hr = mft->GetOutputAvailableType(0, i, &mediaType);
        if (hr != S_OK)
            break;
        WAVEFORMATEX* wavFormat;
        UINT32 wavFormatSize = 0;
        hr = MFCreateWaveFormatExFromMFMediaType(mediaType.get(), &wavFormat, &wavFormatSize);
       
        if (sizeof(WAVEFORMATEX) < wavFormatSize)
        {
            auto extensible = (WAVEFORMATEXTENSIBLE*)wavFormat;
            auto formatName = GetGUIDNameConst(extensible->SubFormat);
            if (MFAudioFormat_Float == extensible->SubFormat && wavFormat->nChannels == 6)
            {
                outputMediaType = mediaType;
            }
        }
        CoTaskMemFree(wavFormat);
    }

    WAVEFORMATEX* wavFormat;
    UINT32 wavFormatSize = 0;
    hr = MFCreateWaveFormatExFromMFMediaType(outputMediaType.get(), &wavFormat, &wavFormatSize);
    if (sizeof(WAVEFORMATEX) < wavFormatSize)
    {
        auto extensible = (WAVEFORMATEXTENSIBLE*)wavFormat;
        wavFormatSize = 0;
    }
    CoTaskMemFree(wavFormat);
    hr = mft->SetOutputType(0, outputMediaType.get(), NULL);
#pragma endregion


    hr = mft->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);

    MFT_INPUT_STREAM_INFO inputInfo;
    hr = mft->GetInputStreamInfo(0, &inputInfo);

    MFT_OUTPUT_STREAM_INFO outputInfo;
    hr = mft->GetOutputStreamInfo(0, &outputInfo);

    auto bitStreamBuffer = getRawBitStream(sourceFile);
    auto index = 0;
    uint32_t totalSize = bitStreamBuffer.size();
    uint32_t avaliableSize = totalSize;
    uint32_t processedSize = 0;
    bool endOfProcess = false;

    PCMWriter writer{ targetFile };

    while (!endOfProcess)
    {
        bool isTimesliceComplete = false;
        while (!isTimesliceComplete)
        {
            wil::com_ptr<IMFSample> inputSample;
            hr = MFCreateSample(&inputSample);
            wil::com_ptr<IMFMediaBuffer> buffer;
            hr = MFCreateMemoryBuffer(DDPIN_BUFFER_SIZE, &buffer);
            hr = inputSample->AddBuffer(buffer.get());

            byte* tempBuffer = nullptr;
            DWORD maxBufferLength = 0;
            DWORD currentLength = 0;
            auto loadedSize = DDPIN_BUFFER_SIZE;
            hr = buffer->Lock(&tempBuffer, &maxBufferLength, &currentLength);
            {
                if (index + DDPIN_BUFFER_SIZE > totalSize)
                {
                    loadedSize = totalSize - index;
                    endOfProcess = true;
                }
                memcpy(tempBuffer, bitStreamBuffer.data() + index, loadedSize);
                index += loadedSize;
                avaliableSize -= loadedSize;
                hr = buffer->SetCurrentLength(loadedSize);
            }
            hr = buffer->Unlock();
            
            DWORD inputBufferLength = 0;
            hr = buffer->GetCurrentLength(&inputBufferLength);
            std::cout << "Load buffer from bitstream: " << inputBufferLength << " bytes" << std::endl;

            hr = mft->ProcessInput(0, inputSample.get(), NULL);
            if (hr == MF_E_NOTACCEPTING)
            {
                index -= loadedSize;
                avaliableSize += loadedSize;
                isTimesliceComplete = true;
                endOfProcess = false;
            }
            if (hr == S_OK && endOfProcess)
            {
                isTimesliceComplete = true;
            }
        }
        {
            MFT_OUTPUT_DATA_BUFFER output;
            memset(&output, 0, sizeof(output));

            hr = MFCreateSample(&output.pSample);
            wil::com_ptr<IMFMediaBuffer> buffer;
            hr = MFCreateMemoryBuffer(outputInfo.cbSize, &buffer);
            hr = output.pSample->AddBuffer(buffer.get());

            bool isTimeliceConsumed = false;
            while (!isTimeliceConsumed)
            {
                DWORD status = 0;
                hr = mft->ProcessOutput(NULL, 1, &output, &status);
                if (hr == S_OK)
                {
                    byte* tempBuffer = nullptr;
                    DWORD currentLength;
                    hr = buffer->Lock(&tempBuffer, nullptr, &currentLength);
                    writer.Write(tempBuffer, currentLength);
                    hr = buffer->Unlock();
                }
                else
                {
                    isTimeliceConsumed = true;
                }
            }
            
        }
        
    }
    hr = mft->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
    hr = mft->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0);
}

int main()
{
    const char* sourceFile = "C:\\Users\\xx\\Desktop\\decoded\\output_joc.wav"; //try to parse bitstream from wav
    const char* targetFile = "C:\\Users\\xx\\Desktop\\decoded\\MFStreamReader_output.raw";

    DecodeAudio(sourceFile, targetFile);
}