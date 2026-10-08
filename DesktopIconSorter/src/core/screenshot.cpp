// screenshot.cpp : Desktop screenshot to PNG via WIC
#include "screenshot.h"
#include <Windows.h>
#include <wincodec.h>

#pragma comment(lib, "windowscodecs.lib")

bool Desktop_Screenshot(const wchar_t* pngPath) {
    int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (w <= 0 || h <= 0) return false;

    HDC hdcScreen = GetDC(nullptr);
    if (!hdcScreen) return false;
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hbm = CreateCompatibleBitmap(hdcScreen, w, h);

    bool ok = false;
    if (!hdcMem || !hbm) goto done;

    {
        HGDIOBJ old = SelectObject(hdcMem, hbm);
        ok = BitBlt(hdcMem, 0, 0, w, h, hdcScreen, x, y, SRCCOPY) != 0;
        SelectObject(hdcMem, old);
    }
    if (!ok) goto done;

    // WIC PNG encode
    {
        IWICImagingFactory* factory = nullptr;
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_IWICImagingFactory, (void**)&factory)))
            goto done;

        IWICBitmap* wicBmp = nullptr;
        IWICStream* stream = nullptr;
        IWICBitmapEncoder* enc = nullptr;
        IWICBitmapFrameEncode* frame = nullptr;
        IPropertyBag2* props = nullptr;

        if (FAILED(factory->CreateBitmapFromHBITMAP(hbm, nullptr, WICBitmapIgnoreAlpha, &wicBmp)))
            goto release_factory;
        if (FAILED(factory->CreateStream(&stream)))
            goto release_bitmap;
        if (FAILED(stream->InitializeFromFilename(pngPath, GENERIC_WRITE)))
            goto release_stream;
        if (FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)))
            goto release_stream;
        if (FAILED(enc->Initialize(stream, WICBitmapEncoderNoCache)))
            goto release_encoder;
        if (FAILED(enc->CreateNewFrame(&frame, &props)))
            goto release_encoder;
        if (FAILED(frame->Initialize(props)))
            goto release_frame;
        if (FAILED(frame->SetSize(w, h)))
            goto release_frame;
        {
            WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
            if (FAILED(frame->SetPixelFormat(&fmt)))
                goto release_frame;
        }
        if (FAILED(frame->WriteSource(wicBmp, nullptr)))
            goto release_frame;
        if (FAILED(frame->Commit()))
            goto release_frame;
        if (FAILED(enc->Commit()))
            goto release_frame;
        ok = true;

    release_frame:
        if (props) props->Release();
        if (frame) frame->Release();
    release_encoder:
        enc->Release();
    release_stream:
        stream->Release();
    release_bitmap:
        wicBmp->Release();
    release_factory:
        factory->Release();
    }

done:
    if (hbm) DeleteObject(hbm);
    if (hdcMem) DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);
    return ok;
}
